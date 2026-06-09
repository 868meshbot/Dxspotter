// DXSpotter — ScreenSolar.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Solar weather display: SFI, A/K indices, band conditions.

#pragma once
#include <lvgl.h>

namespace dxs { namespace ui {

class ScreenSolar {
public:
    static void show();

    // Called from UIScreen::tick() when SolarFeed::hasNewData() is true
    static void onNewData();

private:
    static lv_obj_t* _screen;

    static void _buildTopBar(lv_obj_t* parent);
    static void _buildBody  (lv_obj_t* parent);
    static void _refresh    ();

    static void _onHomeClick   (lv_event_t* e);
    static void _onRefreshClick(lv_event_t* e);

    // Individual value labels updated by _refresh()
    static lv_obj_t* _sfiLbl;
    static lv_obj_t* _aLbl;
    static lv_obj_t* _kLbl;
    static lv_obj_t* _xrayLbl;
    static lv_obj_t* _windLbl;
    static lv_obj_t* _updLbl;

    // Band condition labels: [band][0=day,1=night]
    static lv_obj_t* _bandLbl[4][2];
};

}}  // namespace dxs::ui
