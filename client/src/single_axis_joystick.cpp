// client/src/single_axis_joystick.cpp
// Fixed mapAxis deadzone handling
#include "single_axis_joystick.h"
#include <Arduino.h>

SingleAxisJoystick::SingleAxisJoystick(uint8_t pin, Axis axis, uint8_t deadzone)
    : _pin(pin), _axis(axis), _deadzone(deadzone) {}

DriveCommand SingleAxisJoystick::read() {
    DriveCommand cmd;
    int8_t v = mapAxis(analogRead(_pin));
    if (_axis == Axis::THROTTLE) cmd.throttle = v;
    else                         cmd.steering = v;
    return cmd;
}

int8_t SingleAxisJoystick::mapAxis(int raw) const {
#if defined(ESP8266)
    constexpr int ADC_MAX = 1023;  // ESP8266: single 10-bit ADC (A0)
#else
    constexpr int ADC_MAX = 4095;  // ESP32: 12-bit ADC
#endif
    int centered = (raw * 254 / ADC_MAX) - 127;
    if (centered > -_deadzone && centered < _deadzone) return 0;
    return static_cast<int8_t>(centered);
}
