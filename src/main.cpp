// DXSpotter — main entry point
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Initialises hardware, WiFi, data feeds, and the UI task loop.

#include <Arduino.h>
#include "version.h"
#include "hardware/Board.h"
#include "utils/Config.h"
#include "utils/Log.h"
#include "ui/UIScreen.h"
#include "net/WiFiMgr.h"
#include "net/DXFeed.h"
#include "net/HolyClusterFeed.h"
#include "net/DXpeditionFeed.h"
#include "net/DXWorldFeed.h"
#include "net/SolarFeed.h"
#include "net/HamAlertFeed.h"
#include "net/HamAlertApi.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    DXS_LOG("main", "DXSpotter v%s starting", DXS_VERSION_STRING);

    // 1) Board hardware (power rail, I2C, trackball ISRs)
    dxs::Board::instance().init();

    // 2) Load persistent config from NVS
    dxs::config::init();
    dxs::Board::instance().setAudioVolume(dxs::config::get().volume);

    // 3) Keyboard backlight starts off; UIScreen::tick() drives it from config
    //    (manual level, or auto night mode 21:00–07:00 with sleep gating).
    dxs::Board::instance().setKeyboardBacklight(0);

    // 4) Initialise UI (LVGL + TFT) — must be before any screen creation
    dxs::ui::init();

    // Boot chime: "CQ CQ DX" in Morse on a one-shot task. Safe again now that the
    // audio path is reworked (no per-beep task churn). Comment out to silence boot.
    dxs::Board::instance().playBootSound();

    // 5) Start WiFi (connects in background; NTP sync happens once connected)
    dxs::WiFiMgr::instance().init();

    // 6) Start data feed background tasks
    //    Each creates a FreeRTOS task that sleeps until a fetch is requested
    //    or their periodic timer fires. WiFi connectivity is checked internally.
    dxs::DXFeed::init();
    dxs::HolyClusterFeed::init();
    dxs::DXpeditionFeed::init();
    dxs::DXWorldFeed::init();
    dxs::SolarFeed::init();
    dxs::HamAlertFeed::init();
    dxs::HamAlertApi::init();   // HTTPS trigger management (list/add/delete)

    DXS_LOG("main", "Ready");
}

void loop() {
    dxs::Board::instance().tick();
    dxs::WiFiMgr::instance().tick();
    dxs::ui::tick();
}
