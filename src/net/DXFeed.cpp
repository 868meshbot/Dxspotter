// DXSpotter — DXFeed.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DXFeed.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace dxs {

// ── DXwatch feed URL ──────────────────────────────────────────────────
static constexpr const char* URL_DXWATCH =
    "https://dxwatch.com/dxsd1/s.php?s=0&r=20";
static constexpr const char* URL_DXHEAT =
    "https://dxheat.com/dxc/?entries=20";
static constexpr const char* URL_DXSCAPE_EU =
    "http://www.dxscape.com/eucw.html";
static constexpr const char* URL_DXSCAPE_WW =
    "http://www.dxscape.com/ww.html";
static constexpr const char* URL_DXLITE =
    "https://dxlite.g7vjr.org/";

// ── Shared state (protected by mutex) ─────────────────────────────────
static SemaphoreHandle_t s_mutex     = nullptr;
static DXSpot            s_spots[DX_MAX_SPOTS];
static int               s_spotCount = 0;
static bool              s_newData   = false;
static uint32_t          s_fetchedAt = 0;
static int               s_lastStatus = 0;

// ── Task state ────────────────────────────────────────────────────────
static TaskHandle_t      s_taskHandle  = nullptr;
static volatile bool     s_fetchPending = false;

// Skip leading whitespace/newlines (responses sometimes start with a blank line).
static const char* _skipWs(const char* s) {
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
    return s;
}

// Drop any stale spots so the UI shows "no spots" after a bad/blocked response.
static void _clearSpots() {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_spotCount = 0;
    s_newData   = true;
    xSemaphoreGive(s_mutex);
}

void DXFeed::init() {
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(_fetchTask, "dx_fetch", 8192, nullptr, 1, &s_taskHandle);
}

void DXFeed::requestFetch() {
    s_fetchPending = true;
    if (s_taskHandle) xTaskNotifyGive(s_taskHandle);
}

bool DXFeed::hasNewData() {
    return s_newData;
}

int DXFeed::getSpots(DXSpot* buf, int maxCount) {
    if (!buf || maxCount <= 0) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_spotCount < maxCount ? s_spotCount : maxCount;
    memcpy(buf, s_spots, n * sizeof(DXSpot));
    s_newData = false;
    xSemaphoreGive(s_mutex);
    return n;
}

uint32_t DXFeed::lastFetchMs() { return s_fetchedAt; }
int      DXFeed::lastStatus()  { return s_lastStatus; }

// ── Background fetch task ─────────────────────────────────────────────
void DXFeed::_fetchTask(void*) {
    for (;;) {
        // Wait for a fetch request or periodic 60s wakeup
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60000));

        if (WiFi.status() != WL_CONNECTED) {
            DXS_LOG("DXFeed", "WiFi not connected, skipping fetch");
            continue;
        }

        const auto& cfg = config::get();

        // Holy Cluster spots are delivered via WebSocket in HolyClusterFeed
        if (cfg.dxFeedSource == DX_FEED_HOLYCLUSTER) continue;
        static char dxliteUrl[80];
        const char* url;
        switch (cfg.dxFeedSource) {
            case DX_FEED_DXHEAT:     url = URL_DXHEAT;        break;
            case DX_FEED_DXSCAPE_EU: url = URL_DXSCAPE_EU;    break;
            case DX_FEED_DXSCAPE_WW: url = URL_DXSCAPE_WW;    break;
            case DX_FEED_DXLITE:
                // Optional ?dx=<call> filter; base URL = all spots.
                if (cfg.dxLiteCall[0])
                    snprintf(dxliteUrl, sizeof(dxliteUrl), "%s?dx=%s",
                             URL_DXLITE, cfg.dxLiteCall);
                else
                    strncpy(dxliteUrl, URL_DXLITE, sizeof(dxliteUrl) - 1);
                url = dxliteUrl;
                break;
            case DX_FEED_CUSTOM:     url = cfg.dxFeedUrl[0]
                                           ? cfg.dxFeedUrl
                                           : URL_DXWATCH;     break;
            default:                 url = URL_DXWATCH;       break;
        }

        DXS_LOG("DXFeed", "Fetching %s", url);

        // DXScape is plain HTTP; the JSON feeds are HTTPS. Pick the right client.
        bool isHttps = (strncmp(url, "https", 5) == 0);
        WiFiClient       plainClient;
        WiFiClientSecure sslClient;
        sslClient.setInsecure();   // spot data is public — no cert check
        HTTPClient http;
        if (isHttps) http.begin(sslClient, url);
        else         http.begin(plainClient, url);
        http.setTimeout(12000);
        // Browser UA — the firmware's own UA is bot-blocked by several sources.
        http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36");
        http.addHeader("Accept", "text/html,application/json");
        int code = http.GET();
        s_lastStatus = code;

        if (code == 200) {
            String body = http.getString();
            DXS_LOG("DXFeed", "%d bytes: %.120s", (int)body.length(), body.c_str());
            switch (cfg.dxFeedSource) {
                case DX_FEED_DXHEAT:                              _parseDXHeat(body.c_str());   break;
                case DX_FEED_DXSCAPE_EU:
                case DX_FEED_DXSCAPE_WW:                          _parseDXScape(body.c_str());  break;
                case DX_FEED_DXLITE:                              _parseDXLite(body.c_str());   break;
                default:                                          _parseDXWatch(body.c_str());  break;
            }
            s_fetchedAt = millis();
            DXS_LOG("DXFeed", "Parsed %d spots", s_spotCount);
        } else {
            DXS_LOG("DXFeed", "HTTP error %d", code);
        }
        http.end();
    }
}

