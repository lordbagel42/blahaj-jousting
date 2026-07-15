# Blahaj Jousting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build three ESP32C3 firmware targets (server, client, car) communicating over ESP-NOW to run a shark-jousting game with server-side web management UI.

**Architecture:** Hybrid topology — client sends DriveCommand directly to car (~30Hz, 1–3ms latency); all game events route through server. A shared `blahaj_common` library defines the protocol, a template EventBus, and an ESP-NOW transport singleton. Each firmware target registers handlers/providers in `main.cpp`. Server hosts a WiFi AP + AsyncWebServer for referee UI and pairing.

**Tech Stack:** PlatformIO 6+, Arduino framework (espressif32), ESP-NOW, AsyncWebServer, Unity test framework (native host tests), C++17

---

## File Map

```
blahaj-jousting/
├── platformio.ini                          # workspace (lists all project dirs)
├── lib/
│   └── blahaj_common/
│       ├── library.json                    # PlatformIO lib manifest
│       ├── game_state.h                    # GameState, GameEvent, GameContext, DriveCommand, DeviceType
│       ├── protocol.h                      # MessageType, MessageHeader, all packed message structs
│       ├── event_bus.h                     # EventBus<EventType, ContextType> template
│       ├── espnow_transport.h              # EspNowTransport singleton declaration
│       └── espnow_transport.cpp            # EspNowTransport implementation (ESP-IDF calls)
├── test/
│   └── native/
│       ├── test_protocol/test_main.cpp     # struct sizes, header fields
│       ├── test_event_bus/test_main.cpp    # handler registration and firing
│       └── test_game_engine/test_main.cpp  # state machine transitions, win conditions
├── server/
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp                        # setup()/loop(), handler registration
│       ├── game_engine.h / .cpp            # state machine, knockoff counting, event dispatch
│       ├── pairing_manager.h / .cpp        # device registry, HELLO handling, pair assignment
│       ├── web_ui.h / .cpp                 # AsyncWebServer routes, JSON API
│       └── web_ui_html.h                   # const char* UI_HTML = R"(...)" — single-file UI
├── car/
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp                        # driver/pin wiring, setup()/loop()
│       ├── motor_driver.h                  # abstract MotorDriver interface
│       ├── drv8833_driver.h / .cpp         # DRV8833 two-channel PWM driver
│       ├── knockoff_detector.h / .cpp      # debounced pin → single-shot event
│       └── car_node.h / .cpp              # ESP-NOW, watchdog, lockout, reports to server
└── client/
    ├── platformio.ini
    └── src/
        ├── main.cpp                        # input/handler wiring, setup()/loop()
        ├── input_provider.h                # abstract InputProvider interface
        ├── joystick_input.h / .cpp         # analog X/Y ADC → DriveCommand
        ├── button_input.h / .cpp           # 4-button WASD → DriveCommand
        └── client_node.h / .cpp           # ESP-NOW, 30Hz drive loop, game state lockout
```

---

## Phase 1: Foundation — Common Library & Build System

### Task 1: PlatformIO workspace and build system

**Files:**
- Create: `platformio.ini` (workspace root)
- Create: `lib/blahaj_common/library.json`
- Create: `server/platformio.ini`
- Create: `car/platformio.ini`
- Create: `client/platformio.ini`

- [ ] **Step 1: Create root workspace platformio.ini**

```ini
; platformio.ini  (root — workspace only, no env here)
[platformio]
projects_dir = server, car, client

[common_esp32c3]
platform = espressif32
board = lolin_c3_mini
framework = arduino
monitor_speed = 115200
build_flags =
    -std=gnu++17
    -D ESPNOW_CHANNEL=1
    -D ROUNDS_TO_WIN=2
    -D DEVICE_ID=1
lib_extra_dirs = ../lib
```

- [ ] **Step 2: Create blahaj_common library manifest**

```json
{
  "name": "blahaj_common",
  "version": "1.0.0",
  "description": "Shared protocol, event bus, and ESP-NOW transport for blahaj jousting",
  "frameworks": ["arduino"],
  "platforms": ["espressif32", "native"]
}
```

- [ ] **Step 3: Create server/platformio.ini**

```ini
[env:server]
extends = ../common_esp32c3
lib_deps =
    mathieucarbou/ESPAsyncWebServer @ ^3.6.0
build_flags =
    ${common_esp32c3.build_flags}
    -D ROLE_SERVER

[env:native_test]
platform = native
test_framework = unity
build_flags = -std=gnu++17
lib_extra_dirs = ../lib
```

- [ ] **Step 4: Create car/platformio.ini**

```ini
[env:car]
extends = ../common_esp32c3
build_flags =
    ${common_esp32c3.build_flags}
    -D ROLE_CAR
    -D KNOCKOFF_LOCKOUT_MS=3000
    -D WATCHDOG_TIMEOUT_MS=500
```

- [ ] **Step 5: Create client/platformio.ini**

```ini
[env:client]
extends = ../common_esp32c3
build_flags =
    ${common_esp32c3.build_flags}
    -D ROLE_CLIENT
    -D DRIVE_INTERVAL_MS=33
```

- [ ] **Step 6: Create placeholder source files so PlatformIO can compile**

Create `server/src/main.cpp`, `car/src/main.cpp`, `client/src/main.cpp` — each with:

```cpp
#include <Arduino.h>
void setup() {}
void loop() {}
```

- [ ] **Step 7: Verify all three targets compile**

```bash
cd server && pio run -e server
cd ../car && pio run -e car
cd ../client && pio run -e client
```

Expected: `SUCCESS` for all three (no errors, only possible warnings about empty main).

- [ ] **Step 8: Commit**

```bash
git add platformio.ini lib/ server/platformio.ini server/src/main.cpp \
        car/platformio.ini car/src/main.cpp client/platformio.ini client/src/main.cpp
git commit -m "feat: PlatformIO workspace and build system for all three targets"
```

---

### Task 2: Protocol and game state definitions

**Files:**
- Create: `lib/blahaj_common/game_state.h`
- Create: `lib/blahaj_common/protocol.h`
- Create: `test/native/test_protocol/test_main.cpp`

- [ ] **Step 1: Create game_state.h**

```cpp
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
    uint8_t   round_wins[3]        = {};
    uint8_t   knockoffs[3]         = {};
    uint8_t   countdown_remaining  = 0;
    uint8_t   cars_eliminated      = 0;   // bitmask: bit N = car slot N knocked out this round
    uint8_t   last_knockoff_car_id = 0xFF;
};

struct DriveCommand {
    int8_t throttle = 0;  // -127..127: positive = forward
    int8_t steering = 0;  // -127..127: positive = right
};

enum class DeviceType : uint8_t {
    CAR    = 1,
    CLIENT = 2,
};
```

- [ ] **Step 2: Create protocol.h**

```cpp
// lib/blahaj_common/protocol.h
#pragma once
#include <cstdint>
#include "game_state.h"

static constexpr uint8_t BROADCAST_ID = 0xFF;
static constexpr uint8_t ESP_NOW_BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

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
} __attribute__((packed));

struct PingMsg {
    MessageHeader header;
} __attribute__((packed));
```

- [ ] **Step 3: Write native protocol tests**

```cpp
// test/native/test_protocol/test_main.cpp
#include <unity.h>
#include "protocol.h"

void test_message_header_size() {
    TEST_ASSERT_EQUAL(3, sizeof(MessageHeader));
}

void test_hello_msg_size() {
    // header(3) + device_type(1) + device_id(1) = 5
    TEST_ASSERT_EQUAL(5, sizeof(HelloMsg));
}

void test_hello_ack_size() {
    // header(3) + assigned_id(1) + partner_mac(6) = 10
    TEST_ASSERT_EQUAL(10, sizeof(HelloAckMsg));
}

void test_drive_cmd_size() {
    // header(3) + throttle(1) + steering(1) = 5
    TEST_ASSERT_EQUAL(5, sizeof(DriveCmdMsg));
}

void test_game_state_broadcast_fits_espnow() {
    TEST_ASSERT_LESS_THAN(250, sizeof(GameStateBroadcastMsg));
}

void test_header_fields() {
    HelloMsg msg{};
    msg.header.type   = MessageType::HELLO;
    msg.header.src_id = 2;
    msg.header.seq    = 42;
    msg.device_type   = DeviceType::CAR;
    msg.device_id     = 1;

    TEST_ASSERT_EQUAL((uint8_t)MessageType::HELLO, (uint8_t)msg.header.type);
    TEST_ASSERT_EQUAL(2, msg.header.src_id);
    TEST_ASSERT_EQUAL(42, msg.header.seq);
    TEST_ASSERT_EQUAL((uint8_t)DeviceType::CAR, (uint8_t)msg.device_type);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_message_header_size);
    RUN_TEST(test_hello_msg_size);
    RUN_TEST(test_hello_ack_size);
    RUN_TEST(test_drive_cmd_size);
    RUN_TEST(test_game_state_broadcast_fits_espnow);
    RUN_TEST(test_header_fields);
    return UNITY_END();
}
```

- [ ] **Step 4: Run tests — expect PASS**

```bash
cd server && pio test -e native_test --filter test_protocol
```

Expected output:
```
test/native/test_protocol/test_main.cpp:6 test_message_header_size      PASSED
...
6 Tests 0 Failures 0 Ignored
OK
```

- [ ] **Step 5: Commit**

