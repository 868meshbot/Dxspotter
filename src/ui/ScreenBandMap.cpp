// DXSpotter — ScreenBandMap.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenBandMap.h"
#include "ScreenLauncher.h"
#include "Theme.h"
#include "../utils/Log.h"
#include "../net/HolyClusterFeed.h"

#include <cstdio>
#include <cstring>

namespace dxs { namespace ui {

static constexpr int TOP_H  = 28;
static constexpr int HEAD_H = 20;
static constexpr int BANDW  = 40;   // band label column width
static constexpr int GRID_H = DXS_SCREEN_H - TOP_H;   // 212px

// Each page defines a band range in HC_BAND_LABELS/kBandMeters
static constexpr int HF_START  = 0;
static constexpr int HF_COUNT  = HC_NUM_HF;   // 10
static constexpr int VHF_START = HC_NUM_HF;
static constexpr int VHF_COUNT = HC_NUM_VHF;  // 6

// ── Static members ────────────────────────────────────────────────────
lv_obj_t* ScreenBandMap::_screen    = nullptr;
lv_obj_t* ScreenBandMap::_statusLbl = nullptr;
lv_obj_t* ScreenBandMap::_pageLbl   = nullptr;
int8_t    ScreenBandMap::_page      = 0;

lv_obj_t* ScreenBandMap::_cells     [HC_NUM_BANDS][HC_NUM_CONTS] = {};
lv_obj_t* ScreenBandMap::_cellLabels[HC_NUM_BANDS][HC_NUM_CONTS] = {};

// ── Heat colours ──────────────────────────────────────────────────────
static lv_color_t heatBg(uint8_t n) {
    if (n == 0)  return dxs::theme::BG;
    if (n <= 2)  return lv_color_hex(0x0d3b2e);
    if (n <= 5)  return lv_color_hex(0x1a6b3a);
    if (n <= 10) return lv_color_hex(0x27ae60);
    if (n <= 20) return dxs::theme::ORANGE;
    return            dxs::theme::RED;
}
static lv_color_t heatFg(uint8_t n) {
    return (n == 0) ? dxs::theme::TEXT_MUTED : lv_color_white();
}

// ── show() ────────────────────────────────────────────────────────────
void ScreenBandMap::show() {
    if (!_screen) {
        _screen = lv_obj_create(nullptr);
        lv_obj_set_size(_screen, DXS_SCREEN_W, DXS_SCREEN_H);
        lv_obj_set_style_bg_color(_screen, theme::BG, 0);
        lv_obj_set_style_pad_all(_screen, 0, 0);
        lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
        // Gesture event bubbles from cells → grid → screen; register once here
        lv_obj_add_event_cb(_screen, _onGesture, LV_EVENT_GESTURE, nullptr);
        _buildTopBar(_screen);
        _buildGrid(_screen);
    }
    _updateCells();
    lv_scr_load(_screen);
    DXS_LOG("UI", "Band Map shown (page %d)", _page);
}

void ScreenBandMap::onNewData() {
    if (_screen && lv_scr_act() == _screen) _updateCells();
}

// ── _buildTopBar() ────────────────────────────────────────────────────
void ScreenBandMap::_buildTopBar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, DXS_SCREEN_W, TOP_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::BG_CARD, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_set_style_pad_ver(bar, 2, 0);
    lv_obj_set_style_pad_column(bar, 4, 0);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Home
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

    // Title
    lv_obj_t* title = lv_label_create(bar);
    lv_label_set_text(title, LV_SYMBOL_LIST "  Band Map");
    lv_obj_set_style_text_color(title, theme::TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    // Spacer
    lv_obj_t* sp = lv_obj_create(bar);
    lv_obj_set_size(sp, 1, 1);
    lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp, 0, 0);
    lv_obj_set_style_pad_all(sp, 0, 0);
    lv_obj_set_flex_grow(sp, 1);

