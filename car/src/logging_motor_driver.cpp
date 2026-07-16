// car/src/logging_motor_driver.cpp
#include "logging_motor_driver.h"
#include <Arduino.h>

void LoggingMotorDriver::drive(int8_t throttle, int8_t steering) {
    // Only log when the command actually changes, so a steady 30Hz stream of
    // identical values doesn't drown the serial console.
    if (throttle == _last_throttle && steering == _last_steering && !_stopped) return;
    _last_throttle = throttle;
    _last_steering = steering;
    _stopped = false;
    int left  = throttle + steering;
    int right = throttle - steering;
    Serial.printf("[MOTOR] drive throttle=%d steering=%d -> L=%d R=%d\n",
        throttle, steering, left, right);
}

void LoggingMotorDriver::stop() {
    if (_stopped) return;
    _stopped = true;
    _last_throttle = 0;
    _last_steering = 0;
    Serial.println("[MOTOR] stop");
}
