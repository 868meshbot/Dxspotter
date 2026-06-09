// DXSpotter — WiFiMgr.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WiFiMgr.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include <WiFi.h>
#include <time.h>

namespace dxs {

static WiFiMgr s_mgr;

WiFiMgr& WiFiMgr::instance() { return s_mgr; }

void WiFiMgr::init() {
    WiFi.setAutoReconnect(false);  // we manage reconnect ourselves
    WiFi.setAutoConnect(false);
    WiFi.mode(WIFI_STA);

    const auto& cfg = config::get();
    if (cfg.wifiSSID[0] != '\0') {
        DXS_LOG("WiFi", "Connecting to \"%s\"", cfg.wifiSSID);
        WiFi.begin(cfg.wifiSSID, cfg.wifiPass);
    } else {
        DXS_LOG("WiFi", "No SSID configured — skipping connect");
    }
    _initialized  = true;
    _lastConnectAt = millis();
}

void WiFiMgr::tick() {
    if (!_initialized) return;
    uint32_t now = millis();

    const auto& cfg = config::get();
    if (cfg.wifiSSID[0] == '\0') return;

    if (WiFi.status() != WL_CONNECTED) {
        _ntpSynced = false;
        if (now - _lastConnectAt >= RECONNECT_MS) {
            _lastConnectAt = now;
            DXS_LOG("WiFi", "Reconnecting to \"%s\"", cfg.wifiSSID);
            WiFi.disconnect(true);
            delay(100);
            WiFi.begin(cfg.wifiSSID, cfg.wifiPass);
        }
        return;
    }

    // Just connected or first tick after connection
    if (!_ntpSynced || (now - _lastNtpAt >= NTP_INTERVAL)) {
        _lastNtpAt = now;
        DXS_LOG("WiFi", "Connected, IP=%s, syncing NTP", WiFi.localIP().toString().c_str());
        configTime(cfg.timezoneOffsetHours * 3600L, 0, "pool.ntp.org", "time.cloudflare.com");
        // Wait up to 3 s for NTP sync
        struct tm t;
        uint32_t deadline = millis() + 3000;
        while (!getLocalTime(&t, 10) && millis() < deadline) delay(50);
        _ntpSynced = true;
        DXS_LOG("WiFi", "NTP synced: %04d-%02d-%02d %02d:%02d:%02d",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
        WiFi.localIP().toString().toCharArray(_ipBuf, sizeof(_ipBuf));
    }
}

bool WiFiMgr::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

int8_t WiFiMgr::rssi() const {
    return (int8_t)WiFi.RSSI();
}

const char* WiFiMgr::ipStr() const {
    return _ntpSynced ? _ipBuf : "";
}

void WiFiMgr::reconnect() {
    WiFi.disconnect(true);
    _lastConnectAt = 0;
    _ntpSynced = false;
    tick();
}

}  // namespace dxs
