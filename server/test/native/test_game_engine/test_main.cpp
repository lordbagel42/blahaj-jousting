// server/test/native/test_game_engine/test_main.cpp
#include <unity.h>
#include "game_engine.h"

using Bus = EventBus<GameEvent, GameContext>;

void setUp() {}
void tearDown() {}

void test_initial_state_is_lobby() {
    Bus bus;
    GameEngine engine(bus);
    TEST_ASSERT_EQUAL((uint8_t)GameState::LOBBY, (uint8_t)engine.context().state);
}

void test_start_match_transitions_to_countdown() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    TEST_ASSERT_EQUAL((uint8_t)GameState::COUNTDOWN, (uint8_t)engine.context().state);
}

void test_start_match_noop_when_not_lobby() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.startMatch(0);  // second call should be ignored
    TEST_ASSERT_EQUAL((uint8_t)GameState::COUNTDOWN, (uint8_t)engine.context().state);
}

void test_countdown_completes_and_transitions_to_racing() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.tick(3001);
    TEST_ASSERT_EQUAL((uint8_t)GameState::RACING, (uint8_t)engine.context().state);
}

void test_knockoff_eliminates_car() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.tick(3001);
    engine.onKnockoff(0);
    TEST_ASSERT_EQUAL(1, engine.context().knockoffs[0]);
    TEST_ASSERT_TRUE(engine.context().cars_eliminated & 0x01);
}

void test_two_knockoffs_end_round() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.tick(3001);
    engine.onKnockoff(0);
    engine.onKnockoff(1);
    TEST_ASSERT_EQUAL((uint8_t)GameState::ROUND_END, (uint8_t)engine.context().state);
}

void test_survivor_gets_round_win() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.tick(3001);
    engine.onKnockoff(0);
    engine.onKnockoff(1);
    // Car slot 2 survived
    TEST_ASSERT_EQUAL(1, engine.context().round_wins[2]);
    TEST_ASSERT_EQUAL(0, engine.context().round_wins[0]);
    TEST_ASSERT_EQUAL(0, engine.context().round_wins[1]);
}

void test_duplicate_knockoff_ignored() {
    Bus bus;
    GameEngine engine(bus);
    engine.startMatch(0);
    engine.tick(3001);
    engine.onKnockoff(0);
    engine.onKnockoff(0);  // duplicate
    TEST_ASSERT_EQUAL(1, engine.context().knockoffs[0]);
}

void test_match_end_fires_after_rounds_to_win() {
    Bus bus;
    bool match_ended = false;
    bus.on(GameEvent::MATCH_END, [&](const GameContext&) { match_ended = true; });

    GameEngine engine(bus);
    for (int r = 0; r < ROUNDS_TO_WIN; r++) {
        engine.startMatch(0);
        engine.tick(3001);
        engine.onKnockoff(0);
        engine.onKnockoff(1);
        engine.tick(10000);  // advance past round end pause
    }
    TEST_ASSERT_TRUE(match_ended);
}

void test_knockoff_ignored_when_not_racing() {
    Bus bus;
    GameEngine engine(bus);
    engine.onKnockoff(0);  // LOBBY state
    TEST_ASSERT_EQUAL(0, engine.context().knockoffs[0]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_lobby);
    RUN_TEST(test_start_match_transitions_to_countdown);
    RUN_TEST(test_start_match_noop_when_not_lobby);
    RUN_TEST(test_countdown_completes_and_transitions_to_racing);
    RUN_TEST(test_knockoff_eliminates_car);
    RUN_TEST(test_two_knockoffs_end_round);
    RUN_TEST(test_survivor_gets_round_win);
    RUN_TEST(test_duplicate_knockoff_ignored);
    RUN_TEST(test_match_end_fires_after_rounds_to_win);
    RUN_TEST(test_knockoff_ignored_when_not_racing);
    return UNITY_END();
}
