// DXSpotter — ScreenSat.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenSat.h"
#include "ScreenLauncher.h"
#include "Theme.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include "../net/SatFeed.h"
#include "../net/WiFiMgr.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace dxs { namespace ui {

static constexpr int TOP_H = 28;
static constexpr int BOT_H = 30;

lv_obj_t* ScreenSat::_screen    = nullptr;
lv_obj_t* ScreenSat::_list      = nullptr;
lv_obj_t* ScreenSat::_statusLbl = nullptr;

// Column widths sum to the row's usable width (320 - 2*4px pad = 312).
//   Sat 60 + Call 64 + Mode 44 + El 40 + AOS 104 = 312
static constexpr int COL_SAT  = 60;
static constexpr int COL_CALL = 64;
static constexpr int COL_MODE = 44;
static constexpr int COL_EL   = 40;
static constexpr int COL_AOS  = 104;
static constexpr int ROW_H    = 22;

void ScreenSat::show() {
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
    SatFeed::requestFetch();
    DXS_LOG("UI", "Satellites shown");
}

void ScreenSat::onNewData() {
    if (_screen && lv_scr_act() == _screen) _rebuildList();
}

void ScreenSat::_buildTopBar(lv_obj_t* parent) {
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
    lv_label_set_text(title, LV_SYMBOL_GPS "  Satellites");
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

void ScreenSat::_buildList(lv_obj_t* parent) {
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

// Max-elevation colour: higher passes are easier to work.
static lv_color_t _elColor(float el) {
    if (el >= 30) return theme::GREEN;
    if (el >= 15) return theme::ORANGE;
    return theme::RED;
}

// Build a "time to AOS" string (or LIVE / clock time when unsynced).
static void _aosText(time_t aos, time_t los, char* out, size_t n, lv_color_t* col) {
    time_t now = time(nullptr);
    if (now < 1700000000L || aos == 0) {        // clock not synced / no AOS
        if (aos) {
            time_t t = aos + config::get().timezoneOffsetHours * 3600L;
            struct tm g; gmtime_r(&t, &g);
            snprintf(out, n, "%02d:%02d", g.tm_hour, g.tm_min);
        } else {
            snprintf(out, n, "-");
        }
        *col = theme::TEXT_MUTED;
        return;
    }
    if (los && now > los) { snprintf(out, n, "ended"); *col = theme::TEXT_MUTED; return; }
    if (now >= aos)       { snprintf(out, n, "LIVE");  *col = theme::GREEN;      return; }
    long d = (long)(aos - now);
    if      (d < 60)    snprintf(out, n, "in %lds", d);
    else if (d < 3600)  snprintf(out, n, "in %ldm", d / 60);
    else if (d < 86400) snprintf(out, n, "in %ldh%02ldm", d / 3600, (d % 3600) / 60);
    else                snprintf(out, n, "in %ldd", d / 86400);
    *col = theme::ACCENT;
}

static void makeSatRow(lv_obj_t* list, const dxs::SatPass& p, int idx) {
    using namespace dxs::theme;
    lv_obj_t* row = lv_obj_create(list);
    lv_obj_set_size(row, lv_pct(100), ROW_H);
    lv_obj_set_style_bg_color(row, (idx & 1) ? BG_CARD : BG, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_set_style_pad_column(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto mkCell = [](lv_obj_t* par, int w, const char* t, lv_color_t c,
                     lv_text_align_t al) {
        lv_obj_t* l = lv_label_create(par);
        lv_obj_set_width(l, w);
        lv_label_set_text(l, t);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(l, c, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(l, al, 0);
        return l;
    };

    char elBuf[8];
    if (p.maxElevation > 0) snprintf(elBuf, sizeof(elBuf), "%.0f\xC2\xB0", p.maxElevation);
    else                    snprintf(elBuf, sizeof(elBuf), "-");

    char aosBuf[16]; lv_color_t aosCol;
    _aosText(p.aosUtc, p.losUtc, aosBuf, sizeof(aosBuf), &aosCol);

    // Workable passes (operator reachable) get a green callsign.
    lv_color_t callCol = p.workable ? GREEN : TEXT;

    mkCell(row, COL_SAT,  p.satName,            ACCENT,     LV_TEXT_ALIGN_LEFT);
    mkCell(row, COL_CALL, p.callsign[0] ? p.callsign : "-", callCol, LV_TEXT_ALIGN_LEFT);
    mkCell(row, COL_MODE, p.mode[0] ? p.mode : "-", TEXT_MUTED, LV_TEXT_ALIGN_LEFT);
    mkCell(row, COL_EL,   elBuf,                _elColor(p.maxElevation), LV_TEXT_ALIGN_LEFT);
    mkCell(row, COL_AOS,  aosBuf,               aosCol,     LV_TEXT_ALIGN_RIGHT);
}

void ScreenSat::_rebuildList() {
    if (!_list) return;
    lv_obj_clean(_list);

    auto showMsg = [](const char* msg, const char* status) {
        lv_obj_t* m = lv_label_create(_list);
        lv_label_set_text(m, msg);
        lv_obj_set_style_text_color(m, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(m, &lv_font_montserrat_12, 0);
        lv_obj_align(m, LV_ALIGN_TOP_MID, 0, 20);
        if (_statusLbl) lv_label_set_text(_statusLbl, status);
    };

    if (config::get().hamsatKey[0] == '\0') {
        showMsg("hams.at key not set.\nAdd it in Settings\n(generate one at hams.at).", "No key");
        return;
    }
    if (!WiFiMgr::instance().isConnected()) {
        showMsg(LV_SYMBOL_WIFI " No WiFi connection.", "No WiFi");
        return;
    }

    static SatPass passes[SAT_MAX_PASSES];
    int n = SatFeed::getPasses(passes, SAT_MAX_PASSES);

    if (n == 0) {
        int st = SatFeed::lastStatus();
        if (st == 0)        showMsg("Fetching passes from hams.at...", "...");
        else if (st == 401) showMsg("Auth failed — check the key in Settings.", "401");
        else                showMsg("No upcoming passes.", "0 passes");
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
        auto mkH = [](lv_obj_t* p, int w, const char* t, lv_text_align_t al) {
            lv_obj_t* l = lv_label_create(p);
            lv_obj_set_width(l, w);
            lv_label_set_text(l, t);
            lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(l, dxs::theme::ACCENT, 0);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_align(l, al, 0);
        };
        mkH(hdr, COL_SAT,  "Sat",  LV_TEXT_ALIGN_LEFT);
        mkH(hdr, COL_CALL, "Call", LV_TEXT_ALIGN_LEFT);
        mkH(hdr, COL_MODE, "Mode", LV_TEXT_ALIGN_LEFT);
        mkH(hdr, COL_EL,   "El",   LV_TEXT_ALIGN_LEFT);
        mkH(hdr, COL_AOS,  "AOS",  LV_TEXT_ALIGN_RIGHT);
    }

    for (int i = 0; i < n; i++) makeSatRow(_list, passes[i], i);

    if (_statusLbl) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%d passes", n);
        lv_label_set_text(_statusLbl, buf);
    }
}

void ScreenSat::_buildBottomBar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, DXS_SCREEN_W, BOT_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* b = lv_btn_create(bar);
    lv_group_remove_obj(b);
    lv_obj_set_size(b, DXS_SCREEN_W, BOT_H);
    lv_obj_set_style_bg_color(b, theme::PRIMARY, 0);
    lv_obj_set_style_bg_color(b, theme::ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_add_event_cb(b, _onRefreshClick, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, LV_SYMBOL_REFRESH "  Refresh");
    lv_obj_set_style_text_color(l, theme::TEXT, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_center(l);
}

void ScreenSat::_onHomeClick(lv_event_t*)    { ScreenLauncher::show(); }
void ScreenSat::_onRefreshClick(lv_event_t*) { SatFeed::requestFetch(); }

}}  // namespace dxs::ui
