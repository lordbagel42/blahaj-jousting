#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#include <esp_wifi.h>
#endif
#include "espnow_transport.h"
#include "game_engine.h"
#include "pairing_manager.h"
#include "protocol.h"
#include "self_id.h"
#include "settings_store.h"
#include "status_output.h"

#if defined(ESP8266)
static constexpr uint8_t PIN_LED = LED_BUILTIN;  // D1 Mini onboard LED (D4/GPIO2), active LOW
#else
static constexpr uint8_t PIN_LED = 8;            // active LOW
#endif

EventBus<GameEvent, GameContext> bus;
GameEngine     game(bus);
PairingManager pairing;
StatusOutput   status;

static uint8_t seq           = 0;
static uint8_t referee_mac[6] = {};
static bool    referee_known  = false;

static constexpr uint32_t ONLINE_THRESHOLD_MS = 3000;

static void broadcastGameState() {
    const auto& ctx = game.context();
    uint32_t now_ms = millis();
    GameStateBroadcastMsg msg{};
    msg.header.type           = MessageType::GAME_STATE_BROADCAST;
    msg.header.src_id         = 0;
    msg.header.seq            = seq++;
    msg.state                 = ctx.state;
    msg.round                 = ctx.round;
    msg.countdown_remaining   = ctx.countdown_remaining;
    msg.cars_eliminated       = ctx.cars_eliminated;
    msg.last_knockoff_car_id  = ctx.last_knockoff_car_id;
    memcpy(msg.round_wins, ctx.round_wins, sizeof(msg.round_wins));
    memcpy(msg.knockoffs,  ctx.knockoffs,  sizeof(msg.knockoffs));
    msg.led_brightness        = SettingsStore::brightness();
    msg.idle_blue             = SettingsStore::idleBlue() ? 1 : 0;

    msg.pair_count = 0;
    for (const auto& client : pairing.clients()) {
        if (!client.paired || client.partner_slot >= pairing.cars().size()) continue;
        if (msg.pair_count >= MAX_BROADCAST_PAIRS) break;
        const auto& car = pairing.cars()[client.partner_slot];
        PairInfo& p = msg.pairs[msg.pair_count++];
        p.car_device_id    = car.device_id;
        p.client_device_id = client.device_id;
        p.axis_mask        = client.axis_mask;
        p.car_online       = (now_ms - car.last_seen_ms)    < ONLINE_THRESHOLD_MS;
        p.client_online    = (now_ms - client.last_seen_ms) < ONLINE_THRESHOLD_MS;
    }

    auto& t = EspNowTransport::instance();
    for (const auto& car    : pairing.cars())    if (car.paired)    t.send(car.mac,    &msg, sizeof(msg));
    for (const auto& client : pairing.clients()) if (client.paired) t.send(client.mac, &msg, sizeof(msg));
    if (referee_known) t.sendBroadcast(&msg, sizeof(msg));  // ESP32 CYD can't recv unicast from C3
}

