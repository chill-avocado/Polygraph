/*
 * ESP32 BioMonitor — Self-Hosted WiFi Dashboard
 * ──────────────────────────────────────────────
 * Creates its own WiFi AP ("BioMonitor" / "sensor123").
 * Serves the full processing dashboard at http://192.168.4.1/
 * Streams RAW sensor CSV via WebSocket at ws://192.168.4.1/ws
 * All signal processing (HR, HRV, SpO2, GSR, SQI, Polygraph)
 * runs in the browser — no host computer required.
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
static const char*   AP_SSID    = "BioMonitor";
static const char*   AP_PASS    = "sensor123";
static const uint8_t AP_CHANNEL = 6;
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

        // No clients: hold cadence, keep the batch empty.
        if (ws.count() == 0) { used = 0; inBatch = 0; continue; }

        // ── sample RAW values ──
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

        // ── overflow guard: flush before appending if it wouldn't fit ──
        if (used + (size_t)n >= sizeof(batch)) {
            flushBatch(batch, used);
            used = 0; inBatch = 0;
        }

        memcpy(batch + used, line, (size_t)n);
        used   += (size_t)n;
        inBatch++;

        // ── time-based flush (~50 ms cadence) ──
        if (inBatch >= FLUSH_SAMPLES) {
            flushBatch(batch, used);
            used = 0; inBatch = 0;
        }
    }
}

// ─── setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[Boot] ESP32 BioMonitor");

    // ADC
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    // MAX30105
    Wire.begin(21, 22);
    if (sensor.begin(Wire, I2C_SPEED_FAST)) {
        sensor.setup(
            /*powerLevel*/   60,
            /*sampleAverage*/1,
            /*ledMode*/      2,      // IR + Red
            /*sampleRate*/   100,
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
    vTaskDelay(pdMS_TO_TICKS(1000));
}
