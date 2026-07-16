// car/src/car_node.cpp
#include "car_node.h"
#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <esp_wifi.h>
#endif

CarNode::CarNode(MotorDriver& driver, KnockoffDetector& detector,
                 UltrasonicSensor* sonar)
    : _driver(&driver), _detector(&detector), _sonar(sonar) {}

void CarNode::begin() {
    _detector->onTrigger([this]() { onKnockoffTriggered(); });

#if defined(ESP8266)
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);  // active LOW: off
#endif

    auto& t = EspNowTransport::instance();
    t.onReceive([this](const uint8_t* mac, const uint8_t* data, int len) {
        onReceive(mac, data, len, millis());
    });
    if (!t.begin(ESPNOW_CHANNEL)) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    sendHello();
}

void CarNode::tick(uint32_t now_ms) {
    EspNowTransport::instance().poll();
    _detector->tick(now_ms);
    if (_sonar) _sonar->tick(now_ms);

    // Bench motor test (TEST_DRIVE_CMD) overrides normal control, the watchdog
    // and the sonar cutoff — but only until _test_until_ms, so a dropped "stop"
    // command can never leave the motors running.
    if (now_ms < _test_until_ms) {
        _driver->drive(_test_throttle, _test_steering);
    } else {
        // Watchdog: stop motors if either axis hasn't reported recently (each
        // axis may come from a separate physical client device), or if the
        // ultrasonic sensor reads past the cutoff (car lifted off the surface).
        bool racing         = _ctx.state == GameState::RACING;
        bool throttle_stale = (now_ms - _last_throttle_ms) > WATCHDOG_TIMEOUT_MS;
        bool steering_stale = (now_ms - _last_steering_ms) > WATCHDOG_TIMEOUT_MS;
        if (!racing || throttle_stale || steering_stale || sonarCutoff()) {
            _driver->stop();
        }
    }

    // Keepalive ping.
    if (_paired && (now_ms - _last_ping_ms) >= PING_INTERVAL_MS) {
        sendPing();
        _last_ping_ms = now_ms;
    }

    // Periodic HELLO announce. Fast while unpaired (to pair quickly); slower
    // once paired, but never silent — this keeps the referee's device list
    // populated even across a referee reboot, and lets the server re-establish
    // our pairing if WE rebooted (recovery: neither side gets stuck).
    uint32_t hello_interval = _paired ? PAIRED_HELLO_INTERVAL_MS : HELLO_INTERVAL_MS;
    if ((now_ms - _last_hello_ms) >= hello_interval) {
        sendHello();
    }
}

void CarNode::onReceive(const uint8_t* mac, const uint8_t* data, int len, uint32_t now_ms) {
    if (len < (int)sizeof(MessageHeader)) return;
    const auto* hdr = reinterpret_cast<const MessageHeader*>(data);

    switch (hdr->type) {
        case MessageType::HELLO_ACK: {
            if (len < (int)sizeof(HelloAckMsg)) break;
            const auto* ack = reinterpret_cast<const HelloAckMsg*>(data);
            if (ack->assigned_id == 0xFE) break;  // referee registration ack, not ours
            bool was_paired = _paired;
            _assigned_id = ack->assigned_id;
            memcpy(_server_mac, mac, 6);

            // Register client as a peer so we can receive DriveCmd from it.
            EspNowTransport::instance().addPeer(ack->partner_mac);
            _paired = true;
            if (!was_paired) Serial.printf("Paired: id=%d\n", _assigned_id);
            break;
        }

        case MessageType::DRIVE_CMD: {
            if (!_paired) break;
            if (len < (int)sizeof(DriveCmdMsg)) break;
            if (_ctx.state != GameState::RACING) break;

            const auto* cmd = reinterpret_cast<const DriveCmdMsg*>(data);
            if (cmd->axis_mask & DRIVE_AXIS_THROTTLE) {
                _last_throttle    = cmd->throttle;
                _last_throttle_ms = now_ms;
            }
            if (cmd->axis_mask & DRIVE_AXIS_STEERING) {
                _last_steering    = cmd->steering;
                _last_steering_ms = now_ms;
            }
            if (sonarCutoff()) { _driver->stop(); break; }  // off-surface guard
            _driver->drive(_last_throttle, _last_steering);
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
            _ctx.last_knockoff_car_id  = msg->last_knockoff_car_id;
            memcpy(_ctx.round_wins, msg->round_wins, sizeof(_ctx.round_wins));
            memcpy(_ctx.knockoffs,  msg->knockoffs,  sizeof(_ctx.knockoffs));

            if (prev != _ctx.state) {
                if (_ctx.state == GameState::RACING)
                    _bus.emit(GameEvent::MATCH_START, _ctx);
                if (_ctx.state == GameState::ROUND_END)
                    _bus.emit(GameEvent::ROUND_END,  _ctx);
                if (_ctx.state == GameState::LOBBY && prev == GameState::ROUND_END)
                    _bus.emit(GameEvent::MATCH_END, _ctx);
                _bus.emit(GameEvent::STATE_CHANGED, _ctx);
            }
            break;
        }

        case MessageType::PONG:
            break;

        case MessageType::LED_CMD:
            if (len >= (int)sizeof(LedCmdMsg)) {
                const auto* msg = reinterpret_cast<const LedCmdMsg*>(data);
                if (msg->target_type == DeviceType::CAR && msg->target_id == DEVICE_ID) {
                    Serial.printf("[LED] car#%d %s\n", DEVICE_ID, msg->on ? "ON" : "OFF");
#if defined(ESP8266)
                    digitalWrite(PIN_LED, msg->on ? LOW : HIGH);  // active LOW
#endif
                }
            }
            break;

        case MessageType::REBOOT_CMD:
            Serial.println("[REBOOT_CMD] restarting...");
            delay(50);
            ESP.restart();
            break;

        case MessageType::TEST_DRIVE_CMD: {
            if (len < (int)sizeof(TestDriveCmdMsg)) break;
            const auto* msg = reinterpret_cast<const TestDriveCmdMsg*>(data);
            if (msg->target_id != DEVICE_ID && msg->target_id != BROADCAST_ID) break;
            _test_throttle = msg->throttle;
            _test_steering = msg->steering;
            _test_until_ms = now_ms + TEST_DRIVE_HOLD_MS;
            break;
        }

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

void CarNode::sendHello() {
    _last_hello_ms = millis();
#if defined(ESP8266)
    uint8_t prim = WiFi.channel();
#else
    uint8_t prim; wifi_second_chan_t sec;
    esp_wifi_get_channel(&prim, &sec);
#endif
    HelloMsg hello{};
    hello.header.type   = MessageType::HELLO;
    hello.header.src_id = DEVICE_ID;
    hello.header.seq    = _seq++;
    hello.device_type   = DeviceType::CAR;
    hello.device_id     = DEVICE_ID;
    bool bc = EspNowTransport::instance().sendBroadcast(&hello, sizeof(hello));
    Serial.printf("[HELLO] ch=%d bcast=%d\n", prim, bc);
}

void CarNode::sendPing() {
    PingMsg ping{};
    ping.header.type   = MessageType::PING;
    ping.header.src_id = _assigned_id;
    ping.header.seq    = _seq++;
    EspNowTransport::instance().send(_server_mac, &ping, sizeof(ping));
}
