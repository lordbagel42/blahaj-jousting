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

void EspNowTransport::recvCallback(const uint8_t* mac, const uint8_t* data, int len) {
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

void EspNowTransport::poll() {
    while (_recv_tail != _recv_head) {
        RecvEntry& entry = _recv_queue[_recv_tail];
        if (_recv_cb) _recv_cb(entry.mac, entry.data, entry.len);
        _recv_tail = (_recv_tail + 1) % RECV_QUEUE_SIZE;
    }
}

void EspNowTransport::sendCallback(const uint8_t* mac, esp_now_send_status_t status) {
    auto& cb = instance()._send_cb;
    if (cb) cb(mac, status == ESP_NOW_SEND_SUCCESS);
}
