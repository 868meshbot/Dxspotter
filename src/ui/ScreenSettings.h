// DXSpotter — ScreenSettings.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings screen — configures callsign, WiFi, DX feed, and HamAlert.
//
// Layout:
//  ┌────────────────────────────────────┐
//  │ [⌂]     Settings                  │  top bar
//  ├────────────────────────────────────┤
//  │ Callsign           N0CALL      >  │
//  │ WiFi SSID          MyNetwork   >  │
//  │ WiFi Password      ********    >  │
//  │ DX Feed            DXwatch     >  │
//  │ DX Feed URL        (custom)    >  │
//  │ HamAlert User      user        >  │
//  │ HamAlert Key       ********    >  │
//  │ Brightness         80%         >  │
//  │ Colour Theme       Dark        >  │
//  │ Timezone           UTC+0       >  │
//  └────────────────────────────────────┘

#pragma once
#include <lvgl.h>

namespace dxs { namespace ui {

class ScreenSettings {
public:
    static void show();

public:
    static void _onItemClick(lv_event_t* e);  // public for file-scope helper

private:
    static lv_obj_t* _screen;
    static lv_obj_t* _list;

    static void _buildTopBar(lv_obj_t* parent);
    static void _buildList  (lv_obj_t* parent);
    static void _refreshList();

    static void _onHomeClick(lv_event_t* e);
};

}}  // namespace dxs::ui
