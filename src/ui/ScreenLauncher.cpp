// DXSpotter — ScreenLauncher.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenLauncher.h"
#include "ScreenDXSpots.h"
#include "ScreenSolar.h"
#include "ScreenHamAlert.h"
#include "ScreenBandMap.h"
#include "ScreenSettings.h"
#include "ScreenDXpeditions.h"
#include "Theme.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include "../net/WiFiMgr.h"
#include "../net/DXFeed.h"
#include "../net/HolyClusterFeed.h"

#include <time.h>
#include <cstring>
#include <cstdio>

namespace dxs { namespace ui {

static constexpr int TOP_H  = 28;
static constexpr int BOT_H  = 24;
static constexpr int GRID_H = DXS_SCREEN_H - TOP_H - BOT_H;  // 188px

lv_obj_t* ScreenLauncher::_screen  = nullptr;
lv_obj_t* ScreenLauncher::_wifiLbl = nullptr;
lv_obj_t* ScreenLauncher::_timeLbl = nullptr;
lv_obj_t* ScreenLauncher::_battLbl = nullptr;
lv_obj_t* ScreenLauncher::_updLbl  = nullptr;
lv_obj_t* ScreenLauncher::_tiles[6] = {};
int8_t    ScreenLauncher::_selIdx   = 0;

struct TileDesc { const char* symbol; const char* label; };
static const TileDesc kTiles[6] = {
    { LV_SYMBOL_WIFI,     "DX Spots"     },
    { LV_SYMBOL_IMAGE,    "Solar"        },
    { LV_SYMBOL_CALL,     "HamAlert"     },
    { LV_SYMBOL_LIST,     "Band Map"     },
    { LV_SYMBOL_SETTINGS, "Settings"     },
    { LV_SYMBOL_EDIT,     "DXpeditions"  },
};

// ── show() ────────────────────────────────────────────────────────────
void ScreenLauncher::show() {
    if (!_screen) {
        _screen = lv_obj_create(nullptr);
        lv_obj_set_size(_screen, DXS_SCREEN_W, DXS_SCREEN_H);
        lv_obj_set_style_bg_color(_screen, theme::BG, 0);
        lv_obj_set_style_pad_all(_screen, 0, 0);
        lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

        _buildTopBar(_screen);
        _buildGrid(_screen);
        _buildBottomBar(_screen);
        refreshStatus();
    }

    lv_scr_load(_screen);
    _selIdx = 0;
    _updateHighlight();
    DXS_LOG("UI", "Launcher shown");
}

// ── _buildTopBar() ────────────────────────────────────────────────────
void ScreenLauncher::_buildTopBar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, DXS_SCREEN_W, TOP_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::BG_CARD, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 6, 0);
    lv_obj_set_style_pad_ver(bar, 2, 0);
    lv_obj_set_style_pad_column(bar, 4, 0);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Callsign
    lv_obj_t* csLbl = lv_label_create(bar);
    const char* cs = config::get().callsign;
    lv_label_set_text(csLbl, cs[0] ? cs : "DXSpotter");
    lv_obj_set_style_text_color(csLbl, theme::ACCENT, 0);
    lv_obj_set_style_text_font(csLbl, &lv_font_montserrat_12, 0);

