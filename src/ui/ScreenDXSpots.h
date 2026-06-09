// DXSpotter — ScreenDXSpots.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scrollable DX spot list.
//
// Layout (320×240):
//  ┌──────────────────────────────────────────┐  y=0
//  │ [⌂]  DX Spots            [↺ Refresh]    │  top bar 28px
//  ├──────────────────────────────────────────┤  y=28
//  │  14.007  | VK2GR    | G3LDI   | CW 1234 │  row 22px
//  │  21.025  | JA1ABC   | OH2BH   | SSB 1235│
//  │   ...                                    │  scrollable
//  └──────────────────────────────────────────┘

#pragma once
#include <lvgl.h>

namespace dxs { namespace ui {

class ScreenDXSpots {
public:
    static void show();

    // Called from UIScreen::tick() when DXFeed::hasNewData() is true
    static void onNewData();

private:
    static lv_obj_t* _screen;
    static lv_obj_t* _list;
    static lv_obj_t* _statusLbl;

    static void _buildTopBar   (lv_obj_t* parent);
    static void _buildList     (lv_obj_t* parent);
    static void _buildBottomBar(lv_obj_t* parent);
    static void _rebuildList   ();

    static void _onHomeClick   (lv_event_t* e);
    static void _onRefreshClick(lv_event_t* e);
};

}}  // namespace dxs::ui
