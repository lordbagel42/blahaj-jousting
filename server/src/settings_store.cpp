// server/src/settings_store.cpp
#include "settings_store.h"
#include <Arduino.h>
#include <EEPROM.h>

namespace {
constexpr int     EEPROM_SIZE = 8;
constexpr int     ADDR_MAGIC  = 0;
constexpr int     ADDR_BRIGHT = 1;
constexpr int     ADDR_IDLE   = 2;
constexpr int     ADDR_CKSUM  = 3;
constexpr uint8_t MAGIC       = 0xB2;  // bumped from 0xB1 when idle_blue was added

uint8_t g_brightness = SettingsStore::DEFAULT_BRIGHTNESS;
bool    g_idle_blue  = SettingsStore::DEFAULT_IDLE_BLUE;

uint8_t checksum(uint8_t bright, uint8_t idle) { return uint8_t(MAGIC ^ bright ^ idle ^ 0x5A); }

void persist() {
    EEPROM.write(ADDR_MAGIC,  MAGIC);
    EEPROM.write(ADDR_BRIGHT, g_brightness);
    EEPROM.write(ADDR_IDLE,   g_idle_blue ? 1 : 0);
    EEPROM.write(ADDR_CKSUM,  checksum(g_brightness, g_idle_blue ? 1 : 0));
    EEPROM.commit();
}
}  // namespace

namespace SettingsStore {

void begin() {
    EEPROM.begin(EEPROM_SIZE);
    uint8_t magic  = EEPROM.read(ADDR_MAGIC);
    uint8_t bright = EEPROM.read(ADDR_BRIGHT);
    uint8_t idle   = EEPROM.read(ADDR_IDLE);
    uint8_t cksum  = EEPROM.read(ADDR_CKSUM);

    if (magic == MAGIC && cksum == checksum(bright, idle)) {
        g_brightness = bright;
        g_idle_blue  = (idle != 0);
        Serial.printf("[SETTINGS] loaded brightness=%u idle_blue=%d\n", g_brightness, g_idle_blue);
    } else {
        g_brightness = DEFAULT_BRIGHTNESS;
        g_idle_blue  = DEFAULT_IDLE_BLUE;
        Serial.printf("[SETTINGS] blank/corrupt EEPROM, defaults brightness=%u idle_blue=%d\n",
            g_brightness, g_idle_blue);
    }
}

uint8_t brightness() { return g_brightness; }
bool    idleBlue()   { return g_idle_blue; }

uint8_t setBrightness(uint8_t value) {
    if (value == g_brightness) return g_brightness;  // no-op / no flash write
    g_brightness = value;
    persist();
    Serial.printf("[SETTINGS] saved brightness=%u\n", value);
    return g_brightness;
}

bool setIdleBlue(bool value) {
    if (value == g_idle_blue) return g_idle_blue;
    g_idle_blue = value;
    persist();
    Serial.printf("[SETTINGS] saved idle_blue=%d\n", value);
    return g_idle_blue;
}

}  // namespace SettingsStore
