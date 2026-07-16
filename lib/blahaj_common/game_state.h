// lib/blahaj_common/game_state.h
#pragma once
#include <cstdint>

enum class GameState : uint8_t {
    LOBBY     = 0,
    COUNTDOWN = 1,
    RACING    = 2,
    ROUND_END = 3,
};

enum class GameEvent : uint8_t {
    STATE_CHANGED       = 0,
    COUNTDOWN_TICK      = 1,
    MATCH_START         = 2,
    KNOCKOFF            = 3,
    ROUND_END           = 4,
    MATCH_END           = 5,
    DEVICE_CONNECTED    = 6,
    DEVICE_DISCONNECTED = 7,
};

struct GameContext {
    GameState state                = GameState::LOBBY;
    uint8_t   round                = 0;
    uint8_t   round_wins[2]        = {};
    uint8_t   knockoffs[2]         = {};
    uint8_t   countdown_remaining  = 0;
    uint8_t   cars_eliminated      = 0;   // bitmask: bit N = car slot N knocked out this round
    uint8_t   last_knockoff_car_id = 0xFF;
};

struct DriveCommand {
    int8_t throttle = 0;  // -127..127: positive = forward
    int8_t steering = 0;  // -127..127: positive = right
};

enum class DeviceType : uint8_t {
    CAR     = 1,
    CLIENT  = 2,
    REFEREE = 3,
    SERVER  = 4,
};
