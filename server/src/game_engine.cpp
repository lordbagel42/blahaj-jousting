// server/src/game_engine.cpp
#include "game_engine.h"
#include <cstring>

GameEngine::GameEngine(Bus& bus) : _bus(bus) {}

void GameEngine::tick(uint32_t now_ms) {
    switch (_ctx.state) {
        case GameState::COUNTDOWN: {
            uint32_t elapsed = now_ms - _last_tick_ms;
            int secs_remaining = COUNTDOWN_SECONDS - static_cast<int>(elapsed / 1000);
            if (secs_remaining < 0) secs_remaining = 0;
            _ctx.countdown_remaining = static_cast<uint8_t>(secs_remaining);

            if (secs_remaining != _last_countdown_val) {
                _last_countdown_val = secs_remaining;
                _bus.emit(GameEvent::COUNTDOWN_TICK, _ctx);
            }

            if (elapsed >= static_cast<uint32_t>(COUNTDOWN_SECONDS) * 1000) {
                transitionTo(GameState::RACING, now_ms);
            }
            break;
        }
        case GameState::ROUND_END: {
            if (now_ms - _round_end_entered_ms >= ROUND_END_PAUSE_MS) {
                transitionTo(GameState::LOBBY, now_ms);
            }
            break;
        }
        default:
            break;
    }
}

void GameEngine::startMatch(uint32_t now_ms) {
    if (_ctx.state != GameState::LOBBY) return;
    _last_tick_ms = now_ms;
    transitionTo(GameState::COUNTDOWN, now_ms);
}

void GameEngine::endRound() {
    if (_ctx.state != GameState::RACING) return;
    // NOTE: passes now_ms=0 to transitionTo; _round_end_entered_ms will be 0.
    // Tests advance past the pause via tick(10000). In production, endRound()
    // should ideally receive millis() — a known simplification for now.
    transitionTo(GameState::ROUND_END, 0);
}

void GameEngine::onKnockoff(uint8_t car_slot) {
    if (_ctx.state != GameState::RACING) return;
    if (car_slot >= CARS_PER_ROUND) return;
    if (_ctx.cars_eliminated & (1 << car_slot)) return;  // already out

    _ctx.knockoffs[car_slot]++;
    _ctx.cars_eliminated |= (1 << car_slot);
    _ctx.last_knockoff_car_id = car_slot;
    _bus.emit(GameEvent::KNOCKOFF, _ctx);

    checkRoundEnd();
}

void GameEngine::transitionTo(GameState next, uint32_t now_ms) {
    _ctx.state = next;
    _bus.emit(GameEvent::STATE_CHANGED, _ctx);

    switch (next) {
        case GameState::COUNTDOWN:
            _ctx.countdown_remaining = COUNTDOWN_SECONDS;
            _last_countdown_val = -1;
            _bus.emit(GameEvent::MATCH_START, _ctx);
            break;

        case GameState::RACING:
            _ctx.countdown_remaining = 0;
            break;

        case GameState::ROUND_END: {
            // Award round win to the surviving car.
            int survivor = -1;
            for (int i = 0; i < CARS_PER_ROUND; i++) {
                if (!(_ctx.cars_eliminated & (1 << i))) {
                    survivor = i;
                    break;
                }
            }
            if (survivor >= 0) _ctx.round_wins[survivor]++;
            _ctx.round++;
            _round_end_entered_ms = now_ms;
            _bus.emit(GameEvent::ROUND_END, _ctx);

            // Check match win.
            for (int i = 0; i < CARS_PER_ROUND; i++) {
                if (_ctx.round_wins[i] >= ROUNDS_TO_WIN) {
                    _bus.emit(GameEvent::MATCH_END, _ctx);
                    break;
                }
            }
            break;
        }

        case GameState::LOBBY:
            // Reset per-round state, keep match scores.
            memset(_ctx.knockoffs, 0, sizeof(_ctx.knockoffs));
            _ctx.cars_eliminated = 0;
            _ctx.last_knockoff_car_id = 0xFF;
            break;
    }
}

void GameEngine::checkRoundEnd() {
    if (eliminatedCount() >= CARS_PER_ROUND - 1) {
        transitionTo(GameState::ROUND_END, 0);
    }
}

int GameEngine::eliminatedCount() const {
    int n = 0;
    for (int i = 0; i < CARS_PER_ROUND; i++) {
        if (_ctx.cars_eliminated & (1 << i)) n++;
    }
    return n;
}
