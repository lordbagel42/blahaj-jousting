// car/src/drv8833_driver.h
#pragma once
#include "motor_driver.h"

// Controls two DRV8833 H-bridge channels (left side + right side).
// Each channel is driven by two PWM pins: IN1 (forward) and IN2 (reverse).
// FL+BL motors → left channel (ain1, ain2)
// FR+BR motors → right channel (bin1, bin2)
class DRV8833Driver : public MotorDriver {
public:
    DRV8833Driver(uint8_t ain1, uint8_t ain2, uint8_t bin1, uint8_t bin2);

    void drive(int8_t throttle, int8_t steering) override;
    void stop() override;

private:
    void setChannel(uint8_t in1, uint8_t in2, int speed);  // speed: -255..255

    uint8_t _ain1, _ain2, _bin1, _bin2;
};