```bash
git add lib/blahaj_common/game_state.h lib/blahaj_common/protocol.h \
        test/native/test_protocol/
git commit -m "feat: protocol and game state definitions with native tests"
```

---

### Task 3: EventBus

**Files:**
- Create: `lib/blahaj_common/event_bus.h`
- Create: `test/native/test_event_bus/test_main.cpp`

- [ ] **Step 1: Create event_bus.h**

```cpp
// lib/blahaj_common/event_bus.h
#pragma once
#include <functional>
#include <map>
#include <vector>

template <typename EventType, typename ContextType>
class EventBus {
public:
    using Handler = std::function<void(const ContextType&)>;

    void on(EventType event, Handler handler) {
        _handlers[static_cast<int>(event)].push_back(std::move(handler));
    }

    void emit(EventType event, const ContextType& ctx) const {
        auto it = _handlers.find(static_cast<int>(event));
        if (it == _handlers.end()) return;
        for (const auto& h : it->second) h(ctx);
    }

    int handlerCount(EventType event) const {
        auto it = _handlers.find(static_cast<int>(event));
        return it == _handlers.end() ? 0 : static_cast<int>(it->second.size());
    }

private:
    std::map<int, std::vector<Handler>> _handlers;
};
```

- [ ] **Step 2: Write EventBus tests**

```cpp
// test/native/test_event_bus/test_main.cpp
#include <unity.h>
#include "event_bus.h"
#include "game_state.h"

using Bus = EventBus<GameEvent, GameContext>;

void test_no_handlers_emits_safely() {
    Bus bus;
    GameContext ctx{};
    // Should not crash
    bus.emit(GameEvent::MATCH_START, ctx);
    TEST_PASS();
}

void test_handler_called_on_emit() {
    Bus bus;
    bool called = false;
    bus.on(GameEvent::MATCH_START, [&](const GameContext&) { called = true; });

    GameContext ctx{};
    bus.emit(GameEvent::MATCH_START, ctx);

    TEST_ASSERT_TRUE(called);
}

void test_handler_not_called_for_other_event() {
    Bus bus;
    bool called = false;
    bus.on(GameEvent::MATCH_START, [&](const GameContext&) { called = true; });

    GameContext ctx{};
    bus.emit(GameEvent::KNOCKOFF, ctx);

    TEST_ASSERT_FALSE(called);
}

void test_multiple_handlers_all_called() {
    Bus bus;
    int count = 0;
    bus.on(GameEvent::KNOCKOFF, [&](const GameContext&) { count++; });
    bus.on(GameEvent::KNOCKOFF, [&](const GameContext&) { count++; });
    bus.on(GameEvent::KNOCKOFF, [&](const GameContext&) { count++; });

    GameContext ctx{};
    bus.emit(GameEvent::KNOCKOFF, ctx);

    TEST_ASSERT_EQUAL(3, count);
}

void test_context_passed_to_handler() {
    Bus bus;
    uint8_t received_round = 0;
    bus.on(GameEvent::ROUND_END, [&](const GameContext& ctx) {
        received_round = ctx.round;
    });

    GameContext ctx{};
    ctx.round = 7;
    bus.emit(GameEvent::ROUND_END, ctx);

    TEST_ASSERT_EQUAL(7, received_round);
}

void test_handler_count() {
    Bus bus;
    bus.on(GameEvent::MATCH_START, [](const GameContext&) {});
    bus.on(GameEvent::MATCH_START, [](const GameContext&) {});
    TEST_ASSERT_EQUAL(2, bus.handlerCount(GameEvent::MATCH_START));
    TEST_ASSERT_EQUAL(0, bus.handlerCount(GameEvent::KNOCKOFF));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_handlers_emits_safely);
    RUN_TEST(test_handler_called_on_emit);
    RUN_TEST(test_handler_not_called_for_other_event);
    RUN_TEST(test_multiple_handlers_all_called);
    RUN_TEST(test_context_passed_to_handler);
    RUN_TEST(test_handler_count);
    return UNITY_END();
}
```

- [ ] **Step 3: Run tests — expect PASS**

```bash
cd server && pio test -e native_test --filter test_event_bus
```

Expected: `6 Tests 0 Failures 0 Ignored`

- [ ] **Step 4: Commit**

```bash
git add lib/blahaj_common/event_bus.h test/native/test_event_bus/
git commit -m "feat: template EventBus with native tests"
```

---

### Task 4: ESP-NOW transport wrapper

**Files:**
- Create: `lib/blahaj_common/espnow_transport.h`
- Create: `lib/blahaj_common/espnow_transport.cpp`

Note: This file is excluded from native tests — it calls ESP-IDF APIs unavailable on host. Logic tested indirectly through GameEngine tests that mock the send function.

- [ ] **Step 1: Create espnow_transport.h**

```cpp
// lib/blahaj_common/espnow_transport.h
#pragma once
#include <cstdint>
#include <functional>
#include "protocol.h"

class EspNowTransport {
public:
    using ReceiveCallback = std::function<void(const uint8_t* mac, const uint8_t* data, int len)>;
    using SendCallback    = std::function<void(const uint8_t* mac, bool success)>;

    static EspNowTransport& instance();

    // Call once in setup(), after WiFi.mode(WIFI_AP_STA) or WIFI_STA.
    void begin(uint8_t channel);

    // Send to a specific peer. Peer must be added first with addPeer().
    bool send(const uint8_t* mac, const void* data, size_t len);

    // Send to ESP-NOW broadcast address (FF:FF:FF:FF:FF:FF).
    // No peer registration needed for broadcast.
    bool sendBroadcast(const void* data, size_t len);

    // Register a MAC as a peer so we can send to it.
    bool addPeer(const uint8_t* mac);

    void onReceive(ReceiveCallback cb) { _recv_cb = std::move(cb); }
    void onSend(SendCallback cb)       { _send_cb = std::move(cb); }

    // Returns this device's own MAC address.
    void getMac(uint8_t out_mac[6]);

private:
    EspNowTransport() = default;
    EspNowTransport(const EspNowTransport&) = delete;

    static void recvCallback(const esp_now_recv_info_t* info, const uint8_t* data, int len);
    static void sendCallback(const uint8_t* mac, esp_now_send_status_t status);

    ReceiveCallback _recv_cb;
    SendCallback    _send_cb;
    bool            _broadcast_peer_added = false;
};
```

- [ ] **Step 2: Create espnow_transport.cpp**

```cpp
// lib/blahaj_common/espnow_transport.cpp
#include "espnow_transport.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

EspNowTransport& EspNowTransport::instance() {
    static EspNowTransport inst;
    return inst;
}

void EspNowTransport::begin(uint8_t channel) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) return;

    esp_now_register_recv_cb(recvCallback);
    esp_now_register_send_cb(sendCallback);
}

bool EspNowTransport::send(const uint8_t* mac, const void* data, size_t len) {
    return esp_now_send(mac, static_cast<const uint8_t*>(data), len) == ESP_OK;
}

bool EspNowTransport::sendBroadcast(const void* data, size_t len) {
    if (!_broadcast_peer_added) {
        esp_now_peer_info_t peer{};
        memset(peer.peer_addr, 0xFF, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
        _broadcast_peer_added = true;
    }
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    return esp_now_send(bcast, static_cast<const uint8_t*>(data), len) == ESP_OK;
}

bool EspNowTransport::addPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

void EspNowTransport::getMac(uint8_t out_mac[6]) {
    esp_wifi_get_mac(WIFI_IF_STA, out_mac);
}

void EspNowTransport::recvCallback(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    auto& cb = instance()._recv_cb;
    if (cb) cb(info->src_addr, data, len);
}

void EspNowTransport::sendCallback(const uint8_t* mac, esp_now_send_status_t status) {
    auto& cb = instance()._send_cb;
    if (cb) cb(mac, status == ESP_NOW_SEND_SUCCESS);
}
```

- [ ] **Step 3: Verify server target still compiles (transport will be linked but not called yet)**

```bash
cd server && pio run -e server
```

Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add lib/blahaj_common/espnow_transport.h lib/blahaj_common/espnow_transport.cpp
git commit -m "feat: ESP-NOW transport singleton wrapper"
```

---

## Phase 2: Server

### Task 5: GameEngine — state machine and event dispatch

**Files:**
- Create: `server/src/game_engine.h`
- Create: `server/src/game_engine.cpp`
- Create: `test/native/test_game_engine/test_main.cpp`

- [ ] **Step 1: Create game_engine.h**

```cpp
// server/src/game_engine.h
#pragma once
#include <cstdint>
#include "game_state.h"
#include "event_bus.h"

class GameEngine {
public:
    using Bus = EventBus<GameEvent, GameContext>;

    static constexpr int  CARS_PER_ROUND       = 3;
    static constexpr int  COUNTDOWN_SECONDS     = 3;
    static constexpr uint32_t ROUND_END_PAUSE_MS = 3000;

    explicit GameEngine(Bus& bus);

    // Called every loop iteration. now_ms = millis().
    void tick(uint32_t now_ms);

    // Referee actions — no-ops if state is wrong.
    void startMatch();
    void endRound();

    // Called by pairing manager when a knockoff is confirmed.
    void onKnockoff(uint8_t car_slot);

    const GameContext& context() const { return _ctx; }

private:
    void transitionTo(GameState next);
    void checkRoundEnd();
    int  eliminatedCount() const;

