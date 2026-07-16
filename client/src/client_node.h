#pragma once
#include <cstdint>
#include <cstring>
#include "input_provider.h"
#include "espnow_transport.h"
#include "game_state.h"
#include "event_bus.h"
#include "protocol.h"

class ClientNode {
public:
    // axis_mask: which axes this physical client reports (DRIVE_AXIS_BOTH by
    // default; use DRIVE_AXIS_THROTTLE/DRIVE_AXIS_STEERING when a single
    // joystick's two axes are split across separate client devices).
    explicit ClientNode(InputProvider& input, uint8_t axis_mask = DRIVE_AXIS_BOTH);

    void setInputProvider(InputProvider& input) { _input = &input; }
    void begin();
    void tick(uint32_t now_ms);

    EventBus<GameEvent, GameContext>& events() { return _bus; }
    const GameContext& gameContext() const { return _ctx; }

private:
    void onReceive(const uint8_t* mac, const uint8_t* data, int len);
    void sendDriveCommand(const DriveCommand& cmd);
    void sendPing();
    void sendHello();

    InputProvider*   _input;
    EventBus<GameEvent, GameContext> _bus;
    GameContext      _ctx{};
    uint8_t          _assigned_id   = 0xFF;
    uint8_t          _server_mac[6] = {};
    uint8_t          _car_mac[6]    = {};
    bool             _paired        = false;
    uint8_t          _seq           = 0;
    uint8_t          _axis_mask     = DRIVE_AXIS_BOTH;
    uint32_t         _last_drive_ms = 0;
    uint32_t         _last_ping_ms  = 0;
    uint32_t         _last_hello_ms = 0;

    static constexpr uint32_t DRIVE_HZ_INTERVAL_MS     = 33;   // ~30Hz
    static constexpr uint32_t PING_HZ_INTERVAL_MS      = 1000;
    static constexpr uint32_t HELLO_INTERVAL_MS        = 2000;
    static constexpr uint32_t PAIRED_HELLO_INTERVAL_MS = 3000;

#if defined(ESP8266)
    static constexpr uint8_t PIN_LED = 2;  // D1 Mini onboard LED (== LED_BUILTIN), active LOW
#endif
};
