// DXSpotter — ScreenBoot.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Splash screen on power-up; auto-transitions to launcher after 2.5 s.

#pragma once
#include <lvgl.h>

namespace dxs { namespace ui {

class ScreenBoot {
public:
    static void show();

private:
    static lv_obj_t* _screen;
    static void _onTimerDone(lv_timer_t* t);
};

}}  // namespace dxs::ui
