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

    // Call once in setup(), after WiFi.mode() has been set.
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
    EspNowTransport& operator=(const EspNowTransport&) = delete;

    static void recvCallback(const esp_now_recv_info_t* info, const uint8_t* data, int len);
    static void sendCallback(const uint8_t* mac, esp_now_send_status_t status);

    ReceiveCallback _recv_cb;
    SendCallback    _send_cb;
    bool            _broadcast_peer_added = false;
};
