// DXSpotter — WiFiMgr.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Manages WiFi connection lifecycle: connect on boot, auto-reconnect,
// NTP sync once connected. Singleton; call tick() from the main loop.

#pragma once
#include <Arduino.h>

namespace dxs {

class WiFiMgr {
public:
    static WiFiMgr& instance();

    // Call once from setup() after config is loaded.
    void init();

    // Call every loop iteration. Handles reconnect and NTP sync.
    void tick();

    bool isConnected() const;
    int8_t rssi() const;
    const char* ipStr() const;   // dotted-decimal IP or "" if not connected

    // Trigger an immediate reconnect attempt (e.g. after SSID/pass change).
    void reconnect();

private:
    bool    _initialized   = false;
    bool    _ntpSynced     = false;
    uint32_t _lastConnectAt = 0;
    uint32_t _lastNtpAt    = 0;
    char    _ipBuf[20]     = {};

    static constexpr uint32_t RECONNECT_MS = 30000;
    static constexpr uint32_t NTP_INTERVAL = 3600000UL;  // re-sync every hour
};

}  // namespace dxs
