// server/src/web_ui.h
#pragma once
#include "pairing_manager.h"
#include "game_engine.h"
#include <ESPAsyncWebServer.h>
#include <vector>

class WebUI {
public:
    WebUI(PairingManager& pairing, GameEngine& engine);

    void begin();  // Start the AsyncWebServer on port 80.

    // Called from main loop to consume pending actions. Each returns true once, then resets.
    bool hasPendingStart()  { bool v = _pending_start;  _pending_start  = false; return v; }
    bool hasPendingEnd()    { bool v = _pending_end;    _pending_end    = false; return v; }
    bool hasPendingPair()   { bool v = _pending_pair;   _pending_pair   = false; return v; }

    struct PairAssignment { uint8_t car_idx; uint8_t client_idx; };
    const std::vector<PairAssignment>& pendingPairs() const { return _pending_pairs; }

private:
    PairingManager& _pairing;
    GameEngine&     _engine;
    AsyncWebServer  _server{80};

    bool _pending_start = false;
    bool _pending_end   = false;
    bool _pending_pair  = false;
    std::vector<PairAssignment> _pending_pairs;
    portMUX_TYPE _pair_mux = portMUX_INITIALIZER_UNLOCKED;

    String buildStatusJson(uint32_t now_ms);
};
