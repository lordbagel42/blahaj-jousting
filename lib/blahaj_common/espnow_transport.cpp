// lib/blahaj_common/espnow_transport.cpp
#include "espnow_transport.h"
#include <Arduino.h>
#include <cstring>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <espnow.h>
#else
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#endif

EspNowTransport& EspNowTransport::instance() {
    static EspNowTransport inst;
    return inst;
}

#if defined(ESP8266)

bool EspNowTransport::begin(uint8_t /*channel*/) {
    if (esp_now_init() != 0) return false;
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(recvCallback);
    esp_now_register_send_cb(sendCallback);
    return true;
}

bool EspNowTransport::send(const uint8_t* mac, const void* data, size_t len) {
    return esp_now_send(const_cast<uint8_t*>(mac),
        static_cast<uint8_t*>(const_cast<void*>(data)), len) == 0;
}

bool EspNowTransport::sendBroadcast(const void* data, size_t len) {
    if (!_broadcast_peer_added) {
        _broadcast_peer_added = (esp_now_add_peer(const_cast<uint8_t*>(ESP_NOW_BROADCAST_MAC),
            ESP_NOW_ROLE_COMBO, 0, nullptr, 0) == 0);
    }
    if (!_broadcast_peer_added) return false;
    return esp_now_send(const_cast<uint8_t*>(ESP_NOW_BROADCAST_MAC),
        static_cast<uint8_t*>(const_cast<void*>(data)), len) == 0;
}

bool EspNowTransport::addPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(const_cast<uint8_t*>(mac))) return true;
    return esp_now_add_peer(const_cast<uint8_t*>(mac), ESP_NOW_ROLE_COMBO, 0, nullptr, 0) == 0;
}

void EspNowTransport::getMac(uint8_t out_mac[6]) {
    WiFi.macAddress(out_mac);
}

void EspNowTransport::recvCallback(uint8_t* mac, uint8_t* data, uint8_t len) {
    Serial.printf("[ESP-NOW RX] len=%d from %02X:%02X:%02X:%02X:%02X:%02X\n",
        len, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    auto& inst = instance();
    int next_head = (inst._recv_head + 1) % RECV_QUEUE_SIZE;
    if (next_head == inst._recv_tail) return;  // queue full, drop

    RecvEntry& entry = inst._recv_queue[inst._recv_head];
    memcpy(entry.mac, mac, 6);
    int copy_len = len < 250 ? len : 250;
    memcpy(entry.data, data, copy_len);
    entry.len = copy_len;
    inst._recv_head = next_head;  // publish (single-core: assignment is atomic)
}

void EspNowTransport::sendCallback(uint8_t* mac, uint8_t status) {
    Serial.printf("[ESP-NOW TX] to %02X:%02X status=%d\n", mac[4], mac[5], status);
    auto& cb = instance()._send_cb;
    if (cb) cb(mac, status == 0);
}

#else

bool EspNowTransport::begin(uint8_t /*channel*/) {
    // Channel is determined by the WiFi connection (shared AP) on all device roles.
    if (esp_now_init() != ESP_OK) return false;

    esp_now_register_recv_cb(recvCallback);
    esp_now_register_send_cb(sendCallback);
    return true;
}

bool EspNowTransport::send(const uint8_t* mac, const void* data, size_t len) {
    return esp_now_send(mac, static_cast<const uint8_t*>(data), len) == ESP_OK;
}

bool EspNowTransport::sendBroadcast(const void* data, size_t len) {
    if (!_broadcast_peer_added) {
        esp_now_peer_info_t peer{};
        memcpy(peer.peer_addr, ESP_NOW_BROADCAST_MAC, 6);
        peer.channel = 0;
        peer.encrypt = false;
        _broadcast_peer_added = (esp_now_add_peer(&peer) == ESP_OK);
    }
    if (!_broadcast_peer_added) return false;
    return esp_now_send(ESP_NOW_BROADCAST_MAC, static_cast<const uint8_t*>(data), len) == ESP_OK;
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

void EspNowTransport::recvCallback(const uint8_t* mac, const uint8_t* data, int len) {
    Serial.printf("[ESP-NOW RX] len=%d from %02X:%02X:%02X:%02X:%02X:%02X\n",
        len, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    auto& inst = instance();
    int next_head = (inst._recv_head + 1) % RECV_QUEUE_SIZE;
    if (next_head == inst._recv_tail) return;  // queue full, drop

    RecvEntry& entry = inst._recv_queue[inst._recv_head];
    memcpy(entry.mac, mac, 6);
    int copy_len = len < 250 ? len : 250;
    memcpy(entry.data, data, copy_len);
    entry.len = copy_len;
    inst._recv_head = next_head;  // publish (single-core: assignment is atomic)
}

void EspNowTransport::sendCallback(const uint8_t* mac, esp_now_send_status_t status) {
    Serial.printf("[ESP-NOW TX] to %02X:%02X status=%d\n", mac[4], mac[5], status);
    auto& cb = instance()._send_cb;
    if (cb) cb(mac, status == ESP_NOW_SEND_SUCCESS);
}

#endif

void EspNowTransport::poll() {
    while (_recv_tail != _recv_head) {
        RecvEntry& entry = _recv_queue[_recv_tail];
        if (_recv_cb) _recv_cb(entry.mac, entry.data, entry.len);
        _recv_tail = (_recv_tail + 1) % RECV_QUEUE_SIZE;
    }
}
