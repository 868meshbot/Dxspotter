// DXSpotter — SatFeed.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fetches upcoming satellite passes from hams.at's JSON API. Pass geometry is
// computed server-side for the location on the account linked to the API key;
// the device just renders the list. Port target: schrockwell/hamsat_ex
// (HamsatWeb.API.AlertsController :upcoming → /api/alerts/upcoming).

#include "SatFeed.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdio>

namespace dxs {

static constexpr const char* URL = "https://hams.at/api/alerts/upcoming";

static SemaphoreHandle_t s_mutex      = nullptr;
static SatPass           s_passes[SAT_MAX_PASSES];
static int               s_count      = 0;
static bool              s_newData    = false;
static uint32_t          s_fetchedAt  = 0;
static int               s_lastStatus = 0;
static TaskHandle_t      s_taskHandle = nullptr;

// ── Civil date → Unix UTC (Howard Hinnant's days_from_civil) ──────────
// The system clock runs in UTC (set by NTP; no TZ configured), so we convert
// the API's ISO-8601 UTC timestamps directly without mktime's local-time bias.
static long _daysFromCivil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static time_t _parseIso(const char* s) {
    if (!s || !*s) return 0;
    int Y = 0, M = 0, D = 0, h = 0, mi = 0, sec = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &sec) < 5) return 0;
    return (time_t)(_daysFromCivil(Y, (unsigned)M, (unsigned)D) * 86400L
                    + h * 3600L + mi * 60L + sec);
}

void SatFeed::init() {
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(_fetchTask, "sat_fetch", 8192, nullptr, 1, &s_taskHandle);
    DXS_LOG("SatFeed", "Initialised");
}

void SatFeed::requestFetch() {
    if (s_taskHandle) xTaskNotifyGive(s_taskHandle);
}

bool     SatFeed::hasNewData()  { return s_newData; }
uint32_t SatFeed::lastFetchMs() { return s_fetchedAt; }
int      SatFeed::lastStatus()  { return s_lastStatus; }

int SatFeed::getPasses(SatPass* buf, int maxCount) {
    if (!buf || maxCount <= 0) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_count < maxCount ? s_count : maxCount;
    memcpy(buf, s_passes, (size_t)n * sizeof(SatPass));
    s_newData = false;
    xSemaphoreGive(s_mutex);
    return n;
}

// ── Parser ────────────────────────────────────────────────────────────
void SatFeed::_parse(const char* json) {
    // Filter to the fields we render — keeps the document small for ~20 alerts.
    JsonDocument filter;
    filter["data"][0]["aos_at"]              = true;
    filter["data"][0]["los_at"]              = true;
    filter["data"][0]["callsign"]            = true;
    filter["data"][0]["mode"]                = true;
    filter["data"][0]["mhz"]                 = true;
    filter["data"][0]["max_elevation"]       = true;
    filter["data"][0]["is_workable"]         = true;
    filter["data"][0]["satellite"]["name"]   = true;
    filter["data"][0]["satellite"]["number"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, json, DeserializationOption::Filter(filter));
    if (err) { DXS_LOG("SatFeed", "JSON err: %s", err.c_str()); return; }

    JsonArray arr = doc["data"].as<JsonArray>();
    if (arr.isNull()) { DXS_LOG("SatFeed", "no data[] array"); return; }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_count = 0;
    for (JsonObject a : arr) {
        if (s_count >= SAT_MAX_PASSES) break;
        SatPass& p = s_passes[s_count++];
        memset(&p, 0, sizeof(p));
        JsonObject sat = a["satellite"];
        strncpy(p.satName,  sat["name"] | "?",  sizeof(p.satName) - 1);
        strncpy(p.callsign, a["callsign"] | "", sizeof(p.callsign) - 1);
        strncpy(p.mode,     a["mode"] | "",     sizeof(p.mode) - 1);
        p.satNumber    = sat["number"] | 0;
        p.mhz          = a["mhz"] | 0.0;
        p.maxElevation = a["max_elevation"] | 0.0f;
        p.workable     = a["is_workable"] | false;
        p.aosUtc       = _parseIso(a["aos_at"] | "");
        p.losUtc       = _parseIso(a["los_at"] | "");
    }
    s_newData   = true;
    s_fetchedAt = millis();
    xSemaphoreGive(s_mutex);
    DXS_LOG("SatFeed", "Parsed %d passes", s_count);
}

// ── Background fetch task (10-min periodic, wakes on requestFetch) ─────
void SatFeed::_fetchTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(600000));  // 10 min

        if (WiFi.status() != WL_CONNECTED) { s_lastStatus = -1; continue; }
        const auto& cfg = config::get();
        if (cfg.hamsatKey[0] == '\0') { s_lastStatus = -1; continue; }

        WiFiClientSecure sslClient;
        sslClient.setInsecure();
        HTTPClient http;
        if (!http.begin(sslClient, URL)) { s_lastStatus = -1; continue; }
        http.setTimeout(12000);
        char auth[64];
        snprintf(auth, sizeof(auth), "Bearer %s", cfg.hamsatKey);
        http.addHeader("Authorization", auth);
        http.addHeader("Accept",        "application/json");
        http.addHeader("User-Agent",    "DXSpotter/0.1 (ESP32)");

        int code = http.GET();
        s_lastStatus = code;
        if (code == 200) {
            String body = http.getString();
            _parse(body.c_str());
        } else {
            DXS_LOG("SatFeed", "HTTP %d", code);
        }
        http.end();
    }
}

}  // namespace dxs
