// car/src/logging_motor_driver.h
#pragma once
#include "motor_driver.h"

// Stand-in for a real motor driver until the DRV8833 is wired up: logs every
// drive/stop call to serial so comms can be verified end-to-end without
// physical motors. Rate-limited so a 30Hz drive stream doesn't flood serial.
class LoggingMotorDriver : public MotorDriver {
public:
    void drive(int8_t throttle, int8_t steering) override;
    void stop() override;

private:
    int8_t   _last_throttle = 0;
    int8_t   _last_steering = 0;
    bool     _stopped       = true;
};