static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(MessageHeader)) return;
    const auto* hdr = reinterpret_cast<const MessageHeader*>(data);
    uint32_t now = millis();

    pairing.updateLastSeen(mac, now);

    switch (hdr->type) {
        case MessageType::HELLO: {
            if (len < (int)sizeof(HelloMsg)) break;
            const auto& msg = *reinterpret_cast<const HelloMsg*>(data);
            Serial.printf("[HELLO] type=%d id=%d mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
                (int)msg.device_type, msg.device_id,
                mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

            if (msg.device_type == DeviceType::REFEREE) {
                memcpy(referee_mac, mac, 6);
                referee_known = true;
                // ACK so referee learns our MAC
                HelloAckMsg ack{};
                ack.header.type   = MessageType::HELLO_ACK;
                ack.header.src_id = 0;
                ack.header.seq    = seq++;
                ack.assigned_id   = 0xFE;
                bool send_ok = EspNowTransport::instance().sendBroadcast(&ack, sizeof(ack));
                Serial.printf("[REFEREE] send_broadcast=%d\n", send_ok);
                broadcastGameState();  // send current state immediately
            } else {
                pairing.onHello(mac, msg, now);
                // Recovery: if this device is already paired but is announcing
                // again (it rebooted and came up unpaired), re-send its ACK so
                // it re-learns the pairing without needing the referee.
                if (pairing.reconfirm(mac, EspNowTransport::instance(), seq++)) {
                    Serial.println("[HELLO] re-confirmed pairing for returning device");
                }
                broadcastGameState();  // so referee sees updated device counts
            }
            break;
        }

        case MessageType::KNOCKOFF_EVENT:
            if (len >= (int)sizeof(KnockoffEventMsg)) {
                const auto* msg = reinterpret_cast<const KnockoffEventMsg*>(data);
                Serial.printf("[KNOCKOFF] car_id=%d\n", msg->car_id);
                game.onKnockoff(msg->car_id, now);
            }
            break;

        case MessageType::PING: {
            Serial.printf("[PING] from %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            PingMsg pong{};
            pong.header.type   = MessageType::PONG;
            pong.header.src_id = 0;
            pong.header.seq    = seq++;
            EspNowTransport::instance().addPeer(mac);
            EspNowTransport::instance().send(mac, &pong, sizeof(pong));
            break;
        }

        case MessageType::START_CMD:
            Serial.println("[START_CMD]");
            game.startMatch(now);
            break;

        case MessageType::END_CMD:
            Serial.println("[END_CMD]");
            game.endRound(now);
            break;

        case MessageType::PAIR_CMD:
            if (len >= (int)sizeof(PairCmdMsg)) {
                const auto* msg = reinterpret_cast<const PairCmdMsg*>(data);
                Serial.printf("[PAIR_CMD] car=%d client=%d\n", msg->car_idx, msg->client_idx);
                pairing.assignPair(msg->car_idx, msg->client_idx);
                pairing.confirmPairings(EspNowTransport::instance(), seq++);
            }
            break;

        case MessageType::LED_CMD:
            if (len >= (int)sizeof(LedCmdMsg)) {
                const auto* msg = reinterpret_cast<const LedCmdMsg*>(data);
                if (msg->target_type == DeviceType::SERVER) {
                    digitalWrite(PIN_LED, msg->on ? LOW : HIGH);  // active LOW
                    Serial.printf("[LED_CMD] server %s\n", msg->on ? "ON" : "OFF");
                } else {
                    // Relay to the addressed car/client — the referee can't
                    // reliably unicast to them directly, but we already know
                    // every paired device's MAC.
                    const auto& list = (msg->target_type == DeviceType::CAR) ? pairing.cars() : pairing.clients();
                    for (const auto& d : list) {
                        if (d.device_id != msg->target_id) continue;
                        EspNowTransport::instance().addPeer(d.mac);
                        EspNowTransport::instance().send(d.mac, msg, sizeof(*msg));
                        Serial.printf("[LED_CMD] relayed to type=%d id=%d\n",
                            (int)msg->target_type, msg->target_id);
                        break;
                    }
                }
            }
            break;

        case MessageType::SET_BRIGHTNESS_CMD:
            if (len >= (int)sizeof(SetBrightnessCmdMsg)) {
                const auto* msg = reinterpret_cast<const SetBrightnessCmdMsg*>(data);
                uint8_t b = SettingsStore::setBrightness(msg->brightness);
                status.setBrightness(b);
                Serial.printf("[SET_BRIGHTNESS] %u\n", b);
                broadcastGameState();  // echo new value so referee sliders converge
            }
            break;

        case MessageType::SET_IDLE_BLUE_CMD:
            if (len >= (int)sizeof(SetIdleBlueCmdMsg)) {
                const auto* msg = reinterpret_cast<const SetIdleBlueCmdMsg*>(data);
                bool on = SettingsStore::setIdleBlue(msg->on != 0);
                status.setIdleBlue(on);
                Serial.printf("[SET_IDLE_BLUE] %d\n", on);
                broadcastGameState();  // echo so referee toggle converges
            }
            break;

        case MessageType::RESET_CMD:
            Serial.println("[RESET_CMD]");
            game.reset();
            break;

        case MessageType::REBOOT_CMD:
            Serial.println("[REBOOT_CMD] restarting...");
            delay(50);
            ESP.restart();
            break;

        default:
            Serial.printf("[UNKNOWN] type=%d len=%d\n", (int)hdr->type, len);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);  // off

    bus.on(GameEvent::STATE_CHANGED,  [](const GameContext&) { broadcastGameState(); });
    bus.on(GameEvent::KNOCKOFF,       [](const GameContext&) { broadcastGameState(); });
    bus.on(GameEvent::COUNTDOWN_TICK, [](const GameContext&) { broadcastGameState(); });

    status.begin();
    SettingsStore::begin();
    status.setBrightness(SettingsStore::brightness());  // apply persisted brightness
    status.setIdleBlue(SettingsStore::idleBlue());      // apply persisted idle-blue toggle

#if defined(ESP8266)
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.softAP("BlahajSrv", nullptr, ESPNOW_CHANNEL);
    delay(100);
    Serial.printf("actual ch: %d ESPNOW_CHANNEL=%d\n", WiFi.channel(), ESPNOW_CHANNEL);
#else
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("BlahajSrv", nullptr, ESPNOW_CHANNEL);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_err_t ch_err = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.printf("ch_set: %d\n", ch_err);

    uint8_t prim; wifi_second_chan_t sec;
    esp_wifi_get_channel(&prim, &sec);
    Serial.printf("actual ch: %d ESPNOW_CHANNEL=%d\n", prim, ESPNOW_CHANNEL);
#endif

    auto& transport = EspNowTransport::instance();
    bool init_ok = transport.begin(ESPNOW_CHANNEL);
    Serial.printf("espnow_init: %s\n", init_ok ? "OK" : "FAILED");
    transport.onReceive(onReceive);

    uint8_t my_mac[6];
    transport.getMac(my_mac);
    printSelfId(DeviceType::SERVER, 0xFE, 0, my_mac);
    Serial.println("Server ready");
    digitalWrite(PIN_LED, LOW);  // LED on = booted
}

static uint32_t last_hb_ms = 0;
static uint32_t last_state_broadcast_ms = 0;

#if defined(COMMS_SELFTEST)
// Automated end-to-end comms exercise (no referee touch needed): once a car
// and at least one client have registered, auto-pair them, start a match, and
// periodically fire LED commands at each device so every hop can be verified
// from serial. Enabled only in the `server_selftest` build.
static void commsSelftest(uint32_t now) {
    static uint32_t last_step = 0;
    static int      step = 0;
    static bool     paired_done = false;
    static bool     started = false;

    if (now - last_step < 2500) return;
    last_step = now;

    if (!paired_done) {
        if (!pairing.cars().empty() && !pairing.clients().empty()) {
            Serial.println("[SELFTEST] pairing all clients to car 0");
            for (size_t c = 0; c < pairing.clients().size(); c++) {
                pairing.assignPair(0, (uint8_t)c);
            }
            pairing.confirmPairings(EspNowTransport::instance(), seq++);
            paired_done = true;
        } else {
            Serial.printf("[SELFTEST] waiting for devices: cars=%d clients=%d\n",
                (int)pairing.cars().size(), (int)pairing.clients().size());
        }
        return;
    }

    if (!started) {
        Serial.println("[SELFTEST] starting match");
        game.startMatch(now);
        started = true;
        return;
    }

    // Cycle LED commands across server / car / clients so each LED path and
    // the server->device relay are exercised and visible in serial.
    LedCmdMsg led{};
    led.header.type   = MessageType::LED_CMD;
    led.header.src_id = 0;
    led.header.seq    = seq++;
    led.on            = (step % 2 == 0) ? 1 : 0;
    switch (step % 3) {
        case 0:
            led.target_type = DeviceType::SERVER; led.target_id = 0;
            digitalWrite(PIN_LED, led.on ? LOW : HIGH);
            Serial.printf("[SELFTEST] LED server %s\n", led.on ? "ON" : "OFF");
            break;
        case 1:
            if (!pairing.cars().empty()) {
                const auto& car = pairing.cars()[0];
                led.target_type = DeviceType::CAR; led.target_id = car.device_id;
                EspNowTransport::instance().addPeer(car.mac);
                EspNowTransport::instance().send(car.mac, &led, sizeof(led));
                Serial.printf("[SELFTEST] LED car#%d %s\n", car.device_id, led.on ? "ON" : "OFF");
            }
            break;
        case 2:
            if (!pairing.clients().empty()) {
                const auto& cli = pairing.clients()[0];
                led.target_type = DeviceType::CLIENT; led.target_id = cli.device_id;
                EspNowTransport::instance().addPeer(cli.mac);
                EspNowTransport::instance().send(cli.mac, &led, sizeof(led));
                Serial.printf("[SELFTEST] LED client#%d %s\n", cli.device_id, led.on ? "ON" : "OFF");
            }
            break;
    }
    step++;
}
#endif

void loop() {
    uint32_t now = millis();
    EspNowTransport::instance().poll();
    game.tick(now);
    status.update(game.context(), now);  // strip + buzzer follow game state

#if defined(COMMS_SELFTEST)
    commsSelftest(now);
#endif

    // Periodic state broadcast (not just on game events) so the referee's
    // per-device online/offline indicators stay live.
    if (now - last_state_broadcast_ms > 1000) {
        last_state_broadcast_ms = now;
        broadcastGameState();
    }

    if (now - last_hb_ms > 3000) {
        last_hb_ms = now;
        Serial.printf("[HB] t=%lus referee=%d\n", now/1000, referee_known);
        // Test broadcast: send a PING so we can see if car receives us
        PingMsg ping{};
        ping.header.type   = MessageType::PING;
        ping.header.src_id = 0;
        ping.header.seq    = seq++;
        bool ok = EspNowTransport::instance().sendBroadcast(&ping, sizeof(ping));
        Serial.printf("[HB] test_bcast=%d\n", ok);
    }
}
