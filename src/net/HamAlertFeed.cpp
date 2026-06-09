// DXSpotter — HamAlertFeed.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HamAlert delivers a user's triggered spots in real time over a telnet
// DX-cluster feed (hamalert.org:7300), authenticated with the HamAlert
// account username + password. The old REST endpoint (/api/spots) returns an
// empty array, so we keep a persistent telnet connection and parse the
// standard "DX de ..." cluster lines as they stream in.

#include "HamAlertFeed.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace dxs {

static constexpr const char* HA_HOST = "hamalert.org";
static constexpr uint16_t    HA_PORT = 7300;

// lastStatus codes:  -1 = disconnected, 0 = connecting, 200 = streaming, 401 = login failed
static SemaphoreHandle_t s_mutex      = nullptr;
static HamAlert          s_alerts[HA_MAX_ALERTS];
static int               s_count      = 0;
static bool              s_newData    = false;
static uint32_t          s_fetchedAt  = 0;
static int               s_lastStatus = 0;
static uint32_t          s_totalInserted = 0;   // monotonic since boot
static TaskHandle_t      s_taskHandle = nullptr;

void HamAlertFeed::init() {
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(_fetchTask, "ha_telnet", 8192, nullptr, 1, &s_taskHandle);
}

void HamAlertFeed::requestFetch() {
    // Wakes the task (forces a prompt reconnect if currently disconnected)
    if (s_taskHandle) xTaskNotifyGive(s_taskHandle);
}

bool HamAlertFeed::hasNewData() { return s_newData; }

int HamAlertFeed::getAlerts(HamAlert* buf, int maxCount) {
    if (!buf || maxCount <= 0) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_count < maxCount ? s_count : maxCount;
    memcpy(buf, s_alerts, n * sizeof(HamAlert));
    s_newData = false;
    xSemaphoreGive(s_mutex);
    return n;
}

uint32_t HamAlertFeed::lastFetchMs() { return s_fetchedAt; }
int      HamAlertFeed::lastStatus()  { return s_lastStatus; }

uint32_t HamAlertFeed::totalInserted() { return s_totalInserted; }

bool HamAlertFeed::getLatest(HamAlert& out) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool ok = (s_count > 0);
    if (ok) out = s_alerts[0];
    xSemaphoreGive(s_mutex);
    return ok;
}

// ── Insert one alert, newest-first ────────────────────────────────────
static void _insert(const HamAlert& h) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int keep = s_count < HA_MAX_ALERTS ? s_count : HA_MAX_ALERTS - 1;
    for (int i = keep; i > 0; i--) s_alerts[i] = s_alerts[i - 1];
    s_alerts[0] = h;
    if (s_count < HA_MAX_ALERTS) s_count++;
    s_totalInserted++;
    s_fetchedAt = millis();
    s_newData   = true;
    xSemaphoreGive(s_mutex);
}

// ── Parse one "DX de SPOTTER:  FREQ  DXCALL  comment ...  HHMMZ" line ──
static void _processLine(const char* line) {
    if (strncmp(line, "DX de ", 6) != 0) return;

    const char* colon = strchr(line + 6, ':');
    if (!colon) return;

    HamAlert h;
    memset(&h, 0, sizeof(h));

    // Spotter: between "DX de " and the ':'
    int sl = (int)(colon - (line + 6));
    if (sl > (int)sizeof(h.spotter) - 1) sl = sizeof(h.spotter) - 1;
    strncpy(h.spotter, line + 6, sl);

    // Frequency (kHz) then DX call
    const char* rest = colon + 1;
    char* endp = nullptr;
    double kHz = strtod(rest, &endp);
    if (endp == rest || kHz <= 0) return;
    while (*endp == ' ') endp++;

    int i = 0;
    while (*endp && *endp != ' ' && i < (int)sizeof(h.dxCall) - 1) h.dxCall[i++] = *endp++;
    h.dxCall[i] = '\0';
    if (h.dxCall[0] == '\0') return;
    while (*endp == ' ') endp++;

    // Remainder is "comment ... HHMMZ"
    char body[96];
    strncpy(body, endp, sizeof(body) - 1);
    body[sizeof(body) - 1] = '\0';
    int bl = (int)strlen(body);
    while (bl > 0 && (body[bl - 1] == ' ' || body[bl - 1] == '\t')) body[--bl] = '\0';

    // Trailing time token "HHMMZ"
    if (bl >= 5 && (body[bl - 1] == 'Z' || body[bl - 1] == 'z') &&
        isdigit((unsigned char)body[bl - 2]) && isdigit((unsigned char)body[bl - 3]) &&
        isdigit((unsigned char)body[bl - 4]) && isdigit((unsigned char)body[bl - 5])) {
        strncpy(h.utcTime, body + bl - 5, 4);
        h.utcTime[4] = '\0';
        int cut = bl - 5;
        while (cut > 0 && body[cut - 1] == ' ') cut--;
        body[cut] = '\0';
    }

    strncpy(h.comment, body, sizeof(h.comment) - 1);
    snprintf(h.freq, sizeof(h.freq), "%.3f", kHz / 1000.0);
    strncpy(h.source, "HamAlert", sizeof(h.source) - 1);

    // Light mode detection from the first comment token
    char m[8] = {0};
    if (sscanf(body, "%7s", m) == 1) {
        static const char* kModes[] = { "CW","SSB","FT8","FT4","RTTY","PSK","FM","AM" };
        for (const char* mm : kModes)
            if (strcasecmp(m, mm) == 0) { strncpy(h.mode, mm, sizeof(h.mode) - 1); break; }
    }

    _insert(h);
}

