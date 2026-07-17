#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#include <esp_wifi.h>
#endif
#include "joystick_input.h"
#include "button_input.h"
#include "single_axis_joystick.h"
#include "client_node.h"
#include "espnow_transport.h"
#include "self_id.h"

// ── Input selection ────────────────────────────────────────────────────────
// A D1 Mini has only one ADC pin (A0), so a full 2-axis analog joystick is
// split across two physical client devices, each wired to one axis of the
// same joystick module and reporting only that axis. Select which half this
// build is via -D CLIENT_AXIS_THROTTLE or -D CLIENT_AXIS_STEERING.
#if defined(CLIENT_AXIS_STEERING)
SingleAxisJoystick input(A0, SingleAxisJoystick::Axis::STEERING);
ClientNode         client(input, DRIVE_AXIS_STEERING);
#elif defined(CLIENT_AXIS_THROTTLE)
SingleAxisJoystick input(A0, SingleAxisJoystick::Axis::THROTTLE);
ClientNode         client(input, DRIVE_AXIS_THROTTLE);
#else
// Full 2-axis joystick — only valid on hardware with two independent ADC
// pins (e.g. ESP32). Not usable on D1 Mini.
static constexpr uint8_t PIN_JOY_X = 32;
static constexpr uint8_t PIN_JOY_Y = 33;
JoystickInput input(PIN_JOY_X, PIN_JOY_Y);
// ButtonInput input(PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT);  // alternative
ClientNode client(input);
#endif

#if defined(ESP8266)
static constexpr uint8_t PIN_STATUS_LED = LED_BUILTIN;  // D1 Mini onboard LED, active LOW
#else
static constexpr uint8_t PIN_STATUS_LED = 8;
#endif

void setup() {
    Serial.begin(115200);

    pinMode(PIN_STATUS_LED, OUTPUT);

    // Register game state handlers.
    client.events().on(GameEvent::MATCH_START, [](const GameContext&) {
#if defined(ESP8266)
        digitalWrite(PIN_STATUS_LED, LOW);   // active LOW: on
#else
        digitalWrite(PIN_STATUS_LED, HIGH);
#endif
    });
    client.events().on(GameEvent::MATCH_END, [](const GameContext&) {
#if defined(ESP8266)
        digitalWrite(PIN_STATUS_LED, HIGH);  // active LOW: off
#else
        digitalWrite(PIN_STATUS_LED, LOW);
#endif
    });

#if defined(ESP8266)
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.softAP("BlahajClient", nullptr, ESPNOW_CHANNEL);
    delay(100);
#else
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("BlahajClient", nullptr, ESPNOW_CHANNEL);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
#endif

    client.begin();

#if defined(CLIENT_AXIS_STEERING)
    constexpr uint8_t kAxisMask = DRIVE_AXIS_STEERING;
#elif defined(CLIENT_AXIS_THROTTLE)
    constexpr uint8_t kAxisMask = DRIVE_AXIS_THROTTLE;
#else
    constexpr uint8_t kAxisMask = DRIVE_AXIS_BOTH;
#endif
    uint8_t my_mac[6];
    EspNowTransport::instance().getMac(my_mac);
    printSelfId(DeviceType::CLIENT, DEVICE_ID, kAxisMask, my_mac);

    Serial.println("Client ready — waiting for pairing");
}

void loop() {
    client.tick(millis());
}
