// test/native/test_protocol/test_main.cpp
#include <unity.h>
#include "protocol.h"

void test_message_header_size() {
    TEST_ASSERT_EQUAL(3, sizeof(MessageHeader));
}

void test_hello_msg_size() {
    // header(3) + device_type(1) + device_id(1) + axis_mask(1) = 6
    TEST_ASSERT_EQUAL(6, sizeof(HelloMsg));
}

void test_hello_ack_size() {
    // header(3) + assigned_id(1) + partner_mac(6) = 10
    TEST_ASSERT_EQUAL(10, sizeof(HelloAckMsg));
}

void test_drive_cmd_size() {
    // header(3) + throttle(1) + steering(1) + axis_mask(1) = 6
    TEST_ASSERT_EQUAL(6, sizeof(DriveCmdMsg));
}

void test_knockoff_event_size() {
    // header(3) + car_id(1) = 4
    TEST_ASSERT_EQUAL(4, sizeof(KnockoffEventMsg));
}

void test_game_state_broadcast_size() {
    // header(3) + state(1) + round(1) + round_wins(2) + knockoffs(2) + countdown(1)
    // + cars_eliminated(1) + last_knockoff_car_id(1) + pair_count(1)
    // + pairs(8 * PairInfo(5) = 40) + led_brightness(1) + idle_blue(1) = 55
    TEST_ASSERT_EQUAL(55, sizeof(GameStateBroadcastMsg));
}

void test_header_fields() {
    HelloMsg msg{};
    msg.header.type   = MessageType::HELLO;
    msg.header.src_id = 2;
    msg.header.seq    = 42;
    msg.device_type   = DeviceType::CAR;
    msg.device_id     = 1;

    TEST_ASSERT_EQUAL((uint8_t)MessageType::HELLO, (uint8_t)msg.header.type);
    TEST_ASSERT_EQUAL(2, msg.header.src_id);
    TEST_ASSERT_EQUAL(42, msg.header.seq);
    TEST_ASSERT_EQUAL((uint8_t)DeviceType::CAR, (uint8_t)msg.device_type);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_message_header_size);
    RUN_TEST(test_hello_msg_size);
    RUN_TEST(test_hello_ack_size);
    RUN_TEST(test_drive_cmd_size);
    RUN_TEST(test_knockoff_event_size);
    RUN_TEST(test_game_state_broadcast_size);
    RUN_TEST(test_header_fields);
    return UNITY_END();
}
