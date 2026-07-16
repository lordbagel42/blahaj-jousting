#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <lvgl.h>
#include "espnow_transport.h"
#include "protocol.h"
#include "self_id.h"
#include "ui.h"

// CYD touch is on its own SPI bus (not shared with TFT)
static constexpr uint8_t TOUCH_CS   = 33;
static constexpr uint8_t TOUCH_IRQ  = 36;
static constexpr uint8_t TOUCH_SCK  = 25;
static constexpr uint8_t TOUCH_MISO = 39;
static constexpr uint8_t TOUCH_MOSI = 32;

static constexpr int32_t SCREEN_W = 320;
static constexpr int32_t SCREEN_H = 240;

SPIClass            touchSPI(VSPI);
TFT_eSPI            tft;
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

static uint8_t server_mac[6] = {};
static bool    server_known  = false;
static uint8_t seq           = 0;

// ── ui.h glue: how the UI actually sends messages / restarts ──────────────────

static void sendToServerImpl(const void* data, size_t len) {
    bool peer_ok = EspNowTransport::instance().addPeer(server_mac);
    bool send_ok = EspNowTransport::instance().send(server_mac, data, len);
    Serial.printf("[TX] peer=%d send=%d to %02X:%02X:%02X:%02X:%02X:%02X\n",
        peer_ok, send_ok,
        server_mac[0],server_mac[1],server_mac[2],
        server_mac[3],server_mac[4],server_mac[5]);
}

static void sendBroadcastImpl(const void* data, size_t len) {
    EspNowTransport::instance().sendBroadcast(data, len);
}

static void restartImpl() {
    delay(50);
    ESP.restart();
}

static void sendHello() {
    HelloMsg hello{};
    hello.header.type   = MessageType::HELLO;
    hello.header.src_id = 0xFE;
    hello.header.seq    = seq++;
    hello.device_type   = DeviceType::REFEREE;
    hello.device_id     = 0xFE;
    Serial.println("[TX] HELLO broadcast");
    EspNowTransport::instance().sendBroadcast(&hello, sizeof(hello));
}

// ── LVGL <-> TFT_eSPI / touch bridging ─────────────────────────────────────────

static void lvglFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixels(reinterpret_cast<uint16_t*>(px_map), w * h);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

static uint32_t lvglTickCb() { return millis(); }

static void lvglTouchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    if (!touch.tirqTouched() || !touch.touched()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    TS_Point p = touch.getPoint();
    int x = map(p.x, 200, 3900, 0, SCREEN_W);
    int y = map(p.y, 200, 3900, 0, SCREEN_H);
    data->point.x = constrain(x, 0, SCREEN_W - 1);
    data->point.y = constrain(y, 0, SCREEN_H - 1);
    data->state   = LV_INDEV_STATE_PRESSED;
}

// ── ESP-NOW receive ───────────────────────────────────────────────────────────

static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(MessageHeader)) {
        Serial.printf("[RX] too short: %d bytes\n", len);
        return;
    }
    const auto* hdr = reinterpret_cast<const MessageHeader*>(data);
    Serial.printf("[RX] type=%d len=%d from %02X:%02X:%02X:%02X:%02X:%02X\n",
        (int)hdr->type, len, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

    switch (hdr->type) {
        case MessageType::HELLO_ACK:
            Serial.println("[RX] HELLO_ACK -> server found!");
            if (!server_known) {
                memcpy(server_mac, mac, 6);
                server_known = true;
                ui_set_server_known(true);
            }
            break;

        case MessageType::HELLO:
            if (len >= (int)sizeof(HelloMsg)) {
                const auto* msg = reinterpret_cast<const HelloMsg*>(data);
                Serial.printf("[RX] HELLO device type=%d id=%d\n",
                    (int)msg->device_type, msg->device_id);
                ui_handle_hello(mac, msg->device_type, msg->device_id, msg->axis_mask);
            }
            break;

        case MessageType::GAME_STATE_BROADCAST:
            if (len >= (int)sizeof(GameStateBroadcastMsg)) {
                const auto* msg = reinterpret_cast<const GameStateBroadcastMsg*>(data);
                if (!server_known) {
                    memcpy(server_mac, mac, 6);
                    server_known = true;
                    ui_set_server_known(true);
                }
                ui_handle_state_broadcast(*msg);
                Serial.printf("[RX] STATE_BROADCAST state=%d round=%d pairs=%d\n",
                    (int)msg->state, msg->round, msg->pair_count);
            }
            break;

        default:
            Serial.printf("[RX] unhandled type=%d\n", (int)hdr->type);
            break;
    }
}

// ── Setup / loop ──────────────────────────────────────────────────────────────

static uint8_t lvglBuf[SCREEN_W * 10 * 2];  // partial render buffer (10 rows)

void setup() {
    Serial.begin(115200);
    Serial.println("Referee starting...");

    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);  // LVGL's RGB565 buffers are byte-swapped vs. what TFT_eSPI expects
    tft.fillScreen(TFT_BLACK);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // TFT_eSPI uses HSPI; configure default SPI (VSPI) for touch
    SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin();
    touch.setRotation(1);

    lv_init();
    lv_tick_set_cb(lvglTickCb);

    lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, lvglFlushCb);
    lv_display_set_buffers(disp, lvglBuf, nullptr, sizeof(lvglBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvglTouchReadCb);

    ui_init(sendToServerImpl, sendBroadcastImpl, restartImpl);
    ui_build();

    // ESP32 in STA-only mode does background channel scans which break ESP-NOW reception.
    // Starting a dummy AP anchors the radio to a fixed channel.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("BlahajRef", nullptr, ESPNOW_CHANNEL);  // hidden AP just to lock channel
    delay(200);

    esp_err_t ch_err = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.printf("esp_wifi_set_channel: %d (%s)\n", ch_err, esp_err_to_name(ch_err));
    delay(100);

    auto& transport = EspNowTransport::instance();
    bool ok = transport.begin(ESPNOW_CHANNEL);
    Serial.printf("transport.begin: %s\n", ok ? "OK" : "FAILED");
    transport.onReceive(onReceive);

    uint8_t my_mac[6];
    transport.getMac(my_mac);
    printSelfId(DeviceType::REFEREE, 0xFE, 0, my_mac);

    Serial.println("ESP-NOW ready, sending HELLO...");
    sendHello();
}

static uint32_t last_hello_ms = 0;

void loop() {
    uint32_t now = millis();
    EspNowTransport::instance().poll();

    if (!server_known && now - last_hello_ms > 2000) {
        last_hello_ms = now;
        sendHello();
    }

    ui_tick();

    lv_timer_handler();
    delay(5);
}
