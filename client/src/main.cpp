#include <Arduino.h>
#include <WiFi.h>
#include "joystick_input.h"
#include "button_input.h"
#include "client_node.h"

static constexpr uint8_t PIN_JOY_X      = 1;
static constexpr uint8_t PIN_JOY_Y      = 2;
static constexpr uint8_t PIN_STATUS_LED = 8;

JoystickInput input(PIN_JOY_X, PIN_JOY_Y);
// ButtonInput input(PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT);  // alternative

ClientNode client(input);

void setup() {
    Serial.begin(115200);

    pinMode(PIN_STATUS_LED, OUTPUT);

    // Register game state handlers.
    client.events().on(GameEvent::MATCH_START, [](const GameContext&) {
        digitalWrite(PIN_STATUS_LED, HIGH);
    });
    client.events().on(GameEvent::MATCH_END, [](const GameContext&) {
        digitalWrite(PIN_STATUS_LED, LOW);
    });

    WiFi.mode(WIFI_STA);
    client.begin();

    Serial.println("Client ready — waiting for pairing");
}

void loop() {
    client.tick(millis());
}
