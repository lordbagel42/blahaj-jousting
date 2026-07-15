#include <Arduino.h>
#include <WiFi.h>
#include "espnow_transport.h"
#include "game_engine.h"
#include "pairing_manager.h"
#include "web_ui.h"
#include "protocol.h"

// ── Globals ────────────────────────────────────────────────────────────────────
EventBus<GameEvent, GameContext> bus;
GameEngine    game(bus);
PairingManager pairing;
WebUI         ui(pairing, game);

static uint8_t seq = 0;

// Broadcast full game state to all paired devices.
static void broadcastGameState() {
    const auto& ctx = game.context();
    GameStateBroadcastMsg msg{};
    msg.header.type    = MessageType::GAME_STATE_BROADCAST;
    msg.header.src_id  = 0;
    msg.header.seq     = seq++;
    msg.state          = ctx.state;
    msg.round          = ctx.round;
    memcpy(msg.round_wins, ctx.round_wins, sizeof(msg.round_wins));
    memcpy(msg.knockoffs,  ctx.knockoffs,  sizeof(msg.knockoffs));
    msg.countdown_remaining  = ctx.countdown_remaining;
    msg.cars_eliminated      = ctx.cars_eliminated;
    msg.last_knockoff_car_id = ctx.last_knockoff_car_id;

    auto& t = EspNowTransport::instance();
    for (const auto& car    : pairing.cars())    if (car.paired)    t.send(car.mac,    &msg, sizeof(msg));
    for (const auto& client : pairing.clients()) if (client.paired) t.send(client.mac, &msg, sizeof(msg));
}

// ESP-NOW receive callback (called from poll() in loop context).
static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(MessageHeader)) return;
    const auto* hdr = reinterpret_cast<const MessageHeader*>(data);
    uint32_t now = millis();

    pairing.updateLastSeen(mac, now);

    switch (hdr->type) {
        case MessageType::HELLO:
            if (len >= (int)sizeof(HelloMsg))
                pairing.onHello(mac, *reinterpret_cast<const HelloMsg*>(data), now);
            break;

        case MessageType::KNOCKOFF_EVENT:
            if (len >= (int)sizeof(KnockoffEventMsg)) {
                const auto* msg = reinterpret_cast<const KnockoffEventMsg*>(data);
                game.onKnockoff(msg->car_id, now);
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

void setup() {
    Serial.begin(115200);

    // Register event bus handlers — broadcast on every state/score change.
    bus.on(GameEvent::STATE_CHANGED,  [](const GameContext&) { broadcastGameState(); });
    bus.on(GameEvent::KNOCKOFF,       [](const GameContext&) { broadcastGameState(); });
    bus.on(GameEvent::COUNTDOWN_TICK, [](const GameContext&) { broadcastGameState(); });
    // Add hardware handlers here:
    // bus.on(GameEvent::MATCH_START, [](const GameContext&) { digitalWrite(PIN_LED, HIGH); });

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("BlahajJousting", nullptr, ESPNOW_CHANNEL);

    auto& transport = EspNowTransport::instance();
    if (!transport.begin(ESPNOW_CHANNEL)) {
        Serial.println("ERROR: ESP-NOW init failed");
    }
    transport.onReceive(onReceive);

    ui.begin();

    Serial.println("Server ready. Connect to 'BlahajJousting' and open 192.168.4.1");
}

void loop() {
    uint32_t now = millis();

    EspNowTransport::instance().poll();  // drain recv ring buffer (thread safety)

    if (ui.hasPendingPair()) {
        for (const auto& pa : ui.pendingPairs())
            pairing.assignPair(pa.car_idx, pa.client_idx);
        pairing.confirmPairings(EspNowTransport::instance(), seq++);
    }
    if (ui.hasPendingStart()) game.startMatch(now);
    if (ui.hasPendingEnd())   game.endRound(now);

    game.tick(now);
}
