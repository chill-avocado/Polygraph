/*
 * ESP32 BioMonitor — sensor front-end (two roles, both always active)
 * ───────────────────────────────────────────────────────────────────
 * ROLE 1 (primary) — ADC for the Raspberry Pi.
 *   Samples at 100 Hz and prints `t,gsr,ir,red` to USB SERIAL at 921600 baud.
 *   The Pi does all the heavy analysis (NeuroKit2 cvxEDA / SparsEDA / PPG
 *   quality, FLIRT features). No WiFi is involved in this path at all.
 *
 * ROLE 2 (fallback) — standalone, no host computer.
 *   Also creates its own WiFi AP ("BioMonitor" / "sensor123"), serves the full
 *   browser app at http://192.168.4.1/ and streams the same CSV over WebSocket
 *   at ws://192.168.4.1/ws, with all processing done in the browser.
 *   This keeps working if the Pi is absent or switched off.
 *
 * Sampling happens unconditionally; the WebSocket batch is only built when a
 * browser is actually attached, so the fallback costs nothing when unused.
 *
 * ── Wire format (PINNED CONTRACT — the frontend parses exactly this) ──
 *   Each WebSocket TEXT frame carries one OR MORE newline-separated lines.
 *   Each line is EXACTLY:   t,gsr,ir,red\n
 *     t   = millis()                    (unsigned long, monotonic ms)
 *     gsr = raw GSR ADC on GPIO34       (int, 0..4095)
 *     ir  = raw IR count   (MAX30105)   (unsigned, integer)
 *     red = raw RED count  (MAX30105)   (unsigned, integer)
 *   Column order is fixed: t, then gsr, then ir, then red.
 *   Values are RAW — no on-device filtering. The browser engine does all of
 *   its own filtering; an on-device EMA lowpass would attenuate the pulse, so
 *   none is applied here (the former alpha=0.20 EMA has been removed).
 *
 * ── Streaming behavior ──
 *   Sampled at 100 Hz (PERIOD_MS=10). Rather than sending one frame per sample
 *   (100 frames/s floods AsyncWebSocket), samples are BATCHED into a text
 *   buffer and flushed as a single ws.textAll() about every 50 ms
 *   (~5 samples/frame). The buffer also flushes early if it would overflow.
 *   Sends are skipped when there are no clients (ws.count()==0) or when the
 *   client TX queue is saturated (ws.availableForWriteAll()==false); in the
 *   saturated case the pending batch is dropped. The stream task never blocks.
 *
 * ── Device health & keepalive (does NOT touch the stream/wire format) ──
 *   GET /status returns application/json with device health, e.g.
 *     {"fw":"biomonitor-1","sensorOK":true,"uptimeMs":123456,"clients":1,
 *      "freeHeap":210000,"minFreeHeap":190000,"sampleHz":100,"gsrPin":34,
 *      "ssid":"BioMonitor"}
 *   It is built with snprintf into a small static buffer (no heap churn) and
 *   registered before onNotFound so the "/" redirect doesn't swallow it.
 *   Separately, loop() calls ws.pingAll() about every 5 s (alongside the
 *   existing ws.cleanupClients()) so dead/half-open WebSocket clients are
 *   detected and pruned, improving the browser's reconnect behavior. Neither
 *   adds any blocking delay to the 100 Hz stream task.
 *
 * Hardware:
 *   MAX30105 PPG sensor  →  I2C  SDA=21  SCL=22
 *   GSR electrode pair   →  GPIO 34  (ADC1_CH6)
 *
 * Libraries (Arduino Library Manager):
 *   ESPAsyncWebServer + AsyncTCP
 *   SparkFun MAX3010x Pulse and Proximity Sensor Library
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include "MAX30105.h"
#include "web_assets.h"     // PROGMEM: the validated app + ES modules (see build_web_assets.py)

// ─── Configuration ─────────────────────────────────────────────────────────────
static const char*   FW_VERSION = "biomonitor-1";   // reported by GET /status
static const char*   AP_SSID    = "BioMonitor";
static const char*   AP_PASS    = "sensor123";
static const uint8_t AP_CHANNEL = 6;
// GPIO34: confirmed by the [ADC] boot probe as the pin carrying the GSR
// signal (34 reads live, VP/36 and VN/39 read a hard zero). Input-only and on
// ADC1, which is required -- ADC2 cannot be read while WiFi is active.
static const gpio_num_t GSR_PIN = GPIO_NUM_34;
static const uint32_t PERIOD_MS = 10;   // 100 Hz target

// ─── Globals ───────────────────────────────────────────────────────────────────
static MAX30105          sensor;
static AsyncWebServer    httpServer(80);
static AsyncWebSocket    ws("/ws");
static bool              sensorOK = false;

// ─── WebSocket event handler ───────────────────────────────────────────────────
static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* c,
                      AwsEventType type, void*, uint8_t*, size_t) {
    if      (type == WS_EVT_CONNECT)    Serial.printf("[WS] +client %u\n", c->id());
    else if (type == WS_EVT_DISCONNECT) Serial.printf("[WS] -client %u\n", c->id());
}

// ─── Batch flush — non-blocking, honors backpressure ──────────────────────────
// Enqueue the accumulated CSV batch as a single TEXT frame. Silently drops the
// batch (never blocks) if there are no clients or if any client's TX queue is
// saturated, so the 100 Hz stream task always keeps its cadence.
static inline void flushBatch(const char* data, size_t len) {
    if (len == 0)                    return;
    if (ws.count() == 0)             return;   // no listeners
    if (!ws.availableForWriteAll())  return;   // client queue full → drop, don't block
    ws.textAll(data, len);
}

// ─── Stream task — Core 1 ─────────────────────────────────────────────────────
static void streamTask(void*) {
    TickType_t       last   = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(PERIOD_MS);

    // Batch buffer: accumulate several "t,gsr,ir,red\n" lines, flush as one
    // frame ~every 50 ms. Kept static (off the 4 KB task stack).
    static char   batch[512];
    size_t        used           = 0;   // bytes currently in batch
    uint8_t       inBatch         = 0;   // samples currently in batch
    const uint8_t FLUSH_SAMPLES   = 5;   // 5 samples * 10 ms ≈ 50 ms per frame
    char          line[48];              // one CSV line (worst case < 40 bytes)

    for (;;) {
        vTaskDelayUntil(&last, period);

        // ── sample RAW values (ALWAYS — the Pi is fed over USB serial even
        //    when no browser is attached to the WiFi fallback) ──
        unsigned long ir = 0, red = 0;
        if (sensorOK) {
            sensor.check();
            ir  = (unsigned long)sensor.getIR();
            red = (unsigned long)sensor.getRed();
        }
        unsigned int gsr = (unsigned int)analogRead(GSR_PIN);   // 0..4095

        // ── format one line:  t,gsr,ir,red\n ──
        int n = snprintf(line, sizeof(line), "%lu,%u,%lu,%lu\n",
                         (unsigned long)millis(), gsr, ir, red);
        if (n <= 0) continue;
        if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;   // truncation guard

        // ── PRIMARY PATH: USB serial → Raspberry Pi (ADC role) ──
        // ~30 B/sample at 100 Hz = ~3 kB/s, far below the 921600 baud link, so
        // this never blocks the cadence. The boot/status lines printed below
        // contain no commas, so the Pi's parser (which requires >=3 numeric
        // CSV fields) skips them harmlessly.
        Serial.write((const uint8_t*)line, (size_t)n);

        // ── FALLBACK PATH: batch to WebSocket only when a browser is attached
        //    to the ESP32's own AP (standalone mode, unchanged behaviour) ──
        if (ws.count() == 0) { used = 0; inBatch = 0; continue; }

        if (used + (size_t)n >= sizeof(batch)) {   // overflow guard
            flushBatch(batch, used);
            used = 0; inBatch = 0;
        }

        memcpy(batch + used, line, (size_t)n);
        used   += (size_t)n;
        inBatch++;

        if (inBatch >= FLUSH_SAMPLES) {            // ~50 ms cadence
            flushBatch(batch, used);
            used = 0; inBatch = 0;
        }
    }
}

// ─── setup ─────────────────────────────────────────────────────────────────────
void setup() {
    // 921600: the USB link to the Raspberry Pi carries the 100 Hz sample stream.
    Serial.begin(921600);
    delay(200);
    Serial.println("\n[Boot] ESP32 BioMonitor");

    // ADC
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    // Report every ADC1 pin the GSR could plausibly be on, so the live one is
    // obvious rather than guessed. 34 = "34", 36 = "VP", 39 = "VN".
    {
        const gpio_num_t cand[] = {GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_36, GPIO_NUM_39};
        const char*      name[] = {"34", "35", "VP/36", "VN/39"};
        Serial.print("[ADC]   ");
        for (uint8_t i = 0; i < 4; i++) {
            uint32_t acc = 0;
            for (uint8_t n = 0; n < 16; n++) acc += analogRead(cand[i]);
            Serial.printf("%s=%-4u ", name[i], (unsigned)(acc / 16));
        }
        Serial.printf(" (GSR configured on GPIO%d)\n", (int)GSR_PIN);
    }

    // MAX30105
    Wire.begin(21, 22);

    // Bus scan before probing the sensor. Distinguishes "nothing on the bus at
    // all" (wiring/power) from "something responds but isn't the expected part"
    // (wrong module or address) -- otherwise a failed begin() is ambiguous.
    {
        uint8_t found = 0;
        Serial.print("[I2C]   scanning SDA=21 SCL=22 ...");
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.printf(" 0x%02X", addr);
                found++;
            }
        }
        if (found == 0) Serial.print(" none");
        Serial.printf("  (%u device%s)\n", found, found == 1 ? "" : "s");
        Serial.println("[I2C]   MAX3010x is expected at 0x57");
    }

    if (sensor.begin(Wire, I2C_SPEED_FAST)) {
        // sampleRate is deliberately 2x the 100 Hz polling cadence. The library's
        // getIR()/getRed() call safeCheck(), which BLOCKS until a fresh FIFO
        // sample exists; running the sensor at exactly the poll rate meant any
        // jitter left nothing ready, stalling the loop and halving the effective
        // stream rate to ~50 Hz. Extra headroom keeps a sample always waiting.
        sensor.setup(
            /*powerLevel*/   60,
            /*sampleAverage*/1,
            /*ledMode*/      2,      // IR + Red
            /*sampleRate*/   200,
            /*pulseWidth*/   411,
            /*adcRange*/     16384
        );
        sensor.setPulseAmplitudeRed(0x3C);
        sensor.setPulseAmplitudeIR(0x3C);
        sensorOK = true;
        Serial.println("[Sensor] MAX30105 OK");
    } else {
        Serial.println("[Sensor] MAX30105 NOT FOUND — streaming zeros for PPG");
    }

    // WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
    delay(100);
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WiFi]  AP: %-16s  IP: %s\n", AP_SSID, ip.toString().c_str());

    // WebSocket
    ws.onEvent(onWsEvent);
    httpServer.addHandler(&ws);

    // Serve the app at "/" and its ES modules at their own routes (correct JS
    // MIME so the browser's `import` works). Generated by build_web_assets.py.
    REGISTER_WEB_ASSETS(httpServer);

    // Device-health JSON. Built with snprintf into a small static buffer (no
    // heap churn). Registered BEFORE onNotFound so the "/" redirect below does
    // not swallow it. Read-only — does not touch the stream task or wire format.
    httpServer.on("/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        static char json[256];
        int n = snprintf(json, sizeof(json),
            "{\"fw\":\"%s\",\"sensorOK\":%s,\"uptimeMs\":%lu,\"clients\":%u,"
            "\"freeHeap\":%u,\"minFreeHeap\":%u,\"sampleHz\":%u,\"gsrPin\":%u,"
            "\"ssid\":\"%s\"}",
            FW_VERSION,
            sensorOK ? "true" : "false",
            (unsigned long)millis(),
            (unsigned)ws.count(),
            (unsigned)ESP.getFreeHeap(),
            (unsigned)ESP.getMinFreeHeap(),
            (unsigned)(1000 / PERIOD_MS),
            (unsigned)GSR_PIN,
            AP_SSID);
        if (n < 0) n = 0;
        if (n >= (int)sizeof(json)) n = (int)sizeof(json) - 1;   // truncation guard
        req->send(200, "application/json", json);
    });

    // Redirect anything else to root
    httpServer.onNotFound([](AsyncWebServerRequest* req) {
        req->redirect("/");
    });

    httpServer.begin();
    Serial.printf("[HTTP]  Dashboard → http://%s/\n", ip.toString().c_str());
    Serial.printf("[WS]    Stream    → ws://%s/ws\n", ip.toString().c_str());

    // Stream task on Core 1
    xTaskCreatePinnedToCore(streamTask, "Stream", 4096, nullptr, 5, nullptr, 1);
}

// ─── loop ──────────────────────────────────────────────────────────────────────
void loop() {
    ws.cleanupClients();

    // Keepalive: ping all clients ~every 5 s (this loop ticks ~every 1 s) so
    // dead/half-open connections are detected and pruned by AsyncWebSocket,
    // improving the browser's reconnect behavior. Non-blocking; independent of
    // the Core 1 stream task.
    static uint8_t pingTick = 0;
    if (++pingTick >= 5) {
        pingTick = 0;
        ws.pingAll();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}