// ── Read from the socket until `token` appears (login handshake) ──────
static bool _waitFor(WiFiClient& c, const char* token, uint32_t timeoutMs) {
    String acc;
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        while (c.available()) {
            char ch = (char)c.read();
            acc += ch;
            if (acc.length() > 96) acc.remove(0, acc.length() - 48);
            if (acc.indexOf(token) >= 0) return true;
        }
        if (!c.connected()) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

// ── Background telnet task ────────────────────────────────────────────
void HamAlertFeed::_fetchTask(void*) {
    WiFiClient client;
    char lineBuf[200];
    int  lineLen = 0;

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            if (client.connected()) client.stop();
            s_lastStatus = -1;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));
            continue;
        }

        const auto& cfg = config::get();
        if (cfg.hamAlertUser[0] == '\0' || cfg.hamAlertKey[0] == '\0') {
            if (client.connected()) client.stop();
            s_lastStatus = -1;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
            continue;
        }

        // (Re)connect + log in if needed
        if (!client.connected()) {
            lineLen = 0;
            s_lastStatus = 0;  // connecting
            DXS_LOG("HamAlert", "Telnet connecting to %s:%u as %s",
                    HA_HOST, HA_PORT, cfg.hamAlertUser);
            if (!client.connect(HA_HOST, HA_PORT)) {
                DXS_LOG("HamAlert", "Telnet connect failed");
                s_lastStatus = -1;
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
                continue;
            }

            if (!_waitFor(client, "login", 10000)) {
                DXS_LOG("HamAlert", "No login prompt");
                client.stop();
                s_lastStatus = -1;
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
                continue;
            }
            client.print(cfg.hamAlertUser);
            client.print("\r\n");

            if (!_waitFor(client, "password", 10000)) {
                DXS_LOG("HamAlert", "No password prompt");
                client.stop();
                s_lastStatus = -1;
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
                continue;
            }
            client.print(cfg.hamAlertKey);
            client.print("\r\n");

            s_lastStatus = 200;  // streaming
            DXS_LOG("HamAlert", "Telnet logged in, streaming spots");
        }

        // Drain available bytes into line buffer, process complete lines
        while (client.available()) {
            char ch = (char)client.read();
            if (ch == '\n' || ch == '\r') {
                if (lineLen > 0) {
                    lineBuf[lineLen] = '\0';
                    // A second login prompt mid-stream means auth was rejected
                    if (strstr(lineBuf, "login:") || strstr(lineBuf, "Login incorrect"))
                        s_lastStatus = 401;
                    _processLine(lineBuf);
                    lineLen = 0;
                }
            } else if (lineLen < (int)sizeof(lineBuf) - 1) {
                lineBuf[lineLen++] = ch;
            } else {
                lineLen = 0;  // overflow — drop the line
            }
        }

        if (!client.connected()) {
            DXS_LOG("HamAlert", "Telnet disconnected");
            client.stop();
            s_lastStatus = -1;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
            continue;
        }

        // Idle wait — wakes early on requestFetch()
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(150));
    }
}

}  // namespace dxs
