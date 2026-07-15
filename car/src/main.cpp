// car/src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "drv8833_driver.h"
#include "knockoff_detector.h"
#include "car_node.h"

// ── Pin assignments (edit to match your wiring) ───────────────────────────────
static constexpr uint8_t PIN_AIN1          = 4;
static constexpr uint8_t PIN_AIN2          = 5;
static constexpr uint8_t PIN_BIN1          = 6;
static constexpr uint8_t PIN_BIN2          = 7;
static constexpr uint8_t PIN_SHARK_SWITCH  = 3;

// ── Hardware instances ─────────────────────────────────────────────────────────
DRV8833Driver     motors(PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2);
KnockoffDetector  knockoff(PIN_SHARK_SWITCH, /*active_low=*/true);
CarNode           car(motors, knockoff);

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);

    car.begin();

    // ── Register game state handlers here ──────────────────────────────────────
    // Example: car.events().on(GameEvent::ROUND_END, [](const GameContext&) { ... });

    Serial.println("Car ready, waiting for server pairing...");
}

void loop() {
    car.tick(millis());
}
