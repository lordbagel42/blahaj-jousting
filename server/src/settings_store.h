// server/src/settings_store.h
//
// Tiny EEPROM-backed persistence for server settings that must survive reboots.
// Currently just the status-strip brightness. Self-healing: a blank or corrupt
// EEPROM (bad magic/checksum) reads back the safe default rather than garbage.
#pragma once
#include <cstdint>

namespace SettingsStore {

inline constexpr uint8_t DEFAULT_BRIGHTNESS = 64;    // sane for 150 LEDs (full white ~9A)
inline constexpr bool    DEFAULT_IDLE_BLUE  = true;  // strip glows blue when idle (LOBBY)

// Call once in setup() before any get/set.
void begin();

uint8_t brightness();
bool    idleBlue();

// Each setter persists only if the value actually changed (flash-wear guard)
// and returns the stored value.
uint8_t setBrightness(uint8_t value);
bool    setIdleBlue(bool value);

}  // namespace SettingsStore
