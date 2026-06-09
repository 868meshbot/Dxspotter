// DXSpotter — DXWorldFeed.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DXWorldFeed.h"
#include "../utils/Log.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>

namespace dxs {

static constexpr const char* URL =
    "https://www.hamradiotimeline.com/timeline/dxw_timeline_1_1.php";
// Browser User-Agent — the firmware's own UA is bot-blocked by Cloudflare.
static constexpr const char* BROWSER_UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0 Safari/537.36";

static SemaphoreHandle_t s_mutex      = nullptr;
static DXpedition        s_peds[DXPED_MAX];
static int               s_pedCount   = 0;
static bool              s_newData    = false;
static uint32_t          s_fetchedAt  = 0;
static int               s_lastStatus = 0;
static TaskHandle_t      s_taskHandle = nullptr;

void DXWorldFeed::init() {
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(_fetchTask, "dxw_fetch", 8192, nullptr, 1, &s_taskHandle);
    DXS_LOG("DXW", "Initialised");
}

void DXWorldFeed::requestFetch() {
    if (s_taskHandle) xTaskNotifyGive(s_taskHandle);
}

bool     DXWorldFeed::hasNewData() { return s_newData; }
uint32_t DXWorldFeed::lastFetchMs() { return s_fetchedAt; }
int      DXWorldFeed::lastStatus()  { return s_lastStatus; }

int DXWorldFeed::getDXpeditions(DXpedition* buf, int maxCount) {
    if (!buf || maxCount <= 0) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_pedCount < maxCount ? s_pedCount : maxCount;
    memcpy(buf, s_peds, (size_t)n * sizeof(DXpedition));
    s_newData = false;
    xSemaphoreGive(s_mutex);
    return n;
}

// ── Parse helpers ─────────────────────────────────────────────────────
static int _monthNum(const char* m) {
    static const char* k[] = { "jan","feb","mar","apr","may","jun",
                               "jul","aug","sep","oct","nov","dec" };
    char p[4] = { (char)tolower((unsigned char)m[0]),
                  (char)tolower((unsigned char)m[1]),
                  (char)tolower((unsigned char)m[2]), 0 };
    for (int i = 0; i < 12; i++) if (strcmp(p, k[i]) == 0) return i + 1;
    return 0;
}

// Extract single-quoted tokens from [p,end) into out[][stride]. Returns count.
static int _parseQuoted(const char* p, const char* end,
                        char* out, int stride, int maxN) {
    int n = 0;
    while (p < end && n < maxN) {
        if (*p == '\'') {
            const char* q = p + 1;
            int i = 0;
            while (q < end && *q != '\'') { if (i < stride - 1) out[n * stride + i++] = *q; q++; }
            out[n * stride + i] = '\0';
            n++;
            p = (q < end) ? q + 1 : q;
        } else p++;
    }
    return n;
}

// Extract <b>…</b> contents from [p,end) into out[][stride]. Returns count.
static int _parseBold(const char* p, const char* end,
                      char* out, int stride, int maxN) {
    int n = 0;
    while (n < maxN) {
        const char* b = strstr(p, "<b>");
        if (!b || b >= end) break;
        b += 3;
        const char* e = strstr(b, "</b>");
        if (!e || e >= end) break;
        int i = 0;
        for (const char* q = b; q < e && i < stride - 1; q++) out[n * stride + i++] = *q;
        out[n * stride + i] = '\0';
        n++;
        p = e + 4;
    }
    return n;
}

// Extract Gantt event arrays [startDay, duration] from [p,end). An event array
// is a '[' whose first non-space char is a digit (outer row arrays start with
// another '['). Returns count.
static int _parseEvents(const char* p, const char* end,
                        int* start, int* dur, int maxN) {
    int n = 0;
    while (p < end && n < maxN) {
        if (*p == '[') {
            const char* q = p + 1;
            while (q < end && isspace((unsigned char)*q)) q++;
            if (q < end && isdigit((unsigned char)*q)) {
                start[n] = atoi(q);
                while (q < end && *q != ',' && *q != ']') q++;
                int d = 0;
                if (q < end && *q == ',') {
                    q++;
                    while (q < end && isspace((unsigned char)*q)) q++;
                    if (q < end && isdigit((unsigned char)*q)) d = atoi(q);
                }
                dur[n] = d;
                n++;
                p = q;
                continue;
            }
        }
        p++;
    }
    return n;
}

void DXWorldFeed::_parse(const char* html) {
    static constexpr int MAXN = 40;
    static char call[MAXN][16];
    static char ent [MAXN][40];
    static int  evStart[MAXN];
    static int  evDur  [MAXN];

    // Month / year from "Last update: <Month> <day>, <year>"
    int mon = 0, year = 0, day = 0;
    const char* lu = strstr(html, "Last update:");
    if (lu) {
        char mname[16] = {0};
        if (sscanf(lu, "Last update: %15s %d, %d", mname, &day, &year) >= 1)
            mon = _monthNum(mname);
    }
    if (!mon || !year) {
        time_t t = time(nullptr);
        struct tm tmv;
        gmtime_r(&t, &tmv);
        if (!mon)  mon  = tmv.tm_mon + 1;
        if (!year) year = tmv.tm_year + 1900;
    }

    // The callsign array is `var labels = [` inside ondraw (the other 'labels'
    // is the 1..30 day axis, which we ignore).
    int nCall = 0;
    const char* lbCall = strstr(html, "var labels = [");
    if (lbCall) {
        const char* e = strchr(lbCall, ']');
        if (e) nCall = _parseQuoted(lbCall, e, &call[0][0], 16, MAXN);
    }

    // .set('tooltips', [ "<b>NAME</b>...","..." ])  → entity names
    int nEnt = 0;
    const char* tt = strstr(html, "'tooltips'");
    if (tt) {
        const char* e = strstr(tt, "])");
        if (e) nEnt = _parseBold(tt, e, &ent[0][0], 40, MAXN);
    }

    // data = [ [[s,d,...],[s,d,...]], ... ]  → date bars (until 'var gantt2')
    int nEv = 0;
    const char* dt = strstr(html, "data = [");
    if (!dt) dt = strstr(html, "data=[");
    if (dt) {
        const char* e = strstr(dt, "var gantt2");
        if (!e) e = dt + strlen(dt);
        nEv = _parseEvents(dt + 6, e, evStart, evDur, MAXN);
    }

    if (nCall == 0 || nEv == 0) {
        DXS_LOG("DXW", "Parse failed (calls=%d ents=%d evs=%d) — page blocked or changed?",
                nCall, nEnt, nEv);
        return;
    }

    int n = nCall;
    if (nEv  < n) n = nEv;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_pedCount = 0;
    for (int k = 0; k < n && s_pedCount < DXPED_MAX; k++) {
        if (call[k][0] == '\0' || evDur[k] <= 0) continue;   // blank/placeholder bar
        int sDay = evStart[k] + 1;
        int eDay = evStart[k] + evDur[k];
        if (sDay < 1)  sDay = 1;   if (sDay > 31) sDay = 31;
        if (eDay < 1)  eDay = 1;   if (eDay > 31) eDay = 31;

        DXpedition& d = s_peds[s_pedCount++];
        memset(&d, 0, sizeof(d));
        strncpy(d.callsign, call[k], sizeof(d.callsign) - 1);
        if (k < nEnt) strncpy(d.entity, ent[k], sizeof(d.entity) - 1);
        snprintf(d.startDate, sizeof(d.startDate), "%04d-%02d-%02d", year, mon, sDay);
        snprintf(d.endDate,   sizeof(d.endDate),   "%04d-%02d-%02d", year, mon, eDay);
        d.isActive = false;   // derived from dates in the UI
    }
    s_newData   = true;
    s_fetchedAt = millis();
    xSemaphoreGive(s_mutex);
    DXS_LOG("DXW", "Parsed %d DXpeditions (%d-%02d)", s_pedCount, year, mon);
}

// ── Background fetch task (30-min periodic) ───────────────────────────
void DXWorldFeed::_fetchTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1800000));  // 30 min

        if (WiFi.status() != WL_CONNECTED) {
            DXS_LOG("DXW", "No WiFi, skipping");
            continue;
        }

        DXS_LOG("DXW", "Fetching %s", URL);
        WiFiClientSecure sslClient;
        sslClient.setInsecure();
        HTTPClient http;
        http.begin(sslClient, URL);
        http.setTimeout(15000);
        http.setUserAgent(BROWSER_UA);
        http.addHeader("Referer", "https://www.hamradiotimeline.com/");
        http.addHeader("Accept",  "text/html,application/xhtml+xml");
        int code = http.GET();
        s_lastStatus = code;

        if (code == 200) {
            String body = http.getString();
            DXS_LOG("DXW", "%d bytes", (int)body.length());
            _parse(body.c_str());
        } else {
            DXS_LOG("DXW", "HTTP error %d", code);
        }
        http.end();
    }
}

}  // namespace dxs
