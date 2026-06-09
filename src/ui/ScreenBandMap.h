// DXSpotter — ScreenBandMap.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two-page band×continent activity heatmap from Holy Cluster spot stream.
//   Page 0 (HF):     160m–10m  (10 bands)
//   Page 1 (VHF/UHF): 6m–23cm  (6 bands)

#pragma once
#include <lvgl.h>
#include "../net/HolyClusterFeed.h"

namespace dxs { namespace ui {

class ScreenBandMap {
public:
    static void show();
    static void onNewData();   // called from UIScreen::tick()
    static bool isActive();
    static void switchPage();  // flip HF ↔ VHF/UHF; safe to call from outside event

private:
    static lv_obj_t* _screen;
    static lv_obj_t* _cells     [HC_NUM_BANDS][HC_NUM_CONTS];
    static lv_obj_t* _cellLabels[HC_NUM_BANDS][HC_NUM_CONTS];
    static lv_obj_t* _statusLbl;
    static lv_obj_t* _pageLbl;
    static int8_t    _page;   // 0 = HF, 1 = VHF/UHF

    static void _buildTopBar(lv_obj_t* parent);
    static void _buildGrid  (lv_obj_t* parent);
    static void _updateCells();

    static void _onHomeClick   (lv_event_t* e);
    static void _onRefreshClick(lv_event_t* e);
    static void _onPageClick   (lv_event_t* e);
    static void _onGesture     (lv_event_t* e);   // left/right swipe → page flip
};

}}  // namespace dxs::ui
