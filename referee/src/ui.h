// referee/src/ui.h
//
// Hardware-agnostic referee UI (LVGL widget tree + state). No Arduino, WiFi,
// or ESP-NOW dependency — only lvgl.h and the shared protocol headers — so
// it can be linked into both the real firmware and a native/desktop
// simulator (see referee/sim/) for fast visual iteration without flashing.
#pragma once
#include <cstddef>
#include <cstdint>
#include <lvgl.h>
#include "protocol.h"

using UiSendFn    = void (*)(const void* data, size_t len);
using UiRestartFn = void (*)();

// Call once before ui_build(), wiring up how the UI actually gets messages
// out and restarts the device. The real firmware passes real ESP-NOW/ESP.restart
// implementations; the simulator passes stubs that just log.
void ui_init(UiSendFn send_to_server, UiSendFn send_broadcast, UiRestartFn restart_fn);

// Builds the LVGL widget tree. Call once after lv_init() and display/indev setup.
void ui_build();

// Call periodically (e.g. once per loop()). Rebuilds the parts of the UI
// that need it if ui_mark_dirty() was called since the last tick.
void ui_tick();

void ui_mark_dirty();

// Feed events in from the transport layer (onReceive dispatch stays in
// main.cpp/the simulator harness since it's inherently platform-specific).
void ui_set_server_known(bool known);
void ui_handle_hello(const uint8_t* mac, DeviceType type, uint8_t device_id, uint8_t axis_mask);
void ui_handle_state_broadcast(const GameStateBroadcastMsg& msg);

// Testing/simulator only — jump directly to a tab (0=Cars, 1=Ctrl, 2=Match)
// (3=Test) without needing to land a simulated tap on the tab bar.
void ui_debug_set_tab(int idx);
