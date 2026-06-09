// DXSpotter — ScreenDXpeditions.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenDXpeditions.h"
#include "ScreenLauncher.h"
#include "Theme.h"
#include "../utils/Log.h"
#include "../net/DXpeditionFeed.h"
#include "../net/DXWorldFeed.h"
#include "../net/HamAlertApi.h"
#include "../net/WiFiMgr.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>

namespace dxs { namespace ui {

static constexpr int TOP_H = 28;
static constexpr int ROW_H = 36;

// Derive status from the date range: -1 unknown (clock not set), 0 upcoming,
// 1 active, 2 ended. Dates are "YYYY-MM-DD".
static int _pedStatus(const char* startYmd, const char* endYmd) {
    time_t now = time(nullptr);
    if (now < 1000000000) return -1;   // NTP not synced yet
    struct tm g;
    gmtime_r(&now, &g);
    int today = (g.tm_year + 1900) * 10000 + (g.tm_mon + 1) * 100 + g.tm_mday;
    int y, m, d, s = 0, e = 0;
    if (sscanf(startYmd, "%d-%d-%d", &y, &m, &d) == 3) s = y * 10000 + m * 100 + d;
    if (sscanf(endYmd,   "%d-%d-%d", &y, &m, &d) == 3) e = y * 10000 + m * 100 + d;
    if (s && today < s) return 0;
    if (e && today > e) return 2;
    return 1;
}

lv_obj_t* ScreenDXpeditions::_screen    = nullptr;
lv_obj_t* ScreenDXpeditions::_list      = nullptr;
lv_obj_t* ScreenDXpeditions::_statusLbl = nullptr;
lv_obj_t* ScreenDXpeditions::_titleLbl  = nullptr;
lv_obj_t* ScreenDXpeditions::_pageLbl   = nullptr;
int8_t    ScreenDXpeditions::_page      = 0;

// File-scope copy of the rows currently shown, so the row-tap handler can read
// the tapped entry by index (the display array would otherwise be local).
static DXpedition s_peds[DXPED_MAX];
static int        s_pedCount = 0;

// ── show() ────────────────────────────────────────────────────────────
void ScreenDXpeditions::show() {
    if (!_screen) {
        _screen = lv_obj_create(nullptr);
        lv_obj_set_size(_screen, DXS_SCREEN_W, DXS_SCREEN_H);
        lv_obj_set_style_bg_color(_screen, theme::BG, 0);
        lv_obj_set_style_pad_all(_screen, 0, 0);
        lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

        _buildTopBar(_screen);
        _buildList(_screen);
        // Left/right swipe flips between Holy Cluster and DX-World pages
        lv_obj_add_event_cb(_screen, _onGesture, LV_EVENT_GESTURE, nullptr);
    }

    _rebuildList();
    lv_scr_load(_screen);
    _fetchCurrent();   // kick off a fresh fetch for the active page
    DXS_LOG("UI", "DXpeditions shown (page %d)", _page);
}

// ── onNewData() ───────────────────────────────────────────────────────
void ScreenDXpeditions::onNewData() {
    if (_screen && lv_scr_act() == _screen) {
        _rebuildList();
    }
}

// Request a fetch for whichever page is active (feeds otherwise poll slowly).
void ScreenDXpeditions::_fetchCurrent() {
    if (_page == 0) DXpeditionFeed::requestFetch();
    else            DXWorldFeed::requestFetch();
}

// Apply a page change: refresh the title/page-button labels and the list.
// Run via lv_async_call so it never deletes list rows from inside a gesture cb.
void ScreenDXpeditions::_applyPage(void*) {
    if (!_screen) return;
    if (_titleLbl) lv_label_set_text(_titleLbl,
        _page == 0 ? LV_SYMBOL_EDIT "  DXpeditions" : LV_SYMBOL_EDIT "  DX-World");
    if (_pageLbl)  lv_label_set_text(_pageLbl, _page == 0 ? "DXW>" : "<HC");
    _rebuildList();
    _fetchCurrent();
}

// ── _buildTopBar() ────────────────────────────────────────────────────
void ScreenDXpeditions::_buildTopBar(lv_obj_t* parent) {
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

    // Home button
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

    // Title (reflects the active page)
    _titleLbl = lv_label_create(bar);
    lv_label_set_text(_titleLbl,
        _page == 0 ? LV_SYMBOL_EDIT "  DXpeditions" : LV_SYMBOL_EDIT "  DX-World");
    lv_obj_set_style_text_color(_titleLbl, theme::TEXT, 0);
    lv_obj_set_style_text_font(_titleLbl, &lv_font_montserrat_14, 0);

    // Spacer
    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    // Page-switch button (Holy Cluster ↔ DX-World)
    lv_obj_t* pageBtn = lv_btn_create(bar);
    lv_group_remove_obj(pageBtn);
    lv_obj_set_height(pageBtn, TOP_H - 6);
    lv_obj_set_style_bg_color(pageBtn, theme::BG, 0);
    lv_obj_set_style_bg_color(pageBtn, theme::PRIMARY, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(pageBtn, theme::BORDER, 0);
    lv_obj_set_style_border_width(pageBtn, 1, 0);
    lv_obj_set_style_radius(pageBtn, 4, 0);
    lv_obj_set_style_shadow_width(pageBtn, 0, 0);
    lv_obj_set_style_pad_hor(pageBtn, 6, 0);
    lv_obj_add_event_cb(pageBtn, _onPageClick, LV_EVENT_CLICKED, nullptr);
    _pageLbl = lv_label_create(pageBtn);
    lv_label_set_text(_pageLbl, _page == 0 ? "DXW>" : "<HC");
    lv_obj_set_style_text_color(_pageLbl, theme::TEXT, 0);
    lv_obj_set_style_text_font(_pageLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(_pageLbl);

    // Status label
    _statusLbl = lv_label_create(bar);
    lv_label_set_text(_statusLbl, "...");
    lv_obj_set_style_text_color(_statusLbl, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(_statusLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_pad_right(_statusLbl, 4, 0);

    // Refresh button
    lv_obj_t* refBtn = lv_btn_create(bar);
    lv_group_remove_obj(refBtn);
    lv_obj_set_height(refBtn, TOP_H - 6);
    lv_obj_set_style_bg_color(refBtn, theme::PRIMARY, 0);
    lv_obj_set_style_bg_color(refBtn, theme::ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(refBtn, 4, 0);
    lv_obj_set_style_border_width(refBtn, 0, 0);
    lv_obj_set_style_shadow_width(refBtn, 0, 0);
    lv_obj_set_style_pad_hor(refBtn, 6, 0);
    lv_obj_add_event_cb(refBtn, _onRefreshClick, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* refLbl = lv_label_create(refBtn);
    lv_label_set_text(refLbl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(refLbl, theme::TEXT, 0);
    lv_obj_set_style_text_font(refLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(refLbl);
}

// ── _buildList() ──────────────────────────────────────────────────────
void ScreenDXpeditions::_buildList(lv_obj_t* parent) {
    _list = lv_obj_create(parent);
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
}

// ── _rebuildList() ────────────────────────────────────────────────────
void ScreenDXpeditions::_rebuildList() {
    if (!_list) return;
    lv_obj_clean(_list);

    if (!WiFiMgr::instance().isConnected()) {
        lv_obj_t* msg = lv_label_create(_list);
        lv_label_set_text(msg, LV_SYMBOL_WIFI " No WiFi — configure SSID in Settings");
        lv_obj_set_style_text_color(msg, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 20);
        if (_statusLbl) lv_label_set_text(_statusLbl, "No WiFi");
        return;
    }

    int n      = (_page == 0) ? DXpeditionFeed::getDXpeditions(s_peds, DXPED_MAX)
                              : DXWorldFeed::getDXpeditions(s_peds, DXPED_MAX);
    s_pedCount = n;
    int status = (_page == 0) ? DXpeditionFeed::lastStatus()
                              : DXWorldFeed::lastStatus();

    if (n == 0) {
        lv_obj_t* msg = lv_label_create(_list);
        lv_label_set_text(msg, status == 0
            ? "Fetching DXpeditions..."
            : "No DXpeditions on record");
        lv_obj_set_style_text_color(msg, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 20);
        if (_statusLbl) lv_label_set_text(_statusLbl, "0 entries");
        return;
    }

    for (int i = 0; i < n; i++) {
        const DXpedition& p = s_peds[i];
        int  st     = _pedStatus(p.startDate, p.endDate);
        bool active = (st == 1);
        lv_obj_t* row = lv_obj_create(_list);
        lv_obj_set_size(row, DXS_SCREEN_W, ROW_H);
        lv_obj_set_style_bg_color(row, (i & 1) ? theme::BG_CARD : theme::BG, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_set_style_pad_ver(row, 2, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // DX-World rows (page 1) are tappable → details + add-to-HamAlert popup.
        if (_page == 1) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(row, theme::PRIMARY, LV_STATE_PRESSED);
            lv_obj_add_event_cb(row, _onRowClick, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }

        // Callsign
        lv_obj_t* callLbl = lv_label_create(row);
        lv_obj_set_width(callLbl, 84);
        lv_label_set_text(callLbl, p.callsign[0] ? p.callsign : "---");
        lv_label_set_long_mode(callLbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(callLbl, active ? theme::GREEN : theme::ACCENT, 0);
        lv_obj_set_style_text_font(callLbl, &lv_font_montserrat_14, 0);

        // Middle column: entity/country when the feed provides it (DX-World),
        // otherwise the date-derived status (Holy Cluster has no entity field).
        const char* stText = (st == 1) ? "Active"
                           : (st == 0) ? "Upcoming"
                           : (st == 2) ? "Ended" : "";
        lv_color_t  stCol  = (st == 1) ? theme::GREEN
                           : (st == 0) ? theme::ACCENT
                                       : theme::TEXT_MUTED;
        bool hasEnt = (p.entity[0] != '\0');
        lv_obj_t* midLbl = lv_label_create(row);
        lv_obj_set_flex_grow(midLbl, 1);
        lv_label_set_text(midLbl, hasEnt ? p.entity : stText);
        lv_label_set_long_mode(midLbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(midLbl, hasEnt ? theme::TEXT : stCol, 0);
        lv_obj_set_style_text_font(midLbl, &lv_font_montserrat_12, 0);

        // Date range
        char dates[32];
        if (p.startDate[0] && p.endDate[0]) {
            // Shorten: "2026-06-01" → "Jun 01"
            static const char* kMon[] = {
                "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
            };
            int sm = 0, sd = 0, em = 0, ed = 0;
            sscanf(p.startDate + 5, "%d-%d", &sm, &sd);
            sscanf(p.endDate   + 5, "%d-%d", &em, &ed);
            if (sm >= 1 && sm <= 12 && em >= 1 && em <= 12) {
                snprintf(dates, sizeof(dates), "%s%02d-%s%02d",
                         kMon[sm-1], sd, kMon[em-1], ed);
            } else {
                snprintf(dates, sizeof(dates), "%.10s", p.startDate);
            }
        } else {
            strncpy(dates, "Date unknown", sizeof(dates) - 1);
        }
        lv_obj_t* dateLbl = lv_label_create(row);
        lv_obj_set_width(dateLbl, 90);
        lv_label_set_text(dateLbl, dates);
        lv_label_set_long_mode(dateLbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(dateLbl, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(dateLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(dateLbl, LV_TEXT_ALIGN_RIGHT, 0);

        // Active indicator
        if (active) {
            lv_obj_t* dot = lv_label_create(row);
            lv_label_set_text(dot, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(dot, theme::GREEN, 0);
            lv_obj_set_style_text_font(dot, &lv_font_montserrat_10, 0);
        }
    }

    if (_statusLbl) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d entries", n);
        lv_label_set_text(_statusLbl, buf);
    }
}

// ── Add-to-HamAlert popup (DX-World row tap) ──────────────────────────
// Tapping a DX-World row opens a details card with Cancel / Add. On open we
// fetch the user's HamAlert triggers (async) to see if the callsign already
// has an alert; if so the Add button becomes a disabled "Already in". Add posts
// the callsign with the DXpedition's date range as the trigger comment.

enum DXPopMode { DP_NONE, DP_CHECK, DP_ADD };

static lv_obj_t*   s_dxPopup   = nullptr;
static lv_obj_t*   s_dxStatus  = nullptr;
static lv_obj_t*   s_dxAddBtn  = nullptr;
static lv_obj_t*   s_dxAddLbl  = nullptr;
static lv_timer_t* s_dxTimer   = nullptr;
static lv_obj_t*   s_dxPrevScr = nullptr;
static DXPopMode   s_dxMode    = DP_NONE;
static bool        s_dxReady   = false;   // duplicate check finished, add allowed
static bool        s_dxExists  = false;   // callsign already has a trigger
static char        s_dxCall    [20] = {0};
static char        s_dxComment [48] = {0};

static void _dxClosePopup() {
    if (s_dxTimer) { lv_timer_del(s_dxTimer); s_dxTimer = nullptr; }
    if (s_dxPopup) { lv_obj_del_async(s_dxPopup); s_dxPopup = nullptr; }
    s_dxStatus = s_dxAddBtn = s_dxAddLbl = nullptr;
    s_dxMode = DP_NONE;
    s_dxReady = s_dxExists = false;
    if (s_dxPrevScr) { lv_scr_load(s_dxPrevScr); s_dxPrevScr = nullptr; }
}

static void _dxOnCancel(lv_event_t*) { _dxClosePopup(); }

static void _dxSetStatus(const char* txt, lv_color_t c) {
    if (!s_dxStatus) return;
    lv_label_set_text(s_dxStatus, txt);
    lv_obj_set_style_text_color(s_dxStatus, c, 0);
}

// Clean a callsign: upper-case, strip spaces, keep the longer half of a "/" split.
static void _dxCleanCall(const char* in, char* out, size_t n) {
    char tmp[24] = {0};
    int j = 0;
    for (const char* p = in; *p && j < (int)sizeof(tmp) - 1; ++p) {
        if (*p == ' ' || *p == '\t') continue;
        tmp[j++] = (char)toupper((unsigned char)*p);
    }
    const char* slash = strchr(tmp, '/');
    if (slash) {
        size_t la = (size_t)(slash - tmp), lb = strlen(slash + 1);
        if (lb >= la) { strncpy(out, slash + 1, n - 1); out[n - 1] = '\0'; }
        else { size_t k = la < n ? la : n - 1; strncpy(out, tmp, k); out[k] = '\0'; }
    } else {
        strncpy(out, tmp, n - 1);
        out[n - 1] = '\0';
    }
}

// True if `target` matches any callsign token in a trigger's (possibly
// comma-joined) callsign field, case-insensitively.
static bool _dxCallMatches(const char* trigCsv, const char* target) {
    char buf[64];
    strncpy(buf, trigCsv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char* tok = strtok(buf, ","); tok; tok = strtok(nullptr, ",")) {
        while (*tok == ' ') tok++;
        int e = (int)strlen(tok);
        while (e > 0 && tok[e - 1] == ' ') tok[--e] = '\0';
        if (strcasecmp(tok, target) == 0) return true;
    }
    return false;
}

static void _dxOnAdd(lv_event_t*) {
    if (!s_dxReady || s_dxExists || s_dxCall[0] == '\0') return;
    _dxSetStatus("Adding...", theme::TEXT_MUTED);
    s_dxMode = DP_ADD;
    HamAlertApi::requestAdd(s_dxCall, s_dxComment);
}

static void _dxPoll(lv_timer_t*) {
    if (HamAlertApi::status() == HamAlertApi::BUSY) return;
    bool ok = (HamAlertApi::status() == HamAlertApi::OK);

    if (s_dxMode == DP_CHECK) {
        if (s_dxTimer) { lv_timer_del(s_dxTimer); s_dxTimer = nullptr; }
        s_dxExists = false;
        if (ok) {
            static HATrigger trigs[HA_MAX_TRIGGERS];
            int n = HamAlertApi::getTriggers(trigs, HA_MAX_TRIGGERS);
            for (int i = 0; i < n; i++)
                if (_dxCallMatches(trigs[i].callsign, s_dxCall)) { s_dxExists = true; break; }
        }
        s_dxReady = true;
        if (s_dxExists) {
            if (s_dxAddLbl) lv_label_set_text(s_dxAddLbl, "Already in");
            if (s_dxAddBtn) {
                lv_obj_add_state(s_dxAddBtn, LV_STATE_DISABLED);
                lv_obj_set_style_bg_color(s_dxAddBtn, theme::BG_CARD, 0);
            }
            _dxSetStatus("Already has an alert", theme::TEXT_MUTED);
        } else {
            if (s_dxAddLbl) lv_label_set_text(s_dxAddLbl, "Add");
            _dxSetStatus(ok ? "" : "Couldn't verify existing alerts", ok ? theme::TEXT_MUTED : theme::ORANGE);
        }
        return;
    }

    if (s_dxMode == DP_ADD) {
        if (s_dxTimer) { lv_timer_del(s_dxTimer); s_dxTimer = nullptr; }
        _dxSetStatus(HamAlertApi::message(), ok ? theme::GREEN : theme::RED);
        if (ok) {
            s_dxMode = DP_NONE;
            s_dxTimer = lv_timer_create([](lv_timer_t*) { _dxClosePopup(); }, 1100, nullptr);
        }
        return;
    }

    if (s_dxTimer) { lv_timer_del(s_dxTimer); s_dxTimer = nullptr; }
}

void ScreenDXpeditions::_onRowClick(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_pedCount) return;
    const DXpedition& p = s_peds[idx];
    if (p.callsign[0] == '\0') return;

    _dxCleanCall(p.callsign, s_dxCall, sizeof(s_dxCall));
    // Date range → trigger comment
    if (p.startDate[0] && p.endDate[0])
        snprintf(s_dxComment, sizeof(s_dxComment), "%s - %s", p.startDate, p.endDate);
    else if (p.startDate[0]) snprintf(s_dxComment, sizeof(s_dxComment), "%s", p.startDate);
    else if (p.endDate[0])   snprintf(s_dxComment, sizeof(s_dxComment), "%s", p.endDate);
    else                     s_dxComment[0] = '\0';

    int st = _pedStatus(p.startDate, p.endDate);
    const char* stText = (st == 1) ? "Active" : (st == 0) ? "Upcoming"
                       : (st == 2) ? "Ended" : "Unknown";

    s_dxReady = s_dxExists = false;
    s_dxMode  = DP_CHECK;
    s_dxPrevScr = lv_scr_act();

    s_dxPopup = lv_obj_create(nullptr);
    lv_obj_set_size(s_dxPopup, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(s_dxPopup, theme::BG, 0);
    lv_obj_set_style_pad_all(s_dxPopup, 0, 0);
    lv_obj_clear_flag(s_dxPopup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* box = lv_obj_create(s_dxPopup);
    lv_obj_set_size(box, DXS_SCREEN_W - 24, 188);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, theme::BG_CARD, 0);
    lv_obj_set_style_border_color(box, theme::BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(box);
    lv_label_set_text(title, p.callsign);
    lv_obj_set_style_text_color(title, theme::ACCENT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    char det[200];
    if (p.entity[0])
        snprintf(det, sizeof(det), "%s\nDates: %s\nStatus: %s",
                 p.entity, s_dxComment[0] ? s_dxComment : "unknown", stText);
    else
        snprintf(det, sizeof(det), "Dates: %s\nStatus: %s",
                 s_dxComment[0] ? s_dxComment : "unknown", stText);
    lv_obj_t* detLbl = lv_label_create(box);
    lv_obj_set_width(detLbl, lv_pct(100));
    lv_label_set_long_mode(detLbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(detLbl, det);
    lv_obj_set_style_text_color(detLbl, theme::TEXT, 0);
    lv_obj_set_style_text_font(detLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(detLbl, LV_ALIGN_TOP_LEFT, 0, 22);

    s_dxStatus = lv_label_create(box);
    lv_label_set_text(s_dxStatus, "Checking...");
    lv_obj_set_width(s_dxStatus, lv_pct(100));
    lv_label_set_long_mode(s_dxStatus, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_dxStatus, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_dxStatus, &lv_font_montserrat_10, 0);
    lv_obj_align(s_dxStatus, LV_ALIGN_TOP_LEFT, 0, 112);

    lv_obj_t* btnRow = lv_obj_create(box);
    lv_obj_set_size(btnRow, lv_pct(100), 30);
    lv_obj_align(btnRow, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 8, 0);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_size(cancel, 80, 30);
    lv_obj_set_style_bg_color(cancel, theme::BG_CARD, 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(cancel, 4, 0);
    lv_obj_set_style_border_color(cancel, theme::BORDER, 0);
    lv_obj_set_style_border_width(cancel, 1, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    lv_obj_add_event_cb(cancel, _dxOnCancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, theme::TEXT, 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_12, 0);
    lv_obj_center(cl);

    s_dxAddBtn = lv_btn_create(btnRow);
    lv_obj_set_size(s_dxAddBtn, 90, 30);
    lv_obj_set_style_bg_color(s_dxAddBtn, theme::PRIMARY, 0);
    lv_obj_set_style_bg_color(s_dxAddBtn, theme::ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_dxAddBtn, 4, 0);
    lv_obj_set_style_border_width(s_dxAddBtn, 0, 0);
    lv_obj_set_style_shadow_width(s_dxAddBtn, 0, 0);
    lv_obj_add_event_cb(s_dxAddBtn, _dxOnAdd, LV_EVENT_CLICKED, nullptr);
    s_dxAddLbl = lv_label_create(s_dxAddBtn);
    lv_label_set_text(s_dxAddLbl, "Checking...");
    lv_obj_set_style_text_color(s_dxAddLbl, theme::TEXT, 0);
    lv_obj_set_style_text_font(s_dxAddLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(s_dxAddLbl);

    lv_scr_load(s_dxPopup);

    // Kick off the duplicate check.
    HamAlertApi::requestList();
    s_dxTimer = lv_timer_create(_dxPoll, 250, nullptr);
}

// ── Event handlers ────────────────────────────────────────────────────
void ScreenDXpeditions::_onHomeClick(lv_event_t*) { ScreenLauncher::show(); }

void ScreenDXpeditions::_onRefreshClick(lv_event_t*) {
    if (_statusLbl) lv_label_set_text(_statusLbl, "...");
    _fetchCurrent();
    _rebuildList();
}

// Top-bar page toggle — defer the rebuild so we don't delete rows mid-event.
void ScreenDXpeditions::_onPageClick(lv_event_t*) {
    _page = (_page == 0) ? 1 : 0;
    lv_async_call(_applyPage, nullptr);
}

// Left/right swipe flips the page (event bubbles from the list to the screen).
void ScreenDXpeditions::_onGesture(lv_event_t*) {
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    _page = (_page == 0) ? 1 : 0;
    lv_async_call(_applyPage, nullptr);
}

}}  // namespace dxs::ui
