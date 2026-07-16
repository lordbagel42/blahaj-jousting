// car/src/knockoff_detector.cpp
#include "knockoff_detector.h"
#include <Arduino.h>

KnockoffDetector::KnockoffDetector(uint8_t pin, bool active_low,
                                   uint32_t debounce_ms, uint32_t lockout_ms)
    : _pin(pin), _active_low(active_low),
      _debounce_ms(debounce_ms), _lockout_ms(lockout_ms) {
#if defined(ESP8266)
    // ESP8266 has no generic internal pulldown (only GPIO16); active-low wiring
    // (the only mode this project uses) only needs INPUT_PULLUP.
    pinMode(_pin, active_low ? INPUT_PULLUP : INPUT);
#else
    pinMode(_pin, active_low ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif
}

void KnockoffDetector::tick(uint32_t now_ms) {
    // In lockout window — ignore all input.
    if (_triggered && (now_ms - _trigger_time_ms) < _lockout_ms) return;
    if (_triggered) {
        _triggered = false;
        _change_time_ms = now_ms;  // restart debounce from lockout exit
    }

    bool raw = digitalRead(_pin);
    bool active = _active_low ? !raw : raw;

    if (active != _last_active) {
        _last_active = active;
        _change_time_ms = now_ms;
    }

    if (active && (now_ms - _change_time_ms) >= _debounce_ms) {
        _triggered = true;
        _trigger_time_ms = now_ms;
        if (_callback) _callback();
    }
}
