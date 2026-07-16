// client/src/single_axis_joystick.h
#pragma once
#include "input_provider.h"

// Reads one analog ADC pin (e.g. D1 Mini's sole A0) as either the throttle
// or steering axis. Used when a joystick's two axes are split across two
// physical client devices, each reporting only the axis it's wired to.
class SingleAxisJoystick : public InputProvider {
public:
    enum class Axis : uint8_t { THROTTLE, STEERING };

    SingleAxisJoystick(uint8_t pin, Axis axis, uint8_t deadzone = 10);

    DriveCommand read() override;

private:
    int8_t mapAxis(int raw) const;

    uint8_t _pin;
    Axis    _axis;
    uint8_t _deadzone;
};
