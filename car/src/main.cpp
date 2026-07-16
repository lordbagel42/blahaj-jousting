// car/src/main.cpp
#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#include <esp_wifi.h>
#endif
#include "knockoff_detector.h"
#include "ultrasonic_sensor.h"
#include "car_node.h"
#include "self_id.h"

// Until the DRV8833 is physically wired, build with -D MOTOR_LOG_ONLY to swap
// in a driver that just logs drive/stop to serial (for comms verification).
#if defined(MOTOR_LOG_ONLY)
#include "logging_motor_driver.h"
#else
#include "drv8833_driver.h"
#endif

// ── Pin assignments (edit to match your wiring) ───────────────────────────────
// D1 Mini silkscreen -> GPIO:  D7=13 D6=12 D5=14 D8=15 D3=0 D2=4 D1=5
//
// DRV8833:  IN1/IN2 = LEFT motor, IN3/IN4 = RIGHT motor.
//   Left  channel (A): AIN1=GPIO13 (D7), AIN2=GPIO12 (D6)
//   Right channel (B): BIN1=GPIO14 (D5), BIN2=GPIO15 (D8)
//   (GPIO16/D0 cannot do PWM on the ESP8266, so the right channel's second
//    pin lives on GPIO15/D8 instead — move that wire from D0 to D8.)
// Knockoff/shark switch: GPIO0 (D3), active-low. Don't hold it pressed at boot
//   (GPIO0 low at reset enters flash mode).
// HC-SR04 ultrasonic (Trig/Echo, NOT I2C): Trig=GPIO4 (D2), Echo=GPIO5 (D1).
//   Echo is 5V — level-shift down to 3.3V before the ESP8266 pin.
#if defined(ESP8266)
static constexpr uint8_t PIN_AIN1           = 13;  // D7  left  IN1
static constexpr uint8_t PIN_AIN2           = 12;  // D6  left  IN2
static constexpr uint8_t PIN_BIN1           = 14;  // D5  right IN3
static constexpr uint8_t PIN_BIN2           = 15;  // D8  right IN4
static constexpr uint8_t PIN_SHARK_SWITCH   = 0;   // D3
static constexpr uint8_t PIN_ULTRASONIC_TRIG = 4;  // D2 (labeled SDA)
static constexpr uint8_t PIN_ULTRASONIC_ECHO = 5;  // D1 (labeled SCL)
#else
static constexpr uint8_t PIN_AIN1           = 4;
static constexpr uint8_t PIN_AIN2           = 5;
static constexpr uint8_t PIN_BIN1           = 6;
static constexpr uint8_t PIN_BIN2           = 7;
static constexpr uint8_t PIN_SHARK_SWITCH   = 3;
static constexpr uint8_t PIN_ULTRASONIC_TRIG = 21;
static constexpr uint8_t PIN_ULTRASONIC_ECHO = 22;
#endif

// Hardcoded cutoff: stop the motors whenever the ultrasonic reads *past* this
// distance (car lifted off the surface / off the edge). Flip the comparison in
// UltrasonicSensor::beyondCutoff() if you want the opposite (obstacle-too-close).
static constexpr uint16_t ULTRASONIC_CUTOFF_CM = 8;

// ── Hardware instances ─────────────────────────────────────────────────────────
#if defined(MOTOR_LOG_ONLY)
LoggingMotorDriver motors;
#else
DRV8833Driver     motors(PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2);
#endif
KnockoffDetector  knockoff(PIN_SHARK_SWITCH, /*active_low=*/true);
UltrasonicSensor  sonar(PIN_ULTRASONIC_TRIG, PIN_ULTRASONIC_ECHO, ULTRASONIC_CUTOFF_CM);
CarNode           car(motors, knockoff, &sonar);

void setup() {
    Serial.begin(115200);
#if defined(ESP8266)
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.softAP("BlahajCar", nullptr, ESPNOW_CHANNEL);
    delay(100);
#else
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("BlahajCar", nullptr, ESPNOW_CHANNEL);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_err_t ch_err = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.printf("ch_set: %d\n", ch_err);
#endif

    uint8_t my_mac[6];
    EspNowTransport::instance().getMac(my_mac);
    printSelfId(DeviceType::CAR, DEVICE_ID, 0, my_mac);

    sonar.begin();
    car.begin();

    // ── Register game state handlers here ──────────────────────────────────────
    // Example: car.events().on(GameEvent::ROUND_END, [](const GameContext&) { ... });

    Serial.println("Car ready, waiting for server pairing...");
}

void loop() {
    car.tick(millis());
}
