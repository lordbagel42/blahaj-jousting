// car/src/car_node.h
#pragma once
#include <cstdint>
#include <cstring>
#include "motor_driver.h"
#include "knockoff_detector.h"
#include "ultrasonic_sensor.h"
#include "espnow_transport.h"
#include "game_state.h"
#include "event_bus.h"
#include "protocol.h"

class CarNode {
public:
    // sonar is optional; pass nullptr to disable the distance cutoff.
    CarNode(MotorDriver& driver, KnockoffDetector& detector,
            UltrasonicSensor* sonar = nullptr);

    // Call once in setup().
    void begin();

    // Call every loop(). now_ms = millis().
    void tick(uint32_t now_ms);

    // Event bus for game state events received from server.
    EventBus<GameEvent, GameContext>& events() { return _bus; }

    const GameContext& gameContext() const { return _ctx; }

private:
    void onReceive(const uint8_t* mac, const uint8_t* data, int len, uint32_t now_ms);
    void onKnockoffTriggered();
    void sendPing();
    void sendHello();

    // True when the ultrasonic reads past its cutoff (car lifted off the
    // surface / off the edge). No sonar attached => never cuts.
    bool sonarCutoff() const { return _sonar && _sonar->beyondCutoff(); }

    MotorDriver*      _driver;
    KnockoffDetector* _detector;
    UltrasonicSensor* _sonar;

    EventBus<GameEvent, GameContext> _bus;
    GameContext  _ctx{};
    uint8_t      _assigned_id  = 0xFF;
    uint8_t      _server_mac[6]= {};
    bool         _paired       = false;
    uint8_t      _seq          = 0;

    int8_t   _last_throttle    = 0;
    int8_t   _last_steering    = 0;
    uint32_t _last_throttle_ms = 0;
    uint32_t _last_steering_ms = 0;
    uint32_t _last_ping_ms     = 0;
    uint32_t _last_hello_ms    = 0;

    // Bench motor test (TEST_DRIVE_CMD): drive directly until _test_until_ms,
    // bypassing pairing/RACING/sonar. Auto-stops if commands stop arriving.
    int8_t   _test_throttle    = 0;
    int8_t   _test_steering    = 0;
    uint32_t _test_until_ms    = 0;

    static constexpr uint32_t PING_INTERVAL_MS         = 1000;
    static constexpr uint32_t HELLO_INTERVAL_MS        = 2000;
    static constexpr uint32_t PAIRED_HELLO_INTERVAL_MS = 3000;
    static constexpr uint32_t TEST_DRIVE_HOLD_MS       = 1500;

#if defined(ESP8266)
    static constexpr uint8_t PIN_LED = 2;  // D1 Mini onboard LED (== LED_BUILTIN), active LOW
#endif
};
