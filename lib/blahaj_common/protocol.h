// lib/blahaj_common/protocol.h
#pragma once
#include <cstdint>
#include "game_state.h"

inline constexpr uint8_t BROADCAST_ID = 0xFF;
inline constexpr uint8_t ESP_NOW_BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

enum class MessageType : uint8_t {
    HELLO                = 1,
    HELLO_ACK            = 2,
    DRIVE_CMD            = 3,
    KNOCKOFF_EVENT       = 4,
    GAME_STATE_BROADCAST = 5,
    PING                 = 6,
    PONG                 = 7,
    START_CMD            = 10,
    END_CMD              = 11,
    PAIR_CMD             = 12,
    LED_CMD              = 13,
    RESET_CMD            = 14,
    REBOOT_CMD           = 15,
    SET_BRIGHTNESS_CMD   = 16,
    SET_IDLE_BLUE_CMD    = 17,
    TEST_DRIVE_CMD       = 18,
};

struct MessageHeader {
    MessageType type;
    uint8_t     src_id;
    uint8_t     seq;
} __attribute__((packed));

inline constexpr uint8_t DRIVE_AXIS_THROTTLE = 0x01;
inline constexpr uint8_t DRIVE_AXIS_STEERING = 0x02;
inline constexpr uint8_t DRIVE_AXIS_BOTH     = DRIVE_AXIS_THROTTLE | DRIVE_AXIS_STEERING;

struct HelloMsg {
    MessageHeader header;
    DeviceType    device_type;
    uint8_t       device_id;   // compile-time DEVICE_ID build flag
    // For CLIENT devices: which axis this physical unit reports (see
    // DRIVE_AXIS_*). Meaningless for other device types.
    uint8_t       axis_mask;
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
    uint8_t       axis_mask;  // which of throttle/steering this message actually reports
} __attribute__((packed));

struct KnockoffEventMsg {
    MessageHeader header;
    uint8_t       car_id;
} __attribute__((packed));

inline constexpr uint8_t MAX_BROADCAST_PAIRS = 8;

// One paired (car, client) association, for referee display. Sent as part
// of GameStateBroadcastMsg since that's already broadcast to the referee.
struct PairInfo {
    uint8_t car_device_id;
    uint8_t client_device_id;
    uint8_t axis_mask;     // client's DRIVE_AXIS_* role, for the "#1A"/"#1B" tag
    uint8_t car_online;    // 1 if the car has sent something recently
    uint8_t client_online; // 1 if the client has sent something recently
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
    uint8_t       pair_count;
    PairInfo      pairs[MAX_BROADCAST_PAIRS];
    uint8_t       led_brightness;  // server status strip brightness (0-255), so referee slider can sync
    uint8_t       idle_blue;       // 1 = strip glows blue when idle, so referee toggle can sync
} __attribute__((packed));

struct PingMsg {
    MessageHeader header;
} __attribute__((packed));

struct StartCmdMsg {
    MessageHeader header;
} __attribute__((packed));

struct EndCmdMsg {
    MessageHeader header;
} __attribute__((packed));

struct PairCmdMsg {
    MessageHeader header;
    uint8_t       car_idx;
    uint8_t       client_idx;
} __attribute__((packed));

struct LedCmdMsg {
    MessageHeader header;
    uint8_t       on;          // 1 = on, 0 = off
    DeviceType    target_type; // which device this LED command is addressed to
    uint8_t       target_id;   // that device's device_id (ignored for SERVER)
} __attribute__((packed));

// Wipes round/wins/knockoffs and returns to LOBBY without touching pairings.
struct ResetCmdMsg {
    MessageHeader header;
} __attribute__((packed));

// Broadcast to every device; each one restarts itself (same as a physical
// reset button press).
struct RebootCmdMsg {
    MessageHeader header;
} __attribute__((packed));

// Sets the server status LED strip brightness. Persisted to EEPROM by the server.
struct SetBrightnessCmdMsg {
    MessageHeader header;
    uint8_t       brightness;  // 0-255, scales the whole strip
} __attribute__((packed));

// Toggles whether the server strip glows blue when idle. Persisted to EEPROM.
struct SetIdleBlueCmdMsg {
    MessageHeader header;
    uint8_t       on;  // 1 = blue when idle, 0 = dark when idle
} __attribute__((packed));

// Bench motor test: the referee broadcasts this to drive a car's motors
// directly, bypassing pairing / RACING / the sonar cutoff. The car auto-
// stops shortly after the last one arrives (see CarNode::TEST_DRIVE_HOLD_MS).
struct TestDriveCmdMsg {
    MessageHeader header;
    uint8_t       target_id;  // car device_id, or BROADCAST_ID (0xFF) for all cars
    int8_t        throttle;   // -127..127
    int8_t        steering;   // -127..127
} __attribute__((packed));