// ── DXwatch JSON parser ───────────────────────────────────────────────
// Current format (as of 2026):
//   {"s": {"SPOT_ID": [spotter, freq_kHz, dx_call, comment, "HHMMz DD Mon", ...]}}
// Legacy format (kept as fallback):
//   {"dx_data": [{"dx_call":"...","freq":"...","spotter":"...","comment":"...","time":"..."}]}
void DXFeed::_parseDXWatch(const char* body) {
    body = _skipWs(body);
    if (*body == '<' || *body == '\0') {
        DXS_LOG("DXFeed", "Got HTML/empty — service blocked or moved");
        _clearSpots();
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        DXS_LOG("DXFeed", "JSON parse error: %s", err.c_str());
        _clearSpots();
        return;
    }

    // ── Current format: {"s": {id: [spotter, freqKHz, dxCall, comment, time, ...]}} ──
    JsonObject sObj = doc["s"].as<JsonObject>();
    if (sObj) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_spotCount = 0;
        for (JsonPair kv : sObj) {
            if (s_spotCount >= DX_MAX_SPOTS) break;
            JsonArray a = kv.value().as<JsonArray>();
            if (!a || a.size() < 5) continue;
            DXSpot& s = s_spots[s_spotCount++];
            memset(&s, 0, sizeof(s));
            strncpy(s.spotter, a[0] | "", sizeof(s.spotter) - 1);
            snprintf(s.freq, sizeof(s.freq), "%.1f", (double)a[1].as<float>());
            strncpy(s.dxCall,  a[2] | "", sizeof(s.dxCall)  - 1);
            strncpy(s.comment, a[3] | "", sizeof(s.comment) - 1);
            // time field: "1807z 07 Jun" — keep only the HHMM part
            const char* t = a[4] | "";
            strncpy(s.utcTime, t, 4);
            s.utcTime[4] = '\0';
        }
        s_newData = true;
        xSemaphoreGive(s_mutex);
        DXS_LOG("DXFeed", "Parsed %d spots (new format)", s_spotCount);
        return;
    }

    // ── Legacy fallback: {"dx_data": [{...}]} ────────────────────────────
    JsonArray arr = doc["dx_data"].as<JsonArray>();
    if (!arr) {
        DXS_LOG("DXFeed", "Unknown JSON structure — first 80 chars: %.80s", body);
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_spotCount = 0;
    for (JsonObject spot : arr) {
        if (s_spotCount >= DX_MAX_SPOTS) break;
        DXSpot& s = s_spots[s_spotCount++];
        memset(&s, 0, sizeof(s));
        strncpy(s.dxCall,  spot["dx_call"] | "", sizeof(s.dxCall)  - 1);
        strncpy(s.spotter, spot["spotter"]  | "", sizeof(s.spotter) - 1);
        strncpy(s.freq,    spot["freq"]     | "", sizeof(s.freq)    - 1);
        strncpy(s.comment, spot["comment"]  | "", sizeof(s.comment) - 1);
        strncpy(s.utcTime, spot["time"]     | "", sizeof(s.utcTime) - 1);
    }
    s_newData = true;
    xSemaphoreGive(s_mutex);
    DXS_LOG("DXFeed", "Parsed %d spots (legacy format)", s_spotCount);
}

