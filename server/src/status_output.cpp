// server/src/status_output.cpp
#include "status_output.h"
#include <Arduino.h>

// ── Config (override via build flags) ────────────────────────────────────────
#ifndef STATUS_STRIP_PIN
#define STATUS_STRIP_PIN   D3    // WS2812 data
#endif
#ifndef STATUS_STRIP_LEDS
#define STATUS_STRIP_LEDS  8
#endif
#ifndef STATUS_BUZZER_PIN
#define STATUS_BUZZER_PIN  D8    // passive buzzer
#endif

static constexpr uint32_t FLASH_PERIOD_MS = 350;  // countdown red blink rate

#if defined(STATUS_STRIP_NEOPIXEL)
#include <Adafruit_NeoPixel.h>
static Adafruit_NeoPixel g_strip(STATUS_STRIP_LEDS, STATUS_STRIP_PIN, NEO_GRB + NEO_KHZ800);
#endif

void StatusOutput::begin() {
#if defined(STATUS_STRIP_NEOPIXEL)
    g_strip.begin();
    g_strip.clear();
    g_strip.show();
    pinMode(STATUS_BUZZER_PIN, OUTPUT);
#endif
    _initialized = true;
    Serial.println("[STATUS] output ready");
    setColor(0, 0, 0);
}

void StatusOutput::setColor(uint8_t r, uint8_t g, uint8_t b) {
    if (r == _cur_r && g == _cur_g && b == _cur_b) return;  // no-op if unchanged
    _cur_r = r; _cur_g = g; _cur_b = b;
#if defined(STATUS_STRIP_NEOPIXEL)
    for (int i = 0; i < STATUS_STRIP_LEDS; i++) g_strip.setPixelColor(i, g_strip.Color(r, g, b));
    g_strip.show();
#else
    Serial.printf("[STATUS] strip -> #%02X%02X%02X\n", r, g, b);
#endif
}

void StatusOutput::setBrightness(uint8_t b) {
#if defined(STATUS_STRIP_NEOPIXEL)
    g_strip.setBrightness(b);
    g_strip.show();  // re-apply current colors at the new brightness
#else
    Serial.printf("[STATUS] brightness -> %u\n", b);
#endif
}

void StatusOutput::beep(uint16_t freq, uint16_t ms) {
#if defined(STATUS_STRIP_NEOPIXEL)
    tone(STATUS_BUZZER_PIN, freq, ms);
#else
    Serial.printf("[STATUS] beep %uHz %ums\n", freq, ms);
#endif
}

void StatusOutput::update(const GameContext& ctx, uint32_t now_ms) {
    bool state_changed = (ctx.state != _last_state);

    switch (ctx.state) {
        case GameState::COUNTDOWN: {
            // One crisp red flash + beep per countdown second (3 total for a
            // 3s countdown), rather than a free-running blink.
            if (ctx.countdown_remaining != _last_countdown) {
                _last_countdown = ctx.countdown_remaining;
                _last_flash_ms  = now_ms;
                _flash_on       = true;
                beep(880, 120);
            }
            if (_flash_on && now_ms - _last_flash_ms >= FLASH_PERIOD_MS) {
                _flash_on = false;  // flash off, stay dark until next second
            }
            setColor(_flash_on ? 255 : 0, 0, 0);
            break;
        }
        case GameState::RACING:
            if (state_changed) beep(1320, 400);  // rising "go" tone
            setColor(0, 255, 0);
            break;
        case GameState::ROUND_END:
            if (state_changed) beep(440, 300);
            setColor(255, 0, 0);
            break;
        case GameState::LOBBY:
        default:
            // Idle: blue glow if enabled, otherwise dark.
            if (_idle_blue) setColor(0, 0, 255);
            else            setColor(0, 0, 0);
            _last_countdown = 0xFF;
            break;
    }

    _last_state = ctx.state;
}
