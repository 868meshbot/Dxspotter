// DXSpotter — ScreenDXpeditions.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scrollable list of active/upcoming DXpeditions from Holy Cluster.
//
// Layout (320×240):
//  ┌──────────────────────────────────────────────┐  y=0
//  │  [⌂]  DXpeditions               [↺]         │  top bar 28px
//  ├──────────────────────────────────────────────┤  y=28
//  │ VK9LA   Lord Howe Is.    Jun 01 – Jun 15     │  row 30px
//  │ T32X    Kiribati         Jun 05 – Jun 20  ●  │  ● = active
//  │  ...                                         │  scrollable
//  └──────────────────────────────────────────────┘

#pragma once
#include <lvgl.h>

namespace dxs { namespace ui {

class ScreenDXpeditions {
public:
    static void show();

    // Called from UIScreen::tick() when DXpeditionFeed::hasNewData() is true
    static void onNewData();

private:
    static lv_obj_t* _screen;
    static lv_obj_t* _list;
    static lv_obj_t* _statusLbl;
    static lv_obj_t* _titleLbl;
    static lv_obj_t* _pageLbl;
    static int8_t    _page;       // 0 = Holy Cluster, 1 = DX-World timeline

    static void _buildTopBar(lv_obj_t* parent);
    static void _buildList  (lv_obj_t* parent);
    static void _rebuildList();
    static void _fetchCurrent();          // request a fetch for the active page
    static void _applyPage  (void* unused);  // update labels + rebuild (async-safe)

    static void _onHomeClick   (lv_event_t* e);
    static void _onRefreshClick(lv_event_t* e);
    static void _onPageClick   (lv_event_t* e);  // top-bar page toggle
    static void _onGesture     (lv_event_t* e);  // left/right swipe → page flip
    static void _onRowClick    (lv_event_t* e);  // DX-World row tap → details/add popup
};

}}  // namespace dxs::ui
