// DXSpotter — UIScreen.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Top-level UI controller. Owns the LVGL display driver and manages
// which screen is active. init()/tick() called from main.cpp.

#pragma once

namespace dxs { namespace ui {

void init();
void tick();

// Navigate to the main launcher from any screen.
void showLauncher();

// Force the keyboard-backlight auto/manual logic to re-apply on the next tick
// (call after a live brightness preview in Settings so the auto state reasserts).
void resetKbBacklight();

// Show a sample HamAlert popup — used by Settings to confirm the "Alert Popup"
// option works (there's otherwise no popup until a real alert arrives).
void showAlertPopupTest();

}}  // namespace dxs::ui