// ── DXheat JSON parser ────────────────────────────────────────────────
// NOTE: dxheat.com has started returning HTML instead of JSON (service changed).
// The parser detects this and returns early so the UI shows "no spots" cleanly.
// Expected JSON (when working): [{"dxcall":"...","freq":"...","spottercall":"...","comment":"...","time":"...","mode":"..."},...]
void DXFeed::_parseDXHeat(const char* body) {
    body = _skipWs(body);
    if (*body == '<' || *body == '\0') {
        DXS_LOG("DXFeed", "DXheat returned HTML — API moved/blocked. Switch to Holy Cluster.");
        _clearSpots();
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        DXS_LOG("DXFeed", "JSON parse error: %s", err.c_str());
        _clearSpots();
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (!arr) {
        DXS_LOG("DXFeed", "Expected top-level JSON array");
        _clearSpots();
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_spotCount = 0;
    for (JsonObject spot : arr) {
        if (s_spotCount >= DX_MAX_SPOTS) break;
        DXSpot& s = s_spots[s_spotCount++];
        memset(&s, 0, sizeof(s));
        strncpy(s.dxCall,  spot["dxcall"]      | "", sizeof(s.dxCall)  - 1);
        strncpy(s.spotter, spot["spottercall"]  | "", sizeof(s.spotter) - 1);
        strncpy(s.freq,    spot["freq"]         | "", sizeof(s.freq)    - 1);
        strncpy(s.comment, spot["comment"]      | "", sizeof(s.comment) - 1);
        strncpy(s.utcTime, spot["time"]         | "", sizeof(s.utcTime) - 1);
        strncpy(s.mode,    spot["mode"]         | "", sizeof(s.mode)    - 1);
    }
    s_newData = true;
    xSemaphoreGive(s_mutex);
}

// ── DXScape HTML cluster parser ───────────────────────────────────────
// The dxscape.com pages embed a fixed-column spot table inside <pre>:
//   IQ7PU        2138Z   3590.5 80 Years Italian Republic rtty  IU7OZL
//   <dxcall>     <HHMMZ>  <freq> <comment ...>                   <spotter>
// Lines that aren't spot rows (headers, table markup) fail validation and are
// skipped. HTML entities (&#45; etc.) are decoded; freq is already in kHz.

// Strip HTML tags and decode the few entities DXScape uses, in place into out.
static void _sanitize(const char* in, char* out, int outSz) {
    int o = 0;
    for (int i = 0; in[i] && o < outSz - 1; ) {
        if (in[i] == '<') {                       // skip an HTML tag
            while (in[i] && in[i] != '>') i++;
            if (in[i] == '>') i++;
            continue;
        }
        if (in[i] == '&') {
            if      (!strncmp(in + i, "&#45;", 5)) { out[o++] = '-'; i += 5; continue; }
            else if (!strncmp(in + i, "&lt;",  4)) { out[o++] = '<'; i += 4; continue; }
            else if (!strncmp(in + i, "&gt;",  4)) { out[o++] = '>'; i += 4; continue; }
            else if (!strncmp(in + i, "&amp;", 5)) { out[o++] = '&'; i += 5; continue; }
            else if (!strncmp(in + i, "&nbsp;",6)) { out[o++] = ' '; i += 6; continue; }
        }
        out[o++] = in[i++];
    }
    out[o] = '\0';
}

void DXFeed::_parseDXScape(const char* body) {
    // Start at the <pre> block if present (skips the worldwide page's header).
    const char* p = strstr(body, "<pre>");
    if (!p) p = strstr(body, "<PRE>");
    p = p ? p + 5 : body;
    const char* end = strstr(p, "</pre>");
    if (!end) end = strstr(p, "</PRE>");

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_spotCount = 0;

    while (*p && (!end || p < end) && s_spotCount < DX_MAX_SPOTS) {
        const char* nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);

        char raw[200];
        int n = len < (int)sizeof(raw) - 1 ? len : (int)sizeof(raw) - 1;
        memcpy(raw, p, n);
        raw[n] = '\0';
        p = nl ? nl + 1 : p + len;

        char line[200];
        _sanitize(raw, line, sizeof(line));

        const char* q = line;
        while (*q == ' ' || *q == '\t') q++;

        // dxcall
        char dxcall[16]; int i = 0;
        while (*q && *q != ' ' && i < 15) dxcall[i++] = *q++;
        dxcall[i] = '\0';
        while (*q == ' ') q++;

        // time "HHMMZ"
        char tm[8]; i = 0;
        while (*q && *q != ' ' && i < 7) tm[i++] = *q++;
        tm[i] = '\0';
        while (*q == ' ') q++;
        if (strlen(tm) < 5 || (tm[4] != 'Z' && tm[4] != 'z') ||
            !isdigit((unsigned char)tm[0]) || !isdigit((unsigned char)tm[1]) ||
            !isdigit((unsigned char)tm[2]) || !isdigit((unsigned char)tm[3]))
            continue;   // not a spot row

        // frequency (kHz)
        char fq[16]; i = 0;
        while (*q && *q != ' ' && i < 15) fq[i++] = *q++;
        fq[i] = '\0';
        if (atof(fq) <= 0) continue;
        while (*q == ' ') q++;

        // remainder = "comment ... spotter"; spotter is the last token
        char rest[160];
        strncpy(rest, q, sizeof(rest) - 1);
        rest[sizeof(rest) - 1] = '\0';
        int rl = (int)strlen(rest);
        while (rl > 0 && (rest[rl - 1] == ' ' || rest[rl - 1] == '\t')) rest[--rl] = '\0';

        char spotter[16] = {0};
        char* sp = strrchr(rest, ' ');
        if (sp) {
            strncpy(spotter, sp + 1, sizeof(spotter) - 1);
            *sp = '\0';
            int cl = (int)strlen(rest);
            while (cl > 0 && rest[cl - 1] == ' ') rest[--cl] = '\0';
        } else {
            strncpy(spotter, rest, sizeof(spotter) - 1);
            rest[0] = '\0';
        }

        DXSpot& s = s_spots[s_spotCount++];
        memset(&s, 0, sizeof(s));
        strncpy(s.dxCall,  dxcall,  sizeof(s.dxCall)  - 1);
        strncpy(s.spotter, spotter, sizeof(s.spotter) - 1);
        strncpy(s.freq,    fq,      sizeof(s.freq)    - 1);
        strncpy(s.comment, rest,    sizeof(s.comment) - 1);
        strncpy(s.utcTime, tm, 4);   // drop the trailing 'Z'
        s.utcTime[4] = '\0';
        // mode is inferred from frequency in the UI (DXScape has no mode column)
    }

    s_newData = true;
    xSemaphoreGive(s_mutex);
}

// ── DXLite HTML cluster parser ────────────────────────────────────────
// dxlite.g7vjr.org returns one <tr> per spot, five <td> cells:
//   <td>SPOTTER</td><td>FREQ_kHz</td><td><b>DXCALL</b></td>
//   <td><small>COMMENT</small></td><td>TIME</td>
// TIME is "HH:MMZ" (all-spots view) or "YYYY-MM-DD HH:MM:SS" (?dx= history view);
// we keep the HH:MM part. Inner tags/entities are stripped by _sanitize().
// No mode column → inferred from frequency in the UI.
void DXFeed::_parseDXLite(const char* body) {
    body = _skipWs(body);
    if (*body != '<') {           // expected HTML; anything else = blocked/empty
        DXS_LOG("DXFeed", "DXLite: non-HTML response — blocked?");
        _clearSpots();
        return;
    }

    const char* p = body;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_spotCount = 0;

    while (s_spotCount < DX_MAX_SPOTS) {
        const char* tr = strstr(p, "<tr");
        if (!tr) break;
        const char* trEnd = strstr(tr, "</tr>");
        if (!trEnd) break;

        // Pull the (up to) five <td> cell contents from this row.
        char cells[5][64];
        int  cn = 0;
        const char* q = tr;
        while (cn < 5) {
            const char* td = strstr(q, "<td");
            if (!td || td >= trEnd) break;
            const char* gt = strchr(td, '>');
            if (!gt || gt >= trEnd) break;
            const char* cs = gt + 1;
            const char* ce = strstr(cs, "</td>");
            if (!ce || ce > trEnd) break;
            char raw[80];
            int rn = (int)(ce - cs);
            if (rn > (int)sizeof(raw) - 1) rn = sizeof(raw) - 1;
            memcpy(raw, cs, rn);
            raw[rn] = '\0';
            _sanitize(raw, cells[cn], sizeof(cells[cn]));   // strip <b>/<small>, decode entities
            cn++;
            q = ce + 5;
        }
        p = trEnd + 5;
        if (cn < 5) continue;                // not a complete spot row
        if (atof(cells[1]) <= 0) continue;   // freq must be numeric

        // Trim trailing whitespace from the comment (DXLite pads it).
        int cl = (int)strlen(cells[3]);
        while (cl > 0 && (cells[3][cl - 1] == ' ' || cells[3][cl - 1] == '\t'))
            cells[3][--cl] = '\0';

        // Time → HH:MM. History view is "YYYY-MM-DD HH:MM:SS"; take after the space.
        const char* t = cells[4];
        const char* sp = strrchr(t, ' ');
        if (sp) t = sp + 1;

        DXSpot& s = s_spots[s_spotCount++];
        memset(&s, 0, sizeof(s));
        strncpy(s.spotter, cells[0], sizeof(s.spotter) - 1);
        strncpy(s.freq,    cells[1], sizeof(s.freq)    - 1);
        strncpy(s.dxCall,  cells[2], sizeof(s.dxCall)  - 1);
        strncpy(s.comment, cells[3], sizeof(s.comment) - 1);
        strncpy(s.utcTime, t, 5);            // "HH:MM" (drops trailing 'Z' / ':SS')
        s.utcTime[5] = '\0';
    }

    s_newData = true;
    xSemaphoreGive(s_mutex);
}

}  // namespace dxs
