// car/src/drv8833_driver.cpp
#include "drv8833_driver.h"
#include <Arduino.h>

static inline int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

DRV8833Driver::DRV8833Driver(uint8_t ain1, uint8_t ain2, uint8_t bin1, uint8_t bin2)
    : _ain1(ain1), _ain2(ain2), _bin1(bin1), _bin2(bin2) {
#if defined(ESP8266)
    analogWriteRange(255);  // match ESP32's default 8-bit duty resolution
#endif
    pinMode(_ain1, OUTPUT); pinMode(_ain2, OUTPUT);
    pinMode(_bin1, OUTPUT); pinMode(_bin2, OUTPUT);
    stop();
}

void DRV8833Driver::drive(int8_t throttle, int8_t steering) {
    // Mix throttle+steering to left/right. Scale to -255..255.
    int left  = (static_cast<int>(throttle) + static_cast<int>(steering)) * 2;
    int right = (static_cast<int>(throttle) - static_cast<int>(steering)) * 2;
    left  = clampInt(left,  -255, 255);
    right = clampInt(right, -255, 255);
    setChannel(_ain1, _ain2, left);
    setChannel(_bin1, _bin2, right);
}

void DRV8833Driver::stop() {
    analogWrite(_ain1, 0); analogWrite(_ain2, 0);
    analogWrite(_bin1, 0); analogWrite(_bin2, 0);
}

void DRV8833Driver::setChannel(uint8_t in1, uint8_t in2, int speed) {
    if (speed > 0) {
        analogWrite(in1, speed);
        analogWrite(in2, 0);
    } else if (speed < 0) {
        analogWrite(in1, 0);
        analogWrite(in2, -speed);
    } else {
        analogWrite(in1, 0);
        analogWrite(in2, 0);
    }
}
