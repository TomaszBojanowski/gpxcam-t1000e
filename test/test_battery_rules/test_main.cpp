// Unit tests for src/battery_rules.cpp, run on the PC: pio test -e native
#include <unity.h>

#include "battery_rules.h"

void setUp() {}
void tearDown() {}

void test_never_with_usb()
{
    // A flat battery with the cable: the card starts and charges, also at start.
    uint8_t low = 0;
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_FALSE(battery_rules::powerOffForLowBattery(3000, true, true, &low));
        TEST_ASSERT_FALSE(battery_rules::powerOffForLowBattery(3000, true, false, &low));
    }
    TEST_ASSERT_EQUAL_UINT8(0, low);
}

void test_at_start_after_one_low_reading()
{
    uint8_t low = 0;
    TEST_ASSERT_TRUE(battery_rules::powerOffForLowBattery(3399, false, true, &low));
    low = 0;
    TEST_ASSERT_FALSE(battery_rules::powerOffForLowBattery(3400, false, true, &low)); // the limit itself is fine
}

void test_running_after_two_low_readings_in_a_row()
{
    uint8_t low = 0;
    TEST_ASSERT_FALSE(battery_rules::powerOffForLowBattery(3350, false, false, &low));
    TEST_ASSERT_TRUE(battery_rules::powerOffForLowBattery(3350, false, false, &low));
    // a good reading in between starts the count again
    low = 0;
    TEST_ASSERT_FALSE(battery_rules::powerOffForLowBattery(3350, false, false, &low));
    TEST_ASSERT_FALSE(battery_rules::powerOffForLowBattery(3700, false, false, &low));
    TEST_ASSERT_FALSE(battery_rules::powerOffForLowBattery(3350, false, false, &low));
    // and so does the cable
    TEST_ASSERT_FALSE(battery_rules::powerOffForLowBattery(3350, true, false, &low));
    TEST_ASSERT_FALSE(battery_rules::powerOffForLowBattery(3350, false, false, &low));
    TEST_ASSERT_TRUE(battery_rules::powerOffForLowBattery(3350, false, false, &low));
}

// Expected values computed in Python from Meshtastic's formula (src/Power.cpp; in brackets),
// then scaled so that the power-off voltage (Meshtastic's 5 %) gives 0 %.
void test_percent_left_until_power_off()
{
    TEST_ASSERT_EQUAL_UINT8(100, battery_rules::percentFromMillivolts(4300)); // charging (100)
    TEST_ASSERT_EQUAL_UINT8(100, battery_rules::percentFromMillivolts(4190)); // full (100)
    TEST_ASSERT_EQUAL_UINT8(94, battery_rules::percentFromMillivolts(4116));  // (95)
    TEST_ASSERT_EQUAL_UINT8(89, battery_rules::percentFromMillivolts(4042));  // (90)
    TEST_ASSERT_EQUAL_UINT8(57, battery_rules::percentFromMillivolts(3820));  // (60)
    TEST_ASSERT_EQUAL_UINT8(16, battery_rules::percentFromMillivolts(3700));  // (21)
    TEST_ASSERT_EQUAL_UINT8(5, battery_rules::percentFromMillivolts(3644));   // (10)
    TEST_ASSERT_EQUAL_UINT8(2, battery_rules::percentFromMillivolts(3500));   // (7)
    TEST_ASSERT_EQUAL_UINT8(0, battery_rules::percentFromMillivolts(battery_rules::LOW_MV)); // powers off (5)
    TEST_ASSERT_EQUAL_UINT8(0, battery_rules::percentFromMillivolts(3100));   // (0)
    TEST_ASSERT_EQUAL_UINT8(0, battery_rules::percentFromMillivolts(0));
}

// The percent never falls while the voltage rises.
void test_percent_never_falls_with_the_voltage()
{
    for (uint32_t mv = 2501; mv <= 4500; mv++)
        TEST_ASSERT_TRUE(battery_rules::percentFromMillivolts(mv) >= battery_rules::percentFromMillivolts(mv - 1));
}

void test_entry_round_trip()
{
    const battery_rules::Entry entry = {0x01020304, 4012, 2, battery_rules::FLAG_LOW_BATTERY_OFF};
    uint8_t data[battery_rules::ENTRY_SIZE];
    battery_rules::encode(entry, data);
    const uint8_t expected[] = {0x04, 0x03, 0x02, 0x01, 0xac, 0x0f, 0x02, 0x01}; // 4012 = 0x0fac
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, data, sizeof(expected));
    const battery_rules::Entry back = battery_rules::decode(data);
    TEST_ASSERT_EQUAL_UINT32(entry.uptimeMinutes, back.uptimeMinutes);
    TEST_ASSERT_EQUAL_UINT16(entry.millivolts, back.millivolts);
    TEST_ASSERT_EQUAL_UINT8(entry.activity, back.activity);
    TEST_ASSERT_EQUAL_UINT8(entry.flags, back.flags);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_never_with_usb);
    RUN_TEST(test_at_start_after_one_low_reading);
    RUN_TEST(test_running_after_two_low_readings_in_a_row);
    RUN_TEST(test_percent_left_until_power_off);
    RUN_TEST(test_percent_never_falls_with_the_voltage);
    RUN_TEST(test_entry_round_trip);
    return UNITY_END();
}
