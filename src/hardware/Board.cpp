// DXSpotter — Board.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hardware abstraction for the LilyGo T-Deck Plus (ESP32-S3).
// All pin numbers verified against the official LilyGo schematic.

#include "Board.h"
#include "../utils/Log.h"
#include <Wire.h>
#include <sys/time.h>
#include <driver/i2s.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace dxs {

// ── Pin definitions (T-Deck Plus) ─────────────────────────────────────
static constexpr gpio_num_t PIN_POWERON    = GPIO_NUM_10;  // peripheral rail enable
static constexpr gpio_num_t PIN_BACKLIGHT  = GPIO_NUM_42;

static constexpr gpio_num_t PIN_TBALL_UP    = GPIO_NUM_3;
static constexpr gpio_num_t PIN_TBALL_DOWN  = GPIO_NUM_15;
static constexpr gpio_num_t PIN_TBALL_LEFT  = GPIO_NUM_1;
static constexpr gpio_num_t PIN_TBALL_RIGHT = GPIO_NUM_2;
static constexpr gpio_num_t PIN_TBALL_PRESS = GPIO_NUM_0;

static constexpr gpio_num_t PIN_TOUCH_INT   = GPIO_NUM_16;
static constexpr gpio_num_t I2C_SDA        = GPIO_NUM_18;
static constexpr gpio_num_t I2C_SCL        = GPIO_NUM_8;
static constexpr int        PIN_BATT_ADC   = 4;
static constexpr uint8_t    KB_ADDR        = 0x55;

// I2S audio (onboard MAX98357A class-D amp) — T-Deck Plus pin map
static constexpr int        PIN_I2S_BCLK   = 7;
static constexpr int        PIN_I2S_WS     = 5;   // LRCLK
static constexpr int        PIN_I2S_DOUT   = 6;
static constexpr int        I2S_SAMPLE_RATE = 8000;   // mono; matches OpenMeshOS-proven config

// ── Trackball ISR counters ─────────────────────────────────────────────
static volatile uint32_t s_isrU = 0, s_isrD = 0, s_isrL = 0, s_isrR = 0;

static void IRAM_ATTR isr_tball_up()    { s_isrU++; }
static void IRAM_ATTR isr_tball_down()  { s_isrD++; }
static void IRAM_ATTR isr_tball_left()  { s_isrL++; }
static void IRAM_ATTR isr_tball_right() { s_isrR++; }

static Board s_board;

Board& Board::instance() { return s_board; }

void Board::init() {
    DXS_LOG("Board", "Initialising T-Deck Plus hardware");

    // 1) Power on the peripheral rail FIRST
    pinMode(PIN_POWERON, OUTPUT);
    digitalWrite(PIN_POWERON, HIGH);
    delay(100);

    // 2) Display backlight
    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, HIGH);

    // 3) I2C bus
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(50);
    Wire.beginTransmission(KB_ADDR);
    _keyboardPresent = (Wire.endTransmission(true) == 0);
    DXS_LOG("Board", "BBQ10 keyboard: %s", _keyboardPresent ? "FOUND" : "NOT FOUND");

    // 4) Trackball GPIOs
    pinMode(PIN_TBALL_UP,    INPUT_PULLUP);
    pinMode(PIN_TBALL_DOWN,  INPUT_PULLUP);
    pinMode(PIN_TBALL_LEFT,  INPUT_PULLUP);
    pinMode(PIN_TBALL_RIGHT, INPUT_PULLUP);
    pinMode(PIN_TBALL_PRESS, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PIN_TBALL_UP),    isr_tball_up,    FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_TBALL_DOWN),  isr_tball_down,  FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_TBALL_LEFT),  isr_tball_left,  FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_TBALL_RIGHT), isr_tball_right, FALLING);

    // 5) Touch interrupt
    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);

    // 6) Seed RTC from compile timestamp
    {
        static const char* months[] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };
        struct tm tm = {};
        char mon[4] = {};
        sscanf(__DATE__, "%3s %d %d", mon, &tm.tm_mday, &tm.tm_year);
        sscanf(__TIME__, "%d:%d:%d",  &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
        tm.tm_year -= 1900;
        for (int i = 0; i < 12; i++) {
            if (strcmp(mon, months[i]) == 0) { tm.tm_mon = i; break; }
        }
        time_t ct = mktime(&tm);
        struct timeval tv = { ct, 0 };
        settimeofday(&tv, nullptr);
    }

    // 7) Audio (I2S amp). Reworked to the OpenMeshOS-proven model — 8 kHz mono,
    //    DMA large enough to hold a whole short sound, and a pre-generated chirp in
    //    PSRAM written directly (NO per-beep FreeRTOS task). The old per-beep
    //    xTaskCreate/vTaskDelete churn was the lockup cause under alert streaming.
    initAudio();

    _initialized = true;
    DXS_LOG("Board", "Hardware ready");
}

