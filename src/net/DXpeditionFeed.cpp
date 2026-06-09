// DXSpotter — DXpeditionFeed.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DXpeditionFeed.h"
#include "../utils/Log.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>

namespace dxs {

static constexpr const char* URL =
    "https://holycluster.iarc.org/dxpeditions";

static SemaphoreHandle_t s_mutex      = nullptr;
static DXpedition        s_peds[DXPED_MAX];
static int               s_pedCount   = 0;
static bool              s_newData    = false;
static uint32_t          s_fetchedAt  = 0;
static int               s_lastStatus = 0;

static TaskHandle_t s_taskHandle = nullptr;

void DXpeditionFeed::init() {
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(_fetchTask, "dxped_fetch", 6144, nullptr, 1, &s_taskHandle);
    DXS_LOG("DXped", "Initialised");
}

void DXpeditionFeed::requestFetch() {
    if (s_taskHandle) xTaskNotifyGive(s_taskHandle);
}

bool DXpeditionFeed::hasNewData() { return s_newData; }
uint32_t DXpeditionFeed::lastFetchMs() { return s_fetchedAt; }
int      DXpeditionFeed::lastStatus()  { return s_lastStatus; }

int DXpeditionFeed::getDXpeditions(DXpedition* buf, int maxCount) {
    if (!buf || maxCount <= 0) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_pedCount < maxCount ? s_pedCount : maxCount;
    memcpy(buf, s_peds, (size_t)n * sizeof(DXpedition));
    s_newData = false;
    xSemaphoreGive(s_mutex);
    return n;
}

// ── Parser ────────────────────────────────────────────────────────────
void DXpeditionFeed::_parse(const char* json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        DXS_LOG("DXped", "JSON error: %s", err.c_str());
        return;
    }

    // Accept both a top-level array and {"dxpeditions":[...]}
    JsonArray arr;
    if (doc.is<JsonArray>()) {
        arr = doc.as<JsonArray>();
    } else {
        arr = doc["dxpeditions"].as<JsonArray>();
        if (!arr) arr = doc["data"].as<JsonArray>();
    }
    if (!arr) {
        DXS_LOG("DXped", "No array in response");
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_pedCount = 0;
    for (JsonObject p : arr) {
        if (s_pedCount >= DXPED_MAX) break;
        DXpedition& d = s_peds[s_pedCount++];
        memset(&d, 0, sizeof(d));

        // HolyCluster only supplies callsign + ISO start/end dates. There is no
        // entity/country or active flag — entity is left empty and the active
        // state is derived from the dates at display time (see ScreenDXpeditions).
        const char* call = p["callsign"] | (p["call"] | "");
        const char* s1   = p["start_date"] | (p["start"] | "");
        const char* e1   = p["end_date"]   | (p["end"]   | "");

        strncpy(d.callsign,  call, sizeof(d.callsign) - 1);
        d.entity[0] = '\0';
        strncpy(d.startDate, s1, 10);  // keep YYYY-MM-DD from the ISO string
        d.startDate[10] = '\0';
        strncpy(d.endDate,   e1, 10);
        d.endDate[10] = '\0';
        d.isActive = false;            // computed from dates in the UI
    }
    s_newData   = true;
    s_fetchedAt = millis();
    xSemaphoreGive(s_mutex);
    DXS_LOG("DXped", "Parsed %d DXpeditions", s_pedCount);
}

// ── Background fetch task (10-min periodic) ───────────────────────────
void DXpeditionFeed::_fetchTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(600000));  // 10 min

        if (WiFi.status() != WL_CONNECTED) {
            DXS_LOG("DXped", "No WiFi, skipping");
            continue;
        }

        DXS_LOG("DXped", "Fetching %s", URL);
        WiFiClientSecure sslClient;
        sslClient.setInsecure();
        HTTPClient http;
        http.begin(sslClient, URL);
        http.setTimeout(12000);
        http.addHeader("User-Agent", "DXSpotter/0.1 (ESP32)");
        http.addHeader("Accept",     "application/json");
        int code = http.GET();
        s_lastStatus = code;

        if (code == 200) {
            String body = http.getString();
            DXS_LOG("DXped", "%d bytes: %.80s", (int)body.length(), body.c_str());
            _parse(body.c_str());
        } else {
            DXS_LOG("DXped", "HTTP error %d", code);
        }
        http.end();
    }
}

}  // namespace dxs
