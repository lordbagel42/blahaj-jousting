// car/src/knockoff_detector.h
#pragma once
#include <cstdint>
#include <functional>

class KnockoffDetector {
public:
    using TriggerCallback = std::function<void()>;

    // pin: GPIO pin connected to shark switch.
    // active_low: true if switch pulls pin LOW when triggered.
    // debounce_ms: minimum ms the pin must be in trigger state before firing.
    // lockout_ms: ms to ignore further triggers after one fires.
    KnockoffDetector(uint8_t pin, bool active_low,
                     uint32_t debounce_ms = 50,
                     uint32_t lockout_ms  = 3000);

    void onTrigger(TriggerCallback cb) { _callback = std::move(cb); }

    // Call from loop(). now_ms = millis().
    void tick(uint32_t now_ms);

private:
    uint8_t  _pin;
    bool     _active_low;
    uint32_t _debounce_ms;
    uint32_t _lockout_ms;

    TriggerCallback _callback;

    bool     _last_active    = false;
    uint32_t _change_time_ms = 0;
    bool     _triggered      = false;
    uint32_t _trigger_time_ms= 0;
};
