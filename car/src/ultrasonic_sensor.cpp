// car/src/ultrasonic_sensor.cpp
#include "ultrasonic_sensor.h"
#include <Arduino.h>

UltrasonicSensor::UltrasonicSensor(uint8_t trig, uint8_t echo, uint16_t cutoff_cm)
    : _trig(trig), _echo(echo), _cutoff_cm(cutoff_cm) {}

void UltrasonicSensor::begin() {
    pinMode(_trig, OUTPUT);
    pinMode(_echo, INPUT);
    digitalWrite(_trig, LOW);
}

void UltrasonicSensor::tick(uint32_t now_ms) {
    if ((now_ms - _last_read_ms) < SAMPLE_INTERVAL_MS) return;
    _last_read_ms = now_ms;
    _distance_cm  = measureOnce();
}

uint16_t UltrasonicSensor::measureOnce() {
    // 10us trigger pulse.
    digitalWrite(_trig, LOW);
    delayMicroseconds(2);
    digitalWrite(_trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(_trig, LOW);

    unsigned long echo_us = pulseIn(_echo, HIGH, ECHO_TIMEOUT_US);
    if (echo_us == 0) return FAR;  // timed out: nothing within range

    uint32_t cm = echo_us / US_PER_CM;
    if (cm >= FAR) return FAR - 1;
    return static_cast<uint16_t>(cm);
}
