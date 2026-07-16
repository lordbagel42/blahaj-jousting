#include "client_node.h"
#include <Arduino.h>

ClientNode::ClientNode(InputProvider& input, uint8_t axis_mask)
    : _input(&input), _axis_mask(axis_mask) {}

void ClientNode::begin() {
#if defined(ESP8266)
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);  // active LOW: off
#endif

    auto& t = EspNowTransport::instance();
    t.onReceive([this](const uint8_t* mac, const uint8_t* data, int len) {
        onReceive(mac, data, len);
    });
    if (!t.begin(ESPNOW_CHANNEL)) {
        Serial.println("ERROR: ESP-NOW init failed");
        return;
    }

    sendHello();
}

void ClientNode::tick(uint32_t now_ms) {
    EspNowTransport::instance().poll();

    // Periodic HELLO announce. Fast while unpaired, slower once paired, but
    // never silent — keeps the referee's device list populated across a
    // referee reboot and lets the server re-establish our pairing if we
    // rebooted (recovery: neither side gets stuck).
    uint32_t hello_interval = _paired ? PAIRED_HELLO_INTERVAL_MS : HELLO_INTERVAL_MS;
    if ((now_ms - _last_hello_ms) >= hello_interval) {
        sendHello();
    }

    if (!_paired) return;

    if ((now_ms - _last_drive_ms) >= DRIVE_HZ_INTERVAL_MS) {
        DriveCommand cmd = _input->read();
        if (_ctx.state != GameState::RACING) {
            cmd.throttle = 0;
            cmd.steering = 0;
        }
        sendDriveCommand(cmd);
        _last_drive_ms = now_ms;
    }

    if ((now_ms - _last_ping_ms) >= PING_HZ_INTERVAL_MS) {
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
            if (ack->assigned_id == 0xFE) break;  // referee registration ack, not ours
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
            _ctx.last_knockoff_car_id = msg->last_knockoff_car_id;
            memcpy(_ctx.round_wins, msg->round_wins, sizeof(_ctx.round_wins));
            memcpy(_ctx.knockoffs,  msg->knockoffs,  sizeof(_ctx.knockoffs));

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

        case MessageType::LED_CMD:
            if (len >= (int)sizeof(LedCmdMsg)) {
                const auto* msg = reinterpret_cast<const LedCmdMsg*>(data);
                if (msg->target_type == DeviceType::CLIENT && msg->target_id == DEVICE_ID) {
                    Serial.printf("[LED] client#%d %s\n", DEVICE_ID, msg->on ? "ON" : "OFF");
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
    msg.axis_mask     = _axis_mask;
    EspNowTransport::instance().send(_car_mac, &msg, sizeof(msg));
}

void ClientNode::sendPing() {
    PingMsg ping{};
    ping.header.type   = MessageType::PING;
    ping.header.src_id = _assigned_id;
    ping.header.seq    = _seq++;
    EspNowTransport::instance().send(_server_mac, &ping, sizeof(ping));
}

void ClientNode::sendHello() {
    _last_hello_ms = millis();
    HelloMsg hello{};
    hello.header.type   = MessageType::HELLO;
    hello.header.src_id = DEVICE_ID;
    hello.header.seq    = _seq++;
    hello.device_type   = DeviceType::CLIENT;
    hello.device_id     = DEVICE_ID;
    hello.axis_mask     = _axis_mask;
    EspNowTransport::instance().sendBroadcast(&hello, sizeof(hello));
}
