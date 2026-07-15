// car/src/motor_driver.h
#pragma once
#include <cstdint>

class MotorDriver {
public:
    virtual ~MotorDriver() = default;

    // throttle: -127..127 (positive = forward)
    // steering: -127..127 (positive = right)
    virtual void drive(int8_t throttle, int8_t steering) = 0;
    virtual void stop() = 0;
};
