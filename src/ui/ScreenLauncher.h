// DXSpotter — ScreenLauncher.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 3×2 main menu grid:
//
//  ┌──────────────────────────────────────────────┐  y=0
//  │  N0CALL         WiFi ▲       ⌚ 12:34        │  top bar 28px
//  ├──────────────────────────────────────────────┤  y=28
//  │              │              │                 │
//  │  📡 DX Spots │  ☀ Solar    │  🔔 HamAlert    │
//  │              │              │                 │
//  ├──────────────┼──────────────┼─────────────────┤
//  │              │              │                 │
//  │  ≡ Band Map  │  ⚙ Settings  │  ✎ DXpeditions  │
//  │              │              │                 │
//  ├──────────────┴──────────────┴─────────────────┤  y=216
//  │  Updated 12:34                    🔋 87%      │  bottom bar 24px
//  └──────────────────────────────────────────────┘  y=240

#pragma once
#include <lvgl.h>

namespace dxs { namespace ui {

class ScreenLauncher {
public:
    static void show();

    // Trackball navigation across the 3×2 grid
    static void navigate(int dx, int dy);
    static void confirmSelect();

    static bool isActive();

    // Called from UIScreen::tick()
    static void refreshStatus();
    static void refreshBattery(int percent, bool charging);

private:
    static lv_obj_t* _screen;
    static lv_obj_t* _wifiLbl;
    static lv_obj_t* _timeLbl;
    static lv_obj_t* _battLbl;
    static lv_obj_t* _updLbl;
    static lv_obj_t* _tiles[7];   // 0=DXSpots 1=Solar 2=HamAlert 3=BandMap 4=Settings 5=DXpeditions 6=Satellites
    static int8_t    _selIdx;      // 0-6

    static void _buildTopBar   (lv_obj_t* parent);
    static void _buildGrid     (lv_obj_t* parent);
    static void _buildBottomBar(lv_obj_t* parent);
    static void _updateHighlight();
    static void _onTileClick   (lv_event_t* e);
};

}}  // namespace dxs::ui
