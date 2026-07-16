// referee/src/ui.cpp
//
// See ui.h. This file must stay free of Arduino/WiFi/ESP-NOW/TFT_eSPI
// includes so it can be linked into the native simulator unmodified.
#include "ui.h"
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#define UI_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define UI_LOG(...) printf(__VA_ARGS__)
#endif

static constexpr int32_t SCREEN_W = 320;
static constexpr int32_t SCREEN_H = 240;

static UiSendFn    g_send_to_server  = nullptr;
static UiSendFn    g_send_broadcast  = nullptr;
static UiRestartFn g_restart         = nullptr;

static bool        server_known = false;
static bool        led_on       = false;
static bool        idle_blue    = true;   // server strip glows blue when idle; synced from broadcast
static bool        dirty        = true;
static uint8_t     seq          = 0;
static GameContext ctx          = {};

struct SeenDevice {
    uint8_t    mac[6];
    DeviceType type;
    uint8_t    device_id;
    uint8_t    axis_mask = 0;   // CLIENT only: DRIVE_AXIS_* (which joystick axis)
    bool       valid = false;
};
static constexpr int MAX_SEEN = 32;
static SeenDevice seen[MAX_SEEN];
static int        seen_count      = 0;
static int        selected_car    = -1;
static int        selected_client = -1;

// Current pairing state, as last reported by the server (GameStateBroadcastMsg).
static PairInfo  paired_info[MAX_BROADCAST_PAIRS];
static uint8_t   paired_count = 0;