    Bus&        _bus;
    GameContext _ctx{};
    uint32_t    _last_tick_ms         = 0;
    uint32_t    _round_end_entered_ms = 0;
    int         _last_countdown_val   = -1;
};
```

- [ ] **Step 2: Create game_engine.cpp**

```cpp
// server/src/game_engine.cpp
#include "game_engine.h"
#include <cstring>

GameEngine::GameEngine(Bus& bus) : _bus(bus) {}

void GameEngine::tick(uint32_t now_ms) {
    switch (_ctx.state) {
        case GameState::COUNTDOWN: {
            uint32_t elapsed = now_ms - _last_tick_ms;
            int secs_remaining = COUNTDOWN_SECONDS - static_cast<int>(elapsed / 1000);
            if (secs_remaining < 0) secs_remaining = 0;
            _ctx.countdown_remaining = static_cast<uint8_t>(secs_remaining);

            if (secs_remaining != _last_countdown_val) {
                _last_countdown_val = secs_remaining;
                _bus.emit(GameEvent::COUNTDOWN_TICK, _ctx);
            }

            if (elapsed >= static_cast<uint32_t>(COUNTDOWN_SECONDS) * 1000) {
                transitionTo(GameState::RACING);
            }
            break;
        }
        case GameState::ROUND_END: {
            if (now_ms - _round_end_entered_ms >= ROUND_END_PAUSE_MS) {
                transitionTo(GameState::LOBBY);
            }
            break;
        }
        default:
            break;
    }
}

void GameEngine::startMatch() {
    if (_ctx.state != GameState::LOBBY) return;
    _last_tick_ms       = 0;  // caller must pass now_ms; set via startMatch(uint32_t)
    transitionTo(GameState::COUNTDOWN);
}

void GameEngine::endRound() {
    if (_ctx.state != GameState::RACING) return;
    transitionTo(GameState::ROUND_END);
}

void GameEngine::onKnockoff(uint8_t car_slot) {
    if (_ctx.state != GameState::RACING) return;
    if (car_slot >= CARS_PER_ROUND) return;
    if (_ctx.cars_eliminated & (1 << car_slot)) return;  // already out

    _ctx.knockoffs[car_slot]++;
    _ctx.cars_eliminated |= (1 << car_slot);
    _ctx.last_knockoff_car_id = car_slot;
    _bus.emit(GameEvent::KNOCKOFF, _ctx);

    checkRoundEnd();
}

void GameEngine::transitionTo(GameState next) {
    _ctx.state = next;
    _bus.emit(GameEvent::STATE_CHANGED, _ctx);

    switch (next) {
        case GameState::COUNTDOWN:
            _ctx.countdown_remaining = COUNTDOWN_SECONDS;
            _last_countdown_val = -1;
            _bus.emit(GameEvent::MATCH_START, _ctx);
            break;

        case GameState::RACING:
            _ctx.countdown_remaining = 0;
            break;

        case GameState::ROUND_END: {
            // Award round win to the surviving car.
            int survivor = -1;
            for (int i = 0; i < CARS_PER_ROUND; i++) {
                if (!(_ctx.cars_eliminated & (1 << i))) {
                    survivor = i;
                    break;
                }
            }
            if (survivor >= 0) _ctx.round_wins[survivor]++;
            _ctx.round++;
            _round_end_entered_ms = 0;  // caller sets via tick
            _bus.emit(GameEvent::ROUND_END, _ctx);

            // Check match win.
            for (int i = 0; i < CARS_PER_ROUND; i++) {
                if (_ctx.round_wins[i] >= ROUNDS_TO_WIN) {
                    _bus.emit(GameEvent::MATCH_END, _ctx);
                    break;
                }
            }
            break;
        }

        case GameState::LOBBY:
            // Reset per-round state, keep match scores.
            memset(_ctx.knockoffs, 0, sizeof(_ctx.knockoffs));
            _ctx.cars_eliminated = 0;
            _ctx.last_knockoff_car_id = 0xFF;
            break;
    }
}

void GameEngine::checkRoundEnd() {
    if (eliminatedCount() >= CARS_PER_ROUND - 1) {
        transitionTo(GameState::ROUND_END);
    }
}

int GameEngine::eliminatedCount() const {
    int n = 0;
    for (int i = 0; i < CARS_PER_ROUND; i++) {
        if (_ctx.cars_eliminated & (1 << i)) n++;
    }
    return n;
}
```

Note: `startMatch()` needs `now_ms` to start the countdown timer. Update the signature:

```cpp
// In game_engine.h, replace:
void startMatch();
// with:
void startMatch(uint32_t now_ms);

// In game_engine.cpp, replace:
void GameEngine::startMatch() {
    if (_ctx.state != GameState::LOBBY) return;
    _last_tick_ms = 0;
    transitionTo(GameState::COUNTDOWN);
}
// with:
void GameEngine::startMatch(uint32_t now_ms) {
    if (_ctx.state != GameState::LOBBY) return;
    _last_tick_ms = now_ms;
    transitionTo(GameState::COUNTDOWN);
}

// Similarly endRound needs no changes, but ROUND_END_PAUSE_MS needs now_ms:
// In transitionTo(ROUND_END):
//   _round_end_entered_ms = 0;   <-- replace with:
//   _round_end_entered_ms = now_ms;  // but we don't have now_ms here...
```

Simplest fix: pass `now_ms` to `transitionTo`:

```cpp
// game_engine.h: change private method to:
void transitionTo(GameState next, uint32_t now_ms = 0);

// game_engine.cpp: change ROUND_END case:
_round_end_entered_ms = now_ms;

// tick() calls: transitionTo(GameState::RACING, now_ms) and transitionTo(GameState::ROUND_END, now_ms)
// startMatch calls: transitionTo(GameState::COUNTDOWN, now_ms)
```

Apply these corrections before writing tests.

- [ ] **Step 3: Write GameEngine native tests**

```cpp
// test/native/test_game_engine/test_main.cpp
#include <unity.h>
#include "game_engine.h"

using Bus = EventBus<GameEvent, GameContext>;

void test_initial_state_is_lobby() {
    Bus bus;
    GameEngine engine(bus);
    TEST_ASSERT_EQUAL((uint8_t)GameState::LOBBY, (uint8_t)engine.context().state);
}

void test_start_match_transitions_to_countdown() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    TEST_ASSERT_EQUAL((uint8_t)GameState::COUNTDOWN, (uint8_t)engine.context().state);
}

void test_start_match_noop_when_not_lobby() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.startMatch(0);  // second call should be ignored
    TEST_ASSERT_EQUAL((uint8_t)GameState::COUNTDOWN, (uint8_t)engine.context().state);
}

void test_countdown_completes_and_transitions_to_racing() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    // Tick past 3 seconds
    engine.tick(3001);
    TEST_ASSERT_EQUAL((uint8_t)GameState::RACING, (uint8_t)engine.context().state);
}

void test_knockoff_eliminates_car() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.tick(3001);  // advance to RACING

    engine.onKnockoff(0);
    TEST_ASSERT_EQUAL(1, engine.context().knockoffs[0]);
    TEST_ASSERT_TRUE(engine.context().cars_eliminated & 0x01);
}

void test_two_knockoffs_end_round() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.tick(3001);

    engine.onKnockoff(0);
    engine.onKnockoff(1);

    TEST_ASSERT_EQUAL((uint8_t)GameState::ROUND_END, (uint8_t)engine.context().state);
}

void test_survivor_gets_round_win() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.tick(3001);

    engine.onKnockoff(0);
    engine.onKnockoff(1);

    // Car slot 2 survived
    TEST_ASSERT_EQUAL(1, engine.context().round_wins[2]);
    TEST_ASSERT_EQUAL(0, engine.context().round_wins[0]);
    TEST_ASSERT_EQUAL(0, engine.context().round_wins[1]);
}

void test_duplicate_knockoff_ignored() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.tick(3001);

    engine.onKnockoff(0);
    engine.onKnockoff(0);  // duplicate — should not double-count

    TEST_ASSERT_EQUAL(1, engine.context().knockoffs[0]);
}

void test_match_end_fires_after_rounds_to_win() {
    Bus bus;
    bool match_ended = false;
    bus.on(GameEvent::MATCH_END, [&](const GameContext&) { match_ended = true; });

    GameEngine engine(bus);
    // Win ROUNDS_TO_WIN rounds for car 2
    for (int r = 0; r < ROUNDS_TO_WIN; r++) {
        engine.startMatch(0);
        engine.tick(3001);
        engine.onKnockoff(0);
        engine.onKnockoff(1);
        engine.tick(10000);  // advance past round end pause
    }
    TEST_ASSERT_TRUE(match_ended);
}

