// DXSpotter — ScreenSolar.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenSolar.h"
#include "ScreenLauncher.h"
#include "Theme.h"
#include "../utils/Log.h"
#include "../net/SolarFeed.h"
#include "../net/WiFiMgr.h"

#include <cstdio>
#include <cstring>

namespace dxs { namespace ui {

static constexpr int TOP_H = 28;
static constexpr int BOT_H = 30;   // bottom refresh bar

lv_obj_t* ScreenSolar::_screen  = nullptr;
lv_obj_t* ScreenSolar::_sfiLbl  = nullptr;
lv_obj_t* ScreenSolar::_aLbl    = nullptr;
lv_obj_t* ScreenSolar::_kLbl    = nullptr;
lv_obj_t* ScreenSolar::_xrayLbl = nullptr;
lv_obj_t* ScreenSolar::_windLbl = nullptr;
lv_obj_t* ScreenSolar::_updLbl  = nullptr;
lv_obj_t* ScreenSolar::_bandLbl[4][2] = {};

// ── show() ────────────────────────────────────────────────────────────
void ScreenSolar::show() {
    if (!_screen) {
        _screen = lv_obj_create(nullptr);
        lv_obj_set_size(_screen, DXS_SCREEN_W, DXS_SCREEN_H);
        lv_obj_set_style_bg_color(_screen, theme::BG, 0);
        lv_obj_set_style_pad_all(_screen, 0, 0);
        lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

        _buildTopBar(_screen);
        _buildBody(_screen);
    }

    _refresh();
    lv_scr_load(_screen);
    SolarFeed::requestFetch();
    DXS_LOG("UI", "Solar shown");
}

// ── _buildTopBar() ────────────────────────────────────────────────────
void ScreenSolar::_buildTopBar(lv_obj_t* parent) {
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
    lv_label_set_text(title, LV_SYMBOL_IMAGE "  Solar Weather");
    lv_obj_set_style_text_color(title, theme::TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    // Update-status label (right side; refresh moved to the bottom bar)
    _updLbl = lv_label_create(bar);
    lv_label_set_text(_updLbl, "No data");
    lv_obj_set_style_text_color(_updLbl, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(_updLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_pad_right(_updLbl, 4, 0);
}

// ── _buildBody() ──────────────────────────────────────────────────────
// Layout (body = 320×212 px below the top bar):
//  ┌────────────────────────────────────┐  y=28
//  │  SFI: ---  A: --  K: -  X: ---    │  stats row 30px
//  │  Wind: --- km/s                    │
//  ├────────────────────────────────────┤  y=78
//  │  Band        Day       Night       │  header row
//  │  80m-40m     Good      Good        │
//  │  30m-20m     Fair      Good        │
//  │  17m-15m     Poor      Fair        │
//  │  12m-10m     Poor      Poor        │
//  ├────────────────────────────────────┤
//  │  Updated: --:--                    │  footer
//  └────────────────────────────────────┘

static lv_obj_t* makeStatLabel(lv_obj_t* par, const char* heading,
                               lv_obj_t** valueOut) {
    lv_obj_t* col = lv_obj_create(par);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 2, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t* hLbl = lv_label_create(col);
    lv_label_set_text(hLbl, heading);
    lv_obj_set_style_text_font(hLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hLbl, dxs::theme::TEXT_MUTED, 0);

    lv_obj_t* vLbl = lv_label_create(col);
    lv_label_set_text(vLbl, "---");
    lv_obj_set_style_text_font(vLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(vLbl, dxs::theme::ACCENT, 0);
    if (valueOut) *valueOut = vLbl;

    return col;
}

void ScreenSolar::_buildBody(lv_obj_t* parent) {
    using namespace theme;

    // Stats panel (horizontal flex row)
    lv_obj_t* statsRow = lv_obj_create(parent);
    lv_obj_set_size(statsRow, DXS_SCREEN_W, 54);
    lv_obj_align(statsRow, LV_ALIGN_TOP_LEFT, 0, TOP_H);
    lv_obj_set_style_bg_color(statsRow, BG_CARD, 0);
    lv_obj_set_style_border_width(statsRow, 0, 0);
    lv_obj_set_style_radius(statsRow, 0, 0);
    lv_obj_set_style_pad_hor(statsRow, 8, 0);
    lv_obj_set_style_pad_ver(statsRow, 4, 0);
    lv_obj_set_style_pad_column(statsRow, 8, 0);
    lv_obj_clear_flag(statsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(statsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(statsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    makeStatLabel(statsRow, "SFI",    &_sfiLbl);
    makeStatLabel(statsRow, "A-idx",  &_aLbl);
    makeStatLabel(statsRow, "K-idx",  &_kLbl);
    makeStatLabel(statsRow, "X-ray",  &_xrayLbl);
    makeStatLabel(statsRow, "Wind",   &_windLbl);

    // Band conditions table
    static constexpr int TABLE_Y = TOP_H + 54;
    static constexpr int TABLE_H = DXS_SCREEN_H - TABLE_Y - BOT_H;  // leave room for bottom bar

    lv_obj_t* table = lv_obj_create(parent);
    lv_obj_set_size(table, DXS_SCREEN_W, TABLE_H);
    lv_obj_align(table, LV_ALIGN_TOP_LEFT, 0, TABLE_Y);
    lv_obj_set_style_bg_color(table, BG, 0);
    lv_obj_set_style_border_width(table, 0, 0);
    lv_obj_set_style_radius(table, 0, 0);
    lv_obj_set_style_pad_all(table, 0, 0);
    lv_obj_set_style_pad_row(table, 0, 0);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(table, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(table, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Column widths: Band=100, Day=110, Night=110
    static constexpr int W_BAND  = 100;
    static constexpr int W_COND  = 110;
    static constexpr int ROW_H   = 26;

    static const char* kBandNames[] = { "80m-40m", "30m-20m", "17m-15m", "12m-10m" };

    // Header row
    {
        lv_obj_t* hdr = lv_obj_create(table);
        lv_obj_set_size(hdr, DXS_SCREEN_W, ROW_H);
        lv_obj_set_style_bg_color(hdr, BG_CARD, 0);
        lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(hdr, 0, 0);
        lv_obj_set_style_radius(hdr, 0, 0);
        lv_obj_set_style_pad_hor(hdr, 8, 0);
        lv_obj_set_style_pad_ver(hdr, 2, 0);
        lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto mkHdr = [](lv_obj_t* p, int w, const char* t) {
            lv_obj_t* l = lv_label_create(p);
            lv_obj_set_width(l, w);
            lv_label_set_text(l, t);
            lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(l, dxs::theme::ACCENT, 0);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        };
        mkHdr(hdr, W_BAND, "Band");
        mkHdr(hdr, W_COND, "Day");
        mkHdr(hdr, W_COND, "Night");
    }

    // Data rows
    for (int b = 0; b < 4; b++) {
        lv_obj_t* row = lv_obj_create(table);
        lv_obj_set_size(row, DXS_SCREEN_W, ROW_H);
        lv_color_t bg = (b & 1) ? BG_CARD : BG;
        lv_obj_set_style_bg_color(row, bg, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_set_style_pad_ver(row, 2, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* bandLbl = lv_label_create(row);
        lv_obj_set_width(bandLbl, W_BAND);
        lv_label_set_text(bandLbl, kBandNames[b]);
        lv_label_set_long_mode(bandLbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(bandLbl, TEXT, 0);
        lv_obj_set_style_text_font(bandLbl, &lv_font_montserrat_10, 0);

        for (int d = 0; d < 2; d++) {
            _bandLbl[b][d] = lv_label_create(row);
            lv_obj_set_width(_bandLbl[b][d], W_COND);
            lv_label_set_text(_bandLbl[b][d], "---");
            lv_label_set_long_mode(_bandLbl[b][d], LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(_bandLbl[b][d], TEXT_MUTED, 0);
            lv_obj_set_style_text_font(_bandLbl[b][d], &lv_font_montserrat_10, 0);
        }
    }

    // Bottom bar: full-width refresh button (swapped here from the top bar)
    lv_obj_t* refBtn = lv_btn_create(parent);
    lv_group_remove_obj(refBtn);
    lv_obj_set_size(refBtn, DXS_SCREEN_W, BOT_H);
    lv_obj_align(refBtn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(refBtn, PRIMARY, 0);
    lv_obj_set_style_bg_color(refBtn, ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(refBtn, 0, 0);
    lv_obj_set_style_radius(refBtn, 0, 0);
    lv_obj_set_style_shadow_width(refBtn, 0, 0);
    lv_obj_set_style_pad_all(refBtn, 0, 0);
    lv_obj_add_event_cb(refBtn, _onRefreshClick, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* refLbl = lv_label_create(refBtn);
    lv_label_set_text(refLbl, LV_SYMBOL_REFRESH "  Refresh");
    lv_obj_set_style_text_color(refLbl, TEXT, 0);
    lv_obj_set_style_text_font(refLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(refLbl);
}

// ── onNewData() ───────────────────────────────────────────────────────
void ScreenSolar::onNewData() {
    if (_screen && lv_scr_act() == _screen) {
        _refresh();
    }
}

// ── _refresh() ────────────────────────────────────────────────────────
void ScreenSolar::_refresh() {
    SolarData d;
    if (!SolarFeed::getData(d)) {
        // No data yet — leave placeholders
        return;
    }

    char buf[20];
    if (_sfiLbl)  { snprintf(buf, sizeof(buf), "%d", d.sfi);           lv_label_set_text(_sfiLbl,  buf); }
    if (_aLbl)    { snprintf(buf, sizeof(buf), "%d", d.aindex);        lv_label_set_text(_aLbl,    buf); }
    if (_kLbl)    { snprintf(buf, sizeof(buf), "%d", d.kindex);        lv_label_set_text(_kLbl,    buf); }
    if (_xrayLbl) { lv_label_set_text(_xrayLbl, d.xray[0] ? d.xray : "---"); }
    if (_windLbl) { snprintf(buf, sizeof(buf), "%d", d.solarWindKmS);  lv_label_set_text(_windLbl, buf); }

    // SFI: >130 = good conditions (green), 100-130 = fair (orange), <100 = poor (red)
    if (_sfiLbl) {
        lv_color_t c = (d.sfi > 130) ? theme::GREEN
                     : (d.sfi >= 100) ? theme::ORANGE
                                      : theme::RED;
        lv_obj_set_style_text_color(_sfiLbl, c, 0);
    }
    // A-index: <9 = quiet (green), 9-29 = unsettled (orange), ≥30 = storm (red)
    if (_aLbl) {
        lv_color_t c = (d.aindex < 9)  ? theme::GREEN
                     : (d.aindex < 30) ? theme::ORANGE
                                       : theme::RED;
        lv_obj_set_style_text_color(_aLbl, c, 0);
    }
    // K-index colour: 0-3=green 4-5=orange 6+=red
    if (_kLbl) {
        lv_color_t kc = (d.kindex <= 3) ? theme::GREEN
                      : (d.kindex <= 5) ? theme::ORANGE
                                        : theme::RED;
        lv_obj_set_style_text_color(_kLbl, kc, 0);
    }

    const char* dayBands[4]   = { d.b80_40_day,   d.b30_20_day,   d.b17_15_day,   d.b12_10_day   };
    const char* nightBands[4] = { d.b80_40_night, d.b30_20_night, d.b17_15_night, d.b12_10_night };

    for (int b = 0; b < 4; b++) {
        if (_bandLbl[b][0]) {
            lv_label_set_text(_bandLbl[b][0], dayBands[b][0] ? dayBands[b] : "---");
            lv_obj_set_style_text_color(_bandLbl[b][0], theme::condColor(dayBands[b]), 0);
        }
        if (_bandLbl[b][1]) {
            lv_label_set_text(_bandLbl[b][1], nightBands[b][0] ? nightBands[b] : "---");
            lv_obj_set_style_text_color(_bandLbl[b][1], theme::condColor(nightBands[b]), 0);
        }
    }

    if (_updLbl) {
        uint32_t ageSec = (millis() - d.fetchedAt) / 1000;
        snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH " %lus ago", (unsigned long)ageSec);
        lv_label_set_text(_updLbl, buf);
    }
}

void ScreenSolar::_onHomeClick(lv_event_t*)    { ScreenLauncher::show(); }
void ScreenSolar::_onRefreshClick(lv_event_t*) { SolarFeed::requestFetch(); }

}}  // namespace dxs::ui
