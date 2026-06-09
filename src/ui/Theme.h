// DXSpotter — Theme.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>
#include <stdint.h>

namespace dxs { namespace theme {

extern lv_color_t BG;
extern lv_color_t BG_CARD;
extern lv_color_t TEXT;
extern lv_color_t TEXT_MUTED;
extern lv_color_t ACCENT;
extern lv_color_t PRIMARY;
extern lv_color_t GREEN;
extern lv_color_t RED;
extern lv_color_t ORANGE;
extern lv_color_t BORDER;

static constexpr int THEME_COUNT = 3;
void applyTheme(uint8_t choice);
void apply(lv_disp_t* disp);

// Colour helpers for propagation condition strings
lv_color_t condColor(const char* cond);  // "Good"→GREEN "Fair"→ORANGE "Poor"→RED

}}  // namespace dxs::theme