void test_knockoff_ignored_when_not_racing() {
    Bus bus;
    GameEngine engine(bus);
    engine.onKnockoff(0);  // LOBBY state — should be ignored
    TEST_ASSERT_EQUAL(0, engine.context().knockoffs[0]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_lobby);
    RUN_TEST(test_start_match_transitions_to_countdown);
    RUN_TEST(test_start_match_noop_when_not_lobby);
    RUN_TEST(test_countdown_completes_and_transitions_to_racing);
    RUN_TEST(test_knockoff_eliminates_car);
    RUN_TEST(test_two_knockoffs_end_round);
    RUN_TEST(test_survivor_gets_round_win);
    RUN_TEST(test_duplicate_knockoff_ignored);
    RUN_TEST(test_match_end_fires_after_rounds_to_win);
    RUN_TEST(test_knockoff_ignored_when_not_racing);
    return UNITY_END();
}
```

- [ ] **Step 4: Add `server/src/` to the native test lib path and run tests**

In `server/platformio.ini`, under `[env:native_test]`, add:
```ini
lib_extra_dirs = ../lib, src
```

Then run:
```bash
cd server && pio test -e native_test --filter test_game_engine
```

Expected: `10 Tests 0 Failures 0 Ignored`

- [ ] **Step 5: Commit**

```bash
git add server/src/game_engine.h server/src/game_engine.cpp \
        test/native/test_game_engine/ server/platformio.ini
git commit -m "feat: GameEngine state machine with countdown, knockoffs, round/match win logic"
```

---

### Task 6: PairingManager

**Files:**
- Create: `server/src/pairing_manager.h`
- Create: `server/src/pairing_manager.cpp`

- [ ] **Step 1: Create pairing_manager.h**

```cpp
// server/src/pairing_manager.h
#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include "protocol.h"
#include "espnow_transport.h"

struct DeviceInfo {
    uint8_t    mac[6]       = {};
    DeviceType type         = DeviceType::CAR;
    uint8_t    device_id    = 0;
    uint8_t    assigned_id  = 0xFF;  // slot index once assigned
    uint8_t    partner_slot = 0xFF;  // index into other list (cars/clients)
    uint32_t   last_seen_ms = 0;
    bool       paired       = false;

    bool macEquals(const uint8_t* other) const {
        return memcmp(mac, other, 6) == 0;
    }
};

class PairingManager {
public:
    // Called when a HELLO broadcast arrives.
    void onHello(const uint8_t* src_mac, const HelloMsg& msg, uint32_t now_ms);

    // Assign client at client_idx to car at car_idx (0-based into their respective lists).
    // Returns false if indices are out of range or already paired differently.
    bool assignPair(uint8_t car_idx, uint8_t client_idx);

    // Send HELLO_ACK to all paired devices.
    void confirmPairings(EspNowTransport& transport, uint8_t server_seq);

    void updateLastSeen(const uint8_t* mac, uint32_t now_ms);

    const std::vector<DeviceInfo>& cars()    const { return _cars;    }
    const std::vector<DeviceInfo>& clients() const { return _clients; }

    bool allPaired() const;

private:
    DeviceInfo* findByMac(const uint8_t* mac);

    std::vector<DeviceInfo> _cars;
    std::vector<DeviceInfo> _clients;
};
```

- [ ] **Step 2: Create pairing_manager.cpp**

```cpp
// server/src/pairing_manager.cpp
#include "pairing_manager.h"
#include <algorithm>

void PairingManager::onHello(const uint8_t* src_mac, const HelloMsg& msg, uint32_t now_ms) {
    auto& list = (msg.device_type == DeviceType::CAR) ? _cars : _clients;

    // De-duplicate by MAC.
    for (auto& d : list) {
        if (d.macEquals(src_mac)) {
            d.last_seen_ms = now_ms;
            return;
        }
    }

    DeviceInfo info;
    memcpy(info.mac, src_mac, 6);
    info.type        = msg.device_type;
    info.device_id   = msg.device_id;
    info.last_seen_ms = now_ms;
    list.push_back(info);
}

bool PairingManager::assignPair(uint8_t car_idx, uint8_t client_idx) {
    if (car_idx >= _cars.size() || client_idx >= _clients.size()) return false;
    _cars[car_idx].partner_slot    = client_idx;
    _clients[client_idx].partner_slot = car_idx;
    _cars[car_idx].assigned_id     = car_idx;
    _clients[client_idx].assigned_id  = client_idx;
    _cars[car_idx].paired          = true;
    _clients[client_idx].paired       = true;
    return true;
}

void PairingManager::confirmPairings(EspNowTransport& transport, uint8_t server_seq) {
    for (uint8_t i = 0; i < _cars.size(); i++) {
        auto& car = _cars[i];
        if (!car.paired) continue;
        auto& client = _clients[car.partner_slot];

        transport.addPeer(car.mac);
        transport.addPeer(client.mac);

        HelloAckMsg ack_car{};
        ack_car.header.type   = MessageType::HELLO_ACK;
        ack_car.header.src_id = 0;  // server
        ack_car.header.seq    = server_seq;
        ack_car.assigned_id   = car.assigned_id;
        memcpy(ack_car.partner_mac, client.mac, 6);
        transport.send(car.mac, &ack_car, sizeof(ack_car));

        HelloAckMsg ack_client{};
        ack_client.header.type   = MessageType::HELLO_ACK;
        ack_client.header.src_id = 0;
        ack_client.header.seq    = server_seq;
        ack_client.assigned_id   = client.assigned_id;
        memcpy(ack_client.partner_mac, car.mac, 6);
        transport.send(client.mac, &ack_client, sizeof(ack_client));
    }
}

void PairingManager::updateLastSeen(const uint8_t* mac, uint32_t now_ms) {
    auto* d = findByMac(mac);
    if (d) d->last_seen_ms = now_ms;
}

bool PairingManager::allPaired() const {
    if (_cars.empty() || _clients.empty()) return false;
    for (const auto& c : _cars)    if (!c.paired) return false;
    for (const auto& c : _clients) if (!c.paired) return false;
    return true;
}

DeviceInfo* PairingManager::findByMac(const uint8_t* mac) {
    for (auto& d : _cars)    if (d.macEquals(mac)) return &d;
    for (auto& d : _clients) if (d.macEquals(mac)) return &d;
    return nullptr;
}
```

- [ ] **Step 3: Verify server compiles**

```bash
cd server && pio run -e server
```

Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add server/src/pairing_manager.h server/src/pairing_manager.cpp
git commit -m "feat: PairingManager — device registry and pair assignment"
```

---

### Task 7: Server web UI backend

**Files:**
- Create: `server/src/web_ui.h`
- Create: `server/src/web_ui.cpp`
- Create: `server/src/web_ui_html.h`

- [ ] **Step 1: Create web_ui_html.h — the single-file management UI**

```cpp
// server/src/web_ui_html.h
#pragma once

static const char UI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Blahaj Jousting</title>
<style>
  body{font-family:monospace;background:#111;color:#eee;max-width:600px;margin:0 auto;padding:1rem}
  h1{color:#f90;margin:0 0 1rem}
  .card{background:#1e1e1e;border-radius:8px;padding:1rem;margin:.5rem 0}
  .dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:.5rem}
  .green{background:#0f0}.red{background:#f00}.gray{background:#555}
  select{background:#333;color:#eee;border:1px solid #555;padding:.3rem;border-radius:4px}
  button{background:#f90;color:#111;border:none;padding:.5rem 1.2rem;border-radius:4px;
         font-weight:bold;cursor:pointer;margin:.3rem}
  button:disabled{background:#555;color:#888;cursor:not-allowed}
  button.danger{background:#c00;color:#fff}
  #status{margin-top:.5rem;font-size:.85rem;color:#888}
  table{width:100%;border-collapse:collapse}
  td{padding:.4rem .3rem;vertical-align:middle}
</style>
</head>
<body>
<h1>&#x1F988; Blahaj Jousting</h1>

<div class="card">
  <b>Devices</b>
  <table id="deviceTable"><tr><td>Scanning...</td></tr></table>
</div>

<div class="card">
  <b>Pairings</b>
  <div id="pairings">No cars discovered yet.</div>
  <button id="confirmBtn" disabled onclick="confirmPairings()">Confirm Pairings</button>
</div>

<div class="card">
  <b>Match Control</b>
  <button id="startBtn" disabled onclick="startMatch()">Start Match</button>
  <button id="endBtn"   class="danger" disabled onclick="endRound()">End Round</button>
  <div id="matchState">State: —</div>
  <div id="scores"></div>
</div>

<div id="status"></div>

<script>
let lastStatus = null;

async function poll() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    render(d);
    lastStatus = d;
    document.getElementById('status').textContent = 'Updated ' + new Date().toLocaleTimeString();
  } catch(e) {
    document.getElementById('status').textContent = 'Connection error';
  }
}

function dot(connected) {
  return `<span class="dot ${connected ? 'green' : 'red'}"></span>`;
}

function macStr(mac) {
  return mac.map(b => b.toString(16).padStart(2,'0')).join(':');
}

function render(d) {
  // Device table
  const rows = [...d.cars, ...d.clients].map(dev => `
    <tr>
      <td>${dot(dev.connected)}${dev.type} #${dev.device_id}</td>
      <td style="color:#888;font-size:.8rem">${macStr(dev.mac)}</td>
      <td>${dev.paired ? '&#x2714; paired' : '<span style="color:#f90">unpaired</span>'}</td>
    </tr>`).join('');
  document.getElementById('deviceTable').innerHTML = rows || '<tr><td>No devices yet</td></tr>';

  // Pairing selects — one per car
  if (d.cars.length > 0) {
    const selects = d.cars.map((car, i) => {
      const opts = d.clients.map((c, ci) =>
        `<option value="${ci}" ${car.partner_slot===ci?'selected':''}>Client #${c.device_id} (${macStr(c.mac).slice(0,8)}…)</option>`
      ).join('');
      return `<div style="margin:.3rem 0">Car #${car.device_id}: <select id="pair_${i}">${opts}</select></div>`;
    }).join('');
    document.getElementById('pairings').innerHTML = selects;
    document.getElementById('confirmBtn').disabled = d.clients.length === 0;
  }

  // Match state
  const stateNames = ['LOBBY','COUNTDOWN','RACING','ROUND_END'];
  document.getElementById('matchState').textContent = 'State: ' + (stateNames[d.game_state] || '?');
  document.getElementById('startBtn').disabled = d.game_state !== 0 || !d.all_paired;
  document.getElementById('endBtn').disabled   = d.game_state !== 2;

  // Scores
  if (d.round_wins) {
    const sc = d.cars.map((car, i) =>
      `Car #${car.device_id}: ${d.round_wins[i] || 0} wins`
    ).join(' &nbsp;|&nbsp; ');
    document.getElementById('scores').innerHTML = sc;
  }
}

