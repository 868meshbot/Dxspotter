// DXSpotter — HolyClusterFeed.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HolyClusterFeed.h"
#include "../utils/Log.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <time.h>

namespace dxs {

// ── Band/continent lookup tables ──────────────────────────────────────
// HF bands (index 0-9), VHF/UHF (index 10-15)
const char* const HC_BAND_LABELS[HC_NUM_BANDS] = {
    "160m","80m","60m","40m","30m","20m","17m","15m","12m","10m",  // HF
    "6m","4m","2m","70cm","33cm","23cm"                             // VHF/UHF
};
const char* const HC_CONT_LABELS[HC_NUM_CONTS] = {
    "EU", "NA", "AS", "AF", "OC", "SA"
};

static const float kBandMeters[HC_NUM_BANDS] = {
    160, 80, 60, 40, 30, 20, 17, 15, 12, 10,   // HF
    6, 4, 2, 0.7f, 0.33f, 0.23f                  // VHF/UHF
};

static int _bandIdx(float m) {
    for (int i = 0; i < HC_NUM_BANDS; i++) {
        if (fabsf(m - kBandMeters[i]) < 0.5f) return i;
    }
    return -1;
}

static int _contIdx(const char* c) {
    if (!c || !c[0]) return -1;
    if (strcmp(c, "EU") == 0) return 0;
    if (strcmp(c, "NA") == 0) return 1;
    if (strcmp(c, "AS") == 0) return 2;
    if (strcmp(c, "AF") == 0) return 3;
    if (strcmp(c, "OC") == 0) return 4;
    if (strcmp(c, "SA") == 0) return 5;
    return -1;
}

// ── Heatmap ring buffer ───────────────────────────────────────────────
struct HeatEntry {
    uint32_t time;
    uint8_t  bandIdx;
    uint8_t  contIdx;
};

// ── Shared state (protected by s_mutex) ──────────────────────────────
static SemaphoreHandle_t s_mutex       = nullptr;
static HCSpot            s_spots[HC_MAX_SPOTS];
static int               s_spotCount   = 0;
static bool              s_newData     = false;
static bool              s_heatmapNew  = false;
static uint32_t          s_fetchedAt   = 0;
static int               s_lastStatus  = -1;
static uint32_t          s_lastSpotTime = 0;

static HeatEntry s_ring[HC_RING_SIZE];
static int       s_ringHead  = 0;
static int       s_ringCount = 0;

// ── WebSocket client (task-local usage only) ──────────────────────────
static WebSocketsClient s_ws;
static volatile bool    s_fetchRequested    = false;
static volatile bool    s_sendInitialPending = false;  // send {"initial":true} after a brief delay

// Forward declaration
static void _parseSpots(const char* json);

// ── WebSocket event callback ──────────────────────────────────────────
static void _onWsEvent(WStype_t type, uint8_t* payload, size_t len) {
    switch (type) {
    case WStype_CONNECTED:
        DXS_LOG("HCFeed", "WebSocket connected to %s", (char*)payload);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_lastStatus   = 200;
        s_lastSpotTime = 0;
        xSemaphoreGive(s_mutex);
        // Delay initial request — some servers close immediately if a message
        // arrives before their post-handshake setup completes.
        s_sendInitialPending = true;
        break;

    case WStype_TEXT:
        DXS_LOG("HCFeed", "RX %u bytes: %.60s", (unsigned)len,
                payload ? (char*)payload : "");
        if (payload && len > 0) {
            payload[len] = '\0';
            _parseSpots((const char*)payload);
        }
        break;

    case WStype_PING:
        DXS_LOG("HCFeed", "PING received");
        break;

    case WStype_PONG:
        DXS_LOG("HCFeed", "PONG received");
        break;

    case WStype_DISCONNECTED:
        // payload contains the numeric close code when available
        DXS_LOG("HCFeed", "WebSocket disconnected (code=%d, reason=%.*s)",
                (int)(payload && len >= 2 ? ((uint8_t)payload[0] << 8 | (uint8_t)payload[1]) : 0),
                (int)(len > 2 ? len - 2 : 0),
                payload && len > 2 ? (char*)payload + 2 : "");
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_lastStatus = -1;
        xSemaphoreGive(s_mutex);
        break;

    case WStype_ERROR:
        DXS_LOG("HCFeed", "WebSocket error: %.*s", (int)len, payload ? (char*)payload : "");
        break;

    default:
        DXS_LOG("HCFeed", "WS event type=%d len=%u", (int)type, (unsigned)len);
        break;
    }
}

// ── Spot JSON parser ──────────────────────────────────────────────────
// HC sends spots oldest-first in both initial and update messages.
// Display buffer s_spots[] is kept newest-first:
//   - initial: collect then reverse into s_spots[]
//   - update:  prepend each spot in order (last prepended = newest = index 0)
static void _parseSpots(const char* json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        DXS_LOG("HCFeed", "JSON error: %s", err.c_str());
        return;
    }

