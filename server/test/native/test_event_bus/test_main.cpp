// server/test/native/test_event_bus/test_main.cpp
#include <unity.h>
#include "event_bus.h"
#include "game_state.h"

using Bus = EventBus<GameEvent, GameContext>;

void setUp() {}
void tearDown() {}

void test_no_handlers_emits_safely() {
    Bus bus;
    GameContext ctx{};
    bus.emit(GameEvent::MATCH_START, ctx);
    TEST_PASS();
}

void test_handler_called_on_emit() {
    Bus bus;
    bool called = false;
    bus.on(GameEvent::MATCH_START, [&](const GameContext&) { called = true; });

    GameContext ctx{};
    bus.emit(GameEvent::MATCH_START, ctx);

    TEST_ASSERT_TRUE(called);
}

void test_handler_not_called_for_other_event() {
    Bus bus;
    bool called = false;
    bus.on(GameEvent::MATCH_START, [&](const GameContext&) { called = true; });

    GameContext ctx{};
    bus.emit(GameEvent::KNOCKOFF, ctx);

    TEST_ASSERT_FALSE(called);
}

void test_multiple_handlers_all_called() {
    Bus bus;
    int count = 0;
    bus.on(GameEvent::KNOCKOFF, [&](const GameContext&) { count++; });
    bus.on(GameEvent::KNOCKOFF, [&](const GameContext&) { count++; });
    bus.on(GameEvent::KNOCKOFF, [&](const GameContext&) { count++; });

    GameContext ctx{};
    bus.emit(GameEvent::KNOCKOFF, ctx);

    TEST_ASSERT_EQUAL(3, count);
}

void test_context_passed_to_handler() {
    Bus bus;
    uint8_t received_round = 0;
    bus.on(GameEvent::ROUND_END, [&](const GameContext& ctx) {
        received_round = ctx.round;
    });

    GameContext ctx{};
    ctx.round = 7;
    bus.emit(GameEvent::ROUND_END, ctx);

    TEST_ASSERT_EQUAL(7, received_round);
}

void test_handler_count() {
    Bus bus;
    bus.on(GameEvent::MATCH_START, [](const GameContext&) {});
    bus.on(GameEvent::MATCH_START, [](const GameContext&) {});
    TEST_ASSERT_EQUAL(2, bus.handlerCount(GameEvent::MATCH_START));
    TEST_ASSERT_EQUAL(0, bus.handlerCount(GameEvent::KNOCKOFF));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_handlers_emits_safely);
    RUN_TEST(test_handler_called_on_emit);
    RUN_TEST(test_handler_not_called_for_other_event);
    RUN_TEST(test_multiple_handlers_all_called);
    RUN_TEST(test_context_passed_to_handler);
    RUN_TEST(test_handler_count);
    return UNITY_END();
}
