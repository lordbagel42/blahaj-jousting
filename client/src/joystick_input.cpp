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
    constexpr float ADC_MAX = 1023.0f;
#else
    constexpr float ADC_MAX = 4095.0f;
#endif

    // Map to float range -127.0 to 127.0
    float val = ((static_cast<float>(raw) / ADC_MAX) * 254.0f) - 127.0f;

    // Apply deadzone with smooth linear re-mapping
    if (abs(val) < _deadzone) {
        return 0;
    }

    // Rescale the remaining range (deadzone to 127) back to (0 to 127)
    float sign = (val > 0) ? 1.0f : -1.0f;
    float rescaled = sign * ((abs(val) - _deadzone) / (127.0f - _deadzone)) * 127.0f;

    // Constrain and cast
    return static_cast<int8_t>(constrain(round(rescaled), -127, 127));
}
