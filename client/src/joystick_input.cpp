// client/src/joystick_input.cpp
// Fixed mapAxis deadzone handling
#include "joystick_input.h"
#include <Arduino.h>

JoystickInput::JoystickInput(uint8_t pin_x, uint8_t pin_y, uint8_t deadzone)
    : _pin_x(pin_x), _pin_y(pin_y), _deadzone(deadzone) {}

DriveCommand JoystickInput::read() {
    DriveCommand cmd;
    cmd.steering = mapAxis(analogRead(_pin_x));
    cmd.throttle = mapAxis(analogRead(_pin_y));
    return cmd;
}

int8_t JoystickInput::mapAxis(int raw) const {
#if defined(ESP8266)
    constexpr int ADC_MAX = 1023;  // ESP8266: single 10-bit ADC (A0)
#else
    constexpr int ADC_MAX = 4095;  // ESP32: 12-bit ADC
#endif
    int centered = (raw * 254 / ADC_MAX) - 127;
    if (centered > -_deadzone && centered < _deadzone) return 0;
    return static_cast<int8_t>(centered);
}
