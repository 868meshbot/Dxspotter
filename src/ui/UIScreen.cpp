// DXSpotter — UIScreen.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Initialises LVGL 8, the TFT_eSPI display driver, GT911 touch, and
// the trackball encoder input device. Drives the screensaver and
// routes keyboard/trackball input to the active screen.

#include "UIScreen.h"
#include "ScreenBoot.h"
#include "ScreenLauncher.h"
#include "ScreenDXSpots.h"
#include "ScreenBandMap.h"
#include "ScreenDXpeditions.h"
#include "ScreenHamAlert.h"
#include "ScreenSolar.h"
#include "Theme.h"
#include "../hardware/Board.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include "../net/WiFiMgr.h"
#include "../net/DXFeed.h"
#include "../net/HolyClusterFeed.h"
#include "../net/SolarFeed.h"
#include "../net/HamAlertFeed.h"
#include "../net/DXpeditionFeed.h"
#include "../net/DXWorldFeed.h"

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <time.h>
#include <cstring>

static TFT_eSPI tft = TFT_eSPI();

// LVGL display driver — double-buffered 10-line strips in DMA SRAM
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static lv_disp_t*         s_disp  = nullptr;
static lv_color_t*        s_buf1  = nullptr;
static lv_color_t*        s_buf2  = nullptr;

static constexpr uint32_t BUF_PIXELS = DXS_SCREEN_W * 10;

// LVGL encoder indev (trackball)
static lv_indev_drv_t s_indev_drv;
static lv_indev_t*    s_indev = nullptr;
static int16_t        enc_diff = 0;

// LVGL touch indev (GT911 capacitive)
static lv_indev_drv_t s_touch_drv;
static lv_indev_t*    s_touch   = nullptr;
static uint8_t        s_gt911   = 0x5D;
static bool           s_touchOk = false;

// Screensaver REMOVED (it never activates). These two flags are kept permanently
// false so the scattered `if (s_screensaverActive || s_screenOff)` guards still
// compile and simply always take the awake path. Do not re-add an activation
// trigger — the screensaver path was a recurring lockup source.
static uint32_t  s_lastActivityMs    = 0;   // still updated by input; no longer drives sleep
static const bool s_screensaverActive = false;
static const bool s_screenOff         = false;
static uint8_t   s_kbApplied         = 0xFF;  // last KB backlight written (0xFF = force apply)

// ── GT911 probe ───────────────────────────────────────────────────────
static void gt911_probe() {
    for (uint8_t addr : {(uint8_t)0x5D, (uint8_t)0x14}) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission(true) != 0) continue;
        Wire.beginTransmission(addr);
        Wire.write(0x81); Wire.write(0x40);
        Wire.endTransmission(true);
        char pid[5] = {};
        if (Wire.requestFrom(addr, (uint8_t)4) == 4) {
            for (int i = 0; i < 4; i++) pid[i] = Wire.read();
        }
        while (Wire.available()) Wire.read();
        s_gt911   = addr;
        s_touchOk = true;
        DXS_LOG("Touch", "GT911 at 0x%02X id=\"%s\"", addr, pid);
        return;
    }
    DXS_LOG("Touch", "GT911 not found — touch disabled");
}

// ── LVGL callbacks ────────────────────────────────────────────────────
static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixels((uint16_t*)color_p, w * h);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

static void encoder_read(lv_indev_drv_t*, lv_indev_data_t* data) {
    data->enc_diff = enc_diff;
    data->state    = LV_INDEV_STATE_RELEASED;
    enc_diff = 0;
}

