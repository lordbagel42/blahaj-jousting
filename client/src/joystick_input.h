// client/src/joystick_input.h
#pragma once
#include "input_provider.h"

// Reads two analog ADC pins as X/Y joystick axes.
// Maps 0..4095 → -127..127 with a center deadzone.
class JoystickInput : public InputProvider {
public:
    // pin_x: ADC pin for left/right (steering)
    // pin_y: ADC pin for forward/back (throttle)
    // deadzone: center dead zone radius (0..127)
    JoystickInput(uint8_t pin_x, uint8_t pin_y, uint8_t deadzone = 10);

    DriveCommand read() override;

private:
    int8_t mapAxis(int raw) const;

    uint8_t _pin_x;
    uint8_t _pin_y;
    uint8_t _deadzone;
};