    const char* type = doc["type"] | "";
    JsonArray spots  = doc["spots"].as<JsonArray>();
    if (!spots) return;

    bool isInitial = (strcmp(type, "initial") == 0);
    int  total     = (int)spots.size();
    int  dispStart = (isInitial && total > HC_MAX_SPOTS) ? (total - HC_MAX_SPOTS) : 0;

    // Temp buffer on static storage to avoid large stack frame in callback
    static HCSpot newDisp[HC_MAX_SPOTS];
    int newCount = 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (isInitial) {
        s_spotCount = 0;
        s_ringCount = 0;
        s_ringHead  = 0;
    }

    int i = 0;
    for (JsonObject s : spots) {
        float    freqKHz = s["freq"] | 0.0f;
        float    bandM   = s["band"] | 0.0f;
        uint32_t ts      = (uint32_t)(s["time"].as<float>());
        const char* dx     = s["dx_callsign"]      | "";
        const char* sp     = s["spotter_callsign"] | "";
        const char* mode   = s["mode"]             | "";
        const char* comm   = s["comment"]          | "";
        const char* dxCont = s["dx_continent"]     | "";
        bool isDxped = s["is_dxpedition"] | false;

        // Every spot feeds the heatmap ring buffer
        int bi = _bandIdx(bandM);
        int ci = _contIdx(dxCont);
        if (bi >= 0 && ci >= 0) {
            s_ring[s_ringHead].time    = ts;
            s_ring[s_ringHead].bandIdx = (uint8_t)bi;
            s_ring[s_ringHead].contIdx = (uint8_t)ci;
            s_ringHead = (s_ringHead + 1) % HC_RING_SIZE;
            if (s_ringCount < HC_RING_SIZE) s_ringCount++;
        }
        if (ts > s_lastSpotTime) s_lastSpotTime = ts;

        // Collect display spots
        if (i >= dispStart && newCount < HC_MAX_SPOTS) {
            HCSpot& d = newDisp[newCount++];
            memset(&d, 0, sizeof(d));
            strncpy(d.dxCall,      dx,     sizeof(d.dxCall)      - 1);
            strncpy(d.spotter,     sp,     sizeof(d.spotter)     - 1);
            strncpy(d.mode,        mode,   sizeof(d.mode)        - 1);
            strncpy(d.comment,     comm,   sizeof(d.comment)     - 1);
            strncpy(d.dxContinent, dxCont, sizeof(d.dxContinent) - 1);
            d.freqKHz      = freqKHz;
            d.isDxpedition = isDxped;
            uint32_t sod = ts % 86400;
            snprintf(d.utcTime, sizeof(d.utcTime), "%02u:%02u",
                     (unsigned)(sod / 3600), (unsigned)((sod % 3600) / 60));
        }
        i++;
    }

    if (isInitial) {
        // Store newest-first by reversing the oldest-first input
        s_spotCount = newCount;
        for (int j = 0; j < newCount; j++) {
            s_spots[j] = newDisp[newCount - 1 - j];
        }
    } else {
        // Prepend in original order: last prepend = newest at s_spots[0]
        for (int j = 0; j < newCount; j++) {
            if (s_spotCount < HC_MAX_SPOTS) s_spotCount++;
            memmove(&s_spots[1], &s_spots[0],
                    (size_t)(s_spotCount - 1) * sizeof(HCSpot));
            s_spots[0] = newDisp[j];
        }
    }

    s_newData    = true;
    s_heatmapNew = true;
    s_fetchedAt  = millis();
    xSemaphoreGive(s_mutex);

    DXS_LOG("HCFeed", "%s: %d spots (buf=%d)", type, total, s_spotCount);
}