static void touch_read(lv_indev_drv_t*, lv_indev_data_t* data) {
    static lv_coord_t last_x = 0, last_y = 0;
    if (!s_touchOk) {
        data->point.x = last_x; data->point.y = last_y;
        data->state   = LV_INDEV_STATE_RELEASED;
        return;
    }

    Wire.beginTransmission(s_gt911);
    Wire.write(0x81); Wire.write(0x4E);
    if (Wire.endTransmission(true) != 0) {
        data->point.x = last_x; data->point.y = last_y;
        data->state   = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (Wire.requestFrom(s_gt911, (uint8_t)1) < 1 || !Wire.available()) {
        data->point.x = last_x; data->point.y = last_y;
        data->state   = LV_INDEV_STATE_RELEASED;
        return;
    }
    uint8_t status = Wire.read();
    Wire.beginTransmission(s_gt911);
    Wire.write(0x81); Wire.write(0x4E); Wire.write((uint8_t)0);
    Wire.endTransmission(true);

    if (!(status & 0x80) || (status & 0x0F) == 0) {
        data->point.x = last_x; data->point.y = last_y;
        data->state   = LV_INDEV_STATE_RELEASED;
        return;
    }

    Wire.beginTransmission(s_gt911);
    Wire.write(0x81); Wire.write(0x4F);
    Wire.endTransmission(true);
    if (Wire.requestFrom(s_gt911, (uint8_t)7) >= 7 && Wire.available() >= 7) {
        Wire.read();
        uint8_t xL = Wire.read(), xH = Wire.read();
        uint8_t yL = Wire.read(), yH = Wire.read();
        Wire.read(); Wire.read();
        while (Wire.available()) Wire.read();
        lv_coord_t px = (lv_coord_t)(((uint16_t)(xH & 0x0F) << 8) | xL);
        lv_coord_t py = (lv_coord_t)(((uint16_t)(yH & 0x0F) << 8) | yL);
        last_x = py;                           // portrait-Y → landscape-X
        last_y = (lv_coord_t)(DXS_SCREEN_H - 1 - px);  // portrait-X → landscape-Y (mirrored)
        if (last_x < 0)              last_x = 0;
        if (last_x >= DXS_SCREEN_W)  last_x = DXS_SCREEN_W - 1;
        if (last_y < 0)              last_y = 0;
        if (last_y >= DXS_SCREEN_H)  last_y = DXS_SCREEN_H - 1;
    }
    data->point.x = last_x;
    data->point.y = last_y;
    if (s_screensaverActive) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->state     = LV_INDEV_STATE_PRESSED;
    s_lastActivityMs = millis();
}

// ── Screensaver: REMOVED ──────────────────────────────────────────────
// _activateSS/_deactivateSS/_ssUpdateTime and the dim/screen-off behaviour were
// deleted. The display now stays at the configured brightness at all times.

namespace dxs { namespace ui {

// ── HamAlert notification popup ───────────────────────────────────────
// Floats on the display's top layer so it appears over whichever screen is
// active. Auto-dismisses after a timeout; also closable via its button.
static lv_obj_t*   s_haPopup      = nullptr;
static lv_timer_t* s_haPopupTimer = nullptr;

static void _haPopupTeardown(bool fromTimer) {
    if (!fromTimer && s_haPopupTimer) lv_timer_del(s_haPopupTimer);
    s_haPopupTimer = nullptr;                       // one-shot timer auto-deletes itself
    if (s_haPopup) { lv_obj_del_async(s_haPopup); s_haPopup = nullptr; }
}

static void _haPopupCloseEvt(lv_event_t*) { _haPopupTeardown(false); }
static void _haPopupTimerCb(lv_timer_t*)  { _haPopupTeardown(true);  }

// Called only from tick() (non-event context, never while the screensaver is up),
// so immediate lv_obj_del of any prior popup is safe here; the close *button*
// path uses del_async. Built on lv_scr_act() to match every other popup in the
// app — lv_layer_top() is untested in this LVGL build and locked the device up.
static void _showHaPopup(const dxs::HamAlert& a) {
    using namespace dxs::theme;

    if (s_haPopupTimer) { lv_timer_del(s_haPopupTimer); s_haPopupTimer = nullptr; }
    if (s_haPopup)      { lv_obj_del(s_haPopup);        s_haPopup = nullptr; }

    lv_obj_t* bg = lv_obj_create(lv_scr_act());
    s_haPopup = bg;
    lv_obj_set_size(bg, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);     // absorb taps on the dim area

    lv_obj_t* card = lv_obj_create(bg);
    lv_obj_set_size(card, 288, 188);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, BG, 0);
    lv_obj_set_style_border_color(card, ACCENT, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 5, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* hdr = lv_label_create(card);
    lv_label_set_text(hdr, LV_SYMBOL_CALL "  HamAlert");
    lv_obj_set_style_text_color(hdr, ACCENT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);

    lv_obj_t* call = lv_label_create(card);
    lv_label_set_text(call, a.dxCall[0] ? a.dxCall : "?");
    lv_obj_set_style_text_color(call, TEXT, 0);
    lv_obj_set_style_text_font(call, &lv_font_montserrat_16, 0);

    char line[48];
    snprintf(line, sizeof(line), "%s MHz   %s", a.freq, a.mode[0] ? a.mode : "");
    lv_obj_t* fl = lv_label_create(card);
    lv_label_set_text(fl, line);
    lv_obj_set_style_text_color(fl, GREEN, 0);
    lv_obj_set_style_text_font(fl, &lv_font_montserrat_14, 0);

    auto mkField = [&](const char* label, const char* value) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s %s", label, (value && value[0]) ? value : "-");
        lv_obj_t* l = lv_label_create(card);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_text(l, buf);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(l, TEXT_MUTED, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    };
    mkField("Spotter:", a.spotter);
    mkField("Comment:", a.comment);

    lv_obj_t* closeBtn = lv_btn_create(card);
    lv_group_remove_obj(closeBtn);
    lv_obj_set_width(closeBtn, lv_pct(100));
    lv_obj_set_height(closeBtn, 28);
    lv_obj_set_style_bg_color(closeBtn, PRIMARY, 0);
    lv_obj_set_style_bg_color(closeBtn, ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(closeBtn, 4, 0);
    lv_obj_set_style_border_width(closeBtn, 0, 0);
    lv_obj_set_style_shadow_width(closeBtn, 0, 0);
    lv_obj_add_event_cb(closeBtn, _haPopupCloseEvt, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cl = lv_label_create(closeBtn);
    lv_label_set_text(cl, LV_SYMBOL_CLOSE "  Dismiss");
    lv_obj_set_style_text_color(cl, TEXT, 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_12, 0);
    lv_obj_center(cl);

    s_haPopupTimer = lv_timer_create(_haPopupTimerCb, 12000, nullptr);
    lv_timer_set_repeat_count(s_haPopupTimer, 1);   // one-shot auto-dismiss
}

// Beep and/or pop up details for a newly arrived HamAlert, per user settings.
// Never touches the screensaver or builds UI while it's asleep — that locked up
// the loop task. If the screensaver is up, the popup is deferred (see tick()).
static void _notifyHamAlert(const dxs::HamAlert& a) {
    const auto& cfg = config::get();
    if (cfg.haNotifyBeep) Board::instance().beepNotify();
    // Only build the popup while a normal screen is active. While the screensaver
    // is up we just beep — creating UI (or waking the screensaver) from the notify
    // path locked up the loop task.
    if (cfg.haNotifyPopup && !s_screensaverActive && !s_screenOff) {
        _showHaPopup(a);
    }
}

void init() {
    DXS_LOG("UI", "Initialising display");

    tft.begin();
    tft.setRotation(1);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);

    Board::instance().initBacklightPWM();
    Board::instance().setDisplayBrightness(0);

    lv_init();

    s_buf1 = (lv_color_t*)heap_caps_malloc(BUF_PIXELS * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_buf2 = (lv_color_t*)heap_caps_malloc(BUF_PIXELS * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s_buf1) s_buf1 = (lv_color_t*)ps_malloc(BUF_PIXELS * sizeof(lv_color_t));
    if (!s_buf2) s_buf2 = (lv_color_t*)ps_malloc(BUF_PIXELS * sizeof(lv_color_t));

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, BUF_PIXELS);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = DXS_SCREEN_W;
    s_disp_drv.ver_res  = DXS_SCREEN_H;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp = lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_ENCODER;
    s_indev_drv.read_cb = encoder_read;
    s_indev = lv_indev_drv_register(&s_indev_drv);

    lv_indev_drv_init(&s_touch_drv);
    s_touch_drv.type         = LV_INDEV_TYPE_POINTER;
    s_touch_drv.read_cb      = touch_read;
    s_touch_drv.gesture_limit    = 50;   // 50px minimum swipe distance
    s_touch_drv.gesture_min_velocity = 3;   // minimum velocity for gesture recognition
    s_touch = lv_indev_drv_register(&s_touch_drv);

    lv_group_t* g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(s_indev, g);

    theme::applyTheme((uint8_t)config::get().theme);
    theme::apply(s_disp);
    gt911_probe();

    ScreenBoot::show();
    lv_refr_now(nullptr);
    Board::instance().setDisplayBrightness((uint8_t)config::get().brightness);

    s_lastActivityMs = millis();
    DXS_LOG("UI", "Display ready (%dx%d)", DXS_SCREEN_W, DXS_SCREEN_H);
}

void tick() {
    auto& board  = Board::instance();
    uint32_t now = millis();

    int16_t dx, dy;
    board.consumeTrackballDelta(dx, dy);
    bool tbPress = board.consumeTrackballPress();

    if (dx || dy || tbPress) s_lastActivityMs = now;

    if (ScreenLauncher::isActive()) {
        int ndx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
        int ndy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
        if (ndx || ndy) ScreenLauncher::navigate(ndx, ndy);
        if (tbPress)    ScreenLauncher::confirmSelect();
    } else {
        if (dy != 0 || dx != 0) {
            enc_diff = (dy != 0) ? ((dy > 0) ? 1 : -1)
                                 : ((dx > 0) ? 1 : -1);
        }
        if (tbPress) {
            lv_group_t* pg = lv_group_get_default();
            lv_obj_t*   pf = pg ? lv_group_get_focused(pg) : nullptr;
            if (pf) {
                uint32_t ek = LV_KEY_ENTER;
                lv_event_send(pf, LV_EVENT_KEY, &ek);
                if (!lv_obj_is_editable(pf))
                    lv_event_send(pf, LV_EVENT_CLICKED, nullptr);
            }
        }
    }

    // BBQ10 keyboard input
    static uint32_t lastKbPoll = 0;
    if (now - lastKbPoll >= 50UL) {
        lastKbPoll = now;
        char k = 0;
        if (board.pollKeyboard(k) && !s_screensaverActive) {
            s_lastActivityMs = now;
            lv_group_t* grp = lv_group_get_default();
            lv_obj_t*   foc = grp ? lv_group_get_focused(grp) : nullptr;
            if (foc) {
                if (k == '\r' || k == '\n') {
                    if (ScreenLauncher::isActive()) {
                        ScreenLauncher::confirmSelect();
                    } else {
                        uint32_t ek = LV_KEY_ENTER;
                        lv_event_send(foc, LV_EVENT_KEY, &ek);
                        if (!lv_obj_is_editable(foc))
                            lv_event_send(foc, LV_EVENT_CLICKED, nullptr);
                    }
                } else if (k == 8 || k == 127) {
                    uint32_t bk = LV_KEY_BACKSPACE;
                    lv_event_send(foc, LV_EVENT_KEY, &bk);
                } else if (k == 27) {
                    uint32_t ek = LV_KEY_ESC;
                    lv_event_send(foc, LV_EVENT_KEY, &ek);
                } else {
                    uint32_t ck = (uint32_t)(uint8_t)k;
                    lv_event_send(foc, LV_EVENT_KEY, &ck);
                }
            }
        }
    }

    // Screensaver removed — the display never times out, dims, or sleeps.

    // Keyboard backlight: auto night mode (on 21:00–07:00 local, off in sleep),
    // or a fixed manual level. Only writes I2C when the target changes.
    {
        const auto& cfg = config::get();
        uint8_t desired;
        if (s_screensaverActive || s_screenOff) {
            desired = 0;                                   // low-power: KB off
        } else if (cfg.kbAutoNight) {
            int hour = 12;                                 // default daytime if clock unset
            time_t t = time(nullptr);
            if (t > 1000000000L) {
                struct tm g;
                gmtime_r(&t, &g);
                hour = ((g.tm_hour + cfg.timezoneOffsetHours) % 24 + 24) % 24;
            }
            desired = (hour >= 21 || hour < 7) ? cfg.kbBrightness : 0;
        } else {
            desired = cfg.kbBrightness;                    // manual: always at set level
        }
        if (desired != s_kbApplied) {
            board.setKeyboardBacklight(desired);
            s_kbApplied = desired;
        }
    }

    // Notify screens of new data from feeds
    if (config::get().dxFeedSource == DX_FEED_HOLYCLUSTER) {
        if (HolyClusterFeed::hasNewData()) ScreenDXSpots::onNewData();
    } else {
        if (DXFeed::hasNewData()) ScreenDXSpots::onNewData();
    }
    if (HolyClusterFeed::heatmapUpdated()) ScreenBandMap::onNewData();
    if (SolarFeed::hasNewData())        ScreenSolar::onNewData();
    if (HamAlertFeed::hasNewData())     ScreenHamAlert::onNewData();
    if (DXpeditionFeed::hasNewData())   ScreenDXpeditions::onNewData();

    // HamAlert beep/popup notification for genuinely new alerts. The count is
    // baselined a few seconds after the telnet feed starts streaming, so the
    // initial backlog dump on (re)connect doesn't fire a burst of beeps.
    {
        static int      haPhase    = 0;   // 0=await stream, 1=grace, 2=armed
        static uint32_t haGraceMs  = 0;
        static uint32_t haBaseline = 0;
        if (HamAlertFeed::lastStatus() != 200) {
            haPhase = 0;                              // not streaming → re-arm later
        } else if (haPhase == 0) {
            haPhase = 1; haGraceMs = now;
        } else if (haPhase == 1) {
            if (now - haGraceMs >= 8000UL) {
                haBaseline = HamAlertFeed::totalInserted();
                haPhase    = 2;
            }
        } else {
            uint32_t tot = HamAlertFeed::totalInserted();
            if (tot > haBaseline) {
                haBaseline = tot;
                HamAlert latest;
                if (HamAlertFeed::getLatest(latest)) _notifyHamAlert(latest);
            }
        }
    }

    // If the screensaver takes over while a popup is open, dismiss it (only ever
    // create/destroy the popup from tick(), the LVGL thread).
    if ((s_screensaverActive || s_screenOff) && s_haPopup) _haPopupTeardown(false);
    if (DXWorldFeed::hasNewData())      ScreenDXpeditions::onNewData();

    // Periodic UI updates (clock, battery, WiFi status in launcher)
    static uint32_t lastClock = 0;
    static uint32_t lastBatt  = 0;
    if (now - lastClock >= 30000UL) {
        lastClock = now;
        ScreenLauncher::refreshStatus();
    }
    if (now - lastBatt >= 15000UL) {
        lastBatt = now;
        int sum = 0;
        for (int i = 0; i < 3; i++) sum += board.batteryPercent();
        ScreenLauncher::refreshBattery(sum / 3, board.batteryCharging());
    }

    // LVGL at ~30 FPS
    static uint32_t lastLvgl = 0;
    if (now - lastLvgl >= 33UL) {
        lastLvgl = now;
        lv_timer_handler();
    }
}

void showAlertPopupTest() {
    if (s_screensaverActive || s_screenOff) return;
    dxs::HamAlert sample;
    memset(&sample, 0, sizeof(sample));
    strncpy(sample.dxCall,  "TEST",     sizeof(sample.dxCall)  - 1);
    strncpy(sample.freq,    "14.074",   sizeof(sample.freq)    - 1);
    strncpy(sample.mode,    "FT8",      sizeof(sample.mode)    - 1);
    strncpy(sample.spotter, "DXSPOTTER",sizeof(sample.spotter) - 1);
    strncpy(sample.comment, "Alert popup enabled", sizeof(sample.comment) - 1);
    _showHaPopup(sample);
}

void showLauncher() { ScreenLauncher::show(); }

void resetKbBacklight() { s_kbApplied = 0xFF; }

}}  // namespace dxs::ui