void Board::tick() {
    if (!_initialized) return;

    uint32_t u, d, l, r;
    noInterrupts();
    u = s_isrU; s_isrU = 0;
    d = s_isrD; s_isrD = 0;
    l = s_isrL; s_isrL = 0;
    r = s_isrR; s_isrR = 0;
    interrupts();

    if (u || d || l || r) {
        _trackballX += (int16_t)(r - l);
        _trackballY += (int16_t)(d - u);
    }

    uint32_t now_ms = millis();
    bool curDown = !digitalRead(PIN_TBALL_PRESS);
    if (curDown && !_trackballPrevDown && (now_ms - _trackballPressMs >= 150UL)) {
        _trackballPressed = true;
        _trackballPressMs = now_ms;
    }
    _trackballPrevDown = curDown;
}

void Board::consumeTrackballDelta(int16_t& dx, int16_t& dy) {
    noInterrupts();
    dx = _trackballX; _trackballX = 0;
    dy = _trackballY; _trackballY = 0;
    interrupts();
}

bool Board::consumeTrackballPress() {
    bool p = _trackballPressed;
    _trackballPressed = false;
    return p;
}

int Board::batteryPercent() const {
    // 100k/100k voltage divider on GPIO4; Vbat range ~3.0V-4.2V
    // ADC reference ~3.3V; 12-bit (0-4095)
    int raw = analogRead(PIN_BATT_ADC);
    float vbat = (raw / 4095.0f) * 3.3f * 2.0f;
    int pct = (int)((vbat - 3.0f) / 1.2f * 100.0f);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

bool Board::batteryCharging() const {
    int raw = analogRead(PIN_BATT_ADC);
    float vbat = (raw / 4095.0f) * 3.3f * 2.0f;
    return vbat > 4.1f;
}

// ── Audio (I2S → MAX98357A) ───────────────────────────────────────────
// Ported from the OpenMeshOS Sound module (which runs stable on the same
// T-Deck Plus): 8 kHz MONO, a DMA large enough to hold a whole short sound, and
// a notification chirp pre-generated ONCE into PSRAM. The notification path
// (beepNotify) writes that cached waveform DIRECTLY — there is NO per-beep
// FreeRTOS task. The previous design did xTaskCreate(6 KB)/vTaskDelete on every
// beep; under HamAlert alert streaming that alloc/free churn fragmented the heap
// and locked the device up. Only the one-shot boot CW still uses a task.

// Notification chirp: rising two-tone A6 (1760 Hz, 90 ms) → D7 (2349 Hz, 140 ms).
// 230 ms @ 8 kHz mono = 1840 samples — fits the 2048-sample DMA, so i2s_write
// queues the whole chirp and returns without blocking the caller.
static constexpr int CHIRP_T1_HZ = 1760;
static constexpr int CHIRP_T1_MS = 90;
static constexpr int CHIRP_T2_HZ = 2349;
static constexpr int CHIRP_T2_MS = 140;
static int16_t* s_chirpBuf  = nullptr;   // PSRAM, generated once in initAudio()
static int      s_chirpLen  = 0;         // samples
static uint32_t s_soundEndMs = 0;        // millis() deadline used to throttle beeps

// Persistent audio worker. Created ONCE in initAudio(); EVERY i2s_write runs on
// this task, so a slow/blocked DMA can never stall the LVGL loop task (which is
// what called beepNotify directly before), and there is NO per-beep task
// create/destroy churn. Callers just enqueue a small command and return.
enum class AudioCmd : uint8_t { Chirp, BootCW };
static QueueHandle_t s_audioQ = nullptr;

// Chunk a mono waveform into I2S, scaling by volume (0-255) so live volume
// changes apply without regenerating the buffer. A fixed 256-sample stack buffer
// keeps this off the heap — no per-call allocation. Returns when all queued.
static void _writeScaledMono(const int16_t* buf, int count, uint8_t volume) {
    if (volume == 0) return;
    const float scale = (float)volume / 255.0f;
    int16_t tmp[256];
    while (count > 0) {
        int n = (count < 256) ? count : 256;
        for (int i = 0; i < n; i++) tmp[i] = (int16_t)((float)buf[i] * scale);
        size_t written = 0;
        // Finite timeout (never portMAX_DELAY) so a wedged peripheral can't hang
        // the caller forever; the chirp fits the DMA so this returns at once.
        if (i2s_write(I2S_NUM_0, tmp, (size_t)n * sizeof(int16_t), &written,
                      pdMS_TO_TICKS(250)) != ESP_OK || written == 0) return;
        buf   += n;
        count -= n;
    }
}

// Generate a fade-shaped mono sine tone at full amplitude into dst; returns the
// sample count written. Used to bake the chirp once and (live) for boot CW.
static int _genTone(int16_t* dst, int freqHz, int durMs) {
    const int   n    = (I2S_SAMPLE_RATE * durMs) / 1000;
    const int   fade = I2S_SAMPLE_RATE / 200;          // ~5 ms fade in/out (no clicks)
    const float w    = 2.0f * 3.14159265f * (float)freqHz / (float)I2S_SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float env = 1.0f;
        if (i < fade)            env = (float)i / fade;
        else if (i > n - fade)   env = (float)(n - i) / fade;
        dst[i] = (int16_t)(sinf(w * i) * 14000.0f * env);
    }
    return n;
}

