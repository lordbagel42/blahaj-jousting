// server/src/game_engine.h
#pragma once
#include <cstdint>
#include "game_state.h"
#include "event_bus.h"

class GameEngine {
public:
    using Bus = EventBus<GameEvent, GameContext>;

    static constexpr int      CARS_PER_ROUND        = 3;
    static constexpr int      COUNTDOWN_SECONDS      = 3;
    static constexpr uint32_t ROUND_END_PAUSE_MS     = 3000;

    explicit GameEngine(Bus& bus);

    // Called every loop iteration. now_ms = millis().
    void tick(uint32_t now_ms);

    // Referee actions — no-ops if state is wrong.
    void startMatch(uint32_t now_ms);
    void endRound(uint32_t now_ms);

    // Wipes round/wins/knockoffs and returns to LOBBY, regardless of
    // current state. Does not touch pairings (that's PairingManager's job).
    void reset();

    // Called when a knockoff event is confirmed.
    void onKnockoff(uint8_t car_slot, uint32_t now_ms);

    const GameContext& context() const { return _ctx; }

private:
    void transitionTo(GameState next, uint32_t now_ms = 0);
    void checkRoundEnd(uint32_t now_ms);
    int  eliminatedCount() const;

    Bus&        _bus;
    GameContext _ctx{};
    uint32_t    _last_tick_ms         = 0;
    uint32_t    _round_end_entered_ms = 0;
    int         _last_countdown_val   = -1;
    bool        _match_ended          = false;
};
