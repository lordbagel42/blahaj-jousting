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
    for (size_t i = 0; i < _cars.size(); i++) {
        auto& car = _cars[i];
        if (!car.paired) continue;
        if (car.partner_slot >= _clients.size()) continue;
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
