// car/src/car_node.h
#pragma once
#include <cstdint>
#include <cstring>
#include "motor_driver.h"
#include "knockoff_detector.h"
#include "espnow_transport.h"
#include "game_state.h"
#include "event_bus.h"
#include "protocol.h"

class CarNode {
public:
    CarNode(MotorDriver& driver, KnockoffDetector& detector);

    // Call once in setup().
    void begin();

    // Call every loop(). now_ms = millis().
    void tick(uint32_t now_ms);

    // Event bus for game state events received from server.
    EventBus<GameEvent, GameContext>& events() { return _bus; }

    const GameContext& gameContext() const { return _ctx; }

private:
    void onReceive(const uint8_t* mac, const uint8_t* data, int len);
    void onKnockoffTriggered();
    void sendPing(uint32_t now_ms);

    MotorDriver*      _driver;
    KnockoffDetector* _detector;

    EventBus<GameEvent, GameContext> _bus;
    GameContext  _ctx{};
    uint8_t      _assigned_id  = 0xFF;
    uint8_t      _server_mac[6]= {};
    bool         _paired       = false;
    uint8_t      _seq          = 0;

    uint32_t _last_drive_ms    = 0;
    uint32_t _last_ping_ms     = 0;

    static constexpr uint32_t PING_INTERVAL_MS = 1000;
};
