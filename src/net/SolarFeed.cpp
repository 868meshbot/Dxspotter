// DXSpotter — SolarFeed.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SolarFeed.h"
#include "../utils/Log.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace dxs {

static constexpr const char* SOLAR_URL = "https://www.hamqsl.com/solarxml.php";

static SemaphoreHandle_t s_mutex      = nullptr;
static SolarData         s_data       = {};
static bool              s_hasData    = false;
static bool              s_newData    = false;
static int               s_lastStatus = 0;
static TaskHandle_t      s_taskHandle = nullptr;

void SolarFeed::init() {
    s_mutex = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));
    xTaskCreate(_fetchTask, "solar_fetch", 8192, nullptr, 1, &s_taskHandle);
}

void SolarFeed::requestFetch() {
    if (s_taskHandle) xTaskNotifyGive(s_taskHandle);
}

bool SolarFeed::hasNewData() { return s_newData; }

bool SolarFeed::getData(SolarData& out) {
    if (!s_hasData) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&out, &s_data, sizeof(out));
    s_newData = false;
    xSemaphoreGive(s_mutex);
    return true;
}

int SolarFeed::lastStatus() { return s_lastStatus; }

void SolarFeed::_fetchTask(void*) {
    for (;;) {
        // Refresh every 10 minutes, or on explicit request
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(600000));

        if (WiFi.status() != WL_CONNECTED) continue;

        DXS_LOG("Solar", "Fetching %s", SOLAR_URL);
        WiFiClientSecure sslClient;
        sslClient.setInsecure();
        HTTPClient http;
        http.begin(sslClient, SOLAR_URL);
        http.setTimeout(12000);
        int code = http.GET();
        s_lastStatus = code;

        if (code == 200) {
            String body = http.getString();
            _parseXml(body.c_str());
            DXS_LOG("Solar", "SFI=%d A=%d K=%d", s_data.sfi, s_data.aindex, s_data.kindex);
        } else {
            DXS_LOG("Solar", "HTTP error %d", code);
        }
        http.end();
    }
}

// ── Minimal XML extraction helpers ────────────────────────────────────
// We avoid a full XML library; hamqsl.com's format is simple enough for
// string search. Returns the text content of the first matching tag.

static const char* _findTag(const char* xml, const char* open, const char* close, size_t* len) {
    const char* s = strstr(xml, open);
    if (!s) return nullptr;
    s += strlen(open);
    const char* e = strstr(s, close);
    if (!e) return nullptr;
    *len = (size_t)(e - s);
    return s;
}

int SolarFeed::_xmlInt(const char* xml, const char* tag, int defaultVal) {
    char open[64], close[64];
    snprintf(open,  sizeof(open),  "<%s>",  tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    size_t len = 0;
    const char* v = _findTag(xml, open, close, &len);
    if (!v || len == 0) return defaultVal;
    char buf[32];
    size_t cpLen = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, v, cpLen);
    buf[cpLen] = '\0';
    return atoi(buf);
}

float SolarFeed::_xmlFloat(const char* xml, const char* tag, float defaultVal) {
    char open[64], close[64];
    snprintf(open,  sizeof(open),  "<%s>",  tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    size_t len = 0;
    const char* v = _findTag(xml, open, close, &len);
    if (!v || len == 0) return defaultVal;
    char buf[32];
    size_t cpLen = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, v, cpLen);
    buf[cpLen] = '\0';
    return (float)atof(buf);
}

bool SolarFeed::_xmlStr(const char* xml, const char* tag, char* out, size_t sz) {
    char open[64], close[64];
    snprintf(open,  sizeof(open),  "<%s>",  tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    size_t len = 0;
    const char* v = _findTag(xml, open, close, &len);
    if (!v) return false;
    size_t cpLen = len < sz - 1 ? len : sz - 1;
    memcpy(out, v, cpLen);
    out[cpLen] = '\0';
    return true;
}

// Parses <band name="80m-40m" time="day">Good</band>
bool SolarFeed::_xmlBand(const char* xml, const char* name, const char* time, char* out, size_t sz) {
    // Find the <band> tag with matching name and time attributes
    const char* p = xml;
    while ((p = strstr(p, "<band")) != nullptr) {
        const char* end = strstr(p, "</band>");
        if (!end) break;
        // Check name attribute
        if (!strstr(p, name)) { p++; continue; }
        // Check time attribute
        if (!strstr(p, time)) { p++; continue; }
        // Extract text between > and </band>
        const char* gt = strchr(p, '>');
        if (!gt || gt >= end) { p++; continue; }
        gt++;
        size_t len = (size_t)(end - gt);
        size_t cpLen = len < sz - 1 ? len : sz - 1;
        memcpy(out, gt, cpLen);
        out[cpLen] = '\0';
        return true;
    }
    return false;
}

void SolarFeed::_parseXml(const char* body) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_data.sfi            = _xmlInt  (body, "solarflux",     0);
    s_data.aindex         = _xmlInt  (body, "aindex",        0);
    s_data.kindex         = _xmlInt  (body, "kindex",        0);
    s_data.solarWindKmS   = _xmlInt  (body, "solarwind",     0);
    s_data.aurora         = _xmlInt  (body, "aurora",        0);
    s_data.magneticField  = _xmlFloat(body, "magneticfield", 0.f);
    _xmlStr(body, "xray", s_data.xray, sizeof(s_data.xray));

    _xmlBand(body, "80m-40m", "day",   s_data.b80_40_day,   sizeof(s_data.b80_40_day));
    _xmlBand(body, "80m-40m", "night", s_data.b80_40_night, sizeof(s_data.b80_40_night));
    _xmlBand(body, "30m-20m", "day",   s_data.b30_20_day,   sizeof(s_data.b30_20_day));
    _xmlBand(body, "30m-20m", "night", s_data.b30_20_night, sizeof(s_data.b30_20_night));
    _xmlBand(body, "17m-15m", "day",   s_data.b17_15_day,   sizeof(s_data.b17_15_day));
    _xmlBand(body, "17m-15m", "night", s_data.b17_15_night, sizeof(s_data.b17_15_night));
    _xmlBand(body, "12m-10m", "day",   s_data.b12_10_day,   sizeof(s_data.b12_10_day));
    _xmlBand(body, "12m-10m", "night", s_data.b12_10_night, sizeof(s_data.b12_10_night));

    s_data.fetchedAt = millis();
    s_hasData = true;
    s_newData = true;

    xSemaphoreGive(s_mutex);
}

}  // namespace dxs
