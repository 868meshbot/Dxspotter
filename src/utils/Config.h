// DXSpotter — Config.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Persistent configuration stored in ESP32 NVS via Preferences.

#pragma once
#include <Arduino.h>

namespace dxs {

// DX feed source index
enum DXFeedSource : uint8_t {
    DX_FEED_DXWATCH     = 0,  // dxwatch.com JSON API (default)
    DX_FEED_DXHEAT      = 1,  // dxheat.com JSON API (DISABLED — bot-blocked, kept for NVS compat)
    DX_FEED_CUSTOM      = 2,  // user-defined URL
    DX_FEED_HOLYCLUSTER = 3,  // holycluster.iarc.org WebSocket real-time feed
    DX_FEED_DXSCAPE_EU  = 4,  // dxscape.com EU/CW HTML cluster page
    DX_FEED_DXSCAPE_WW  = 5,  // dxscape.com worldwide HTML cluster page
    DX_FEED_DXLITE      = 6,  // dxlite.g7vjr.org HTML cluster (optional ?dx=<call> filter)
};

struct Config {
    char    callsign[16];           // ham callsign shown in UI
    char    wifiSSID[64];           // WiFi network name
    char    wifiPass[64];           // WiFi password
    uint8_t dxFeedSource;           // DXFeedSource enum
    char    dxFeedUrl[128];         // custom feed URL (used when source==DX_FEED_CUSTOM)
    char    dxLiteCall[16];         // DXLite filter callsign (empty = all spots)
    char    hamAlertUser[32];       // HamAlert.org username (telnet login name)
    char    hamAlertKey[64];        // HamAlert telnet password (set in HamAlert; NOT the account password)
    char    hamAlertPass[64];       // HamAlert.org account/web password — used by HamAlertApi to log into hamalert.org and manage triggers (add/delete). Separate from the telnet password above.
    uint8_t haNotifyBeep;           // 1 = beep on a new HamAlert ("HamAlert Notification")
    uint8_t haNotifyPopup;          // 1 = show a details popup on a new HamAlert
    uint8_t volume;                 // audio output volume 0-255 (beeps, boot chime)
    uint8_t brightness;             // display backlight 0-255
    uint8_t kbBrightness;           // keyboard backlight level 0-255
    uint8_t kbAutoNight;            // 1 = auto KB backlight 21:00–07:00, off in sleep
    uint8_t theme;                  // colour theme 0-2
    int8_t  timezoneOffsetHours;    // UTC offset -11 to +11
};

namespace config {
    void init();
    void save();
    const Config& get();

    void setCallsign(const char* cs);
    void setWiFi(const char* ssid, const char* pass);
    void setDXFeed(uint8_t source, const char* customUrl = "");
    void setDXLiteCall(const char* call);
    void setHamAlert(const char* user, const char* key);
    void setHamAlertPass(const char* pass);
    void setHamAlertBeep(bool on);
    void setHamAlertPopup(bool on);
    void setVolume(uint8_t v);
    void setBrightness(uint8_t b);
    void setKbBrightness(uint8_t level);
    void setKbAutoNight(bool on);
    void setTheme(uint8_t t);
    void setTimezone(int8_t offsetHours);
}

}  // namespace dxs
