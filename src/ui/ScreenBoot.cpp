// DXSpotter — ScreenBoot.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenBoot.h"
#include "ScreenLauncher.h"
#include "Theme.h"
#include "../version.h"
#include <cstdio>

namespace dxs { namespace ui {

lv_obj_t* ScreenBoot::_screen = nullptr;

void ScreenBoot::show() {
    if (_screen) { lv_obj_del(_screen); _screen = nullptr; }

    _screen = lv_obj_create(nullptr);
    lv_obj_set_size(_screen, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(_screen, theme::BG, 0);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* title = lv_label_create(_screen);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  DXSpotter");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, theme::ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -24);

    // Subtitle
    lv_obj_t* sub = lv_label_create(_screen);
    lv_label_set_text(sub, "DX Spots  •  Solar  •  HamAlert  •  Band Map");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sub, theme::TEXT_MUTED, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 2);

    // Version
    char ver[32];
    snprintf(ver, sizeof(ver), "v%s", DXS_VERSION_STRING);
    lv_obj_t* verLbl = lv_label_create(_screen);
    lv_label_set_text(verLbl, ver);
    lv_obj_set_style_text_font(verLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(verLbl, theme::TEXT_MUTED, 0);
    lv_obj_align(verLbl, LV_ALIGN_BOTTOM_MID, 0, -8);

    // Loading spinner
    lv_obj_t* spin = lv_spinner_create(_screen, 1000, 60);
    lv_obj_set_size(spin, 36, 36);
    lv_obj_align(spin, LV_ALIGN_CENTER, 0, 36);
    lv_obj_set_style_arc_color(spin, theme::ACCENT, LV_PART_INDICATOR);

    lv_scr_load(_screen);
    lv_timer_t* t = lv_timer_create(_onTimerDone, 2500, nullptr);
    lv_timer_set_repeat_count(t, 1);
}

void ScreenBoot::_onTimerDone(lv_timer_t*) {
    ScreenLauncher::show();
    if (_screen) { lv_obj_del(_screen); _screen = nullptr; }
}

}}  // namespace dxs::ui
