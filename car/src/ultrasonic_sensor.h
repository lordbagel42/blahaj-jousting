// car/src/ultrasonic_sensor.h
#pragma once
#include <cstdint>

// HC-SR04 ultrasonic range finder (Trig/Echo, NOT I2C).
//
// Rate-limited, non-accumulating: tick() fires one measurement every
// SAMPLE_INTERVAL_MS. A single reading uses a blocking pulseIn() bounded by a
// short timeout, so the worst-case stall is a few ms and only when nothing is
// in range. If no echo returns (object beyond the timeout window, or absent),
// the reading is reported as "far" (distanceCm() == FAR).
//
// NOTE: HC-SR04 Echo is a 5V output. Level-shift/divide it down to 3.3V before
// the ESP8266 pin, and power the module from 5V.
class UltrasonicSensor {
public:
    static constexpr uint16_t FAR = 0xFFFF;  // no echo within timeout window

    // trig/echo: GPIO pins. cutoff_cm: distances strictly greater than this
    // count as "beyond cutoff" (used by CarNode to cut the motors).
    UltrasonicSensor(uint8_t trig, uint8_t echo, uint16_t cutoff_cm);

    void begin();

    // Call from loop(). now_ms = millis(). Internally rate-limited.
    void tick(uint32_t now_ms);

    // Last measured distance in cm, or FAR if the last read timed out.
    uint16_t distanceCm() const { return _distance_cm; }

    // True when the last reading is strictly beyond the cutoff (or FAR).
    bool beyondCutoff() const { return _distance_cm > _cutoff_cm; }

private:
    uint16_t measureOnce();  // one Trig pulse + Echo read, returns cm or FAR

    uint8_t  _trig;
    uint8_t  _echo;
    uint16_t _cutoff_cm;
    uint16_t _distance_cm  = FAR;  // start "far": fail-safe (motors cut) until a real read
    uint32_t _last_read_ms = 0;

    static constexpr uint32_t SAMPLE_INTERVAL_MS = 60;
    // Echo timeout: covers a bit past ~1.3m, so worst-case blocking is ~8ms and
    // only when nothing is within range. Anything past this reads as FAR.
    static constexpr uint32_t ECHO_TIMEOUT_US = 8000;
    // HC-SR04: distance_cm = echo_us / 58 (speed of sound, round trip).
    static constexpr uint32_t US_PER_CM = 58;
};