// ── Background WebSocket task ─────────────────────────────────────────
void HolyClusterFeed::_wsTask(void*) {
    // Wait for initial WiFi connection before starting WSS
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    s_ws.onEvent(_onWsEvent);
    s_ws.setReconnectInterval(8000);
    // Only send Origin — the library already adds its own User-Agent and having
    // two User-Agent headers can cause server-side validation failures.
    s_ws.setExtraHeaders("Origin: https://holycluster.iarc.org");
    // Empty protocol string: the default "arduino" subprotocol may be rejected
    // by servers that only accept standard browser subprotocol negotiation.
    // beginSSL without a CA cert: library auto-calls setInsecure() (ESP32 SDK >= 2).
    s_ws.beginSSL("holycluster.iarc.org", 443, "/spots_ws", nullptr, "");

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_lastStatus = 0;  // connecting
    xSemaphoreGive(s_mutex);

    static uint32_t lastReqMs      = 0;
    static uint32_t connectedAtMs  = 0;

    for (;;) {
        s_ws.loop();

        uint32_t now = millis();

        // Send {"initial":true} 800 ms after connect — gives server time to finish
        // its own post-handshake setup before we send the first message.
        if (s_sendInitialPending && s_ws.isConnected()) {
            if (connectedAtMs == 0) connectedAtMs = now;
            if (now - connectedAtMs >= 800UL) {
                DXS_LOG("HCFeed", "Sending initial request");
                s_ws.sendTXT("{\"initial\":true}");
                s_sendInitialPending = false;
                lastReqMs = now;
                connectedAtMs = 0;
            }
        }
        if (!s_ws.isConnected()) connectedAtMs = 0;

        // Periodic incremental update every 60 s
        if (s_ws.isConnected() && !s_sendInitialPending && (now - lastReqMs) >= 60000UL) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            uint32_t lst = s_lastSpotTime;
            xSemaphoreGive(s_mutex);
            if (lst > 0) {
                char msg[48];
                snprintf(msg, sizeof(msg), "{\"last_time\":%lu}", (unsigned long)lst);
                s_ws.sendTXT(msg);
                DXS_LOG("HCFeed", "Polling delta since %lu", (unsigned long)lst);
            }
            lastReqMs = now;
        }

        // On-demand fetch (only after initial handshake is done)
        if (s_fetchRequested && s_ws.isConnected() && !s_sendInitialPending) {
            s_fetchRequested = false;
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            uint32_t lst = s_lastSpotTime;
            xSemaphoreGive(s_mutex);
            char msg[48];
            if (lst > 0)
                snprintf(msg, sizeof(msg), "{\"last_time\":%lu}", (unsigned long)lst);
            else
                strncpy(msg, "{\"initial\":true}", sizeof(msg) - 1);
            s_ws.sendTXT(msg);
            lastReqMs = now;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── Public API ────────────────────────────────────────────────────────
void HolyClusterFeed::init() {
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(_wsTask, "hc_feed", 8192, nullptr, 1, nullptr);
    DXS_LOG("HCFeed", "Initialised");
}

void HolyClusterFeed::requestFetch() { s_fetchRequested = true; }

bool HolyClusterFeed::hasNewData() { return s_newData; }

int HolyClusterFeed::getSpots(HCSpot* buf, int maxCount) {
    if (!buf || maxCount <= 0) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_spotCount < maxCount ? s_spotCount : maxCount;
    memcpy(buf, s_spots, (size_t)n * sizeof(HCSpot));
    s_newData = false;
    xSemaphoreGive(s_mutex);
    return n;
}

uint32_t HolyClusterFeed::lastFetchMs() { return s_fetchedAt; }
int      HolyClusterFeed::lastStatus()  { return s_lastStatus; }

void HolyClusterFeed::getHeatmap(uint8_t out[HC_NUM_BANDS][HC_NUM_CONTS]) {
    memset(out, 0, HC_NUM_BANDS * HC_NUM_CONTS * sizeof(uint8_t));
    // 60-min cutoff using NTP unix time; if not synced, accept all stored spots
    time_t now    = time(nullptr);
    time_t cutoff = (now > 1700000000L) ? (now - 3600) : 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < s_ringCount; i++) {
        if ((time_t)s_ring[i].time < cutoff) continue;
        uint8_t b = s_ring[i].bandIdx;
        uint8_t c = s_ring[i].contIdx;
        if (b < HC_NUM_BANDS && c < HC_NUM_CONTS && out[b][c] < 255) out[b][c]++;
    }
    xSemaphoreGive(s_mutex);
}

bool HolyClusterFeed::heatmapUpdated() {
    if (!s_heatmapNew) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_heatmapNew = false;
    xSemaphoreGive(s_mutex);
    return true;
}

}  // namespace dxs
