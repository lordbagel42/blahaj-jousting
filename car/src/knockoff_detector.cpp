// car/src/knockoff_detector.cpp
#include "knockoff_detector.h"
#include <Arduino.h>

KnockoffDetector::KnockoffDetector(uint8_t pin, bool active_low,
                                   uint32_t debounce_ms, uint32_t lockout_ms)
    : _pin(pin), _active_low(active_low),
      _debounce_ms(debounce_ms), _lockout_ms(lockout_ms) {
    pinMode(_pin, active_low ? INPUT_PULLUP : INPUT_PULLDOWN);
}

void KnockoffDetector::tick(uint32_t now_ms) {
    // In lockout window — ignore all input.
    if (_triggered && (now_ms - _trigger_time_ms) < _lockout_ms) return;
    _triggered = false;

    bool raw = digitalRead(_pin);
    bool active = _active_low ? !raw : raw;

    if (active != _last_raw) {
        _last_raw = active;
        _change_time_ms = now_ms;
    }

    if (active && (now_ms - _change_time_ms) >= _debounce_ms) {
        _triggered = true;
        _trigger_time_ms = now_ms;
        if (_callback) _callback();
    }
}
