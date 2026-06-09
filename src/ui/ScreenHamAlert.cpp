// DXSpotter — ScreenHamAlert.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenHamAlert.h"
#include "ScreenLauncher.h"
#include "Theme.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include "../net/HamAlertFeed.h"
#include "../net/HamAlertApi.h"
#include "../net/WiFiMgr.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>

namespace dxs { namespace ui {

static constexpr int TOP_H = 28;
static constexpr int BOT_H = 30;   // bottom action bar

lv_obj_t* ScreenHamAlert::_screen    = nullptr;
lv_obj_t* ScreenHamAlert::_list      = nullptr;
lv_obj_t* ScreenHamAlert::_statusLbl = nullptr;

void ScreenHamAlert::show() {
    if (!_screen) {
        _screen = lv_obj_create(nullptr);
        lv_obj_set_size(_screen, DXS_SCREEN_W, DXS_SCREEN_H);
        lv_obj_set_style_bg_color(_screen, theme::BG, 0);
        lv_obj_set_style_pad_all(_screen, 0, 0);
        lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

        _buildTopBar(_screen);
        _buildList(_screen);
        _buildBottomBar(_screen);
    }

    _rebuildList();
    lv_scr_load(_screen);
    HamAlertFeed::requestFetch();
    DXS_LOG("UI", "HamAlert shown");
}

// ── onNewData() ───────────────────────────────────────────────────────
void ScreenHamAlert::onNewData() {
    if (_screen && lv_scr_act() == _screen) {
        _rebuildList();
    }
}

void ScreenHamAlert::_buildTopBar(lv_obj_t* parent) {
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
    lv_label_set_text(title, LV_SYMBOL_CALL "  HamAlert");
    lv_obj_set_style_text_color(title, theme::TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    _statusLbl = lv_label_create(bar);
    lv_label_set_text(_statusLbl, "...");
    lv_obj_set_style_text_color(_statusLbl, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(_statusLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_pad_right(_statusLbl, 4, 0);
}

void ScreenHamAlert::_buildList(lv_obj_t* parent) {
    _list = lv_obj_create(parent);
    lv_obj_set_size(_list, DXS_SCREEN_W, DXS_SCREEN_H - TOP_H - BOT_H);
    lv_obj_align(_list, LV_ALIGN_TOP_LEFT, 0, TOP_H);
    lv_obj_set_style_bg_color(_list, theme::BG, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_radius(_list, 0, 0);
    lv_obj_set_style_pad_all(_list, 0, 0);
    lv_obj_set_style_pad_row(_list, 0, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);
}

// Column widths must fit the row's usable width (320 - 2*4px pad = 312).
//   Freq 54 + DX 64 + Mode 36 + Comment 100 + Age 58 = 312
static constexpr int COL_FREQ    = 54;
static constexpr int COL_DXCALL  = 64;
static constexpr int COL_MODE    = 36;
static constexpr int COL_COMMENT = 100;
static constexpr int COL_TIME    = 58;
static constexpr int ROW_H       = 22;

// Convert a "HHMM" UTC spot time to a relative age ("now", "5m ago", "3h ago").
// Falls back to raw "HH:MM" if the system clock isn't NTP-synced yet. The feed
// only carries hour:minute (no date), so ages are taken modulo 24h.
static void _relTime(const char* hhmm, char* out, size_t n) {
    if (!hhmm || strlen(hhmm) < 4) { snprintf(out, n, "%s", hhmm ? hhmm : "-"); return; }
    int hh = (hhmm[0]-'0')*10 + (hhmm[1]-'0');
    int mm = (hhmm[2]-'0')*10 + (hhmm[3]-'0');
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) { snprintf(out, n, "%s", hhmm); return; }

    time_t t = time(nullptr);
    if (t < 1000000000L) { snprintf(out, n, "%02d:%02d", hh, mm); return; }  // clock unset

    struct tm g; gmtime_r(&t, &g);
    int diff = (g.tm_hour*60 + g.tm_min) - (hh*60 + mm);
    if (diff < 0) diff += 1440;   // spot was before midnight UTC
    if      (diff < 1)    snprintf(out, n, "now");
    else if (diff < 60)   snprintf(out, n, "%dm ago", diff);
    else                  snprintf(out, n, "%dh ago", diff / 60);
}

static void makeHARow(lv_obj_t* list, const dxs::HamAlert& alert, int idx) {
    using namespace dxs::theme;
    lv_obj_t* row = lv_obj_create(list);
    lv_obj_set_size(row, lv_pct(100), ROW_H);
    lv_color_t bg = (idx & 1) ? BG_CARD : BG;
    lv_obj_set_style_bg_color(row, bg, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_set_style_pad_column(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto mkCell = [](lv_obj_t* p, int w, const char* t, lv_color_t c,
                     lv_label_long_mode_t mode) -> lv_obj_t* {
        lv_obj_t* l = lv_label_create(p);
        lv_obj_set_width(l, w);
        lv_label_set_text(l, t);
        lv_label_set_long_mode(l, mode);
        lv_obj_set_style_text_color(l, c, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        return l;
    };

    char age[12];
    _relTime(alert.utcTime, age, sizeof(age));

    mkCell(row, COL_FREQ,    alert.freq,    GREEN,      LV_LABEL_LONG_CLIP);
    mkCell(row, COL_DXCALL,  alert.dxCall,  TEXT,       LV_LABEL_LONG_CLIP);
    mkCell(row, COL_MODE,    alert.mode,    ACCENT,     LV_LABEL_LONG_CLIP);
    // Comment can be long ("FT8 -17dB CQ ...") — truncate with an ellipsis
    mkCell(row, COL_COMMENT, alert.comment, TEXT_MUTED, LV_LABEL_LONG_DOT);
    mkCell(row, COL_TIME,    age,           TEXT_MUTED, LV_LABEL_LONG_CLIP);
}

void ScreenHamAlert::_rebuildList() {
    if (!_list) return;
    lv_obj_clean(_list);

    const auto& cfg = config::get();
    if (cfg.hamAlertUser[0] == '\0') {
        lv_obj_t* msg = lv_label_create(_list);
        lv_label_set_text(msg,
            "HamAlert credentials not set.\n"
            "Go to Settings to enter\nyour username and API key.");
        lv_obj_set_style_text_color(msg, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 20);
        if (_statusLbl) lv_label_set_text(_statusLbl, "No creds");
        return;
    }

    if (!WiFiMgr::instance().isConnected()) {
        lv_obj_t* msg = lv_label_create(_list);
        lv_label_set_text(msg, LV_SYMBOL_WIFI " No WiFi connection.");
        lv_obj_set_style_text_color(msg, theme::TEXT_MUTED, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 20);
        if (_statusLbl) lv_label_set_text(_statusLbl, "No WiFi");
        return;
    }

    // static: ~3 KB array; _rebuildList only runs from the LVGL tick context, so
    // static storage avoids loop-task stack pressure (same pattern as DX Spots).
    static HamAlert alerts[HA_MAX_ALERTS];
    int n = HamAlertFeed::getAlerts(alerts, HA_MAX_ALERTS);

    if (n == 0) {
        lv_obj_t* msg = lv_label_create(_list);
        int st = HamAlertFeed::lastStatus();
        if (st == 0)
            lv_label_set_text(msg, "Fetching HamAlert data...");
        else if (st == 401)
            lv_label_set_text(msg, "Auth failed — check credentials in Settings.");
        else
            lv_label_set_text(msg, "No alerts received yet.");
        lv_obj_set_style_text_color(msg, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 20);
        if (_statusLbl) lv_label_set_text(_statusLbl, "0 alerts");
        return;
    }

    // Header
    {
        lv_obj_t* hdr = lv_obj_create(_list);
        lv_obj_set_size(hdr, lv_pct(100), ROW_H);
        lv_obj_set_style_bg_color(hdr, theme::BG_CARD, 0);
        lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(hdr, 0, 0);
        lv_obj_set_style_radius(hdr, 0, 0);
        lv_obj_set_style_pad_hor(hdr, 4, 0);
        lv_obj_set_style_pad_ver(hdr, 2, 0);
        lv_obj_set_style_pad_column(hdr, 0, 0);
        lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto mkH = [](lv_obj_t* p, int w, const char* t) {
            lv_obj_t* l = lv_label_create(p);
            lv_obj_set_width(l, w);
            lv_label_set_text(l, t);
            lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(l, dxs::theme::ACCENT, 0);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        };
        mkH(hdr, COL_FREQ,    "Freq");
        mkH(hdr, COL_DXCALL,  "DX");
        mkH(hdr, COL_MODE,    "Mode");
        mkH(hdr, COL_COMMENT, "Comment");
        mkH(hdr, COL_TIME,    "Age");
    }

    for (int i = 0; i < n; i++) makeHARow(_list, alerts[i], i);

    if (_statusLbl) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%d alerts", n);
        lv_label_set_text(_statusLbl, buf);
    }
}

// ── _buildBottomBar() — Refresh | Add | Delete ────────────────────────
void ScreenHamAlert::_buildBottomBar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, DXS_SCREEN_W, BOT_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 1, 0);   // hairline gap between buttons
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto mkBtn = [](lv_obj_t* p, const char* txt, lv_event_cb_t cb) {
        lv_obj_t* b = lv_btn_create(p);
        lv_group_remove_obj(b);
        lv_obj_set_height(b, BOT_H);
        lv_obj_set_flex_grow(b, 1);   // three equal thirds
        lv_obj_set_style_bg_color(b, theme::PRIMARY, 0);
        lv_obj_set_style_bg_color(b, theme::ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_color(l, theme::TEXT, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_center(l);
    };

    mkBtn(bar, LV_SYMBOL_REFRESH " Refresh", _onRefreshClick);
    mkBtn(bar, LV_SYMBOL_PLUS    " Add",     _onAddClick);
    mkBtn(bar, LV_SYMBOL_TRASH   " Delete",  _onDeleteClick);
}

void ScreenHamAlert::_onHomeClick(lv_event_t*)    { ScreenLauncher::show(); }
void ScreenHamAlert::_onRefreshClick(lv_event_t*) { HamAlertFeed::requestFetch(); }

// ── Trigger management popups (Add / Delete via HamAlertApi) ───────────
// These share a single modal screen and a poll timer that watches the async
// HamAlertApi worker. Pattern mirrors ScreenSettings: lv_obj_del_async on close
// (callbacks run on children of the popup), restore the previous screen after.

enum HAPopMode { HP_NONE, HP_ADD, HP_LIST, HP_DELETE };

static lv_obj_t*   s_haPopup    = nullptr;
static lv_obj_t*   s_haBox      = nullptr;   // content card
static lv_obj_t*   s_haStatus   = nullptr;   // status / error line
static lv_obj_t*   s_haTA       = nullptr;   // callsign textarea (Add)
static lv_obj_t*   s_haList     = nullptr;   // trigger list (Delete)
static lv_timer_t* s_haTimer    = nullptr;
static lv_obj_t*   s_haPrevScr  = nullptr;
static HAPopMode   s_haMode     = HP_NONE;

static HATrigger   s_uiTrigs[HA_MAX_TRIGGERS];
static int         s_uiTrigCount = 0;

static void _haClosePopup() {
    if (s_haTimer) { lv_timer_del(s_haTimer); s_haTimer = nullptr; }
    if (s_haPopup) { lv_obj_del_async(s_haPopup); s_haPopup = nullptr; }
    s_haBox = s_haStatus = s_haTA = s_haList = nullptr;
    s_haMode = HP_NONE;
    if (s_haPrevScr) { lv_scr_load(s_haPrevScr); s_haPrevScr = nullptr; }
}

static void _haOnClose(lv_event_t*) { _haClosePopup(); }

static void _haSetStatus(const char* txt, lv_color_t c) {
    if (!s_haStatus) return;
    lv_label_set_text(s_haStatus, txt);
    lv_obj_set_style_text_color(s_haStatus, c, 0);
}

// Clean a typed callsign: upper-case, trim, keep the longer half of a "/" split
// (matches the importer's clean_callsign()).
static void _cleanCall(const char* in, char* out, size_t n) {
    char tmp[24] = {0};
    int j = 0;
    for (const char* p = in; *p && j < (int)sizeof(tmp) - 1; ++p) {
        if (*p == ' ' || *p == '\t') continue;
        tmp[j++] = (char)toupper((unsigned char)*p);
    }
    const char* slash = strchr(tmp, '/');
    if (slash) {
        const char* a = tmp;            // before '/'
        const char* b = slash + 1;      // after '/'
        size_t la = (size_t)(slash - tmp), lb = strlen(b);
        if (lb >= la) { strncpy(out, b, n - 1); out[n - 1] = '\0'; }
        else          { strncpy(out, a, la < n ? la : n - 1); out[la < n ? la : n - 1] = '\0'; }
    } else {
        strncpy(out, tmp, n - 1);
        out[n - 1] = '\0';
    }
}

// Build the scrollable list of existing triggers (Delete mode).
static void _haDeleteRowClick(lv_event_t* e);  // fwd

static void _haPopulateDeleteList() {
    s_uiTrigCount = HamAlertApi::getTriggers(s_uiTrigs, HA_MAX_TRIGGERS);
    if (!s_haList) return;
    lv_obj_clean(s_haList);

    if (s_uiTrigCount == 0) {
        _haSetStatus("No triggers found", theme::TEXT_MUTED);
        return;
    }
    char sbuf[24];
    snprintf(sbuf, sizeof(sbuf), "%d triggers - tap to delete", s_uiTrigCount);
    _haSetStatus(sbuf, theme::TEXT_MUTED);

    for (int i = 0; i < s_uiTrigCount; i++) {
        lv_obj_t* btn = lv_btn_create(s_haList);
        lv_group_remove_obj(btn);
        lv_obj_set_size(btn, lv_pct(100), 26);
        lv_obj_set_style_bg_color(btn, (i & 1) ? theme::BG_CARD : theme::BG, 0);
        lv_obj_set_style_bg_color(btn, theme::RED, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_hor(btn, 8, 0);
        lv_obj_set_style_pad_ver(btn, 0, 0);
        lv_obj_set_style_pad_column(btn, 6, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(btn, _haDeleteRowClick, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* call = lv_label_create(btn);
        lv_label_set_text(call, s_uiTrigs[i].callsign);
        lv_obj_set_style_text_color(call, theme::TEXT, 0);
        lv_obj_set_style_text_font(call, &lv_font_montserrat_12, 0);

        lv_obj_t* cmt = lv_label_create(btn);
        lv_obj_set_flex_grow(cmt, 1);
        lv_label_set_text(cmt, s_uiTrigs[i].comment);
        lv_label_set_long_mode(cmt, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(cmt, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(cmt, &lv_font_montserrat_10, 0);

        lv_obj_t* tr = lv_label_create(btn);
        lv_label_set_text(tr, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(tr, theme::RED, 0);
        lv_obj_set_style_text_font(tr, &lv_font_montserrat_10, 0);
    }
}

// Poll the worker; dispatch on completion based on the current mode.
static void _haPoll(lv_timer_t*) {
    if (HamAlertApi::status() == HamAlertApi::BUSY) return;
    bool ok = (HamAlertApi::status() == HamAlertApi::OK);

    switch (s_haMode) {
    case HP_LIST:
        if (s_haTimer) { lv_timer_del(s_haTimer); s_haTimer = nullptr; }
        if (ok) _haPopulateDeleteList();
        else    _haSetStatus(HamAlertApi::message(), theme::RED);
        break;

    case HP_ADD:
        if (s_haTimer) { lv_timer_del(s_haTimer); s_haTimer = nullptr; }
        _haSetStatus(HamAlertApi::message(), ok ? theme::GREEN : theme::RED);
        if (ok) {
            // brief confirmation, then auto-close
            s_haMode = HP_NONE;
            s_haTimer = lv_timer_create([](lv_timer_t*) { _haClosePopup(); }, 1100, nullptr);
        }
        break;

    case HP_DELETE:
        if (s_haTimer) { lv_timer_del(s_haTimer); s_haTimer = nullptr; }
        if (ok) {
            // re-list to reflect the change and let the user delete more
            s_haMode  = HP_LIST;
            HamAlertApi::requestList();
            s_haTimer = lv_timer_create(_haPoll, 250, nullptr);
        } else {
            _haSetStatus(HamAlertApi::message(), theme::RED);
        }
        break;

    default:
        if (s_haTimer) { lv_timer_del(s_haTimer); s_haTimer = nullptr; }
        break;
    }
}

static void _haDeleteRowClick(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_uiTrigCount) return;
    _haSetStatus("Deleting...", theme::TEXT_MUTED);
    s_haMode = HP_DELETE;
    HamAlertApi::requestDelete(s_uiTrigs[idx].id);
    if (s_haTimer) lv_timer_del(s_haTimer);
    s_haTimer = lv_timer_create(_haPoll, 250, nullptr);
}

// Create the shared modal shell (screen + card + title + status line).
static lv_obj_t* _haOpenPopup(const char* title, int boxH) {
    s_haPrevScr = lv_scr_act();
    s_haPopup   = lv_obj_create(nullptr);
    lv_obj_set_size(s_haPopup, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(s_haPopup, theme::BG, 0);
    lv_obj_set_style_pad_all(s_haPopup, 0, 0);
    lv_obj_clear_flag(s_haPopup, LV_OBJ_FLAG_SCROLLABLE);

    s_haBox = lv_obj_create(s_haPopup);
    lv_obj_set_size(s_haBox, DXS_SCREEN_W - 20, boxH);
    lv_obj_align(s_haBox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_haBox, theme::BG_CARD, 0);
    lv_obj_set_style_border_color(s_haBox, theme::BORDER, 0);
    lv_obj_set_style_border_width(s_haBox, 1, 0);
    lv_obj_set_style_radius(s_haBox, 6, 0);
    lv_obj_set_style_pad_all(s_haBox, 8, 0);
    lv_obj_clear_flag(s_haBox, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr = lv_label_create(s_haBox);
    lv_label_set_text(hdr, title);
    lv_obj_set_style_text_color(hdr, theme::ACCENT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
    return s_haBox;
}

// ── Add popup ──────────────────────────────────────────────────────────
static void _haOnAddSave(lv_event_t*) {
    if (!s_haTA) return;
    char clean[20];
    _cleanCall(lv_textarea_get_text(s_haTA), clean, sizeof(clean));
    if (clean[0] == '\0') { _haSetStatus("Enter a callsign", theme::RED); return; }

    char msg[40];
    snprintf(msg, sizeof(msg), "Adding %s...", clean);
    _haSetStatus(msg, theme::TEXT_MUTED);
    s_haMode = HP_ADD;
    HamAlertApi::requestAdd(clean);
    if (s_haTimer) lv_timer_del(s_haTimer);
    s_haTimer = lv_timer_create(_haPoll, 250, nullptr);
}

void ScreenHamAlert::_onAddClick(lv_event_t*) {
    lv_obj_t* box = _haOpenPopup("Add Callsign", 150);

    s_haTA = lv_textarea_create(box);
    lv_obj_set_size(s_haTA, lv_pct(100), 40);
    lv_obj_align(s_haTA, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_obj_set_style_bg_color(s_haTA, theme::BG, 0);
    lv_obj_set_style_border_color(s_haTA, theme::ACCENT, 0);
    lv_obj_set_style_text_color(s_haTA, theme::TEXT, 0);
    lv_obj_set_style_text_font(s_haTA, &lv_font_montserrat_14, 0);
    lv_textarea_set_one_line(s_haTA, true);
    lv_textarea_set_max_length(s_haTA, 18);
    lv_textarea_set_placeholder_text(s_haTA, "e.g. 3Y0J");
    lv_obj_add_state(s_haTA, LV_STATE_FOCUSED);
    lv_group_add_obj(lv_group_get_default(), s_haTA);
    lv_group_focus_obj(s_haTA);

    s_haStatus = lv_label_create(box);
    lv_label_set_text(s_haStatus, "");
    lv_obj_set_width(s_haStatus, lv_pct(100));
    lv_label_set_long_mode(s_haStatus, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_haStatus, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_haStatus, &lv_font_montserrat_10, 0);
    lv_obj_align(s_haStatus, LV_ALIGN_TOP_LEFT, 0, 66);

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
    mkBtn(btnRow, "Cancel", _haOnClose,   theme::BG_CARD);
    mkBtn(btnRow, "Add",    _haOnAddSave, theme::PRIMARY);

    lv_scr_load(s_haPopup);
}

// ── Delete popup ───────────────────────────────────────────────────────
void ScreenHamAlert::_onDeleteClick(lv_event_t*) {
    lv_obj_t* box = _haOpenPopup("Delete Trigger", DXS_SCREEN_H - 24);

    s_haStatus = lv_label_create(box);
    lv_label_set_text(s_haStatus, "Loading triggers...");
    lv_obj_set_width(s_haStatus, lv_pct(100));
    lv_label_set_long_mode(s_haStatus, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_haStatus, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_haStatus, &lv_font_montserrat_10, 0);
    lv_obj_align(s_haStatus, LV_ALIGN_TOP_LEFT, 0, 22);

    // Close button (top-right of card)
    lv_obj_t* close = lv_btn_create(box);
    lv_group_remove_obj(close);
    lv_obj_set_size(close, 64, 24);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -2);
    lv_obj_set_style_bg_color(close, theme::PRIMARY, 0);
    lv_obj_set_style_bg_color(close, theme::ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(close, 4, 0);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_add_event_cb(close, _haOnClose, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cl = lv_label_create(close);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_color(cl, theme::TEXT, 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_10, 0);
    lv_obj_center(cl);

    s_haList = lv_obj_create(box);
    lv_obj_set_size(s_haList, lv_pct(100), DXS_SCREEN_H - 24 - 16 - 40);
    lv_obj_align(s_haList, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_bg_color(s_haList, theme::BG, 0);
    lv_obj_set_style_border_width(s_haList, 0, 0);
    lv_obj_set_style_radius(s_haList, 0, 0);
    lv_obj_set_style_pad_all(s_haList, 0, 0);
    lv_obj_set_style_pad_row(s_haList, 0, 0);
    lv_obj_set_flex_flow(s_haList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_haList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(s_haList, LV_SCROLLBAR_MODE_AUTO);

    lv_scr_load(s_haPopup);

    s_haMode = HP_LIST;
    HamAlertApi::requestList();
    if (s_haTimer) lv_timer_del(s_haTimer);
    s_haTimer = lv_timer_create(_haPoll, 250, nullptr);
}

}}  // namespace dxs::ui