    // Status label
    _statusLbl = lv_label_create(bar);
    lv_label_set_text(_statusLbl, "Last 60 min");
    lv_obj_set_style_text_color(_statusLbl, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(_statusLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_pad_right(_statusLbl, 2, 0);

    // Page toggle button
    lv_obj_t* pageBtn = lv_btn_create(bar);
    lv_group_remove_obj(pageBtn);
    lv_obj_set_height(pageBtn, TOP_H - 6);
    lv_obj_set_style_bg_color(pageBtn, theme::PRIMARY, 0);
    lv_obj_set_style_bg_color(pageBtn, theme::ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(pageBtn, 4, 0);
    lv_obj_set_style_border_width(pageBtn, 0, 0);
    lv_obj_set_style_shadow_width(pageBtn, 0, 0);
    lv_obj_set_style_pad_hor(pageBtn, 5, 0);
    lv_obj_add_event_cb(pageBtn, _onPageClick, LV_EVENT_CLICKED, nullptr);
    _pageLbl = lv_label_create(pageBtn);
    lv_label_set_text(_pageLbl, _page == 0 ? "VHF>" : "<HF");
    lv_obj_set_style_text_color(_pageLbl, theme::TEXT, 0);
    lv_obj_set_style_text_font(_pageLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(_pageLbl);

    // Refresh
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

// ── _buildGrid() ──────────────────────────────────────────────────────
void ScreenBandMap::_buildGrid(lv_obj_t* parent) {
    int bandStart = (_page == 0) ? HF_START  : VHF_START;
    int bandCount = (_page == 0) ? HF_COUNT  : VHF_COUNT;
    int celh      = (GRID_H - HEAD_H) / bandCount;
    int celw      = (DXS_SCREEN_W - BANDW) / HC_NUM_CONTS;  // ~46px

    lv_obj_t* grid = lv_obj_create(parent);
    lv_obj_set_size(grid, DXS_SCREEN_W, GRID_H);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, TOP_H);
    lv_obj_set_style_bg_color(grid, theme::BG, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_radius(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    // Header row
    {
        lv_obj_t* row = lv_obj_create(grid);
        lv_obj_set_size(row, DXS_SCREEN_W, HEAD_H);
        lv_obj_set_style_bg_color(row, theme::BG_CARD, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 1, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* blank = lv_label_create(row);
        lv_obj_set_width(blank, BANDW);
        lv_label_set_text(blank, "");

        for (int c = 0; c < HC_NUM_CONTS; c++) {
            lv_obj_t* h = lv_label_create(row);
            lv_obj_set_width(h, celw - 1);
            lv_label_set_text(h, HC_CONT_LABELS[c]);
            lv_label_set_long_mode(h, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(h, theme::ACCENT, 0);
            lv_obj_set_style_text_font(h, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
        }
    }

    // Data rows
    for (int bi = 0; bi < bandCount; bi++) {
        int globalIdx = bandStart + bi;

        lv_obj_t* row = lv_obj_create(grid);
        lv_obj_set_size(row, DXS_SCREEN_W, celh);
        lv_obj_set_style_bg_color(row, (bi & 1) ? theme::BG_CARD : theme::BG, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 1, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* bandLbl = lv_label_create(row);
        lv_obj_set_width(bandLbl, BANDW);
        lv_label_set_text(bandLbl, HC_BAND_LABELS[globalIdx]);
        lv_label_set_long_mode(bandLbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(bandLbl, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(bandLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_pad_left(bandLbl, 2, 0);

        for (int c = 0; c < HC_NUM_CONTS; c++) {
            lv_obj_t* cell = lv_obj_create(row);
            _cells     [globalIdx][c] = cell;
            lv_obj_set_size(cell, celw - 1, celh - 2);
            lv_obj_set_style_bg_color(cell, theme::BG, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_radius(cell, 2, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* lbl = lv_label_create(cell);
            _cellLabels[globalIdx][c] = lbl;
            lv_label_set_text(lbl, "");
            lv_obj_set_style_text_color(lbl, theme::TEXT_MUTED, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
            lv_obj_center(lbl);
        }
    }
}

// ── _updateCells() ────────────────────────────────────────────────────
void ScreenBandMap::_updateCells() {
    uint8_t hm[HC_NUM_BANDS][HC_NUM_CONTS];
    HolyClusterFeed::getHeatmap(hm);

    int bandStart = (_page == 0) ? HF_START  : VHF_START;
    int bandCount = (_page == 0) ? HF_COUNT  : VHF_COUNT;
    int total     = 0;

    for (int bi = 0; bi < bandCount; bi++) {
        int gi = bandStart + bi;
        for (int c = 0; c < HC_NUM_CONTS; c++) {
            uint8_t n = hm[gi][c];
            total += n;
            lv_obj_t* cell = _cells     [gi][c];
            lv_obj_t* lbl  = _cellLabels[gi][c];
            if (!cell || !lbl) continue;
            lv_obj_set_style_bg_color(cell, heatBg(n), 0);
            char buf[6];
            if (n == 0) strncpy(buf, "", sizeof(buf));
            else        snprintf(buf, sizeof(buf), "%u", (unsigned)n);
            lv_label_set_text(lbl, buf);
            lv_obj_set_style_text_color(lbl, heatFg(n), 0);
        }
    }

    if (_statusLbl) {
        int st = HolyClusterFeed::lastStatus();
        if (st < 0)       lv_label_set_text(_statusLbl, "Disconnected");
        else if (st == 0) lv_label_set_text(_statusLbl, "Connecting...");
        else if (total==0)lv_label_set_text(_statusLbl, "No activity");
        else              lv_label_set_text(_statusLbl, "Last 60 min");
    }
}

// ── Event handlers ────────────────────────────────────────────────────
bool ScreenBandMap::isActive() {
    return _screen && (lv_scr_act() == _screen);
}

// Public page switch — safe to call from UIScreen::tick() (not inside an event)
void ScreenBandMap::switchPage() {
    if (!_screen) return;
    _page = (_page == 0) ? 1 : 0;
    // Delete the old screen immediately (not async) since we're not inside
    // one of its event callbacks — we're called from the tick() loop.
    lv_obj_t* old = _screen;
    _screen    = nullptr;
    _statusLbl = nullptr;
    _pageLbl   = nullptr;
    memset(_cells,      0, sizeof(_cells));
    memset(_cellLabels, 0, sizeof(_cellLabels));
    show();          // builds and loads the new screen first
    lv_obj_del(old); // then delete the old one
}

void ScreenBandMap::_onHomeClick(lv_event_t*) { ScreenLauncher::show(); }

void ScreenBandMap::_onRefreshClick(lv_event_t*) {
    HolyClusterFeed::requestFetch();
    _updateCells();
}

// Page toggle via top-bar button
void ScreenBandMap::_onPageClick(lv_event_t*) {
    _page = (_page == 0) ? 1 : 0;
    lv_obj_t* old = _screen;
    _screen    = nullptr;
    _statusLbl = nullptr;
    _pageLbl   = nullptr;
    memset(_cells,      0, sizeof(_cells));
    memset(_cellLabels, 0, sizeof(_cellLabels));
    show();
    lv_obj_del_async(old);
}

// Left/right swipe gesture detected on screen (event bubbles from cells)
void ScreenBandMap::_onGesture(lv_event_t*) {
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    _page = (_page == 0) ? 1 : 0;
    lv_obj_t* old = _screen;
    _screen    = nullptr;
    _statusLbl = nullptr;
    _pageLbl   = nullptr;
    memset(_cells,      0, sizeof(_cells));
    memset(_cellLabels, 0, sizeof(_cellLabels));
    show();
    lv_obj_del_async(old);
}

}}  // namespace dxs::ui