async function confirmPairings() {
  const pairs = [];
  const cars = lastStatus?.cars || [];
  cars.forEach((_, i) => {
    const sel = document.getElementById('pair_' + i);
    if (sel) pairs.push({car: i, client: parseInt(sel.value)});
  });
  await fetch('/api/pair', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(pairs)});
  poll();
}

async function startMatch() {
  await fetch('/api/start', {method:'POST'});
  poll();
}

async function endRound() {
  await fetch('/api/end', {method:'POST'});
  poll();
}

poll();
setInterval(poll, 2000);
</script>
</body>
</html>
)rawhtml";
```

- [ ] **Step 2: Create web_ui.h**

```cpp
// server/src/web_ui.h
#pragma once
#include "pairing_manager.h"
#include "game_engine.h"
#include <ESPAsyncWebServer.h>

class WebUI {
public:
    WebUI(PairingManager& pairing, GameEngine& engine);

    void begin();  // Start the AsyncWebServer on port 80.

    // Called from main loop to forward pending actions.
    bool hasPendingStart()  { bool v = _pending_start;  _pending_start  = false; return v; }
    bool hasPendingEnd()    { bool v = _pending_end;    _pending_end    = false; return v; }
    bool hasPendingPair()   { bool v = _pending_pair;   _pending_pair   = false; return v; }

    struct PairAssignment { uint8_t car_idx; uint8_t client_idx; };
    const std::vector<PairAssignment>& pendingPairs() const { return _pending_pairs; }

private:
    PairingManager& _pairing;
    GameEngine&     _engine;
    AsyncWebServer  _server{80};

    bool _pending_start = false;
    bool _pending_end   = false;
    bool _pending_pair  = false;
    std::vector<PairAssignment> _pending_pairs;

    String buildStatusJson(uint32_t now_ms);
};
```

- [ ] **Step 3: Create web_ui.cpp**

```cpp
// server/src/web_ui.cpp
#include "web_ui.h"
#include "web_ui_html.h"
#include <Arduino.h>

WebUI::WebUI(PairingManager& pairing, GameEngine& engine)
    : _pairing(pairing), _engine(engine) {}

void WebUI::begin() {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", UI_HTML);
    });

    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        String json = buildStatusJson(millis());
        req->send(200, "application/json", json);
    });

    _server.on("/api/start", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _pending_start = true;
        req->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/end", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _pending_end = true;
        req->send(200, "application/json", "{\"ok\":true}");
    });

    AsyncCallbackJsonWebHandler* pairHandler =
        new AsyncCallbackJsonWebHandler("/api/pair",
            [this](AsyncWebServerRequest* req, JsonVariant& json) {
                _pending_pairs.clear();
                JsonArray arr = json.as<JsonArray>();
                for (JsonObject obj : arr) {
                    PairAssignment pa;
                    pa.car_idx    = obj["car"];
                    pa.client_idx = obj["client"];
                    _pending_pairs.push_back(pa);
                }
                _pending_pair = true;
                req->send(200, "application/json", "{\"ok\":true}");
            });
    _server.addHandler(pairHandler);

    _server.begin();
}

String WebUI::buildStatusJson(uint32_t now_ms) {
    const auto& cars    = _pairing.cars();
    const auto& clients = _pairing.clients();
    const auto& ctx     = _engine.context();

    String json = "{";
    json += "\"game_state\":" + String((uint8_t)ctx.state) + ",";
    json += "\"all_paired\":" + String(_pairing.allPaired() ? "true" : "false") + ",";

    // Round wins array
    json += "\"round_wins\":[";
    for (int i = 0; i < 3; i++) {
        if (i) json += ",";
        json += String(ctx.round_wins[i]);
    }
    json += "],";

    // Cars
    json += "\"cars\":[";
    for (size_t i = 0; i < cars.size(); i++) {
        if (i) json += ",";
        const auto& c = cars[i];
        bool connected = (now_ms - c.last_seen_ms) < 3000;
        json += "{\"device_id\":" + String(c.device_id);
        json += ",\"type\":\"CAR\"";
        json += ",\"mac\":[";
        for (int m = 0; m < 6; m++) { if(m) json+=","; json += String(c.mac[m]); }
        json += "]";
        json += ",\"paired\":" + String(c.paired ? "true" : "false");
        json += ",\"partner_slot\":" + String(c.partner_slot);
        json += ",\"connected\":" + String(connected ? "true" : "false");
        json += "}";
    }
    json += "],";

    // Clients
    json += "\"clients\":[";
    for (size_t i = 0; i < clients.size(); i++) {
        if (i) json += ",";
        const auto& c = clients[i];
        bool connected = (now_ms - c.last_seen_ms) < 3000;
        json += "{\"device_id\":" + String(c.device_id);
        json += ",\"type\":\"CLIENT\"";
        json += ",\"mac\":[";
        for (int m = 0; m < 6; m++) { if(m) json+=","; json += String(c.mac[m]); }
        json += "]";
        json += ",\"paired\":" + String(c.paired ? "true" : "false");
        json += ",\"partner_slot\":" + String(c.partner_slot);
        json += ",\"connected\":" + String(connected ? "true" : "false");
        json += "}";
    }
    json += "]}";

    return json;
}
```

Note: `AsyncCallbackJsonWebHandler` requires `ArduinoJson`. Add to `server/platformio.ini`:
```ini
lib_deps =
    mathieucarbou/ESPAsyncWebServer @ ^3.6.0
    bblanchon/ArduinoJson @ ^7.0.0
```

- [ ] **Step 4: Verify server compiles**

```bash
cd server && pio run -e server
```

Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add server/src/web_ui.h server/src/web_ui.cpp server/src/web_ui_html.h server/platformio.ini
git commit -m "feat: server web UI — device pairing, match control, live status"
```

---

### Task 8: Server main.cpp — wire everything together

**Files:**
- Modify: `server/src/main.cpp`

- [ ] **Step 1: Write server/src/main.cpp**

```cpp
// server/src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "espnow_transport.h"
#include "game_engine.h"
#include "pairing_manager.h"
#include "web_ui.h"
#include "protocol.h"

// ── Configuration ─────────────────────────────────────────────────────────────
#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 1
#endif
#ifndef ROUNDS_TO_WIN
#define ROUNDS_TO_WIN 2
#endif

// ── Globals ────────────────────────────────────────────────────────────────────
EventBus<GameEvent, GameContext> bus;
GameEngine    game(bus);
PairingManager pairing;
WebUI         ui(pairing, game);

static uint8_t seq = 0;

// ── Peripheral handlers — register yours here ──────────────────────────────────
// Example: flash an LED on match start.
// static void onMatchStart(const GameContext&) { digitalWrite(PIN_LED, HIGH); }

static void broadcastGameState() {
    const auto& ctx = game.context();
    GameStateBroadcastMsg msg{};
    msg.header.type    = MessageType::GAME_STATE_BROADCAST;
    msg.header.src_id  = 0;
    msg.header.seq     = seq++;
    msg.state          = ctx.state;
    msg.round          = ctx.round;
    memcpy(msg.round_wins, ctx.round_wins, 3);
    memcpy(msg.knockoffs,  ctx.knockoffs,  3);
    msg.countdown_remaining = ctx.countdown_remaining;
    msg.cars_eliminated     = ctx.cars_eliminated;

    auto& t = EspNowTransport::instance();
    for (const auto& car    : pairing.cars())    if (car.paired)    t.send(car.mac,    &msg, sizeof(msg));
    for (const auto& client : pairing.clients()) if (client.paired) t.send(client.mac, &msg, sizeof(msg));
}

// ── ESP-NOW receive ────────────────────────────────────────────────────────────
static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(MessageHeader)) return;
    const auto* hdr = reinterpret_cast<const MessageHeader*>(data);

    pairing.updateLastSeen(mac, millis());

    switch (hdr->type) {
        case MessageType::HELLO:
            if (len >= (int)sizeof(HelloMsg)) {
                pairing.onHello(mac, *reinterpret_cast<const HelloMsg*>(data), millis());
            }
            break;

        case MessageType::KNOCKOFF_EVENT:
            if (len >= (int)sizeof(KnockoffEventMsg)) {
                const auto* msg = reinterpret_cast<const KnockoffEventMsg*>(data);
                game.onKnockoff(msg->car_id);
                broadcastGameState();
            }
            break;

        case MessageType::PING: {
            PingMsg pong{};
            pong.header.type   = MessageType::PONG;
            pong.header.src_id = 0;
            pong.header.seq    = seq++;
            EspNowTransport::instance().addPeer(mac);
            EspNowTransport::instance().send(mac, &pong, sizeof(pong));
            break;
        }

        default: break;
    }
}

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Register event handlers.
    bus.on(GameEvent::STATE_CHANGED, [](const GameContext&) { broadcastGameState(); });
    bus.on(GameEvent::KNOCKOFF,      [](const GameContext&) { broadcastGameState(); });
    bus.on(GameEvent::COUNTDOWN_TICK,[](const GameContext&) { broadcastGameState(); });
    // ── Add your hardware handlers below ──────────────────────────────────────
    // bus.on(GameEvent::MATCH_START, onMatchStart);

    // WiFi: AP for management UI + STA for ESP-NOW.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("BlahajJousting", nullptr, ESPNOW_CHANNEL);

    auto& transport = EspNowTransport::instance();
    transport.begin(ESPNOW_CHANNEL);
    transport.onReceive(onReceive);

    ui.begin();

    Serial.println("Server ready. Connect to 'BlahajJousting' and open 192.168.4.1");
}

// ── Loop ───────────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // Process web UI actions.
    if (ui.hasPendingPair()) {
        for (const auto& pa : ui.pendingPairs()) {
            pairing.assignPair(pa.car_idx, pa.client_idx);
        }
        pairing.confirmPairings(EspNowTransport::instance(), seq++);
    }
    if (ui.hasPendingStart()) game.startMatch(now);
    if (ui.hasPendingEnd())   game.endRound();

    game.tick(now);
}
```