// The one place i2s_write is allowed to block. Blocks only THIS task, never the
// UI loop. playMorse() (boot CW) is a long sequence of blocking writes — fine
// here, where it just delays the next queued sound.
static void _audioTask(void*) {
    AudioCmd cmd;
    for (;;) {
        if (xQueueReceive(s_audioQ, &cmd, portMAX_DELAY) != pdTRUE) continue;
        switch (cmd) {
            case AudioCmd::Chirp:
                if (s_chirpBuf)
                    _writeScaledMono(s_chirpBuf, s_chirpLen, Board::instance().volume());
                break;
            case AudioCmd::BootCW:
                Board::instance().playMorse("CQ CQ DX", 700, 45);
                break;
        }
    }
}

void Board::initAudio() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = I2S_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;    // MAX98357A is mono
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = 0;
    // 4 × 512 = 2048 samples of DMA capacity — the whole chirp fits, so i2s_write
    // returns as soon as samples are queued (no long block on the caller).
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = 512;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = 0;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        DXS_LOG("Board", "I2S driver install failed — audio disabled");
        return;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = PIN_I2S_BCLK;
    pins.ws_io_num    = PIN_I2S_WS;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        DXS_LOG("Board", "I2S set_pin failed — audio disabled");
        i2s_driver_uninstall(I2S_NUM_0);
        return;
    }
    i2s_zero_dma_buffer(I2S_NUM_0);

    // Pre-generate the notification chirp ONCE into PSRAM (CPU-read only; the I2S
    // DMA reads from the small stack buffer in _writeScaledMono, never from here).
    const int t1 = (I2S_SAMPLE_RATE * CHIRP_T1_MS) / 1000;
    const int t2 = (I2S_SAMPLE_RATE * CHIRP_T2_MS) / 1000;
    s_chirpBuf = (int16_t*)ps_malloc((size_t)(t1 + t2) * sizeof(int16_t));
    if (s_chirpBuf) {
        int n = _genTone(s_chirpBuf, CHIRP_T1_HZ, CHIRP_T1_MS);
        n    += _genTone(s_chirpBuf + n, CHIRP_T2_HZ, CHIRP_T2_MS);
        s_chirpLen = n;
    } else {
        DXS_LOG("Board", "chirp ps_malloc failed — beep disabled");
    }

    // Spin up the single persistent audio worker that owns all i2s_write.
    s_audioQ = xQueueCreate(4, sizeof(AudioCmd));
    if (!s_audioQ ||
        xTaskCreate(_audioTask, "audio", 6144, nullptr, 1, nullptr) != pdPASS) {
        DXS_LOG("Board", "audio task/queue alloc failed — sounds disabled");
        s_audioQ = nullptr;
    }

    _audioReady = true;
    DXS_LOG("Board", "I2S audio ready (mono %dHz, BCLK=%d WS=%d DOUT=%d)",
            I2S_SAMPLE_RATE, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT);
}

