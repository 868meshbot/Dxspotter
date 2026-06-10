// DXSpotter — Config.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"
#include "Log.h"
#include <Preferences.h>
#include <cstring>

namespace dxs {

static Config s_cfg;
static Preferences s_prefs;

static void _setDefaults() {
    memset(&s_cfg, 0, sizeof(s_cfg));
    strncpy(s_cfg.callsign, "N0CALL", sizeof(s_cfg.callsign) - 1);
    s_cfg.dxFeedSource        = DX_FEED_DXWATCH;
    s_cfg.haNotifyBeep        = 1;
    s_cfg.haNotifyPopup       = 1;
    s_cfg.volume              = 90;   // moderate; full scale 255 is quite loud
    s_cfg.brightness          = 200;
    s_cfg.kbBrightness        = 200;
    s_cfg.kbAutoNight         = 1;
    s_cfg.theme               = 0;
    s_cfg.timezoneOffsetHours = 0;
}

namespace config {

void init() {
    _setDefaults();
    s_prefs.begin("dxspotter", true);  // read-only

    s_prefs.getString("callsign",    s_cfg.callsign,    sizeof(s_cfg.callsign));
    s_prefs.getString("wifiSSID",    s_cfg.wifiSSID,    sizeof(s_cfg.wifiSSID));
    s_prefs.getString("wifiPass",    s_cfg.wifiPass,    sizeof(s_cfg.wifiPass));
    s_prefs.getString("dxFeedUrl",   s_cfg.dxFeedUrl,   sizeof(s_cfg.dxFeedUrl));
    s_prefs.getString("dxLiteCall",  s_cfg.dxLiteCall,  sizeof(s_cfg.dxLiteCall));
    s_prefs.getString("haUser",      s_cfg.hamAlertUser, sizeof(s_cfg.hamAlertUser));
    s_prefs.getString("haKey",       s_cfg.hamAlertKey,  sizeof(s_cfg.hamAlertKey));
    s_prefs.getString("haPass",      s_cfg.hamAlertPass, sizeof(s_cfg.hamAlertPass));
    s_prefs.getString("satKey",      s_cfg.hamsatKey,    sizeof(s_cfg.hamsatKey));

    s_cfg.dxFeedSource        = (uint8_t)s_prefs.getUChar("dxFeedSrc",   DX_FEED_DXWATCH);
    s_cfg.haNotifyBeep        = (uint8_t)s_prefs.getUChar("haBeep",      1);
    s_cfg.haNotifyPopup       = (uint8_t)s_prefs.getUChar("haPopup",     1);
    s_cfg.volume              = (uint8_t)s_prefs.getUChar("volume",      90);
    s_cfg.brightness          = (uint8_t)s_prefs.getUChar("brightness",  200);
    s_cfg.kbBrightness        = (uint8_t)s_prefs.getUChar("kbBright",    200);
    s_cfg.kbAutoNight         = (uint8_t)s_prefs.getUChar("kbAuto",      1);
    s_cfg.theme               = (uint8_t)s_prefs.getUChar("theme",       0);
    s_cfg.timezoneOffsetHours = (int8_t) s_prefs.getChar ("timezone",    0);

    s_prefs.end();
    DXS_LOG("Config", "Loaded: callsign=%s ssid=%s", s_cfg.callsign, s_cfg.wifiSSID);
}

void save() {
    s_prefs.begin("dxspotter", false);  // read-write

    s_prefs.putString("callsign",  s_cfg.callsign);
    s_prefs.putString("wifiSSID",  s_cfg.wifiSSID);
    s_prefs.putString("wifiPass",  s_cfg.wifiPass);
    s_prefs.putString("dxFeedUrl", s_cfg.dxFeedUrl);
    s_prefs.putString("dxLiteCall", s_cfg.dxLiteCall);
    s_prefs.putString("haUser",    s_cfg.hamAlertUser);
    s_prefs.putString("haKey",     s_cfg.hamAlertKey);
    s_prefs.putString("haPass",    s_cfg.hamAlertPass);
    s_prefs.putString("satKey",    s_cfg.hamsatKey);

    s_prefs.putUChar("dxFeedSrc",  s_cfg.dxFeedSource);
    s_prefs.putUChar("haBeep",     s_cfg.haNotifyBeep);
    s_prefs.putUChar("haPopup",    s_cfg.haNotifyPopup);
    s_prefs.putUChar("volume",     s_cfg.volume);
    s_prefs.putUChar("brightness", s_cfg.brightness);
    s_prefs.putUChar("kbBright",   s_cfg.kbBrightness);
    s_prefs.putUChar("kbAuto",     s_cfg.kbAutoNight);
    size_t themeW = s_prefs.putUChar("theme", s_cfg.theme);
    s_prefs.putChar ("timezone",   s_cfg.timezoneOffsetHours);

    // Diagnostic: themeW==0 means the NVS write FAILED (e.g. namespace full);
    // freeEntries near 0 confirms it. This proves/disproves "settings not saved".
    size_t freeE = s_prefs.freeEntries();
    s_prefs.end();
    DXS_LOG("Config", "Saved (theme=%u write=%u freeEntries=%u)",
            (unsigned)s_cfg.theme, (unsigned)themeW, (unsigned)freeE);
}

const Config& get() { return s_cfg; }

void setCallsign(const char* cs) {
    strncpy(s_cfg.callsign, cs, sizeof(s_cfg.callsign) - 1);
    s_cfg.callsign[sizeof(s_cfg.callsign) - 1] = '\0';
    save();
}

void setWiFi(const char* ssid, const char* pass) {
    strncpy(s_cfg.wifiSSID, ssid, sizeof(s_cfg.wifiSSID) - 1);
    strncpy(s_cfg.wifiPass, pass, sizeof(s_cfg.wifiPass) - 1);
    s_cfg.wifiSSID[sizeof(s_cfg.wifiSSID) - 1] = '\0';
    s_cfg.wifiPass[sizeof(s_cfg.wifiPass) - 1] = '\0';
    save();
}

void setDXFeed(uint8_t source, const char* customUrl) {
    s_cfg.dxFeedSource = source;
    strncpy(s_cfg.dxFeedUrl, customUrl, sizeof(s_cfg.dxFeedUrl) - 1);
    s_cfg.dxFeedUrl[sizeof(s_cfg.dxFeedUrl) - 1] = '\0';
    save();
}

void setDXLiteCall(const char* call) {
    strncpy(s_cfg.dxLiteCall, call, sizeof(s_cfg.dxLiteCall) - 1);
    s_cfg.dxLiteCall[sizeof(s_cfg.dxLiteCall) - 1] = '\0';
    save();
}

void setHamAlert(const char* user, const char* key) {
    strncpy(s_cfg.hamAlertUser, user, sizeof(s_cfg.hamAlertUser) - 1);
    strncpy(s_cfg.hamAlertKey,  key,  sizeof(s_cfg.hamAlertKey)  - 1);
    s_cfg.hamAlertUser[sizeof(s_cfg.hamAlertUser) - 1] = '\0';
    s_cfg.hamAlertKey [sizeof(s_cfg.hamAlertKey)  - 1] = '\0';
    save();
}

void setHamAlertPass(const char* pass) {
    strncpy(s_cfg.hamAlertPass, pass, sizeof(s_cfg.hamAlertPass) - 1);
    s_cfg.hamAlertPass[sizeof(s_cfg.hamAlertPass) - 1] = '\0';
    save();
}

void setHamsatKey(const char* key) {
    strncpy(s_cfg.hamsatKey, key, sizeof(s_cfg.hamsatKey) - 1);
    s_cfg.hamsatKey[sizeof(s_cfg.hamsatKey) - 1] = '\0';
    save();
}

void setHamAlertBeep(bool on) {
    s_cfg.haNotifyBeep = on ? 1 : 0;
    save();
}

void setHamAlertPopup(bool on) {
    s_cfg.haNotifyPopup = on ? 1 : 0;
    save();
}

void setVolume(uint8_t v) {
    s_cfg.volume = v;
    save();
}

void setBrightness(uint8_t b) {
    s_cfg.brightness = b;
    save();
}

void setKbBrightness(uint8_t level) {
    s_cfg.kbBrightness = level;
    save();
}

void setKbAutoNight(bool on) {
    s_cfg.kbAutoNight = on ? 1 : 0;
    save();
}

void setTheme(uint8_t t) {
    s_cfg.theme = t;
    save();
}

void setTimezone(int8_t offsetHours) {
    s_cfg.timezoneOffsetHours = offsetHours;
    save();
}

}  // namespace config
}  // namespace dxs
