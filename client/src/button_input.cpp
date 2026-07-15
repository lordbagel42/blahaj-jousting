// client/src/button_input.cpp
#include "button_input.h"
#include <Arduino.h>

ButtonInput::ButtonInput(uint8_t pin_up, uint8_t pin_down,
                         uint8_t pin_left, uint8_t pin_right)
    : _pin_up(pin_up), _pin_down(pin_down),
      _pin_left(pin_left), _pin_right(pin_right) {
    pinMode(_pin_up,    INPUT_PULLUP);
    pinMode(_pin_down,  INPUT_PULLUP);
    pinMode(_pin_left,  INPUT_PULLUP);
    pinMode(_pin_right, INPUT_PULLUP);
}

DriveCommand ButtonInput::read() {
    DriveCommand cmd;
    if (!digitalRead(_pin_up))    cmd.throttle =  100;
    if (!digitalRead(_pin_down))  cmd.throttle = -100;
    if (!digitalRead(_pin_right)) cmd.steering =  100;
    if (!digitalRead(_pin_left))  cmd.steering = -100;
    return cmd;
}
