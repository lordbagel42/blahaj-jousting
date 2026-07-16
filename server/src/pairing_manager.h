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
    uint8_t    axis_mask    = 0;     // CLIENT only: DRIVE_AXIS_* role
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
    // Returns false if indices are out of range.
    bool assignPair(uint8_t car_idx, uint8_t client_idx);

    // Send HELLO_ACK to all paired devices.
    void confirmPairings(EspNowTransport& transport, uint8_t server_seq);

    // If the device with this MAC is paired, re-send the HELLO_ACK for its
    // pair. Lets a device that rebooted (and came back up unpaired) re-learn
    // its pairing without referee intervention. Returns true if sent.
    bool reconfirm(const uint8_t* mac, EspNowTransport& transport, uint8_t server_seq);

    void updateLastSeen(const uint8_t* mac, uint32_t now_ms);

    const std::vector<DeviceInfo>& cars()    const { return _cars;    }
    const std::vector<DeviceInfo>& clients() const { return _clients; }

    bool allPaired() const;

private:
    DeviceInfo* findByMac(const uint8_t* mac);

    std::vector<DeviceInfo> _cars;
    std::vector<DeviceInfo> _clients;
};
