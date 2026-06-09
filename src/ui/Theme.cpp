// DXSpotter — Theme.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Theme.h"
#include <cstring>

namespace dxs { namespace theme {

lv_color_t BG         = LV_COLOR_MAKE(13,  17,  23);
lv_color_t BG_CARD    = LV_COLOR_MAKE(22,  27,  34);
lv_color_t TEXT       = LV_COLOR_MAKE(230, 237, 243);
lv_color_t TEXT_MUTED = LV_COLOR_MAKE(139, 148, 158);
lv_color_t ACCENT     = LV_COLOR_MAKE(88,  166, 255);
lv_color_t PRIMARY    = LV_COLOR_MAKE(0,   51,  170);
lv_color_t GREEN      = LV_COLOR_MAKE(63,  185, 80);
lv_color_t RED        = LV_COLOR_MAKE(248, 81,  73);
lv_color_t ORANGE     = LV_COLOR_MAKE(210, 153, 34);
lv_color_t BORDER     = LV_COLOR_MAKE(48,  54,  61);

void applyTheme(uint8_t choice) {
    switch (choice) {
    case 1:  // Green
        BG         = LV_COLOR_MAKE(10,  20,  10);
        BG_CARD    = LV_COLOR_MAKE(20,  35,  20);
        TEXT       = LV_COLOR_MAKE(220, 255, 220);
        TEXT_MUTED = LV_COLOR_MAKE(120, 170, 120);
        ACCENT     = LV_COLOR_MAKE(80,  220, 80);
        PRIMARY    = LV_COLOR_MAKE(0,   80,  30);
        GREEN      = LV_COLOR_MAKE(80,  220, 80);
        RED        = LV_COLOR_MAKE(248, 81,  73);
        ORANGE     = LV_COLOR_MAKE(210, 153, 34);
        BORDER     = LV_COLOR_MAKE(40,  70,  40);
        break;
    case 2:  // Dracula
        BG         = LV_COLOR_MAKE(40,  42,  54);
        BG_CARD    = LV_COLOR_MAKE(68,  71,  90);
        TEXT       = LV_COLOR_MAKE(248, 248, 242);
        TEXT_MUTED = LV_COLOR_MAKE(98,  114, 164);
        ACCENT     = LV_COLOR_MAKE(189, 147, 249);
        PRIMARY    = LV_COLOR_MAKE(68,  71,  90);
        GREEN      = LV_COLOR_MAKE(80,  250, 123);
        RED        = LV_COLOR_MAKE(255, 85,  85);
        ORANGE     = LV_COLOR_MAKE(255, 184, 108);
        BORDER     = LV_COLOR_MAKE(98,  114, 164);
        break;
    default:  // 0 = Dark (GitHub-inspired)
        BG         = LV_COLOR_MAKE(13,  17,  23);
        BG_CARD    = LV_COLOR_MAKE(22,  27,  34);
        TEXT       = LV_COLOR_MAKE(230, 237, 243);
        TEXT_MUTED = LV_COLOR_MAKE(139, 148, 158);
        ACCENT     = LV_COLOR_MAKE(88,  166, 255);
        PRIMARY    = LV_COLOR_MAKE(0,   51,  170);
        GREEN      = LV_COLOR_MAKE(63,  185, 80);
        RED        = LV_COLOR_MAKE(248, 81,  73);
        ORANGE     = LV_COLOR_MAKE(210, 153, 34);
        BORDER     = LV_COLOR_MAKE(48,  54,  61);
        break;
    }
}

void apply(lv_disp_t* disp) {
    lv_theme_t* th = lv_theme_default_init(disp,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_BLUE),
        true,   // dark mode
        &lv_font_montserrat_14);
    lv_disp_set_theme(disp, th);
}

lv_color_t condColor(const char* cond) {
    if (!cond || cond[0] == '\0') return TEXT_MUTED;
    if (strncmp(cond, "Good", 4) == 0) return GREEN;
    if (strncmp(cond, "Fair", 4) == 0) return ORANGE;
    if (strncmp(cond, "Poor", 4) == 0) return RED;
    return TEXT_MUTED;
}

}}  // namespace dxs::theme
