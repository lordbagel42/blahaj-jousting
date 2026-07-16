// server/src/status_output.h
//
// Drives the server's status LED strip + buzzer in sync with game state:
//   LOBBY      -> strip off (idle)
//   COUNTDOWN  -> strip flashes RED once per second, buzzer beeps each tick
//   RACING     -> strip solid GREEN, one rising "go" beep on entry
//   ROUND_END  -> strip solid RED
//
// Two implementations selected at build time:
//   default            -> logs intended color/beeps to serial (no hardware)
//   -D STATUS_STRIP_NEOPIXEL -> drives a WS2812/NeoPixel strip + passive buzzer
#pragma once
#include <cstdint>
#include "game_state.h"

class StatusOutput {
public:
    void begin();
    // Call every loop with the latest game context. now_ms = millis().
    void update(const GameContext& ctx, uint32_t now_ms);

    // Global strip brightness (0-255), scales all states. Applied immediately.
    void setBrightness(uint8_t b);

    // When true, the idle (LOBBY) state glows blue instead of going dark.
    void setIdleBlue(bool on) { _idle_blue = on; }

private:
    void setColor(uint8_t r, uint8_t g, uint8_t b);  // whole strip
    void beep(uint16_t freq, uint16_t ms);

    GameState _last_state       = GameState::LOBBY;
    bool      _idle_blue        = true;
    uint8_t   _last_countdown   = 0xFF;
    bool      _flash_on         = false;
    uint32_t  _last_flash_ms    = 0;
    bool      _initialized      = false;
    uint8_t   _cur_r = 0xFF, _cur_g = 0xFF, _cur_b = 0xFF;  // force first write
};