void Board::playTone(uint16_t freqHz, uint16_t durationMs) {
    if (!_audioReady || freqHz == 0 || durationMs == 0 || _volume == 0) return;

    const int totalFrames = (int)((uint32_t)I2S_SAMPLE_RATE * durationMs / 1000);
    // _volume 0-255 scales amplitude; 255 → 12000 (~0.37 full-scale).
    const int amp         = (int)((uint32_t)_volume * 12000 / 255);
    if (amp == 0) return;                              // volume 0 → silent
    const int fadeFrames  = I2S_SAMPLE_RATE / 200;     // ~5ms fade in/out to avoid clicks
    const float w         = 2.0f * 3.14159265f * (float)freqHz / (float)I2S_SAMPLE_RATE;

    int16_t buf[256];                                  // mono samples per chunk

    for (int n = 0; n < totalFrames; ) {
        int frames = totalFrames - n;
        if (frames > 256) frames = 256;
        for (int i = 0; i < frames; i++) {
            int g = n + i;
            float env = 1.0f;
            if (g < fadeFrames)                  env = (float)g / fadeFrames;
            else if (g > totalFrames - fadeFrames) env = (float)(totalFrames - g) / fadeFrames;
            buf[i] = (int16_t)(sinf(w * g) * amp * env);
        }
        size_t written = 0;
        // Finite timeout (never portMAX_DELAY) so a wedged I2S peripheral can't
        // hang the audio task forever. Normal writes return well under this.
        if (i2s_write(I2S_NUM_0, buf, frames * sizeof(int16_t), &written,
                      pdMS_TO_TICKS(250)) != ESP_OK || written == 0) return;
        n += frames;
    }
    // NB: do NOT i2s_zero_dma_buffer() here — that wipes samples still queued in
    // DMA but not yet physically played, chopping the tail off every tone (and
    // nearly erasing short dits). Trailing samples drain on their own.
}

// Write `ms` of silence directly into the I2S stream. Used for Morse gaps so the
// timing lives in the audio stream itself (vTaskDelay would just pause the CPU
// while DMA keeps playing tones back-to-back with no real gap between them).
static void _i2sSilence(uint16_t ms) {
    int frames = (int)((uint32_t)I2S_SAMPLE_RATE * ms / 1000);
    int16_t buf[256] = {0};
    while (frames > 0) {
        int chunk = frames > 256 ? 256 : frames;
        size_t written = 0;
        if (i2s_write(I2S_NUM_0, buf, chunk * sizeof(int16_t), &written,
                      pdMS_TO_TICKS(250)) != ESP_OK || written == 0) return;
        frames -= chunk;
    }
}

void Board::beepNotify() {
    if (!_audioReady || !s_chirpBuf || !s_audioQ || _volume == 0) return;
    if (millis() < s_soundEndMs) return;   // throttle rapid repeats (streaming alerts, slider)
    // Just hand the request to the audio task and return — NO i2s_write on the
    // caller (this runs on the LVGL loop). Non-blocking send; drop if the queue
    // is somehow backed up rather than ever stalling the UI.
    s_soundEndMs = millis() + CHIRP_T1_MS + CHIRP_T2_MS + 60;
    AudioCmd c = AudioCmd::Chirp;
    xQueueSend(s_audioQ, &c, 0);
}

// ── Morse / CW sidetone ───────────────────────────────────────────────
// Patterns for A-Z then 0-9 ('.' = dit, '-' = dah).
static const char* const kMorse[] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",      // A-J
    "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",        // K-T
    "..-", "...-", ".--", "-..-", "-.--", "--..",                               // U-Z
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...",      // 0-7
    "---..", "----.",                                                           // 8-9
};

static const char* _morseFor(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return kMorse[c - 'A'];
    if (c >= '0' && c <= '9') return kMorse[26 + (c - '0')];
    return nullptr;   // unsupported char → treated as a gap
}

void Board::playMorse(const char* text, uint16_t freqHz, uint16_t unitMs) {
    if (!_audioReady || !text) return;
    // Everything — tones AND the gaps between them — is written in order into the
    // one I2S stream, so the whole message is a single continuous waveform with
    // exact PARIS timing (dit=1u, dah=3u, element gap=1u, char gap=3u, word=7u).
    for (const char* p = text; *p; p++) {
        const char* code = _morseFor(*p);
        if (!code) {                                   // space / unknown → word gap
            // The preceding character already emitted a 3-unit gap, so add 4 more
            // to reach the standard 7-unit word gap.
            _i2sSilence((uint16_t)(unitMs * 4));
            continue;
        }
        for (const char* e = code; *e; e++) {
            playTone(freqHz, (uint16_t)(unitMs * (*e == '-' ? 3 : 1)));
            if (e[1]) _i2sSilence(unitMs);             // 1-unit gap between elements
        }
        _i2sSilence((uint16_t)(unitMs * 3));           // inter-character gap
    }
}

void Board::playBootSound() {
    if (!_audioReady || !s_audioQ) return;
    AudioCmd c = AudioCmd::BootCW;
    xQueueSend(s_audioQ, &c, 0);   // played by the persistent audio task
}

bool Board::pollKeyboard(char& outKey) {
    if (!_keyboardPresent) return false;
    Wire.requestFrom((uint8_t)KB_ADDR, (uint8_t)1);
    if (!Wire.available()) return false;
    uint8_t k = Wire.read();
    while (Wire.available()) Wire.read();
    if (k == 0) return false;
    outKey = (char)k;
    return true;
}

}  // namespace dxs
