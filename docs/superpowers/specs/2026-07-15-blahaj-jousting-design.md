# Blahaj Jousting — System Design

**Date:** 2026-07-15  
**Platform:** ESP32C3 Supermini × 3 roles (server, client, car)  
**Transport:** ESP-NOW (direct peer-to-peer, 1–3ms latency)  
**Match format:** 3-car free-for-all, best-of-N rounds; round ends when referee hits End Round or a configurable knockoff threshold is reached; fewest knockoffs suffered = round winner

---

## Overview

Three types of firmware nodes communicate over ESP-NOW:

- **Server** — game state authority, scoring, match lifecycle, management web UI
- **Client** (remote controller) — reads driver input, sends drive commands to paired car, displays game state
- **Car** — drives motors, detects shark knockoff via button/switch, enforces safety lockouts

Control commands travel client → car directly (lowest latency path). Game events travel car → server and server → all devices. The server hosts a WiFi AP with a management web UI for pairing and referee controls.

---

## Project Structure

```
blahaj-jousting/
├── platformio.ini            # workspace: points to all three targets
├── lib/
│   └── blahaj_common/        # shared library, auto-linked by PlatformIO
│       ├── protocol.h        # all message structs and type enums
│       ├── espnow_transport.h/.cpp   # ESP-NOW send/receive wrapper
│       ├── event_bus.h       # local pub/sub (no network)
│       └── game_state.h      # GameState enum, GameContext struct
├── car/
│   ├── platformio.ini
│   └── src/main.cpp
├── client/
│   ├── platformio.ini
│   └── src/main.cpp
└── server/
    ├── platformio.ini
    └── src/main.cpp
```

All message types and constants are defined once in `blahaj_common` — no drift between firmware targets.

---

## Communication Protocol

### Message Header (all messages share this)

```cpp
struct MessageHeader {
    uint8_t type;    // MessageType enum
    uint8_t src_id;  // sender device ID (assigned by server at pairing)
    uint8_t seq;     // wrapping sequence number, used to drop duplicates
};
```

### Message Types

| Type | Direction | Purpose |
|------|-----------|---------|
| `HELLO` | any → server | Device announces itself at boot |
| `HELLO_ACK` | server → device | Assigns ID + paired device MAC |
| `DRIVE_CMD` | client → car | Throttle + steering at ~30Hz |
| `KNOCKOFF_EVENT` | car → server | Shark button triggered |
| `GAME_STATE_BROADCAST` | server → all | Full GameState + scores |
| `GAME_EVENT` | server → all | Named event (MATCH_START, etc.) |
| `PING` / `PONG` | any ↔ any | Watchdog keepalive |

All payloads fit within ESP-NOW's 250-byte limit. Duplicate suppression uses the `seq` field.

---

## Pairing Flow

1. Server boots in **AP+STA** WiFi mode, creates `BlahajJousting` hotspot, starts ESP-NOW
2. Cars and clients boot, broadcast `HELLO` (device type + compile-time `DEVICE_ID`) to the server's MAC — set once via `build_flags = -D SERVER_MAC=...` in each target's `platformio.ini`
3. Server adds devices to unassigned pool; management UI shows them live
4. Admin connects to `BlahajJousting` WiFi, opens `192.168.4.1`
5. Admin assigns each client to a car via the UI, clicks **Confirm Pairings**
6. Server sends `HELLO_ACK` to each device with their assigned ID and partner MAC
7. Devices register each other as ESP-NOW peers; control path goes live
8. UI shows per-device connection health (last-seen timestamp, red if stale >2s)

---

## Server

### State Machine

```
LOBBY → COUNTDOWN → RACING → ROUND_END → LOBBY
```

| State | Description |
|-------|-------------|
| `LOBBY` | Waiting for devices; pairing and management UI active; referee can start when all cars show connected |
| `COUNTDOWN` | Referee hit Start; 3-2-1 sequence running; cars locked out |
| `RACING` | Cars enabled; knockoffs accepted |
| `ROUND_END` | Scores tallied; brief pause before returning to LOBBY |

### Event Bus

Handlers are registered at the top of `server/src/main.cpp` and compiled in. Adding a peripheral means adding a handler — nothing else changes.

