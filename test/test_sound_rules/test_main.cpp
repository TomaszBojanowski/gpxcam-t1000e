// Unit tests for src/sound_rules.cpp, run on the PC: pio test -e native
#include <unity.h>

#include "sound_rules.h"

using sound_rules::Kind;
using sound_rules::Setting;

void setUp() {}

void tearDown() {}

void test_loud_and_quiet_play_everything()
{
    TEST_ASSERT_EQUAL_UINT8(10, sound_rules::dutyPercent(Setting::Loud, Kind::Event));
    TEST_ASSERT_EQUAL_UINT8(10, sound_rules::dutyPercent(Setting::Loud, Kind::HoldAnswer));
    TEST_ASSERT_EQUAL_UINT8(5, sound_rules::dutyPercent(Setting::Quiet, Kind::Event));
    TEST_ASSERT_EQUAL_UINT8(5, sound_rules::dutyPercent(Setting::Quiet, Kind::HoldAnswer));
}

void test_off_keeps_only_the_answers_to_a_held_button_quietly()
{
    TEST_ASSERT_EQUAL_UINT8(0, sound_rules::dutyPercent(Setting::Off, Kind::Event));
    TEST_ASSERT_EQUAL_UINT8(5, sound_rules::dutyPercent(Setting::Off, Kind::HoldAnswer));
}

void test_names()
{
    Setting setting = Setting::Loud;
    TEST_ASSERT_TRUE(sound_rules::settingFromName("quiet", &setting));
    TEST_ASSERT_EQUAL(Setting::Quiet, setting);
    TEST_ASSERT_TRUE(sound_rules::settingFromName("off", &setting));
    TEST_ASSERT_EQUAL(Setting::Off, setting);
    TEST_ASSERT_TRUE(sound_rules::settingFromName("loud", &setting));
    TEST_ASSERT_EQUAL(Setting::Loud, setting);
    TEST_ASSERT_EQUAL_STRING("quiet", sound_rules::settingName(Setting::Quiet));
    const char *const UNKNOWN[] = {"", "Loud", "on", "quiet ", "silent"};
    for (const char *unknown : UNKNOWN) {
        setting = Setting::Quiet;
        TEST_ASSERT_FALSE_MESSAGE(sound_rules::settingFromName(unknown, &setting), unknown);
        TEST_ASSERT_EQUAL(Setting::Quiet, setting); // unchanged
    }
}

// The values are stored in the settings file.
void test_stored_values_stay()
{
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)Setting::Loud);
    TEST_ASSERT_EQUAL_UINT8(1, (uint8_t)Setting::Quiet);
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)Setting::Off);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_loud_and_quiet_play_everything);
    RUN_TEST(test_off_keeps_only_the_answers_to_a_held_button_quietly);
    RUN_TEST(test_names);
    RUN_TEST(test_stored_values_stay);
    return UNITY_END();
}