    // Spacer
    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    // WiFi status
    _wifiLbl = lv_label_create(bar);
    lv_label_set_text(_wifiLbl, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(_wifiLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(_wifiLbl, theme::TEXT_MUTED, 0);
    lv_obj_set_style_pad_right(_wifiLbl, 4, 0);

    // Clock
    _timeLbl = lv_label_create(bar);
    lv_label_set_text(_timeLbl, "--:--");
    lv_obj_set_style_text_color(_timeLbl, theme::TEXT, 0);
    lv_obj_set_style_text_font(_timeLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_pad_right(_timeLbl, 2, 0);
}

// ── _buildGrid() — 3×2 layout ─────────────────────────────────────────
void ScreenLauncher::_buildGrid(lv_obj_t* parent) {
    static const lv_coord_t colDsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t rowDsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };

    lv_obj_t* grid = lv_obj_create(parent);
    lv_obj_set_size(grid, DXS_SCREEN_W, GRID_H);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, TOP_H);
    lv_obj_set_style_bg_color(grid, theme::BG, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_radius(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_style_pad_gap(grid, 4, 0);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, colDsc, rowDsc);

    for (int i = 0; i < 6; i++) {
        int col = i % 3;
        int row = i / 3;

        lv_obj_t* cell = lv_btn_create(grid);
        _tiles[i] = cell;
        lv_group_remove_obj(cell);
        lv_obj_set_style_bg_color(cell, theme::BG_CARD, 0);
        lv_obj_set_style_bg_color(cell, theme::PRIMARY, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(cell, theme::BG_CARD, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(cell, theme::ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_color(cell, theme::BORDER, 0);
        lv_obj_set_style_radius(cell, 8, 0);
        lv_obj_set_style_shadow_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 6, 0);
        lv_obj_set_grid_cell(cell,
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* icon = lv_label_create(cell);
        lv_label_set_text(icon, kTiles[i].symbol);
        lv_obj_set_style_text_color(icon, theme::ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);

        lv_obj_t* lbl = lv_label_create(cell);
        lv_label_set_text(lbl, kTiles[i].label);
        lv_obj_set_style_text_color(lbl, theme::TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);

        lv_obj_add_event_cb(cell, _onTileClick, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

// ── _buildBottomBar() ─────────────────────────────────────────────────
void ScreenLauncher::_buildBottomBar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, DXS_SCREEN_W, BOT_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::BG_CARD, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 6, 0);
    lv_obj_set_style_pad_ver(bar, 2, 0);
    lv_obj_set_style_pad_column(bar, 4, 0);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _updLbl = lv_label_create(bar);
    lv_label_set_text(_updLbl, "No data yet");
    lv_obj_set_style_text_color(_updLbl, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(_updLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    _battLbl = lv_label_create(bar);
    lv_label_set_text(_battLbl, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_set_style_text_color(_battLbl, theme::GREEN, 0);
    lv_obj_set_style_text_font(_battLbl, &lv_font_montserrat_10, 0);
}

// ── refreshStatus() ───────────────────────────────────────────────────
void ScreenLauncher::refreshStatus() {
    if (_wifiLbl) {
        bool connected = WiFiMgr::instance().isConnected();
        lv_obj_set_style_text_color(_wifiLbl, connected ? theme::GREEN : theme::TEXT_MUTED, 0);
    }

    if (_timeLbl) {
        time_t t = time(nullptr);
        t += config::get().timezoneOffsetHours * 3600L;
        if (t < 1700000000L) {
            lv_label_set_text(_timeLbl, "--:--");
        } else {
            struct tm lt;
            gmtime_r(&t, &lt);
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
            lv_label_set_text(_timeLbl, buf);
        }
    }

    if (_updLbl) {
        // Show last fetch time from whichever DX feed is active
        uint32_t fetchMs = (config::get().dxFeedSource == DX_FEED_HOLYCLUSTER)
            ? HolyClusterFeed::lastFetchMs()
            : DXFeed::lastFetchMs();
        if (fetchMs == 0) {
            lv_label_set_text(_updLbl, "No data yet");
        } else {
            uint32_t ageSec = (millis() - fetchMs) / 1000;
            char buf[32];
            if (ageSec < 120)
                snprintf(buf, sizeof(buf), "Updated %lus ago", (unsigned long)ageSec);
            else
                snprintf(buf, sizeof(buf), "Updated %lum ago", (unsigned long)(ageSec / 60));
            lv_label_set_text(_updLbl, buf);
        }
    }
}

// ── refreshBattery() ──────────────────────────────────────────────────
void ScreenLauncher::refreshBattery(int percent, bool charging) {
    if (!_battLbl) return;
    const char* sym;
    lv_color_t col;
    if (charging)         { sym = LV_SYMBOL_CHARGE;        col = theme::GREEN;  }
    else if (percent>=75) { sym = LV_SYMBOL_BATTERY_FULL;  col = theme::GREEN;  }
    else if (percent>=50) { sym = LV_SYMBOL_BATTERY_3;     col = theme::GREEN;  }
    else if (percent>=25) { sym = LV_SYMBOL_BATTERY_2;     col = theme::ORANGE; }
    else if (percent>= 5) { sym = LV_SYMBOL_BATTERY_1;     col = theme::ORANGE; }
    else                  { sym = LV_SYMBOL_BATTERY_EMPTY; col = theme::RED;    }
    char buf[20];
    snprintf(buf, sizeof(buf), "%s %d%%", sym, percent);
    lv_label_set_text(_battLbl, buf);
    lv_obj_set_style_text_color(_battLbl, col, 0);
}

// ── Trackball navigation — 3×2 grid ──────────────────────────────────
bool ScreenLauncher::isActive() {
    return _screen && (lv_scr_act() == _screen);
}

void ScreenLauncher::_updateHighlight() {
    for (int i = 0; i < 6; i++) {
        if (!_tiles[i]) continue;
        if (i == _selIdx)
            lv_obj_add_state(_tiles[i], LV_STATE_FOCUSED);
        else
            lv_obj_clear_state(_tiles[i], LV_STATE_FOCUSED);
    }
}

void ScreenLauncher::navigate(int dx, int dy) {
    if (!_screen) return;
    // Layout:  0 1 2
    //          3 4 5
    int col = _selIdx % 3;
    int row = _selIdx / 3;
    col = (col + dx + 3) % 3;
    if (dy > 0 && row < 1) row++;
    else if (dy < 0 && row > 0) row--;
    _selIdx = (int8_t)(row * 3 + col);
    _updateHighlight();
}

void ScreenLauncher::confirmSelect() {
    if (!_screen || _selIdx < 0 || _selIdx > 5) return;
    if (_tiles[_selIdx]) lv_event_send(_tiles[_selIdx], LV_EVENT_CLICKED, nullptr);
}

// ── _onTileClick() ────────────────────────────────────────────────────
void ScreenLauncher::_onTileClick(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
        case 0: ScreenDXSpots::show();     break;
        case 1: ScreenSolar::show();       break;
        case 2: ScreenHamAlert::show();    break;
        case 3: ScreenBandMap::show();     break;
        case 4: ScreenSettings::show();    break;
        case 5: ScreenDXpeditions::show(); break;
    }
}

}}  // namespace dxs::ui
