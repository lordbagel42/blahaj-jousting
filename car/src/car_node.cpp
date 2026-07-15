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
    if (!t.begin(ESPNOW_CHANNEL)) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    // Broadcast HELLO so server can discover us.
    HelloMsg hello{};
    hello.header.type   = MessageType::HELLO;
    hello.header.src_id = DEVICE_ID;
    hello.header.seq    = _seq++;
    hello.device_type   = DeviceType::CAR;
    hello.device_id     = DEVICE_ID;
    t.sendBroadcast(&hello, sizeof(hello));
}

void CarNode::tick(uint32_t now_ms) {
    EspNowTransport::instance().poll();
    _detector->tick(now_ms);

    // Watchdog: stop motors if no drive command received recently.
    bool racing  = _ctx.state == GameState::RACING;
    bool timeout = (now_ms - _last_drive_ms) > WATCHDOG_TIMEOUT_MS;
    if (!racing || timeout) {
        _driver->stop();
    }

    // Keepalive ping.
    if (_paired && (now_ms - _last_ping_ms) >= PING_INTERVAL_MS) {
        sendPing(now_ms);
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

            // Register client as a peer so we can receive DriveCmd from it.
            EspNowTransport::instance().addPeer(ack->partner_mac);
            _paired = true;
            Serial.printf("Paired: id=%d\n", _assigned_id);
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
            _ctx.last_knockoff_car_id  = msg->last_knockoff_car_id;
            memcpy(_ctx.round_wins, msg->round_wins, 3);
            memcpy(_ctx.knockoffs,  msg->knockoffs,  3);

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

void CarNode::sendPing(uint32_t now_ms) {
    (void)now_ms;
    PingMsg ping{};
    ping.header.type   = MessageType::PING;
    ping.header.src_id = _assigned_id;
    ping.header.seq    = _seq++;
    EspNowTransport::instance().send(_server_mac, &ping, sizeof(ping));
}