// A car can have more than one paired controller (e.g. throttle + steering
// halves), so report ALL of them: how many, and whether every one is online.
static int carPairSummary(uint8_t car_device_id, bool& all_online) {
    int count = 0;
    all_online = true;
    for (int i = 0; i < paired_count; i++) {
        if (paired_info[i].car_device_id != car_device_id) continue;
        count++;
        if (!paired_info[i].car_online || !paired_info[i].client_online) all_online = false;
    }
    return count;
}
static bool findPairForClient(uint8_t client_device_id, uint8_t axis_mask, PairInfo& out) {
    for (int i = 0; i < paired_count; i++)
        if (paired_info[i].client_device_id == client_device_id && paired_info[i].axis_mask == axis_mask)
            { out = paired_info[i]; return true; }
    return false;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static const char* stateName(GameState s) {
    switch (s) {
        case GameState::LOBBY:     return "LOBBY";
        case GameState::COUNTDOWN: return "COUNTDOWN";
        case GameState::RACING:    return "RACING";
        case GameState::ROUND_END: return "ROUND END";
    }
    return "?";
}

static void sendToServer(const void* data, size_t len) {
    if (!server_known) { UI_LOG("[TX] no server yet, dropping\n"); return; }
    if (g_send_to_server) g_send_to_server(data, len);
}

// Build the current car/client index lists (indices into `seen[]`), shared
// wherever we need to walk devices by type. The resulting position within
// each list (0-based) is exactly the car_idx/client_idx the server expects
// in PairCmdMsg, since the server builds its own lists in the same
// HELLO-arrival order.
static void buildLists(int* cars, int& car_cnt, int* clients, int& cli_cnt) {
    car_cnt = 0; cli_cnt = 0;
    for (int i = 0; i < seen_count; i++) {
        if (seen[i].type == DeviceType::CAR    && car_cnt < MAX_SEEN) cars[car_cnt++]    = i;
        if (seen[i].type == DeviceType::CLIENT && cli_cnt < MAX_SEEN) clients[cli_cnt++] = i;
    }
}

// CLIENT axis role -> "A"/"B" suffix + accent color. A = steering, B =
// throttle; both halves of one controller share a device_id, so they render
// as e.g. "Ctrl #1A" / "Ctrl #1B" rather than looking like two controllers.
static void axisTag(uint8_t axis_mask, char& suffix, lv_color_t& color) {
    if (axis_mask == DRIVE_AXIS_STEERING)      { suffix = 'A'; color = lv_palette_main(LV_PALETTE_CYAN); }
    else if (axis_mask == DRIVE_AXIS_THROTTLE) { suffix = 'B'; color = lv_palette_main(LV_PALETTE_ORANGE); }
    else                                        { suffix = 0;   color = lv_color_white(); }
}

// ── UI state ───────────────────────────────────────────────────────────────────

static lv_obj_t* status_bar;
static lv_obj_t* srv_label;
static lv_obj_t* state_label;
static lv_obj_t* round_label;
static lv_obj_t* car_list;
static lv_obj_t* ctrl_list;
static lv_obj_t* action_bar;
static lv_obj_t* action_label;
static lv_obj_t* start_btn;
static lv_obj_t* end_btn;
static lv_obj_t* led_btn;
static lv_obj_t* led_btn_label;

static lv_obj_t* match_state_label;
static lv_obj_t* match_round_label;
static lv_obj_t* bright_slider;
static lv_obj_t* bright_label;
static lv_obj_t* idle_blue_btn;
static bool      bright_dragging = false;  // don't fight the user's finger with server echoes
static lv_obj_t* pairings_list;
static lv_obj_t* g_tabview;  // exposed only for ui_debug_set_tab() (sim/testing)

static void updateStatusBar() {
    lv_label_set_text(srv_label, server_known ? "SRV OK" : "SRV ---");
    lv_obj_set_style_text_color(srv_label,
        server_known ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
    lv_label_set_text(state_label, stateName(ctx.state));

    // Compact form for the cramped header row.
    char compact[24];
    int coff = snprintf(compact, sizeof(compact), "R%d", ctx.round);
    for (int i = 0; i < 2 && coff < (int)sizeof(compact); i++)
        coff += snprintf(compact + coff, sizeof(compact) - coff, " %d:%d", i+1, ctx.round_wins[i]);
    lv_label_set_text(round_label, compact);

    // Full form for the Match tab, which has room to spare.
    char full[48];
    int off = snprintf(full, sizeof(full), "Rnd %d  Wins:", ctx.round);
    for (int i = 0; i < 2 && off < (int)sizeof(full); i++)
        off += snprintf(full + off, sizeof(full) - off, " P%d:%d", i+1, ctx.round_wins[i]);
    lv_label_set_text(match_state_label, stateName(ctx.state));
    lv_label_set_text(match_round_label, full);

    // Labels over a solid background can leave stale glyph pixels behind
    // when their text shrinks/changes; force the whole bar to repaint.
    lv_obj_invalidate(status_bar);
    lv_obj_invalidate(round_label);
    lv_obj_invalidate(match_state_label);
    lv_obj_invalidate(match_round_label);
}

static void updatePairingsList() {
    lv_obj_clean(pairings_list);
    if (paired_count == 0) {
        lv_list_add_text(pairings_list, "No pairings yet");
        return;
    }
    for (int i = 0; i < paired_count; i++) {
        const auto& p = paired_info[i];
        char suffix; lv_color_t color;
        axisTag(p.axis_mask, suffix, color);
        char lbl[48];
        snprintf(lbl, sizeof(lbl), "Car #%d %s %s Ctrl #%d%c %s",
            p.car_device_id, p.car_online ? LV_SYMBOL_OK : LV_SYMBOL_WARNING,
            LV_SYMBOL_RIGHT, p.client_device_id, suffix ? suffix : ' ',
            p.client_online ? LV_SYMBOL_OK : LV_SYMBOL_WARNING);
        lv_obj_t* row = lv_list_add_button(pairings_list, nullptr, lbl);
        bool both_online = p.car_online && p.client_online;
        lv_obj_set_style_text_color(row,
            both_online ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_ORANGE), 0);
    }
    lv_obj_invalidate(pairings_list);
}

// The LED button always targets whatever's currently selected in the Cars/
// Ctrl tabs (so you can pick a device, hit LED, and physically spot which
// unit lights up) — falling back to the Server when nothing's selected.
static void ledTarget(DeviceType& type, uint8_t& id, char* label, size_t label_size) {
    int cars[MAX_SEEN], car_cnt, clients[MAX_SEEN], cli_cnt;
    buildLists(cars, car_cnt, clients, cli_cnt);
    if (selected_car >= 0 && selected_car < car_cnt) {
        type = DeviceType::CAR;
        id   = seen[cars[selected_car]].device_id;
        snprintf(label, label_size, "LED Car#%d", id);
    } else if (selected_client >= 0 && selected_client < cli_cnt) {
        type = DeviceType::CLIENT;
        id   = seen[clients[selected_client]].device_id;
        char suffix; lv_color_t color; axisTag(seen[clients[selected_client]].axis_mask, suffix, color);
        snprintf(label, label_size, "LED Ctrl#%d%c", id, suffix ? suffix : ' ');
    } else {
        type = DeviceType::SERVER;
        id   = 0;
        snprintf(label, label_size, "LED Srv");
    }
}

static void updateActionButtons() {
    bool can_start = (ctx.state == GameState::LOBBY || ctx.state == GameState::ROUND_END);
    bool can_end   = (ctx.state == GameState::RACING);
    lv_obj_set_style_bg_color(start_btn, can_start ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_color(end_btn,   can_end   ? lv_palette_main(LV_PALETTE_RED)   : lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_color(led_btn,   led_on    ? lv_palette_main(LV_PALETTE_YELLOW): lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_color(idle_blue_btn, idle_blue ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_main(LV_PALETTE_GREY), 0);

    DeviceType t; uint8_t id; char lbl[20];
    ledTarget(t, id, lbl, sizeof(lbl));
    lv_label_set_text(led_btn_label, lbl);
    lv_obj_invalidate(led_btn);
}

static char last_paired_msg[40] = "";
static void doPair();  // forward decl; defined below with the other button actions

static void onCarClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    selected_car = (selected_car == i) ? -1 : i;
    last_paired_msg[0] = '\0';
    doPair();
    dirty = true;
}

static void onClientClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    selected_client = (selected_client == i) ? -1 : i;
    last_paired_msg[0] = '\0';
    doPair();
    dirty = true;
}

static void refreshLists() {
    lv_obj_clean(car_list);
    lv_obj_clean(ctrl_list);

    int cars[MAX_SEEN], car_cnt, clients[MAX_SEEN], cli_cnt;
    buildLists(cars, car_cnt, clients, cli_cnt);

    for (int i = 0; i < car_cnt; i++) {
        uint8_t car_id = seen[cars[i]].device_id;
        bool all_online;
        int pair_n = carPairSummary(car_id, all_online);

        char lbl[32];
        if (pair_n > 0) {
            snprintf(lbl, sizeof(lbl), "Car #%d  %s x%d %s", car_id,
                LV_SYMBOL_OK, pair_n, all_online ? "" : "(lost)");
        } else {
            snprintf(lbl, sizeof(lbl), "Car #%d", car_id);
        }
        bool car_selected = (selected_car == i);
        lv_obj_t* btn = lv_list_add_button(car_list, nullptr, lbl);
        lv_obj_set_style_bg_color(btn, car_selected
            ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_darken(LV_PALETTE_GREY, 2), 0);
        // Selection background already conveys state; a colored (green/red)
        // text on top of a green selection background can go unreadable, so
        // only color the text for paired status when NOT selected.
        if (car_selected) {
            lv_obj_set_style_text_color(btn, lv_color_white(), 0);
        } else if (pair_n > 0) {
            lv_obj_set_style_text_color(btn,
                all_online ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
        }
        lv_obj_add_event_cb(btn, onCarClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    for (int i = 0; i < cli_cnt; i++) {
        uint8_t client_id = seen[clients[i]].device_id;
        uint8_t axis_mask = seen[clients[i]].axis_mask;
        char suffix; lv_color_t axis_color;
        axisTag(axis_mask, suffix, axis_color);
        PairInfo pi;
        bool paired = findPairForClient(client_id, axis_mask, pi);

        char lbl[32];
        int off = snprintf(lbl, sizeof(lbl), "Ctrl #%d%c", client_id, suffix ? suffix : ' ');
        if (paired) snprintf(lbl + off, sizeof(lbl) - off, "  %s %s",
            LV_SYMBOL_OK, pi.client_online ? "" : "(lost)");
        bool client_selected = (selected_client == i);
        lv_obj_t* btn = lv_list_add_button(ctrl_list, nullptr, lbl);
        lv_obj_set_style_bg_color(btn, client_selected
            ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_darken(LV_PALETTE_GREY, 2), 0);
        lv_obj_set_style_text_color(btn,
            client_selected ? lv_color_white()
            : paired ? (pi.client_online ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED))
                     : axis_color, 0);
        lv_obj_add_event_cb(btn, onClientClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    lv_obj_invalidate(car_list);
    lv_obj_invalidate(ctrl_list);
}

static void updateActionBar() {
    if (last_paired_msg[0]) {
        lv_label_set_text(action_label, last_paired_msg);
        lv_obj_set_style_text_color(action_label, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else if (selected_car >= 0 && selected_client < 0) {
        lv_label_set_text(action_label, "Car selected " LV_SYMBOL_RIGHT " now pick a Controller");
        lv_obj_set_style_text_color(action_label, lv_palette_main(LV_PALETTE_CYAN), 0);
    } else if (selected_client >= 0 && selected_car < 0) {
        lv_label_set_text(action_label, "Controller selected " LV_SYMBOL_RIGHT " now pick a Car");
        lv_obj_set_style_text_color(action_label, lv_palette_main(LV_PALETTE_CYAN), 0);
    } else {
        lv_label_set_text(action_label, "Tap a Car, then a Controller, to pair them");
        lv_obj_set_style_text_color(action_label, lv_color_white(), 0);
    }
    lv_obj_invalidate(action_bar);
}

static void refreshUI() {
    refreshLists();
    updateStatusBar();
    updateActionButtons();
    updateActionBar();
    updatePairingsList();
}

// ── Button actions ──────────────────────────────────────────────────────────────

// Fires as soon as both a car and a controller are selected (from either
// tab) — no separate PAIR button to hunt for.
static void doPair() {
    if (selected_car < 0 || selected_client < 0) return;

    int cars[MAX_SEEN], car_cnt, clients[MAX_SEEN], cli_cnt;
    buildLists(cars, car_cnt, clients, cli_cnt);
    char suffix; lv_color_t color;
    axisTag(seen[clients[selected_client]].axis_mask, suffix, color);
    snprintf(last_paired_msg, sizeof(last_paired_msg), "Paired Car #%d %s Ctrl #%d%c",
        seen[cars[selected_car]].device_id, LV_SYMBOL_OK,
        seen[clients[selected_client]].device_id, suffix ? suffix : ' ');

    PairCmdMsg msg{};
    msg.header.type   = MessageType::PAIR_CMD;
    msg.header.src_id = 0xFE;
    msg.header.seq    = seq++;
    msg.car_idx       = selected_car;
    msg.client_idx    = selected_client;
    UI_LOG("[TX] PAIR car=%d client=%d\n", selected_car, selected_client);
    sendToServer(&msg, sizeof(msg));
    // Keep the Car selected (only clear the Controller) so pairing a second
    // controller — e.g. the other axis half — to the same car is just one
    // more tap, not reselect-the-car-again.
    selected_client = -1;
    dirty = true;
}

static void onStartClicked(lv_event_t*) {
    StartCmdMsg msg{};
    msg.header.type   = MessageType::START_CMD;
    msg.header.src_id = 0xFE;
    msg.header.seq    = seq++;
    UI_LOG("[TX] START\n");
    sendToServer(&msg, sizeof(msg));
}

static void onEndClicked(lv_event_t*) {
    EndCmdMsg msg{};
    msg.header.type   = MessageType::END_CMD;
    msg.header.src_id = 0xFE;
    msg.header.seq    = seq++;
    UI_LOG("[TX] END\n");
    sendToServer(&msg, sizeof(msg));
}

static void onLedClicked(lv_event_t*) {
    led_on = !led_on;
    LedCmdMsg msg{};
    msg.header.type   = MessageType::LED_CMD;
    msg.header.src_id = 0xFE;
    msg.header.seq    = seq++;
    msg.on            = led_on ? 1 : 0;
    char lbl[20];
    ledTarget(msg.target_type, msg.target_id, lbl, sizeof(lbl));
    UI_LOG("[TX] LED %s -> type=%d id=%d\n", led_on ? "ON" : "OFF",
        (int)msg.target_type, msg.target_id);
    sendToServer(&msg, sizeof(msg));
    dirty = true;
}

static void onIdleBlueClicked(lv_event_t*) {
    idle_blue = !idle_blue;
    SetIdleBlueCmdMsg msg{};
    msg.header.type   = MessageType::SET_IDLE_BLUE_CMD;
    msg.header.src_id = 0xFE;
    msg.header.seq    = seq++;
    msg.on            = idle_blue ? 1 : 0;
    UI_LOG("[TX] IDLE_BLUE %s\n", idle_blue ? "ON" : "OFF");
    sendToServer(&msg, sizeof(msg));
    dirty = true;
}

static void onBrightnessEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int val = (int)lv_slider_get_value(bright_slider);
    if (code == LV_EVENT_VALUE_CHANGED) {
        bright_dragging = true;  // user is driving the slider; ignore server echoes
        char buf[16];
        snprintf(buf, sizeof(buf), "Bright %d%%", (val * 100) / 255);
        lv_label_set_text(bright_label, buf);
    } else if (code == LV_EVENT_RELEASED) {
        bright_dragging = false;
        SetBrightnessCmdMsg msg{};
        msg.header.type   = MessageType::SET_BRIGHTNESS_CMD;
        msg.header.src_id = 0xFE;
        msg.header.seq    = seq++;
        msg.brightness    = (uint8_t)val;
        UI_LOG("[TX] BRIGHTNESS %d\n", val);
        sendToServer(&msg, sizeof(msg));
    }
}

static void onResetClicked(lv_event_t*) {
    ResetCmdMsg msg{};
    msg.header.type   = MessageType::RESET_CMD;
    msg.header.src_id = 0xFE;
    msg.header.seq    = seq++;
    UI_LOG("[TX] RESET\n");
    sendToServer(&msg, sizeof(msg));
}

static void onRebootClicked(lv_event_t*) {
    RebootCmdMsg msg{};
    msg.header.type   = MessageType::REBOOT_CMD;
    msg.header.src_id = 0xFE;
    msg.header.seq    = seq++;
    UI_LOG("[TX] REBOOT (broadcast) -- restarting self too\n");
    if (g_send_broadcast) g_send_broadcast(&msg, sizeof(msg));
    if (g_restart) g_restart();
}

// ── UI construction ─────────────────────────────────────────────────────────────

static lv_obj_t* makeActionButton(lv_obj_t* parent, const char* label, lv_event_cb_t cb,
                                   lv_obj_t** label_out = nullptr, int w = 96, int h = 50) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    if (label_out) *label_out = lbl;
    return btn;
}

// ── Motor bench test (Test tab) ─────────────────────────────────────────────
// Quick and dirty: broadcast TEST_DRIVE_CMD to ALL cars. The car firmware obeys
// it directly, bypassing pairing/RACING/sonar, and auto-stops shortly after the
// last command — so a lost "release" can't leave a car driving off the bench.
static void sendTestDrive(int8_t throttle, int8_t steering) {
    TestDriveCmdMsg msg{};
    msg.header.type   = MessageType::TEST_DRIVE_CMD;
    msg.header.src_id = 0xFE;
    msg.header.seq    = seq++;
    msg.target_id     = 0xFF;  // all cars
    msg.throttle      = throttle;
    msg.steering      = steering;
    UI_LOG("[TX] TEST_DRIVE t=%d s=%d\n", throttle, steering);
    if (g_send_broadcast) g_send_broadcast(&msg, sizeof(msg));
}

static void onTestBtnEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    intptr_t packed  = (intptr_t)lv_event_get_user_data(e);
    int8_t   throttle = (int8_t)(packed & 0xFF);
    int8_t   steering = (int8_t)((packed >> 8) & 0xFF);
    if (code == LV_EVENT_PRESSED)       sendTestDrive(throttle, steering);
    else if (code == LV_EVENT_RELEASED) sendTestDrive(0, 0);  // stop on release
}

static lv_obj_t* makeTestButton(lv_obj_t* parent, const char* label,
                                int throttle, int steering, int w, int h) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    intptr_t packed = ((uint8_t)(int8_t)throttle) | (((uint8_t)(int8_t)steering) << 8);
    lv_obj_add_event_cb(btn, onTestBtnEvent, LV_EVENT_PRESSED,  (void*)packed);
    lv_obj_add_event_cb(btn, onTestBtnEvent, LV_EVENT_RELEASED, (void*)packed);
    return btn;
}

static constexpr int STATUS_BAR_H = 30;
static constexpr int TAB_BAR_H    = 30;
static constexpr int ACTION_BAR_H = 30;

void ui_init(UiSendFn send_to_server, UiSendFn send_broadcast, UiRestartFn restart_fn) {
    g_send_to_server = send_to_server;
    g_send_broadcast = send_broadcast;
    g_restart        = restart_fn;
}

void ui_build() {
    lv_obj_t* scr = lv_screen_active();

    // Status bar: SRV / state / round+wins all on one compact row via flex,
    // so nothing overlaps regardless of text length, and no space is wasted
    // on a second stacked row.
    status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, SCREEN_W, STATUS_BAR_H);
    lv_obj_set_pos(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_palette_darken(LV_PALETTE_BLUE, 3), 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(status_bar, 6, 0);
    lv_obj_set_style_pad_column(status_bar, 12, 0);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(status_bar, LV_SCROLLBAR_MODE_OFF);

    srv_label = lv_label_create(status_bar);
    lv_label_set_text(srv_label, "SRV ---");
    lv_obj_set_style_text_color(srv_label, lv_palette_main(LV_PALETTE_RED), 0);

    state_label = lv_label_create(status_bar);
    lv_obj_set_style_text_color(state_label, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_label_set_text(state_label, "LOBBY");

    round_label = lv_label_create(status_bar);
    lv_obj_set_style_text_color(round_label, lv_color_white(), 0);

    // Tabs (Cars / Ctrl / Match) — the built-in tab bar defaults to a much
    // taller strip than it needs; shrink it to reclaim list space.
    int tabs_y = STATUS_BAR_H;
    lv_obj_t* tabview = lv_tabview_create(scr);
    g_tabview = tabview;
    lv_obj_set_size(tabview, SCREEN_W, SCREEN_H - tabs_y - ACTION_BAR_H);
    lv_obj_set_pos(tabview, 0, tabs_y);
    lv_tabview_set_tab_bar_size(tabview, TAB_BAR_H);

    lv_obj_t* tab_cars = lv_tabview_add_tab(tabview, "Cars");
    car_list = lv_list_create(tab_cars);
    lv_obj_set_size(car_list, LV_PCT(100), LV_PCT(100));

    lv_obj_t* tab_ctrl = lv_tabview_add_tab(tabview, "Ctrl");
    ctrl_list = lv_list_create(tab_ctrl);
    lv_obj_set_size(ctrl_list, LV_PCT(100), LV_PCT(100));

    lv_obj_t* tab_match = lv_tabview_add_tab(tabview, "Match");
    lv_obj_set_flex_flow(tab_match, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_match, 4, 0);

    // Big, hard-to-miss match status readout.
    // State + round/wins share one row (like the header) to save vertical
    // space — the Match tab has a lot to fit into a short screen.
    lv_obj_t* match_status_row = lv_obj_create(tab_match);
    lv_obj_remove_style_all(match_status_row);
    lv_obj_set_size(match_status_row, LV_PCT(100), 26);
    lv_obj_set_flex_flow(match_status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(match_status_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(match_status_row, 10, 0);

    match_state_label = lv_label_create(match_status_row);
    lv_obj_set_style_text_font(match_state_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(match_state_label, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_label_set_text(match_state_label, "LOBBY");

    match_round_label = lv_label_create(match_status_row);
    lv_label_set_text(match_round_label, "Rnd 0  Wins: P1:0 P2:0");

    lv_obj_t* btn_row = lv_obj_create(tab_match);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), 40);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 6, 0);
    start_btn   = makeActionButton(btn_row, "START", onStartClicked, nullptr, 70, 36);
    end_btn     = makeActionButton(btn_row, "END", onEndClicked, nullptr, 70, 36);
    led_btn     = makeActionButton(btn_row, "LED", onLedClicked, &led_btn_label, 70, 36);

    // Server status-strip brightness. Applies on release (one ESP-NOW msg,
    // one server-side flash write); the server persists it across reboots.
    lv_obj_t* bright_row = lv_obj_create(tab_match);
    lv_obj_remove_style_all(bright_row);
    lv_obj_set_size(bright_row, LV_PCT(100), 34);
    lv_obj_set_flex_flow(bright_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bright_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bright_row, 8, 0);

    bright_label = lv_label_create(bright_row);
    lv_label_set_text(bright_label, "Bright 25%");

    bright_slider = lv_slider_create(bright_row);
    lv_obj_set_flex_grow(bright_slider, 1);
    lv_slider_set_range(bright_slider, 0, 255);
    lv_slider_set_value(bright_slider, 64, LV_ANIM_OFF);
    lv_obj_add_event_cb(bright_slider, onBrightnessEvent, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(bright_slider, onBrightnessEvent, LV_EVENT_RELEASED, nullptr);

    // Toggle: does the server strip glow blue when idle? Color reflects state.
    idle_blue_btn = makeActionButton(bright_row, "IDLE", onIdleBlueClicked, nullptr, 60, 30);

    // Reset (wipe score, keep pairings) and Reboot (restart every MCU) live
    // in their own row, visually separated since they're more disruptive.
    lv_obj_t* danger_row = lv_obj_create(tab_match);
    lv_obj_remove_style_all(danger_row);
    lv_obj_set_size(danger_row, LV_PCT(100), 40);
    lv_obj_set_flex_flow(danger_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(danger_row, 8, 0);
    lv_obj_t* reset_btn  = makeActionButton(danger_row, "RESET", onResetClicked, nullptr, 140, 36);
    lv_obj_set_style_bg_color(reset_btn, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_t* reboot_btn = makeActionButton(danger_row, "REBOOT", onRebootClicked, nullptr, 140, 36);
    lv_obj_set_style_bg_color(reboot_btn, lv_palette_main(LV_PALETTE_RED), 0);

    lv_obj_t* pairings_label = lv_label_create(tab_match);
    lv_label_set_text(pairings_label, "Pairings:");
    pairings_list = lv_list_create(tab_match);
    lv_obj_set_size(pairings_list, LV_PCT(100), LV_PCT(100));

    // ── Test tab: quick-and-dirty motor bench test ──────────────────────────
    // Hold a direction to drive; release to stop. Broadcasts to ALL cars.
    lv_obj_t* tab_test = lv_tabview_add_tab(tabview, "Test");
    lv_obj_set_flex_flow(tab_test, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab_test, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tab_test, 4, 0);

    lv_obj_t* test_hint = lv_label_create(tab_test);
    lv_label_set_text(test_hint, LV_SYMBOL_WARNING " all cars \xE2\x80\xA2 hold to drive");

    lv_obj_t* test_top = lv_obj_create(tab_test);
    lv_obj_remove_style_all(test_top);
    lv_obj_set_size(test_top, LV_PCT(100), 34);
    lv_obj_set_flex_flow(test_top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(test_top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    makeTestButton(test_top, "FWD", 100, 0, 80, 32);

    lv_obj_t* test_mid = lv_obj_create(tab_test);
    lv_obj_remove_style_all(test_mid);
    lv_obj_set_size(test_mid, LV_PCT(100), 34);
    lv_obj_set_flex_flow(test_mid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(test_mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(test_mid, 6, 0);
    makeTestButton(test_mid, "LEFT", 0, -100, 80, 32);
    lv_obj_t* test_stop = makeTestButton(test_mid, "STOP", 0, 0, 80, 32);
    lv_obj_set_style_bg_color(test_stop, lv_palette_main(LV_PALETTE_RED), 0);
    makeTestButton(test_mid, "RIGHT", 0, 100, 80, 32);

    lv_obj_t* test_bot = lv_obj_create(tab_test);
    lv_obj_remove_style_all(test_bot);
    lv_obj_set_size(test_bot, LV_PCT(100), 34);
    lv_obj_set_flex_flow(test_bot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(test_bot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    makeTestButton(test_bot, "BACK", -100, 0, 80, 32);

    // Persistent pairing status bar: always visible regardless of active
    // tab, so selecting a Car then switching to Ctrl doesn't lose context —
    // pairing itself fires automatically once both are picked.
    action_bar = lv_obj_create(scr);
    lv_obj_set_size(action_bar, SCREEN_W, ACTION_BAR_H);
    lv_obj_set_pos(action_bar, 0, SCREEN_H - ACTION_BAR_H);
    lv_obj_set_style_radius(action_bar, 0, 0);
    lv_obj_set_style_border_width(action_bar, 0, 0);
    lv_obj_set_style_bg_color(action_bar, lv_palette_darken(LV_PALETTE_GREY, 4), 0);
    lv_obj_set_style_bg_opa(action_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(action_bar, 4, 0);
    lv_obj_set_scrollbar_mode(action_bar, LV_SCROLLBAR_MODE_OFF);

    action_label = lv_label_create(action_bar);
    lv_label_set_text(action_label, "Tap a Car, then a Controller, to pair them");

    refreshUI();
}

void ui_mark_dirty() { dirty = true; }

void ui_tick() {
    if (dirty) {
        dirty = false;
        refreshUI();
    }
}

void ui_set_server_known(bool known) {
    if (known != server_known) dirty = true;
    server_known = known;
}

void ui_handle_hello(const uint8_t* mac, DeviceType type, uint8_t device_id, uint8_t axis_mask) {
    if (type == DeviceType::REFEREE) return;
    for (int i = 0; i < seen_count; i++) {
        if (memcmp(seen[i].mac, mac, 6) == 0) return;
    }
    if (seen_count < MAX_SEEN) {
        memcpy(seen[seen_count].mac, mac, 6);
        seen[seen_count].type      = type;
        seen[seen_count].device_id = device_id;
        seen[seen_count].axis_mask = axis_mask;
        seen[seen_count].valid     = true;
        seen_count++;
        dirty = true;
    }
}

void ui_handle_state_broadcast(const GameStateBroadcastMsg& msg) {
    GameState old_state = ctx.state;
    uint8_t   old_round = ctx.round;
    ctx.state               = msg.state;
    ctx.round               = msg.round;
    ctx.countdown_remaining = msg.countdown_remaining;
    memcpy(ctx.round_wins, msg.round_wins, sizeof(ctx.round_wins));
    if (ctx.state != old_state || ctx.round != old_round) dirty = true;

    paired_count = msg.pair_count < MAX_BROADCAST_PAIRS ? msg.pair_count : MAX_BROADCAST_PAIRS;
    memcpy(paired_info, msg.pairs, paired_count * sizeof(PairInfo));
    dirty = true;  // online/offline status can change without state/round changing

    // Sync the idle-blue toggle to the server's actual (persisted) value.
    if ((msg.idle_blue != 0) != idle_blue) idle_blue = (msg.idle_blue != 0);

    // Sync the brightness slider to the server's actual (persisted) value,
    // but only when the user isn't mid-drag — otherwise we'd fight their finger.
    if (bright_slider && !bright_dragging &&
        (int)lv_slider_get_value(bright_slider) != (int)msg.led_brightness) {
        lv_slider_set_value(bright_slider, msg.led_brightness, LV_ANIM_OFF);
        char buf[16];
        snprintf(buf, sizeof(buf), "Bright %d%%", (msg.led_brightness * 100) / 255);
        lv_label_set_text(bright_label, buf);
    }
}

void ui_debug_set_tab(int idx) {
    if (g_tabview) lv_tabview_set_active(g_tabview, idx, LV_ANIM_OFF);
}
