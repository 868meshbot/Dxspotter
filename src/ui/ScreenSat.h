// DXSpotter — ScreenSat.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scrollable list of upcoming amateur-satellite passes from hams.at (SatFeed).

#pragma once
#include <lvgl.h>

namespace dxs { namespace ui {

class ScreenSat {
public:
    static void show();

    // Called from UIScreen::tick() when SatFeed::hasNewData() is true.
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
