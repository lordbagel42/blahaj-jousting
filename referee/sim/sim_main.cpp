// referee/sim/sim_main.cpp
//
// Native/desktop harness for the referee UI: links the same ui.cpp against
// the same LVGL library the firmware uses, renders into an offscreen RGB565
// buffer via a flush_cb (no real display/SDL needed), and dumps PNG
// screenshots — for fast visual iteration without flashing hardware.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <lvgl.h>
#include "ui.h"

static constexpr int32_t SCREEN_W = 320;
static constexpr int32_t SCREEN_H = 240;

static uint16_t framebuf[SCREEN_W * SCREEN_H];

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint16_t* src = reinterpret_cast<uint16_t*>(px_map);
    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) {
            framebuf[y * SCREEN_W + x] = *src++;
        }
    }
    lv_display_flush_ready(disp);
}

static uint32_t fake_tick_ms = 0;
static uint32_t tickCb() { return fake_tick_ms; }

// No real touch in the sim (yet) — indev is required by LVGL but stays idle.
static void touchReadCb(lv_indev_t*, lv_indev_data_t* data) {
    data->state = LV_INDEV_STATE_RELEASED;
}

static void stubSend(const void*, size_t) { printf("[sim] (send suppressed)\n"); }
static void stubRestart() { printf("[sim] (restart suppressed)\n"); }

// Writes framebuf out as a P6 PPM (trivial, no library needed); convert with
// `magick shot.ppm shot.png` or PIL afterwards.
static void dumpPPM(const char* path) {
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint16_t px = framebuf[i];
        uint8_t r = ((px >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((px >> 5)  & 0x3F) * 255 / 63;
        uint8_t b = (px         & 0x1F) * 255 / 31;
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
}

// Drives the input pipeline the same way real touch would: set a point +
// pressed, let LVGL process it, then release — so hit-testing, scrolling,
// and click events all go through the exact same code path as hardware.
static int32_t tap_x = -1, tap_y = -1;
static bool    tap_pressed = false;

static void realTouchReadCb(lv_indev_t*, lv_indev_data_t* data) {
    data->point.x = tap_x;
    data->point.y = tap_y;
    data->state   = tap_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void pump(int frames = 3) {
    for (int i = 0; i < frames; i++) {
        fake_tick_ms += 10;
        ui_tick();
        lv_timer_handler();
    }
}

static void simTap(int x, int y) {
    tap_x = x; tap_y = y; tap_pressed = true;
    pump(3);
    tap_pressed = false;
    pump(3);
}

int main(int argc, char** argv) {
    lv_init();
    lv_tick_set_cb(tickCb);

    lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flushCb);
    static uint8_t buf[SCREEN_W * 20 * 2];
    lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, realTouchReadCb);

    ui_init(stubSend, stubSend, stubRestart);
    ui_build();
    pump(5);

    // ── Scenario: reproduce "car label disappears after pairing 2 controllers" ──
    uint8_t car_mac[6]  = {0xAA, 0, 0, 0, 0, 1};
    uint8_t cli_a_mac[6] = {0xBB, 0, 0, 0, 0, 1};
    uint8_t cli_b_mac[6] = {0xBB, 0, 0, 0, 0, 2};

    ui_set_server_known(true);
    ui_handle_hello(car_mac,  DeviceType::CAR,    1, 0);
    ui_handle_hello(cli_a_mac, DeviceType::CLIENT, 1, DRIVE_AXIS_STEERING);
    ui_handle_hello(cli_b_mac, DeviceType::CLIENT, 1, DRIVE_AXIS_THROTTLE);
    pump(5);
    dumpPPM("/tmp/sim_1_unpaired.ppm");
    printf("Wrote /tmp/sim_1_unpaired.ppm (Cars tab, before pairing)\n");

    GameStateBroadcastMsg msg{};
    msg.header.type = MessageType::GAME_STATE_BROADCAST;
    msg.state = GameState::LOBBY;
    msg.round = 0;
    msg.pair_count = 2;
    msg.pairs[0] = PairInfo{1, 1, DRIVE_AXIS_STEERING, 1, 1};
    msg.pairs[1] = PairInfo{1, 1, DRIVE_AXIS_THROTTLE, 1, 1};
    ui_handle_state_broadcast(msg);
    pump(5);
    dumpPPM("/tmp/sim_2_paired.ppm");
    printf("Wrote /tmp/sim_2_paired.ppm (Cars tab, both controllers paired)\n");

    // ── Now replicate the ACTUAL reported flow: tap the Car row to select it
    // (green background, per the "sticky selection" pairing UX), while it's
    // ALSO marked paired (green text) — this is what the user actually did.
    simTap(150, 85);
    dumpPPM("/tmp/sim_3_selected_and_paired.ppm");
    printf("Wrote /tmp/sim_3_selected_and_paired.ppm (Car row tapped/selected while paired)\n");

    ui_debug_set_tab(2);  // Match tab
    pump(5);
    dumpPPM("/tmp/sim_5_match_tab.ppm");
    printf("Wrote /tmp/sim_5_match_tab.ppm\n");

    return 0;
}