```cpp
game.on(GameEvent::COUNTDOWN_TICK, handleCountdownLEDs);
game.on(GameEvent::MATCH_START,    handleStartBuzzer);
game.on(GameEvent::KNOCKOFF,       handleScoreDisplay);
game.on(GameEvent::MATCH_END,      handleEndLEDs);
```

Handler signature: `void handler(const GameContext& ctx)`

`GameContext` contains: `state`, `round`, `scores[3]`, `countdown_remaining`, `last_knockoff_car_id`.

`game.tick()` called in the main loop: processes ESP-NOW receive queue and fires events.

On every state transition and every knockoff, server broadcasts `GAME_STATE_BROADCAST` to all registered device MACs.

### Management Web UI

- Served from flash as a single self-contained HTML file (vanilla JS, no CDN)
- Polls `/api/status` every 2s for device list and health
- Actions: assign client→car pairs, **Confirm Pairings**, **Start Match**, **End Match**
- `/api/start` triggers the LOBBY → COUNTDOWN transition
- Connection health indicator per device (green/red dot)

---

## Client

### Input Provider

The client calls `input->read()` every 33ms (~30Hz) and sends the result to the paired car. Swap control schemes by changing one line:

```cpp
client.setInputProvider(new JoystickInput(PIN_X, PIN_Y));
// or:
client.setInputProvider(new ButtonInput(PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT));
```

Interface: `DriveCommand InputProvider::read()` — one method, any implementation.

`DriveCommand`: `{ int8_t throttle, int8_t steering }` — both -127 to 127.

Drive is zeroed (locked out) whenever `ctx.state != RACING`, regardless of physical input.

### Game State Handling

Same event bus pattern, fired when a `GAME_STATE_BROADCAST` or `GAME_EVENT` arrives:

```cpp
client.on(GameEvent::MATCH_START, [](const GameContext& ctx) {
    digitalWrite(PIN_STATUS_LED, HIGH); // green
});
client.on(GameEvent::MATCH_END, [](const GameContext& ctx) {
    digitalWrite(PIN_STATUS_LED, LOW);  // red
});
```

---

## Car

### Motor Driver

4× N20 motors in tank configuration: FL+BL = left side, FR+BR = right side. Pluggable driver:

```cpp
car.setMotorDriver(new DRV8833Driver(PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2));
```

Interface: `void MotorDriver::drive(int8_t throttle, int8_t steering)` — driver handles mixing internally.

Safety lockouts (both enforced locally regardless of server state):
- If `ctx.state != RACING`: all motor output zeroed
- If no `DRIVE_CMD` received for >500ms: motors stop (controller drop = safe stop)

### Knockoff Detection

```cpp
car.setKnockoffPin(PIN_SHARK_SWITCH, ACTIVE_LOW);
```

- 50ms debounce, single-shot per event
- On trigger: sends `KNOCKOFF_EVENT` to server, locks out for 3s (shark reset window)
- Server validates event against current game state; ignores if not `RACING`

---

## Scoring

- Server tracks `knockoffs_suffered[car_id]` — incremented on each accepted `KNOCKOFF_EVENT`
- Round winner = car with fewest knockoffs suffered
- Match winner = first to win `N` rounds (configurable compile-time constant, default 2)
- Scores included in every `GAME_STATE_BROADCAST`; clients and displays consume them freely

---

## Watchdog / Recovery

- All devices send `PING` every 1s to server; server tracks last-seen per device
- Server marks device as disconnected after 3s no response; broadcasts updated status
- Car stops motors on disconnect from client (500ms `DRIVE_CMD` timeout)
- Server does not advance game state if any registered car is disconnected during `RACING`
- Management UI reflects disconnected devices in red; referee can manually end round

---

## Extensibility Summary

| What you want to add | Where to add it |
|----------------------|-----------------|
| New peripheral on server | Register handler in `server/src/main.cpp` |
| New control scheme | Implement `InputProvider`, set in `client/src/main.cpp` |
| New motor driver chip | Implement `MotorDriver`, set in `car/src/main.cpp` |
| New game event type | Add to `GameEvent` enum in `blahaj_common/game_state.h` |
| New client status indicator | Register handler in `client/src/main.cpp` |
| New management UI action | Add HTTP route + JS fetch in server UI |
