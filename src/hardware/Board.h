// DXSpotter — Board.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hardware abstraction for the LilyGo T-Deck Plus.
// Pin map verified against the official LilyGo T-Deck Plus schematic.

#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace dxs {

class Board {
public:
    static Board& instance();

    void init();
    void tick();

    // Trackball delta accumulated between calls (-N to +N per axis).
    // Press polled from GPIO0 (BOOT button = trackball click, active-low).
    void consumeTrackballDelta(int16_t& dx, int16_t& dy);
    bool consumeTrackballPress();
    void signalTrackballPress() { _trackballPressed = true; }

    // Battery 0-100 % via voltage divider on GPIO4
    int  batteryPercent()  const;
    bool batteryCharging() const;

    // BBQ10 keyboard: returns true + fills key when a key was pressed.
    bool pollKeyboard(char& outKey);

    // ── Audio (I2S → onboard MAX98357A class-D amp / speaker) ──────────
    // I2S0 master TX: WS=GPIO5, BCLK=GPIO7, DOUT=GPIO6 (T-Deck Plus).
    void initAudio();

    // Play a single blocking sine tone. Blocks the CALLER for ~durationMs once the
    // DMA fills — only ever called from the persistent audio task (never the UI).
    void playTone(uint16_t freqHz, uint16_t durationMs);

    // Two-tone "new alert" chirp. Enqueues a request to the persistent audio task
    // and returns instantly — NO i2s_write on the caller, so it is safe to call
    // from the LVGL loop / feed path. Self-throttles to avoid stacking beeps.
    // No-op if audio failed to initialise or volume is 0.
    void beepNotify();

    // Play a Morse-code string (A-Z, 0-9, space) as CW sidetone. Blocking —
    // call from a dedicated task. unitMs = one "dit" length (PARIS timing).
    void playMorse(const char* text, uint16_t freqHz = 700, uint16_t unitMs = 70);

    // Fire-and-forget boot chime: "CQ CQ DX" in Morse, on a background task.
    void playBootSound();

    // Output volume 0-255 (scales tone amplitude). Applied to beeps & chime.
    void setAudioVolume(uint8_t v) { _volume = v; }
    uint8_t volume() const { return _volume; }   // read by the audio task

    bool audioReady() const { return _audioReady; }

    // Screen backlight PWM on GPIO 42 via ledc (channel 0, 5 kHz, 8-bit).
    static constexpr uint8_t  BL_LEDC_CH   = 0;
    static constexpr uint32_t BL_LEDC_FREQ = 5000;
    static constexpr uint8_t  BL_LEDC_BITS = 8;

    void initBacklightPWM() {
        ledcSetup(BL_LEDC_CH, BL_LEDC_FREQ, BL_LEDC_BITS);
        ledcAttachPin(42, BL_LEDC_CH);
    }
    void setDisplayBrightness(uint8_t val) { ledcWrite(BL_LEDC_CH, val); }

    // Keyboard backlight via keyboard MCU (I2C 0x55).
    // Sends all three protocols — A (single byte), B (reg 0x05 + val), C (reg 0x01 + val).
    // All ACK; the keyboard MCU firmware acts on whichever it implements. (From OpenMeshOS.)
    void setKeyboardBacklight(uint8_t brightness) {
        Wire.beginTransmission((uint8_t)0x55);  // A: single byte
        Wire.write(brightness);
        Wire.endTransmission(true);
        Wire.beginTransmission((uint8_t)0x55);  // B: BBQ10 REG_BKL = 0x05
        Wire.write((uint8_t)0x05);
        Wire.write(brightness);
        Wire.endTransmission(true);
        Wire.beginTransmission((uint8_t)0x55);  // C: reg 0x01
        Wire.write((uint8_t)0x01);
        Wire.write(brightness);
        Wire.endTransmission(true);
    }

    bool initialized() const { return _initialized; }

private:
    bool    _initialized      = false;
    bool    _keyboardPresent  = false;
    bool    _audioReady       = false;
    uint8_t _volume           = 90;   // 0-255; overwritten from Config at startup

    int16_t _trackballX          = 0;
    int16_t _trackballY          = 0;
    bool    _trackballPressed    = false;
    bool    _trackballPrevDown   = false;
    uint32_t _trackballPressMs  = 0;
};

}  // namespace dxs
