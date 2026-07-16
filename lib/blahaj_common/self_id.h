// lib/blahaj_common/self_id.h
//
// One standardized identity banner every node prints at boot, so any board
// can be identified over serial without guessing:
//   [ID] role=CLIENT id=1 axis=THROTTLE mac=AA:BB:CC:DD:EE:FF
// Grep for "[ID]" on a reset to learn exactly what a physical board is.
#pragma once
#include <Arduino.h>
#include "protocol.h"

inline const char* deviceRoleName(DeviceType type) {
    switch (type) {
        case DeviceType::CAR:     return "CAR";
        case DeviceType::CLIENT:  return "CLIENT";
        case DeviceType::REFEREE: return "REFEREE";
        case DeviceType::SERVER:  return "SERVER";
    }
    return "UNKNOWN";
}

inline const char* driveAxisName(uint8_t axis_mask) {
    if (axis_mask == DRIVE_AXIS_THROTTLE) return "THROTTLE";
    if (axis_mask == DRIVE_AXIS_STEERING) return "STEERING";
    if (axis_mask == DRIVE_AXIS_BOTH)     return "BOTH";
    return "-";
}

// axis_mask is meaningful only for CLIENT; pass 0 otherwise. device_id may be
// 0xFE for singletons (server/referee).
inline void printSelfId(DeviceType type, uint8_t device_id, uint8_t axis_mask, const uint8_t mac[6]) {
    Serial.printf("[ID] role=%s id=%u axis=%s mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
        deviceRoleName(type), device_id, driveAxisName(axis_mask),
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
