// DXSpotter — ScreenSettings.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenSettings.h"
#include "ScreenLauncher.h"
#include "UIScreen.h"
#include "Theme.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include "../net/WiFiMgr.h"
#include "../net/DXFeed.h"
#include "../hardware/Board.h"

#include <WiFi.h>
#include <cstring>
#include <cstdio>

namespace dxs { namespace ui {

static constexpr int TOP_H   = 28;
static constexpr int ITEM_H  = 28;

lv_obj_t* ScreenSettings::_screen = nullptr;
lv_obj_t* ScreenSettings::_list   = nullptr;

// Item index enum (order matches the list rows)
enum SettingItem : int {
    SI_CALLSIGN = 0,
    SI_WIFI_SSID,
    SI_WIFI_PASS,
    SI_DX_FEED,
    SI_DX_URL,
    SI_DXLITE_CALL,
    SI_HA_USER,
    SI_HA_PASS,
    SI_HA_KEY,
    SI_HA_BEEP,
    SI_HA_POPUP,
    SI_VOLUME,
    SI_BRIGHTNESS,
    SI_KB_BACKLIGHT,
    SI_KB_AUTO,
    SI_THEME,
    SI_TIMEZONE,
    SI_COUNT
};

static const char* kItemLabels[SI_COUNT] = {
    "Callsign",
    "WiFi SSID",
    "WiFi Password",
    "DX Feed",
    "DX Feed URL",
    "DXLite Callsign",
    "HamAlert User",
    "HamAlert Password",
    "HamAlert Telnet",
    "HamAlert Notification",
    "Alert Popup",
    "Volume",
    "Brightness",
    "KB Backlight",
    "KB Auto Night",
    "Colour Theme",
    "Timezone",
};

static lv_obj_t* s_valLbls[SI_COUNT] = {};
static lv_obj_t* s_listPtr = nullptr;

// ── Value string helpers ───────────────────────────────────────────────
static void _buildValStr(int idx, char* buf, size_t sz) {
    const auto& cfg = config::get();
    switch ((SettingItem)idx) {
    case SI_CALLSIGN:   strncpy(buf, cfg.callsign[0]    ? cfg.callsign    : "(not set)", sz-1); break;
    case SI_WIFI_SSID:  strncpy(buf, cfg.wifiSSID[0]   ? cfg.wifiSSID    : "(not set)", sz-1); break;
    case SI_WIFI_PASS:  strncpy(buf, cfg.wifiPass[0]   ? "••••••••"      : "(not set)", sz-1); break;
    case SI_DX_FEED: {
        // Must match kFeedMap order in _showDXFeedPopup
        switch (cfg.dxFeedSource) {
            case DX_FEED_HOLYCLUSTER: strncpy(buf, "Holy Cluster", sz-1); break;
            case DX_FEED_DXWATCH:    strncpy(buf, "DXwatch",      sz-1); break;
            case DX_FEED_DXSCAPE_EU: strncpy(buf, "DXScape EU",   sz-1); break;
            case DX_FEED_DXSCAPE_WW: strncpy(buf, "DXScape WW",   sz-1); break;
            case DX_FEED_DXLITE:     strncpy(buf, "DXLite",       sz-1); break;
            case DX_FEED_DXHEAT:     strncpy(buf, "DXheat (off)", sz-1); break;
            case DX_FEED_CUSTOM:     strncpy(buf, "Custom URL",   sz-1); break;
            default:                 strncpy(buf, "Unknown",      sz-1); break;
        }
        break;
    }
    case SI_DX_URL:     strncpy(buf, cfg.dxFeedUrl[0]  ? cfg.dxFeedUrl   : "(not set)", sz-1); break;
    case SI_DXLITE_CALL:strncpy(buf, cfg.dxLiteCall[0] ? cfg.dxLiteCall  : "(all spots)", sz-1); break;
    case SI_HA_USER:    strncpy(buf, cfg.hamAlertUser[0]? cfg.hamAlertUser: "(not set)", sz-1); break;
    case SI_HA_PASS:    strncpy(buf, cfg.hamAlertPass[0]? "••••••••"      : "(not set)", sz-1); break;
    case SI_HA_KEY:     strncpy(buf, cfg.hamAlertKey[0] ? "••••••••"      : "(not set)", sz-1); break;
    case SI_HA_BEEP:    strncpy(buf, cfg.haNotifyBeep  ? "On" : "Off", sz-1); break;
    case SI_HA_POPUP:   strncpy(buf, cfg.haNotifyPopup ? "On" : "Off", sz-1); break;
    case SI_VOLUME:
        if (cfg.volume == 0) strncpy(buf, "Off", sz-1);
        else snprintf(buf, sz, "%d%%", (int)(cfg.volume * 100 / 255));
        break;
    case SI_BRIGHTNESS: snprintf(buf, sz, "%d%%", (int)(cfg.brightness * 100 / 255)); break;
    case SI_KB_BACKLIGHT:
        if (cfg.kbBrightness == 0) strncpy(buf, "Off", sz-1);
        else snprintf(buf, sz, "%d%%", (int)(cfg.kbBrightness * 100 / 255));
        break;
    case SI_KB_AUTO:    strncpy(buf, cfg.kbAutoNight ? "On 21-07h" : "Off", sz-1); break;
    case SI_THEME: {
        static const char* kThemeNames[] = { "Dark", "Green", "Dracula" };
        int t = cfg.theme < 3 ? cfg.theme : 0;
        strncpy(buf, kThemeNames[t], sz-1);
        break;
    }
    case SI_TIMEZONE: {
        int8_t tz = cfg.timezoneOffsetHours;
        if (tz == 0) strncpy(buf, "UTC+0", sz-1);
        else         snprintf(buf, sz, "UTC%+d", (int)tz);
        break;
    }
    default: strncpy(buf, "---", sz-1); break;
    }
    buf[sz-1] = '\0';
}

// ── show() ────────────────────────────────────────────────────────────
void ScreenSettings::show() {
    if (!_screen) {
        _screen = lv_obj_create(nullptr);
        lv_obj_set_size(_screen, DXS_SCREEN_W, DXS_SCREEN_H);
        lv_obj_set_style_bg_color(_screen, theme::BG, 0);
        lv_obj_set_style_pad_all(_screen, 0, 0);
        lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
        _buildTopBar(_screen);
        _buildList(_screen);
    } else {
        _refreshList();
    }
    lv_scr_load(_screen);
    DXS_LOG("UI", "Settings shown");
}

// ── _buildTopBar() ────────────────────────────────────────────────────
void ScreenSettings::_buildTopBar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, DXS_SCREEN_W, TOP_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::BG_CARD, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_set_style_pad_ver(bar, 2, 0);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* homeBtn = lv_btn_create(bar);
    lv_group_remove_obj(homeBtn);
    lv_obj_set_height(homeBtn, TOP_H - 6);
    lv_obj_set_style_bg_color(homeBtn, theme::BG, 0);
    lv_obj_set_style_bg_color(homeBtn, theme::PRIMARY, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(homeBtn, theme::BORDER, 0);
    lv_obj_set_style_border_width(homeBtn, 1, 0);
    lv_obj_set_style_radius(homeBtn, 4, 0);
    lv_obj_set_style_shadow_width(homeBtn, 0, 0);
    lv_obj_set_style_pad_hor(homeBtn, 6, 0);
    lv_obj_add_event_cb(homeBtn, _onHomeClick, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* homeLbl = lv_label_create(homeBtn);
    lv_label_set_text(homeLbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(homeLbl, theme::ACCENT, 0);
    lv_obj_set_style_text_font(homeLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(homeLbl);

    lv_obj_t* title = lv_label_create(bar);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Settings");
    lv_obj_set_style_text_color(title, theme::TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
}

// ── _buildList() ──────────────────────────────────────────────────────
void ScreenSettings::_buildList(lv_obj_t* parent) {
    _list = lv_obj_create(parent);
    s_listPtr = _list;
    lv_obj_set_size(_list, DXS_SCREEN_W, DXS_SCREEN_H - TOP_H);
    lv_obj_align(_list, LV_ALIGN_TOP_LEFT, 0, TOP_H);
    lv_obj_set_style_bg_color(_list, theme::BG, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_radius(_list, 0, 0);
    lv_obj_set_style_pad_all(_list, 0, 0);
    lv_obj_set_style_pad_row(_list, 0, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < SI_COUNT; i++) {
        lv_obj_t* row = lv_btn_create(_list);
        lv_group_remove_obj(row);
        lv_obj_set_size(row, DXS_SCREEN_W, ITEM_H);
        lv_obj_set_style_bg_color(row, (i & 1) ? theme::BG_CARD : theme::BG, 0);
        lv_obj_set_style_bg_color(row, theme::PRIMARY, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, _onItemClick, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        // Label (left, fixed width)
        lv_obj_t* nameLbl = lv_label_create(row);
        lv_obj_set_width(nameLbl, 120);
        lv_label_set_text(nameLbl, kItemLabels[i]);
        lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(nameLbl, theme::TEXT, 0);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);

        // Spacer
        lv_obj_t* sp = lv_obj_create(row);
        lv_obj_set_size(sp, 1, 1);
        lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sp, 0, 0);
        lv_obj_set_style_pad_all(sp, 0, 0);
        lv_obj_set_flex_grow(sp, 1);

        // Value label
        char vbuf[64];
        _buildValStr(i, vbuf, sizeof(vbuf));
        s_valLbls[i] = lv_label_create(row);
        lv_obj_set_width(s_valLbls[i], 150);
        lv_label_set_text(s_valLbls[i], vbuf);
        lv_label_set_long_mode(s_valLbls[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(s_valLbls[i], theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(s_valLbls[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(s_valLbls[i], LV_TEXT_ALIGN_RIGHT, 0);

        // Chevron
        lv_obj_t* arrow = lv_label_create(row);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(arrow, &lv_font_montserrat_10, 0);
    }
}

void ScreenSettings::_refreshList() {
    for (int i = 0; i < SI_COUNT; i++) {
        if (!s_valLbls[i]) continue;
        char vbuf[64];
        _buildValStr(i, vbuf, sizeof(vbuf));
        lv_label_set_text(s_valLbls[i], vbuf);
    }
}

// ── Popup text-entry dialog ───────────────────────────────────────────
// Reusable modal textarea with OK/Cancel buttons.
// User data passed to callback is the SettingItem index.

static lv_obj_t*   s_popupScreen  = nullptr;
static lv_obj_t*   s_popupTA      = nullptr;
static int         s_popupItem    = -1;
static lv_obj_t*   s_prevScreen   = nullptr;

// WiFi scan state
static char        s_pendingSSID[64]  = {};   // SSID selected from scan
static lv_obj_t*   s_scanList         = nullptr;
static lv_obj_t*   s_scanStatus       = nullptr;
static lv_timer_t* s_scanTimer        = nullptr;

// _closePopup must use lv_obj_del_async: we are often called from within an event
// on a child of s_popupScreen, so an immediate lv_obj_del causes a use-after-free.
static void _closePopup() {
    if (s_scanTimer) { lv_timer_del(s_scanTimer); s_scanTimer = nullptr; }
    WiFi.scanDelete();
    s_scanList   = nullptr;
    s_scanStatus = nullptr;
    if (s_popupScreen) { lv_obj_del_async(s_popupScreen); s_popupScreen = nullptr; }
    s_popupTA    = nullptr;
    s_popupItem  = -1;
    if (s_prevScreen) { lv_scr_load(s_prevScreen); s_prevScreen = nullptr; }
}

static void _onPopupOK(lv_event_t*) {
    if (!s_popupTA || s_popupItem < 0) { _closePopup(); return; }
    const char* text = lv_textarea_get_text(s_popupTA);
    const auto& cfg = config::get();
    switch ((SettingItem)s_popupItem) {
    case SI_CALLSIGN:  config::setCallsign(text);                          break;
    case SI_WIFI_SSID: config::setWiFi(text, cfg.wifiPass);                break;
    case SI_WIFI_PASS: {
        // If pendingSSID was set via scan popup, use that. Otherwise keep current SSID.
        const char* ssid = s_pendingSSID[0] ? s_pendingSSID : cfg.wifiSSID;
        config::setWiFi(ssid, text);
        s_pendingSSID[0] = '\0';
        WiFiMgr::instance().reconnect();
        break;
    }
    case SI_DX_URL:    config::setDXFeed(DX_FEED_CUSTOM, text);            break;
    case SI_DXLITE_CALL: config::setDXLiteCall(text); DXFeed::requestFetch(); break;
    case SI_HA_USER:   config::setHamAlert(text, cfg.hamAlertKey);         break;
    case SI_HA_PASS:   config::setHamAlertPass(text);                      break;
    case SI_HA_KEY:    config::setHamAlert(cfg.hamAlertUser, text);        break;
    default: break;
    }
    _closePopup();
    ScreenSettings::show();
}

static void _onPopupCancel(lv_event_t*) {
    s_pendingSSID[0] = '\0';
    resetKbBacklight();   // re-apply KB auto/manual state after any live preview
    _closePopup();
    ScreenSettings::show();
}

static void _showTextPopup(int itemIdx, const char* currentVal, bool password) {
    s_popupItem   = itemIdx;
    s_prevScreen  = lv_scr_act();
    s_popupScreen = lv_obj_create(nullptr);
    lv_obj_set_size(s_popupScreen, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(s_popupScreen, theme::BG, 0);
    lv_obj_set_style_pad_all(s_popupScreen, 0, 0);
    lv_obj_clear_flag(s_popupScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* box = lv_obj_create(s_popupScreen);
    lv_obj_set_size(box, DXS_SCREEN_W - 20, 140);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, theme::BG_CARD, 0);
    lv_obj_set_style_border_color(box, theme::BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr = lv_label_create(box);
    lv_label_set_text(hdr, kItemLabels[itemIdx]);
    lv_obj_set_style_text_color(hdr, theme::ACCENT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    s_popupTA = lv_textarea_create(box);
    lv_obj_set_size(s_popupTA, lv_pct(100), 40);
    lv_obj_align(s_popupTA, LV_ALIGN_TOP_LEFT, 0, 18);
    lv_obj_set_style_bg_color(s_popupTA, theme::BG, 0);
    lv_obj_set_style_border_color(s_popupTA, theme::ACCENT, 0);
    lv_obj_set_style_text_color(s_popupTA, theme::TEXT, 0);
    lv_obj_set_style_text_font(s_popupTA, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(s_popupTA, true);
    lv_textarea_set_max_length(s_popupTA, 64);
    if (password) lv_textarea_set_password_mode(s_popupTA, true);
    if (currentVal && currentVal[0]) lv_textarea_set_text(s_popupTA, currentVal);
    lv_obj_add_state(s_popupTA, LV_STATE_FOCUSED);
    lv_group_add_obj(lv_group_get_default(), s_popupTA);
    lv_group_focus_obj(s_popupTA);

    // Buttons row
    lv_obj_t* btnRow = lv_obj_create(box);
    lv_obj_set_size(btnRow, lv_pct(100), 36);
    lv_obj_align(btnRow, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 8, 0);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto mkBtn = [](lv_obj_t* p, const char* t, lv_event_cb_t cb, lv_color_t bg) {
        lv_obj_t* b = lv_btn_create(p);
        lv_obj_set_size(b, 80, 30);
        lv_obj_set_style_bg_color(b, bg, 0);
        lv_obj_set_style_bg_color(b, dxs::theme::ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, t);
        lv_obj_set_style_text_color(l, dxs::theme::TEXT, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_center(l);
    };
    mkBtn(btnRow, "Cancel", _onPopupCancel, theme::BG_CARD);
    mkBtn(btnRow, "Save",   _onPopupOK,    theme::PRIMARY);

    lv_scr_load(s_popupScreen);
}

// ── DX feed selection popup ───────────────────────────────────────────
// Dropdown order: HC first (most capable), then legacy HTTP sources.
// kFeedMap[dropdown_idx] = DXFeedSource enum value.
static const uint8_t kFeedMap[] = {
    DX_FEED_HOLYCLUSTER,   // [0] Holy Cluster (default choice)
    DX_FEED_DXWATCH,       // [1] DXwatch
    DX_FEED_DXSCAPE_EU,    // [2] DXScape EU/CW
    DX_FEED_DXSCAPE_WW,    // [3] DXScape worldwide
    DX_FEED_DXLITE,        // [4] DXLite (g7vjr) — optional callsign filter
    DX_FEED_CUSTOM,        // [5] Custom URL
};
static constexpr int kFeedCount = (int)(sizeof(kFeedMap) / sizeof(kFeedMap[0]));

static void _onDXFeedSel(lv_event_t* e) {
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    if (sel < kFeedCount) config::setDXFeed(kFeedMap[sel], config::get().dxFeedUrl);
    _closePopup();
    ScreenSettings::show();
}

static void _showDXFeedPopup() {
    s_prevScreen  = lv_scr_act();
    s_popupScreen = lv_obj_create(nullptr);
    lv_obj_set_size(s_popupScreen, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(s_popupScreen, theme::BG, 0);
    lv_obj_set_style_pad_all(s_popupScreen, 0, 0);
    lv_obj_clear_flag(s_popupScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* box = lv_obj_create(s_popupScreen);
    lv_obj_set_size(box, 220, 120);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, theme::BG_CARD, 0);
    lv_obj_set_style_border_color(box, theme::BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr = lv_label_create(box);
    lv_label_set_text(hdr, "Select DX Feed");
    lv_obj_set_style_text_color(hdr, theme::ACCENT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* dd = lv_dropdown_create(box);
    lv_obj_set_size(dd, lv_pct(100), 36);
    lv_obj_align(dd, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_dropdown_set_options(dd, "Holy Cluster\nDXwatch\nDXScape EU\nDXScape WW\nDXLite\nCustom URL");
    // Find current source in kFeedMap to set the initial selection
    uint16_t curSel = 0;
    for (uint16_t i = 0; i < kFeedCount; i++) {
        if (kFeedMap[i] == config::get().dxFeedSource) { curSel = i; break; }
    }
    lv_dropdown_set_selected(dd, curSel);
    lv_obj_set_style_bg_color(dd, theme::BG, 0);
    lv_obj_set_style_text_color(dd, theme::TEXT, 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(dd, _onDXFeedSel, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* cancel = lv_btn_create(box);
    lv_obj_set_size(cancel, 80, 28);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(cancel, theme::BG_CARD, 0);
    lv_obj_set_style_border_color(cancel, theme::BORDER, 0);
    lv_obj_set_style_border_width(cancel, 1, 0);
    lv_obj_set_style_radius(cancel, 4, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    lv_obj_add_event_cb(cancel, _onPopupCancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, theme::TEXT, 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_10, 0);
    lv_obj_center(cl);

    lv_scr_load(s_popupScreen);
}

// ── Brightness/Timezone sliders ───────────────────────────────────────
static void _onBrightSlide(lv_event_t* e) {
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    int32_t val  = lv_slider_get_value(sl);
    config::setBrightness((uint8_t)val);
    // Apply immediately via Board
    // Board::instance().setDisplayBrightness((uint8_t)val);  // would need Board include
}

static void _showBrightnessPopup() {
    s_prevScreen  = lv_scr_act();
    s_popupScreen = lv_obj_create(nullptr);
    lv_obj_set_size(s_popupScreen, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(s_popupScreen, theme::BG, 0);
    lv_obj_clear_flag(s_popupScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* box = lv_obj_create(s_popupScreen);
    lv_obj_set_size(box, 240, 100);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, theme::BG_CARD, 0);
    lv_obj_set_style_border_color(box, theme::BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr = lv_label_create(box);
    lv_label_set_text(hdr, "Brightness");
    lv_obj_set_style_text_color(hdr, theme::ACCENT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* sl = lv_slider_create(box);
    lv_obj_set_size(sl, lv_pct(100), 20);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_slider_set_range(sl, 30, 255);
    lv_slider_set_value(sl, config::get().brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, theme::ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, theme::ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, _onBrightSlide, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_group_add_obj(lv_group_get_default(), sl);
    lv_group_focus_obj(sl);

    lv_obj_t* ok = lv_btn_create(box);
    lv_obj_set_size(ok, 70, 28);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(ok, theme::PRIMARY, 0);
    lv_obj_set_style_radius(ok, 4, 0);
    lv_obj_set_style_border_width(ok, 0, 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_add_event_cb(ok, _onPopupCancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ol = lv_label_create(ok);
    lv_label_set_text(ol, "Done");
    lv_obj_set_style_text_color(ol, theme::TEXT, 0);
    lv_obj_set_style_text_font(ol, &lv_font_montserrat_12, 0);
    lv_obj_center(ol);

    lv_scr_load(s_popupScreen);
}

// ── Keyboard backlight slider ─────────────────────────────────────────
static void _onKbSlide(lv_event_t* e) {
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    int32_t val  = lv_slider_get_value(sl);
    config::setKbBrightness((uint8_t)val);
    Board::instance().setKeyboardBacklight((uint8_t)val);  // live preview
}

static void _showKbBacklightPopup() {
    s_prevScreen  = lv_scr_act();
    s_popupScreen = lv_obj_create(nullptr);
    lv_obj_set_size(s_popupScreen, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(s_popupScreen, theme::BG, 0);
    lv_obj_clear_flag(s_popupScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* box = lv_obj_create(s_popupScreen);
    lv_obj_set_size(box, 240, 116);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, theme::BG_CARD, 0);
    lv_obj_set_style_border_color(box, theme::BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr = lv_label_create(box);
    lv_label_set_text(hdr, "Keyboard Backlight");
    lv_obj_set_style_text_color(hdr, theme::ACCENT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* hint = lv_label_create(box);
    lv_label_set_text(hint, "0 = off");
    lv_obj_set_style_text_color(hint, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, 0, 2);

    lv_obj_t* sl = lv_slider_create(box);
    lv_obj_set_size(sl, lv_pct(100), 20);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_slider_set_range(sl, 0, 255);
    lv_slider_set_value(sl, config::get().kbBrightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, theme::ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, theme::ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, _onKbSlide, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_group_add_obj(lv_group_get_default(), sl);
    lv_group_focus_obj(sl);

    lv_obj_t* ok = lv_btn_create(box);
    lv_obj_set_size(ok, 70, 28);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(ok, theme::PRIMARY, 0);
    lv_obj_set_style_radius(ok, 4, 0);
    lv_obj_set_style_border_width(ok, 0, 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_add_event_cb(ok, _onPopupCancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ol = lv_label_create(ok);
    lv_label_set_text(ol, "Done");
    lv_obj_set_style_text_color(ol, theme::TEXT, 0);
    lv_obj_set_style_text_font(ol, &lv_font_montserrat_12, 0);
    lv_obj_center(ol);

    lv_scr_load(s_popupScreen);
}

// ── Volume slider ─────────────────────────────────────────────────────
static void _onVolumeSlide(lv_event_t* e) {
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    int32_t val  = lv_slider_get_value(sl);
    config::setVolume((uint8_t)val);
    Board::instance().setAudioVolume((uint8_t)val);
    // Test beep when the user lets go of the slider so they hear the new level.
    if (lv_event_get_code(e) == LV_EVENT_RELEASED && val > 0)
        Board::instance().beepNotify();
}

static void _showVolumePopup() {
    s_prevScreen  = lv_scr_act();
    s_popupScreen = lv_obj_create(nullptr);
    lv_obj_set_size(s_popupScreen, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(s_popupScreen, theme::BG, 0);
    lv_obj_clear_flag(s_popupScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* box = lv_obj_create(s_popupScreen);
    lv_obj_set_size(box, 240, 116);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, theme::BG_CARD, 0);
    lv_obj_set_style_border_color(box, theme::BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr = lv_label_create(box);
    lv_label_set_text(hdr, "Volume");
    lv_obj_set_style_text_color(hdr, theme::ACCENT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* hint = lv_label_create(box);
    lv_label_set_text(hint, "0 = mute");
    lv_obj_set_style_text_color(hint, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, 0, 2);

    lv_obj_t* sl = lv_slider_create(box);
    lv_obj_set_size(sl, lv_pct(100), 20);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_slider_set_range(sl, 0, 255);
    lv_slider_set_value(sl, config::get().volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, theme::ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, theme::ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, _onVolumeSlide, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sl, _onVolumeSlide, LV_EVENT_RELEASED, nullptr);
    lv_group_add_obj(lv_group_get_default(), sl);
    lv_group_focus_obj(sl);

    lv_obj_t* ok = lv_btn_create(box);
    lv_obj_set_size(ok, 70, 28);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(ok, theme::PRIMARY, 0);
    lv_obj_set_style_radius(ok, 4, 0);
    lv_obj_set_style_border_width(ok, 0, 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_add_event_cb(ok, _onPopupCancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ol = lv_label_create(ok);
    lv_label_set_text(ol, "Done");
    lv_obj_set_style_text_color(ol, theme::TEXT, 0);
    lv_obj_set_style_text_font(ol, &lv_font_montserrat_12, 0);
    lv_obj_center(ol);

    lv_scr_load(s_popupScreen);
}

// ── WiFi scan popup ───────────────────────────────────────────────────
static lv_color_t _rssiColor(int r) {
    if (r > -65) return theme::GREEN;
    if (r > -80) return theme::ORANGE;
    return theme::RED;
}

static void _onNetworkSelect(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    String ssid = WiFi.SSID(idx);
    strncpy(s_pendingSSID, ssid.c_str(), sizeof(s_pendingSSID) - 1);
    s_pendingSSID[sizeof(s_pendingSSID) - 1] = '\0';
    WiFi.scanDelete();
    if (s_scanTimer) { lv_timer_del(s_scanTimer); s_scanTimer = nullptr; }
    s_scanList = s_scanStatus = nullptr;
    _closePopup();
    _showTextPopup(SI_WIFI_PASS, "", true);   // prompt for password
}

static void _onManualSSID(lv_event_t*) {
    s_pendingSSID[0] = '\0';
    WiFi.scanDelete();
    if (s_scanTimer) { lv_timer_del(s_scanTimer); s_scanTimer = nullptr; }
    s_scanList = s_scanStatus = nullptr;
    _closePopup();
    _showTextPopup(SI_WIFI_SSID, config::get().wifiSSID, false);
}

static void _populateScanList(int n) {
    if (!s_scanList || !s_scanStatus) return;
    lv_obj_clean(s_scanList);

    if (n <= 0) {
        lv_label_set_text(s_scanStatus, "No networks found");
        return;
    }

    char sbuf[32];
    snprintf(sbuf, sizeof(sbuf), "%d networks found", n);
    lv_label_set_text(s_scanStatus, sbuf);

    // Sort indices by RSSI descending
    static int ord[24];
    int cnt = n < 24 ? n : 24;
    for (int i = 0; i < cnt; i++) ord[i] = i;
    for (int i = 1; i < cnt; i++) {
        int k = ord[i], j = i - 1;
        while (j >= 0 && WiFi.RSSI(ord[j]) < WiFi.RSSI(k)) { ord[j+1] = ord[j]; j--; }
        ord[j+1] = k;
    }

    for (int i = 0; i < cnt; i++) {
        int  idx  = ord[i];
        int  rssi = WiFi.RSSI(idx);
        bool open = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);

        lv_obj_t* btn = lv_btn_create(s_scanList);
        lv_group_remove_obj(btn);
        lv_obj_set_size(btn, lv_pct(100), 30);
        lv_obj_set_style_bg_color(btn, (i & 1) ? theme::BG_CARD : theme::BG, 0);
        lv_obj_set_style_bg_color(btn, theme::PRIMARY, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_hor(btn, 8, 0);
        lv_obj_set_style_pad_ver(btn, 0, 0);
        lv_obj_set_style_pad_column(btn, 6, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(btn, _onNetworkSelect, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

        lv_obj_t* ssidLbl = lv_label_create(btn);
        lv_obj_set_flex_grow(ssidLbl, 1);
        lv_label_set_text(ssidLbl, WiFi.SSID(idx).c_str());
        lv_label_set_long_mode(ssidLbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(ssidLbl, theme::TEXT, 0);
        lv_obj_set_style_text_font(ssidLbl, &lv_font_montserrat_12, 0);

        char rssiStr[10];
        snprintf(rssiStr, sizeof(rssiStr), "%ddBm", rssi);
        lv_obj_t* sigLbl = lv_label_create(btn);
        lv_label_set_text(sigLbl, rssiStr);
        lv_obj_set_style_text_color(sigLbl, _rssiColor(rssi), 0);
        lv_obj_set_style_text_font(sigLbl, &lv_font_montserrat_10, 0);

        if (!open) {
            lv_obj_t* lockLbl = lv_label_create(btn);
            lv_label_set_text(lockLbl, "*");
            lv_obj_set_style_text_color(lockLbl, theme::TEXT_MUTED, 0);
            lv_obj_set_style_text_font(lockLbl, &lv_font_montserrat_10, 0);
        }
    }
}

static void _onScanPoll(lv_timer_t*) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    lv_timer_del(s_scanTimer);
    s_scanTimer = nullptr;
    _populateScanList(n);
}

static void _showWiFiScanPopup() {
    s_pendingSSID[0] = '\0';
    s_prevScreen     = lv_scr_act();
    s_popupScreen    = lv_obj_create(nullptr);
    lv_obj_set_size(s_popupScreen, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(s_popupScreen, theme::BG, 0);
    lv_obj_set_style_pad_all(s_popupScreen, 0, 0);
    lv_obj_clear_flag(s_popupScreen, LV_OBJ_FLAG_SCROLLABLE);

    // Top bar
    lv_obj_t* bar = lv_obj_create(s_popupScreen);
    lv_obj_set_size(bar, DXS_SCREEN_W, 28);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::BG_CARD, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 6, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* titleLbl = lv_label_create(bar);
    lv_label_set_text(titleLbl, LV_SYMBOL_WIFI "  WiFi Networks");
    lv_obj_set_flex_grow(titleLbl, 1);
    lv_obj_set_style_text_color(titleLbl, theme::TEXT, 0);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);

    lv_obj_t* cancelBtn = lv_btn_create(bar);
    lv_group_remove_obj(cancelBtn);
    lv_obj_set_height(cancelBtn, 22);
    lv_obj_set_style_bg_color(cancelBtn, theme::BG_CARD, 0);
    lv_obj_set_style_bg_color(cancelBtn, theme::PRIMARY, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(cancelBtn, theme::BORDER, 0);
    lv_obj_set_style_border_width(cancelBtn, 1, 0);
    lv_obj_set_style_radius(cancelBtn, 4, 0);
    lv_obj_set_style_shadow_width(cancelBtn, 0, 0);
    lv_obj_set_style_pad_hor(cancelBtn, 6, 0);
    lv_obj_add_event_cb(cancelBtn, _onPopupCancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "Cancel");
    lv_obj_set_style_text_color(cancelLbl, theme::TEXT, 0);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(cancelLbl);

    // Status line
    s_scanStatus = lv_label_create(s_popupScreen);
    lv_label_set_text(s_scanStatus, LV_SYMBOL_REFRESH " Scanning...");
    lv_obj_set_pos(s_scanStatus, 6, 32);
    lv_obj_set_size(s_scanStatus, DXS_SCREEN_W - 12, 18);
    lv_obj_set_style_text_color(s_scanStatus, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_scanStatus, &lv_font_montserrat_10, 0);

    // Manual entry button (fixed at bottom)
    lv_obj_t* manBtn = lv_btn_create(s_popupScreen);
    lv_group_remove_obj(manBtn);
    lv_obj_set_size(manBtn, DXS_SCREEN_W, 30);
    lv_obj_align(manBtn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(manBtn, theme::BG_CARD, 0);
    lv_obj_set_style_bg_color(manBtn, theme::PRIMARY, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(manBtn, 0, 0);
    lv_obj_set_style_radius(manBtn, 0, 0);
    lv_obj_set_style_shadow_width(manBtn, 0, 0);
    lv_obj_add_event_cb(manBtn, _onManualSSID, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* manLbl = lv_label_create(manBtn);
    lv_label_set_text(manLbl, LV_SYMBOL_EDIT " Enter network name manually");
    lv_obj_set_style_text_color(manLbl, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(manLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(manLbl);

    // Scrollable network list
    s_scanList = lv_obj_create(s_popupScreen);
    lv_obj_set_pos(s_scanList, 0, 50);
    lv_obj_set_size(s_scanList, DXS_SCREEN_W, DXS_SCREEN_H - 50 - 30);
    lv_obj_set_style_bg_color(s_scanList, theme::BG, 0);
    lv_obj_set_style_border_width(s_scanList, 0, 0);
    lv_obj_set_style_radius(s_scanList, 0, 0);
    lv_obj_set_style_pad_all(s_scanList, 0, 0);
    lv_obj_set_style_pad_row(s_scanList, 0, 0);
    lv_obj_set_flex_flow(s_scanList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_scanList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(s_scanList, LV_SCROLLBAR_MODE_AUTO);

    lv_scr_load(s_popupScreen);

    WiFi.scanNetworks(/*async=*/true);
    s_scanTimer = lv_timer_create(_onScanPoll, 300, nullptr);
}

// ── _onItemClick() ────────────────────────────────────────────────────
void ScreenSettings::_onItemClick(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const auto& cfg = config::get();
    switch ((SettingItem)idx) {
    case SI_CALLSIGN:  _showTextPopup(idx, cfg.callsign,     false); break;
    case SI_WIFI_SSID: _showWiFiScanPopup();                         break;
    case SI_WIFI_PASS: _showTextPopup(idx, "",               true);  break;
    case SI_DX_FEED:   _showDXFeedPopup();                           break;
    case SI_DX_URL:    _showTextPopup(idx, cfg.dxFeedUrl,   false); break;
    case SI_DXLITE_CALL: _showTextPopup(idx, cfg.dxLiteCall, false); break;
    case SI_HA_USER:   _showTextPopup(idx, cfg.hamAlertUser, false); break;
    case SI_HA_PASS:   _showTextPopup(idx, "",               true);  break;
    case SI_HA_KEY:    _showTextPopup(idx, "",               true);  break;
    case SI_HA_BEEP:
        config::setHamAlertBeep(!cfg.haNotifyBeep);
        if (config::get().haNotifyBeep) Board::instance().beepNotify();  // test chirp
        _refreshList();
        break;
    case SI_HA_POPUP:
        config::setHamAlertPopup(!cfg.haNotifyPopup);
        _refreshList();
        if (config::get().haNotifyPopup) showAlertPopupTest();   // confirm it works
        break;
    case SI_VOLUME:    _showVolumePopup();                           break;
    case SI_BRIGHTNESS:_showBrightnessPopup();                       break;
    case SI_KB_BACKLIGHT: _showKbBacklightPopup();                   break;
    case SI_KB_AUTO:
        config::setKbAutoNight(!cfg.kbAutoNight);
        _refreshList();
        break;
    case SI_THEME: {
        // Cycle through themes
        uint8_t t = (uint8_t)((cfg.theme + 1) % theme::THEME_COUNT);
        config::setTheme(t);
        _refreshList();
        break;
    }
    case SI_TIMEZONE: {
        int8_t tz = (int8_t)((cfg.timezoneOffsetHours + 1 + 12) % 24 - 12);
        config::setTimezone(tz);
        _refreshList();
        break;
    }
    default: break;
    }
}

void ScreenSettings::_onHomeClick(lv_event_t*) { ScreenLauncher::show(); }

}}  // namespace dxs::ui
