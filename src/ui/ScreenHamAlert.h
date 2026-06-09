// DXSpotter — ScreenHamAlert.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scrollable list of recent HamAlert.org spot alerts.

#pragma once
#include <lvgl.h>

namespace dxs { namespace ui {

class ScreenHamAlert {
public:
    static void show();

    // Called from UIScreen::tick() when HamAlertFeed::hasNewData() is true
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
    static void _onAddClick    (lv_event_t* e);
    static void _onDeleteClick (lv_event_t* e);
};

}}  // namespace dxs::ui
