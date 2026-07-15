// client/src/button_input.h
#pragma once
#include "input_provider.h"

// Four-button digital controller (WASD / gamepad style).
// Each button produces full throttle/steering in one direction.
class ButtonInput : public InputProvider {
public:
    // All pins are active-low (INPUT_PULLUP).
    ButtonInput(uint8_t pin_up, uint8_t pin_down,
                uint8_t pin_left, uint8_t pin_right);

    DriveCommand read() override;

private:
    uint8_t _pin_up, _pin_down, _pin_left, _pin_right;
};