- [ ] **Step 2: Verify full server build**

```bash
cd server && pio run -e server
```

Expected: `SUCCESS`

- [ ] **Step 3: Commit**

```bash
git add server/src/main.cpp
git commit -m "feat: server main — WiFi AP, ESP-NOW receive, web UI integration"
```

---

## Phase 3: Car

### Task 9: MotorDriver interface and DRV8833 implementation

**Files:**
- Create: `car/src/motor_driver.h`
- Create: `car/src/drv8833_driver.h`
- Create: `car/src/drv8833_driver.cpp`

- [ ] **Step 1: Create motor_driver.h**

```cpp
// car/src/motor_driver.h
#pragma once
#include <cstdint>

class MotorDriver {
public:
    virtual ~MotorDriver() = default;

    // throttle: -127..127 (positive = forward)
    // steering: -127..127 (positive = right)
    virtual void drive(int8_t throttle, int8_t steering) = 0;
    virtual void stop() = 0;
};
```

- [ ] **Step 2: Create drv8833_driver.h**

```cpp
// car/src/drv8833_driver.h
#pragma once
#include "motor_driver.h"

// Controls two DRV8833 H-bridge channels (left side + right side).
// Each channel is driven by two PWM pins: IN1 (forward) and IN2 (reverse).
// FL+BL motors → left channel (ain1, ain2)
// FR+BR motors → right channel (bin1, bin2)
class DRV8833Driver : public MotorDriver {
public:
    DRV8833Driver(uint8_t ain1, uint8_t ain2, uint8_t bin1, uint8_t bin2);

    void drive(int8_t throttle, int8_t steering) override;
    void stop() override;

private:
    void setChannel(uint8_t in1, uint8_t in2, int speed);  // speed: -255..255

    uint8_t _ain1, _ain2, _bin1, _bin2;
};
```

- [ ] **Step 3: Create drv8833_driver.cpp**

```cpp
// car/src/drv8833_driver.cpp
#include "drv8833_driver.h"
#include <Arduino.h>
#include <algorithm>

DRV8833Driver::DRV8833Driver(uint8_t ain1, uint8_t ain2, uint8_t bin1, uint8_t bin2)
    : _ain1(ain1), _ain2(ain2), _bin1(bin1), _bin2(bin2) {
    pinMode(_ain1, OUTPUT); pinMode(_ain2, OUTPUT);
    pinMode(_bin1, OUTPUT); pinMode(_bin2, OUTPUT);
    stop();
}

void DRV8833Driver::drive(int8_t throttle, int8_t steering) {
    // Mix throttle+steering to left/right. Scale to -255..255.
    int left  = (static_cast<int>(throttle) + static_cast<int>(steering)) * 2;
    int right = (static_cast<int>(throttle) - static_cast<int>(steering)) * 2;
    left  = std::clamp(left,  -255, 255);
    right = std::clamp(right, -255, 255);
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
```

- [ ] **Step 4: Verify car compiles**

```bash
cd car && pio run -e car
```

Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add car/src/motor_driver.h car/src/drv8833_driver.h car/src/drv8833_driver.cpp
git commit -m "feat: MotorDriver interface and DRV8833 tank-drive implementation"
```

---

### Task 10: KnockoffDetector

**Files:**
- Create: `car/src/knockoff_detector.h`
- Create: `car/src/knockoff_detector.cpp`

- [ ] **Step 1: Create knockoff_detector.h**

```cpp
// car/src/knockoff_detector.h
#pragma once
#include <cstdint>
#include <functional>

class KnockoffDetector {
public:
    using TriggerCallback = std::function<void()>;

    // pin: GPIO pin connected to shark switch.
    // active_low: true if switch pulls pin LOW when triggered.
    // debounce_ms: minimum ms the pin must be in trigger state before firing.
    // lockout_ms: ms to ignore further triggers after one fires.
    KnockoffDetector(uint8_t pin, bool active_low,
                     uint32_t debounce_ms = 50,
                     uint32_t lockout_ms  = 3000);

    void onTrigger(TriggerCallback cb) { _callback = std::move(cb); }

    // Call from loop(). now_ms = millis().
    void tick(uint32_t now_ms);

private:
    uint8_t  _pin;
    bool     _active_low;
    uint32_t _debounce_ms;
    uint32_t _lockout_ms;

    TriggerCallback _callback;

    bool     _last_raw       = false;
    uint32_t _change_time_ms = 0;
    bool     _triggered      = false;
    uint32_t _trigger_time_ms= 0;
};
```

- [ ] **Step 2: Create knockoff_detector.cpp**

```cpp
// car/src/knockoff_detector.cpp
#include "knockoff_detector.h"
#include <Arduino.h>

KnockoffDetector::KnockoffDetector(uint8_t pin, bool active_low,
                                   uint32_t debounce_ms, uint32_t lockout_ms)
    : _pin(pin), _active_low(active_low),
      _debounce_ms(debounce_ms), _lockout_ms(lockout_ms) {
    pinMode(_pin, active_low ? INPUT_PULLUP : INPUT_PULLDOWN);
}

void KnockoffDetector::tick(uint32_t now_ms) {
    // In lockout window — ignore all input.
    if (_triggered && (now_ms - _trigger_time_ms) < _lockout_ms) return;
    _triggered = false;

    bool raw = digitalRead(_pin);
    bool active = _active_low ? !raw : raw;

    if (active != _last_raw) {
        _last_raw = active;
        _change_time_ms = now_ms;
    }

    if (active && (now_ms - _change_time_ms) >= _debounce_ms) {
        _triggered = true;
        _trigger_time_ms = now_ms;
        if (_callback) _callback();
    }
}
```

- [ ] **Step 3: Verify car compiles**

```bash
cd car && pio run -e car
```

Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add car/src/knockoff_detector.h car/src/knockoff_detector.cpp
git commit -m "feat: KnockoffDetector — debounced single-shot switch with lockout"
```

---

### Task 11: CarNode and car main.cpp

**Files:**
- Create: `car/src/car_node.h`
- Create: `car/src/car_node.cpp`
- Modify: `car/src/main.cpp`

- [ ] **Step 1: Create car_node.h**

```cpp
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

    void setMotorDriver(MotorDriver& driver) { _driver = &driver; }

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
    void sendPing();

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
```

- [ ] **Step 2: Create car_node.cpp**

