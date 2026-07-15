// lib/blahaj_common/protocol.h
#pragma once
#include <cstdint>
#include "game_state.h"

inline constexpr uint8_t BROADCAST_ID = 0xFF;
inline constexpr uint8_t ESP_NOW_BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

enum class MessageType : uint8_t {
    HELLO               = 1,
    HELLO_ACK           = 2,
    DRIVE_CMD           = 3,
    KNOCKOFF_EVENT      = 4,
    GAME_STATE_BROADCAST = 5,
    PING                = 6,
    PONG                = 7,
};

struct MessageHeader {
    MessageType type;
    uint8_t     src_id;
    uint8_t     seq;
} __attribute__((packed));

struct HelloMsg {
    MessageHeader header;
    DeviceType    device_type;
    uint8_t       device_id;   // compile-time DEVICE_ID build flag
} __attribute__((packed));

struct HelloAckMsg {
    MessageHeader header;
    uint8_t       assigned_id;
    uint8_t       partner_mac[6];
} __attribute__((packed));

struct DriveCmdMsg {
    MessageHeader header;
    int8_t        throttle;
    int8_t        steering;
} __attribute__((packed));

struct KnockoffEventMsg {
    MessageHeader header;
    uint8_t       car_id;
} __attribute__((packed));

struct GameStateBroadcastMsg {
    MessageHeader header;
    GameState     state;
    uint8_t       round;
    uint8_t       round_wins[3];
    uint8_t       knockoffs[3];
    uint8_t       countdown_remaining;
    uint8_t       cars_eliminated;
    uint8_t       last_knockoff_car_id;
} __attribute__((packed));

struct PingMsg {
    MessageHeader header;
} __attribute__((packed));
