/*
 * ESP32 BioMonitor — Self-Hosted WiFi Dashboard
 * ──────────────────────────────────────────────
 * Creates its own WiFi AP ("BioMonitor" / "sensor123").
 * Serves the full processing dashboard at http://192.168.4.1/
 * Streams raw sensor CSV via WebSocket at ws://192.168.4.1/ws
 * All signal processing (HR, HRV, SpO2, GSR, SQI, Polygraph)
 * runs in the browser — no host computer required.
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
#include "html_content.h"   // PROGMEM dashboard HTML

// ─── Configuration ─────────────────────────────────────────────────────────────
static const char*   AP_SSID    = "BioMonitor";
static const char*   AP_PASS    = "sensor123";
static const uint8_t AP_CHANNEL = 6;
static const gpio_num_t GSR_PIN = GPIO_NUM_34;
static const float   EMA_ALPHA  = 0.20f;
static const uint32_t PERIOD_MS = 10;   // 100 Hz target

// ─── Globals ───────────────────────────────────────────────────────────────────
static MAX30105          sensor;
static AsyncWebServer    httpServer(80);
static AsyncWebSocket    ws("/ws");
static bool              sensorOK = false;

// EMA state
static float s_irF = 0, s_redF = 0, s_gsrF = 0;
static bool  s_first = true;

// ─── WebSocket event handler ───────────────────────────────────────────────────
static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* c,
                      AwsEventType type, void*, uint8_t*, size_t) {
    if      (type == WS_EVT_CONNECT)    Serial.printf("[WS] +client %u\n", c->id());
    else if (type == WS_EVT_DISCONNECT) Serial.printf("[WS] -client %u\n", c->id());
}

// ─── Stream task — Core 1 ─────────────────────────────────────────────────────
static void streamTask(void*) {
    TickType_t       last  = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(PERIOD_MS);
    char             buf[96];

    for (;;) {
        vTaskDelayUntil(&last, period);
        if (ws.count() == 0) continue;

        int32_t irRaw = 0, redRaw = 0, gsrRaw = 0;

        if (sensorOK) {
            sensor.check();
            irRaw  = (int32_t)sensor.getIR();
            redRaw = (int32_t)sensor.getRed();
        }
        gsrRaw = (int32_t)analogRead(GSR_PIN);

        if (s_first) {
            s_irF = irRaw; s_redF = redRaw; s_gsrF = gsrRaw;
            s_first = false;
        } else {
            s_irF  = EMA_ALPHA * irRaw  + (1.0f - EMA_ALPHA) * s_irF;
            s_redF = EMA_ALPHA * redRaw + (1.0f - EMA_ALPHA) * s_redF;
            s_gsrF = EMA_ALPHA * gsrRaw + (1.0f - EMA_ALPHA) * s_gsrF;
        }

        int len = snprintf(buf, sizeof(buf),
            "%lu,%ld,%.1f,%ld,%.1f,%ld,%.1f\n",
            (unsigned long)millis(),
            (long)irRaw,  s_irF,
            (long)redRaw, s_redF,
            (long)gsrRaw, s_gsrF);

        if (len > 0 && len < (int)sizeof(buf))
            ws.textAll(buf, (size_t)len);
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
        sensor.check();
        s_irF  = (float)sensor.getIR();
        s_redF = (float)sensor.getRed();
        s_first = false;
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

    // Serve dashboard HTML
    httpServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
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
    vTaskDelay(pdMS_TO_TICKS(1000));
}