```cpp
// car/src/car_node.cpp
#include "car_node.h"
#include <Arduino.h>

CarNode::CarNode(MotorDriver& driver, KnockoffDetector& detector)
    : _driver(&driver), _detector(&detector) {}

void CarNode::begin() {
    _detector->onTrigger([this]() { onKnockoffTriggered(); });

    auto& t = EspNowTransport::instance();
    t.onReceive([this](const uint8_t* mac, const uint8_t* data, int len) {
        onReceive(mac, data, len);
    });
    t.begin(ESPNOW_CHANNEL);

    // Broadcast HELLO.
    HelloMsg hello{};
    hello.header.type   = MessageType::HELLO;
    hello.header.src_id = DEVICE_ID;
    hello.header.seq    = _seq++;
    hello.device_type   = DeviceType::CAR;
    hello.device_id     = DEVICE_ID;
    t.sendBroadcast(&hello, sizeof(hello));
}

void CarNode::tick(uint32_t now_ms) {
    _detector->tick(now_ms);

    // Watchdog: stop if no drive command received recently.
    bool racing  = _ctx.state == GameState::RACING;
    bool timeout = (now_ms - _last_drive_ms) > WATCHDOG_TIMEOUT_MS;
    if (!racing || timeout) {
        _driver->stop();
    }

    // Keepalive ping.
    if (_paired && (now_ms - _last_ping_ms) >= PING_INTERVAL_MS) {
        sendPing();
        _last_ping_ms = now_ms;
    }
}

void CarNode::onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(MessageHeader)) return;
    const auto* hdr = reinterpret_cast<const MessageHeader*>(data);

    switch (hdr->type) {
        case MessageType::HELLO_ACK: {
            if (len < (int)sizeof(HelloAckMsg)) break;
            const auto* ack = reinterpret_cast<const HelloAckMsg*>(data);
            _assigned_id = ack->assigned_id;
            memcpy(_server_mac, mac, 6);

            // Register client as a peer.
            EspNowTransport::instance().addPeer(ack->partner_mac);
            _paired = true;
            break;
        }

        case MessageType::DRIVE_CMD: {
            if (!_paired) break;
            if (len < (int)sizeof(DriveCmdMsg)) break;
            if (_ctx.state != GameState::RACING) break;

            const auto* cmd = reinterpret_cast<const DriveCmdMsg*>(data);
            _driver->drive(cmd->throttle, cmd->steering);
            _last_drive_ms = millis();
            break;
        }

        case MessageType::GAME_STATE_BROADCAST: {
            if (len < (int)sizeof(GameStateBroadcastMsg)) break;
            const auto* msg = reinterpret_cast<const GameStateBroadcastMsg*>(data);
            GameState prev = _ctx.state;
            _ctx.state                 = msg->state;
            _ctx.round                 = msg->round;
            _ctx.countdown_remaining   = msg->countdown_remaining;
            _ctx.cars_eliminated       = msg->cars_eliminated;
            memcpy(_ctx.round_wins, msg->round_wins, 3);
            memcpy(_ctx.knockoffs,  msg->knockoffs,  3);

            if (prev != _ctx.state) {
                if (_ctx.state == GameState::RACING) _bus.emit(GameEvent::MATCH_START, _ctx);
                if (_ctx.state == GameState::ROUND_END) _bus.emit(GameEvent::ROUND_END,  _ctx);
                if (_ctx.state == GameState::LOBBY && prev == GameState::ROUND_END)
                    _bus.emit(GameEvent::MATCH_END, _ctx);
                _bus.emit(GameEvent::STATE_CHANGED, _ctx);
            }
            break;
        }

        case MessageType::PONG:
            break;

        default: break;
    }
}

void CarNode::onKnockoffTriggered() {
    if (!_paired) return;
    KnockoffEventMsg msg{};
    msg.header.type   = MessageType::KNOCKOFF_EVENT;
    msg.header.src_id = _assigned_id;
    msg.header.seq    = _seq++;
    msg.car_id        = _assigned_id;
    EspNowTransport::instance().send(_server_mac, &msg, sizeof(msg));
}

void CarNode::sendPing() {
    PingMsg ping{};
    ping.header.type   = MessageType::PING;
    ping.header.src_id = _assigned_id;
    ping.header.seq    = _seq++;
    EspNowTransport::instance().send(_server_mac, &ping, sizeof(ping));
}
```

- [ ] **Step 3: Write car/src/main.cpp**

```cpp
// car/src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "drv8833_driver.h"
#include "knockoff_detector.h"
#include "car_node.h"

// ── Pin assignments (edit to match your wiring) ───────────────────────────────
static constexpr uint8_t PIN_AIN1          = 4;
static constexpr uint8_t PIN_AIN2          = 5;
static constexpr uint8_t PIN_BIN1          = 6;
static constexpr uint8_t PIN_BIN2          = 7;
static constexpr uint8_t PIN_SHARK_SWITCH  = 3;

// ── Hardware instances ─────────────────────────────────────────────────────────
DRV8833Driver     motors(PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2);
KnockoffDetector  knockoff(PIN_SHARK_SWITCH, /*active_low=*/true);
CarNode           car(motors, knockoff);

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);

    car.begin();

    // ── Register game state handlers here ──────────────────────────────────────
    // Example: blink an LED when the round ends.
    // car.events().on(GameEvent::ROUND_END, [](const GameContext&) {
    //     digitalWrite(PIN_LED, HIGH); delay(200); digitalWrite(PIN_LED, LOW);
    // });

    Serial.println("Car ready, waiting for server pairing...");
}

void loop() {
    car.tick(millis());
}
```

- [ ] **Step 4: Verify car compiles**

```bash
cd car && pio run -e car
```

Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add car/src/car_node.h car/src/car_node.cpp car/src/main.cpp
git commit -m "feat: CarNode — ESP-NOW pairing, drive watchdog, knockoff reporting"
```

---

## Phase 4: Client

### Task 12: InputProvider, JoystickInput, ButtonInput

**Files:**
- Create: `client/src/input_provider.h`
- Create: `client/src/joystick_input.h`
- Create: `client/src/joystick_input.cpp`
- Create: `client/src/button_input.h`
- Create: `client/src/button_input.cpp`

- [ ] **Step 1: Create input_provider.h**

```cpp
// client/src/input_provider.h
#pragma once
#include "game_state.h"

class InputProvider {
public:
    virtual ~InputProvider() = default;
    // Returns the current desired drive command. Called at ~30Hz.
    virtual DriveCommand read() = 0;
};
```

- [ ] **Step 2: Create joystick_input.h**

```cpp
// client/src/joystick_input.h
#pragma once
#include "input_provider.h"

// Reads two analog ADC pins (X axis = steering, Y axis = throttle).
// Assumes centered ADC reads ~2048 on a 12-bit ADC (ESP32 default).
class JoystickInput : public InputProvider {
public:
    JoystickInput(uint8_t pin_x, uint8_t pin_y,
                  uint16_t center   = 2048,
                  uint16_t deadzone = 150);

    DriveCommand read() override;

private:
    int8_t mapAxis(uint16_t raw) const;

    uint8_t  _pin_x;
    uint8_t  _pin_y;
    uint16_t _center;
    uint16_t _deadzone;
};
```

- [ ] **Step 3: Create joystick_input.cpp**

```cpp
// client/src/joystick_input.cpp
#include "joystick_input.h"
#include <Arduino.h>
#include <algorithm>

JoystickInput::JoystickInput(uint8_t pin_x, uint8_t pin_y,
                             uint16_t center, uint16_t deadzone)
    : _pin_x(pin_x), _pin_y(pin_y), _center(center), _deadzone(deadzone) {
    analogReadResolution(12);
}

DriveCommand JoystickInput::read() {
    DriveCommand cmd;
    cmd.steering = mapAxis(analogRead(_pin_x));
    cmd.throttle = mapAxis(analogRead(_pin_y));
    return cmd;
}

int8_t JoystickInput::mapAxis(uint16_t raw) const {
    int delta = static_cast<int>(raw) - static_cast<int>(_center);
    if (abs(delta) < static_cast<int>(_deadzone)) return 0;
    // Map from [-2048, 2048] to [-127, 127], skipping deadzone.
    int sign = (delta > 0) ? 1 : -1;
    int magnitude = abs(delta) - _deadzone;
    int range = 2048 - _deadzone;
    int mapped = (magnitude * 127) / range;
    return static_cast<int8_t>(std::clamp(sign * mapped, -127, 127));
}
```

- [ ] **Step 4: Create button_input.h**

```cpp
// client/src/button_input.h
#pragma once
#include "input_provider.h"

// Four digital buttons → DriveCommand.
// Buttons are active-low (pressed = GPIO pulled LOW via INPUT_PULLUP).
class ButtonInput : public InputProvider {
public:
    ButtonInput(uint8_t pin_up, uint8_t pin_down,
                uint8_t pin_left, uint8_t pin_right,
                int8_t speed = 100);

    DriveCommand read() override;

private:
    uint8_t _up, _down, _left, _right;
    int8_t  _speed;
};
```

- [ ] **Step 5: Create button_input.cpp**

```cpp
// client/src/button_input.cpp
#include "button_input.h"
#include <Arduino.h>

ButtonInput::ButtonInput(uint8_t pin_up, uint8_t pin_down,
                         uint8_t pin_left, uint8_t pin_right, int8_t speed)
    : _up(pin_up), _down(pin_down), _left(pin_left), _right(pin_right), _speed(speed) {
    pinMode(_up,    INPUT_PULLUP);
    pinMode(_down,  INPUT_PULLUP);
    pinMode(_left,  INPUT_PULLUP);
    pinMode(_right, INPUT_PULLUP);
}

DriveCommand ButtonInput::read() {
    DriveCommand cmd;
    if (!digitalRead(_up))    cmd.throttle =  _speed;
    if (!digitalRead(_down))  cmd.throttle = -_speed;
    if (!digitalRead(_right)) cmd.steering =  _speed;
    if (!digitalRead(_left))  cmd.steering = -_speed;
    return cmd;
}
```

- [ ] **Step 6: Verify client compiles**

```bash
cd client && pio run -e client
```

Expected: `SUCCESS`

- [ ] **Step 7: Commit**

```bash
git add client/src/input_provider.h \
        client/src/joystick_input.h client/src/joystick_input.cpp \
        client/src/button_input.h   client/src/button_input.cpp
git commit -m "feat: InputProvider — JoystickInput (ADC) and ButtonInput (WASD) implementations"
```

---

### Task 13: ClientNode and client main.cpp

**Files:**
- Create: `client/src/client_node.h`
- Create: `client/src/client_node.cpp`
- Modify: `client/src/main.cpp`

- [ ] **Step 1: Create client_node.h**

```cpp
// client/src/client_node.h
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
    explicit ClientNode(InputProvider& input);

    void setInputProvider(InputProvider& input) { _input = &input; }

    // Call once in setup().
    void begin();

    // Call every loop(). now_ms = millis().
    void tick(uint32_t now_ms);

    // Event bus for game state events received from server.
    EventBus<GameEvent, GameContext>& events() { return _bus; }

    const GameContext& gameContext() const { return _ctx; }

private:
    void onReceive(const uint8_t* mac, const uint8_t* data, int len);
    void sendDriveCommand(const DriveCommand& cmd);
    void sendPing();

    InputProvider*   _input;
    EventBus<GameEvent, GameContext> _bus;
    GameContext      _ctx{};
    uint8_t          _assigned_id   = 0xFF;
    uint8_t          _server_mac[6] = {};
    uint8_t          _car_mac[6]    = {};
    bool             _paired        = false;
    uint8_t          _seq           = 0;
    uint32_t         _last_drive_ms = 0;
    uint32_t         _last_ping_ms  = 0;

    static constexpr uint32_t PING_INTERVAL_MS = 1000;
};
```

- [ ] **Step 2: Create client_node.cpp**

```cpp
// client/src/client_node.cpp
#include "client_node.h"
#include <Arduino.h>

ClientNode::ClientNode(InputProvider& input) : _input(&input) {}

void ClientNode::begin() {
    auto& t = EspNowTransport::instance();
    t.onReceive([this](const uint8_t* mac, const uint8_t* data, int len) {
        onReceive(mac, data, len);
    });
    t.begin(ESPNOW_CHANNEL);

    // Broadcast HELLO.
    HelloMsg hello{};
    hello.header.type   = MessageType::HELLO;
    hello.header.src_id = DEVICE_ID;
    hello.header.seq    = _seq++;
    hello.device_type   = DeviceType::CLIENT;
    hello.device_id     = DEVICE_ID;
    t.sendBroadcast(&hello, sizeof(hello));
}

void ClientNode::tick(uint32_t now_ms) {
    if (!_paired) return;

    // Send drive command at DRIVE_INTERVAL_MS.
    if ((now_ms - _last_drive_ms) >= DRIVE_INTERVAL_MS) {
        DriveCommand cmd = _input->read();
        if (_ctx.state != GameState::RACING) {
            cmd.throttle = 0;
            cmd.steering = 0;
        }
        sendDriveCommand(cmd);
        _last_drive_ms = now_ms;
    }

    // Keepalive ping.
    if ((now_ms - _last_ping_ms) >= PING_INTERVAL_MS) {
        sendPing();
        _last_ping_ms = now_ms;
    }
}

void ClientNode::onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(MessageHeader)) return;
    const auto* hdr = reinterpret_cast<const MessageHeader*>(data);

    switch (hdr->type) {
        case MessageType::HELLO_ACK: {
            if (len < (int)sizeof(HelloAckMsg)) break;
            const auto* ack = reinterpret_cast<const HelloAckMsg*>(data);
            _assigned_id = ack->assigned_id;
            memcpy(_server_mac, mac, 6);
            memcpy(_car_mac, ack->partner_mac, 6);
            EspNowTransport::instance().addPeer(_car_mac);
            _paired = true;
            break;
        }

        case MessageType::GAME_STATE_BROADCAST: {
            if (len < (int)sizeof(GameStateBroadcastMsg)) break;
            const auto* msg = reinterpret_cast<const GameStateBroadcastMsg*>(data);
            GameState prev = _ctx.state;
            _ctx.state               = msg->state;
            _ctx.round               = msg->round;
            _ctx.countdown_remaining = msg->countdown_remaining;
            _ctx.cars_eliminated     = msg->cars_eliminated;
            memcpy(_ctx.round_wins, msg->round_wins, 3);
            memcpy(_ctx.knockoffs,  msg->knockoffs,  3);

            if (prev != _ctx.state) {
                if (_ctx.state == GameState::RACING)    _bus.emit(GameEvent::MATCH_START, _ctx);
                if (_ctx.state == GameState::ROUND_END) _bus.emit(GameEvent::ROUND_END,   _ctx);
                if (_ctx.state == GameState::LOBBY && prev == GameState::ROUND_END)
                    _bus.emit(GameEvent::MATCH_END, _ctx);
                _bus.emit(GameEvent::STATE_CHANGED, _ctx);
            }
            if (_ctx.state == GameState::COUNTDOWN)
                _bus.emit(GameEvent::COUNTDOWN_TICK, _ctx);
            break;
        }

        case MessageType::PONG:
            break;

        default: break;
    }
}

void ClientNode::sendDriveCommand(const DriveCommand& cmd) {
    DriveCmdMsg msg{};
    msg.header.type   = MessageType::DRIVE_CMD;
    msg.header.src_id = _assigned_id;
    msg.header.seq    = _seq++;
    msg.throttle      = cmd.throttle;
    msg.steering      = cmd.steering;
    EspNowTransport::instance().send(_car_mac, &msg, sizeof(msg));
}

void ClientNode::sendPing() {
    PingMsg ping{};
    ping.header.type   = MessageType::PING;
    ping.header.src_id = _assigned_id;
    ping.header.seq    = _seq++;
    EspNowTransport::instance().send(_server_mac, &ping, sizeof(ping));
}
```

- [ ] **Step 3: Write client/src/main.cpp**

```cpp
// client/src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "joystick_input.h"
#include "button_input.h"
#include "client_node.h"

// ── Pin assignments (edit to match your hardware) ─────────────────────────────
// Joystick mode:
static constexpr uint8_t PIN_JOY_X = 1;
static constexpr uint8_t PIN_JOY_Y = 2;

// Button mode (comment out joystick above and uncomment below):
// static constexpr uint8_t PIN_UP    = 3;
// static constexpr uint8_t PIN_DOWN  = 4;
// static constexpr uint8_t PIN_LEFT  = 5;
// static constexpr uint8_t PIN_RIGHT = 6;

// ── Status LED (optional) ──────────────────────────────────────────────────────
static constexpr uint8_t PIN_STATUS_LED = 8;

// ── Input and client ───────────────────────────────────────────────────────────
JoystickInput input(PIN_JOY_X, PIN_JOY_Y);
// ButtonInput input(PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT);  // alternative

ClientNode client(input);

void setup() {
    Serial.begin(115200);
    pinMode(PIN_STATUS_LED, OUTPUT);
    WiFi.mode(WIFI_STA);

    client.begin();

    // ── Register game state handlers here ──────────────────────────────────────
    client.events().on(GameEvent::MATCH_START, [](const GameContext&) {
        digitalWrite(PIN_STATUS_LED, HIGH);  // green = racing
    });
    client.events().on(GameEvent::ROUND_END, [](const GameContext&) {
        digitalWrite(PIN_STATUS_LED, LOW);   // off = round over
    });
    client.events().on(GameEvent::MATCH_END, [](const GameContext&) {
        // Flash to indicate match over.
        for (int i = 0; i < 5; i++) {
            digitalWrite(PIN_STATUS_LED, HIGH); delay(100);
            digitalWrite(PIN_STATUS_LED, LOW);  delay(100);
        }
    });

    Serial.println("Client ready, waiting for server pairing...");
}

void loop() {
    client.tick(millis());
}
```

- [ ] **Step 4: Verify client compiles**

```bash
cd client && pio run -e client
```

Expected: `SUCCESS`

- [ ] **Step 5: Run all native tests one final time**

```bash
cd server && pio test -e native_test
```

Expected: all test suites pass.

- [ ] **Step 6: Final commit**

```bash
git add client/src/client_node.h client/src/client_node.cpp client/src/main.cpp
git commit -m "feat: ClientNode — drive loop, game state sync, handler registration"
```

---

## Self-Review Notes

**Spec coverage check:**
- [x] Broadcast server discovery (no MAC hardcoding) → Task 4 + Task 8 + Task 11 + Task 13
- [x] Runtime pairing via web UI → Task 6 + Task 7
- [x] `LOBBY → COUNTDOWN → RACING → ROUND_END` state machine → Task 5
- [x] Round ends when 2/3 sharks knocked off → `checkRoundEnd()` in Task 5
- [x] Referee force-end → `/api/end` in Task 7, `endRound()` in Task 5
- [x] Best-of-N match winner → `ROUNDS_TO_WIN` in Task 5
- [x] Direct client→car drive commands at 30Hz → Task 13
- [x] 500ms watchdog on car → `WATCHDOG_TIMEOUT_MS` in Task 11
- [x] Pluggable input providers → Task 12
- [x] Pluggable motor drivers → Task 9
- [x] Pluggable event handlers on server → Task 8 (commented examples)
- [x] Pluggable event handlers on client → Task 13 (commented examples)
- [x] Management UI with device health → Task 7
- [x] PING/PONG keepalive → Tasks 11, 13 (send); Task 8 (server receives PING, responds PONG)
- [x] `ESPNOW_CHANNEL` and `ROUNDS_TO_WIN` build flags → Task 1

**Known gaps to address at hardware time:**
- Pin assignments in car and client `main.cpp` are placeholders — update once hardware is wired.
- `DEVICE_ID` build flag must be different per device (set in each device's `platformio.ini` upload config or via `pio run --environment` overrides).
- `analogWrite` on ESP32C3 uses LEDC under the hood; if PWM channels conflict, assign explicit LEDC channels in `drv8833_driver.cpp`.
