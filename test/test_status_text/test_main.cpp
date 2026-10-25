// Unit tests for src/status_text.cpp, run on the PC: pio test -e native
// Made-up values.
#include <string.h>
#include <unity.h>

#include "status_text.h"

using status_text::State;

namespace {

constexpr size_t MAX_LINES = 12;
char lines[MAX_LINES][status_text::LINE_SIZE];
size_t count = 0;

void collect(const char *line)
{
    if (count < MAX_LINES)
        strncpy(lines[count], line, status_text::LINE_SIZE - 1);
    count++;
}

State someState()
{
    State state = {};
    state.version = "1.3.0";
    state.git = "1234567";
    state.watchdogRestart = false;
    state.storage = "ok";
    state.batteryMillivolts = 4100;
    state.batteryPercent = 89;
    state.charger = battery::Charger::Charging;
    state.gpsOn = true;
    state.gpsFix = true;
    state.gpsSatellites = 11;
    state.paired = true;
    state.link = link_control::State::Connected;
    state.pairing = {pairing::Phase::Idle, 0, pairing::Result::None};
    state.clockOn = true;
    state.timeZone = "Europe/Warsaw";
    state.sound = sound_rules::Setting::Loud;
    state.updates = link_policy::Updates::Normal;
    state.updatesNow = link_policy::Updates::Normal;
    return state;
}

} // namespace

void setUp()
{
    memset(lines, 0, sizeof(lines));
    count = 0;
}

void tearDown() {}

void test_all_lines()
{
    status_text::format(someState(), collect);
    TEST_ASSERT_EQUAL_size_t(11, count);
    TEST_ASSERT_EQUAL_STRING("status: format=1 fw=1.3.0 git=1234567", lines[0]);
    TEST_ASSERT_EQUAL_STRING("status: battery_mv=4100 battery_pct=89 charger=charging", lines[1]);
    TEST_ASSERT_EQUAL_STRING("status: gps=on gps_fix=1 gps_sat=11 gps_hold=0", lines[2]);
    TEST_ASSERT_EQUAL_STRING("status: paired=1 link=connected", lines[3]);
    TEST_ASSERT_EQUAL_STRING("status: pair=idle pair_attempt=0 pair_result=none", lines[4]);
    TEST_ASSERT_EQUAL_STRING("status: clock=on tz=Europe/Warsaw", lines[5]);
    TEST_ASSERT_EQUAL_STRING("status: sound=loud", lines[6]);
    TEST_ASSERT_EQUAL_STRING("status: updates=normal updates_now=normal", lines[7]);
    TEST_ASSERT_EQUAL_STRING("status: watchdog_restart=0", lines[8]);
    TEST_ASSERT_EQUAL_STRING("status: storage=ok", lines[9]);
    TEST_ASSERT_EQUAL_STRING("status: end", lines[10]);
}

void test_other_values()
{
    State state = someState();
    state.batteryMillivolts = 3400;
    state.batteryPercent = 0;
    state.charger = battery::Charger::NoUsb;
    state.gpsOn = false;
    state.gpsFix = false;
    state.gpsSatellites = 0;
    state.paired = false;
    state.link = link_control::State::Stopped;
    state.pairing = {pairing::Phase::Pausing, 2, pairing::Result::None};
    state.clockOn = false;
    state.timeZone = "<+0545>-5:45"; // a rule given directly
    state.sound = sound_rules::Setting::Off;
    state.updates = link_policy::Updates::Frequent;
    state.updatesNow = link_policy::Updates::Normal; // the guard slowed the connection down
    state.watchdogRestart = true;
    state.storage = "damaged";
    status_text::format(state, collect);
    TEST_ASSERT_EQUAL_STRING("status: battery_mv=3400 battery_pct=0 charger=no_usb", lines[1]);
    TEST_ASSERT_EQUAL_STRING("status: gps=off gps_fix=0 gps_sat=0 gps_hold=0", lines[2]);
    TEST_ASSERT_EQUAL_STRING("status: paired=0 link=stopped", lines[3]);
    TEST_ASSERT_EQUAL_STRING("status: pair=pausing pair_attempt=2 pair_result=none", lines[4]);
    TEST_ASSERT_EQUAL_STRING("status: clock=off tz=<+0545>-5:45", lines[5]);
    TEST_ASSERT_EQUAL_STRING("status: sound=off", lines[6]);
    TEST_ASSERT_EQUAL_STRING("status: updates=frequent updates_now=normal", lines[7]);
    TEST_ASSERT_EQUAL_STRING("status: watchdog_restart=1", lines[8]);
    TEST_ASSERT_EQUAL_STRING("status: storage=damaged", lines[9]);
}

void test_frequent_in_effect()
{
    State state = someState();
    state.updates = link_policy::Updates::Frequent;
    state.updatesNow = link_policy::Updates::Frequent;
    status_text::format(state, collect);
    TEST_ASSERT_EQUAL_STRING("status: updates=frequent updates_now=frequent", lines[7]);
}

void test_pairing_results()
{
    State state = someState();
    state.pairing.lastResult = pairing::Result::NotInPairingMode;
    status_text::format(state, collect);
    TEST_ASSERT_EQUAL_STRING("status: pair=idle pair_attempt=0 pair_result=not_in_pairing_mode", lines[4]);
}

// 'pair' that switched to the older dialect waits for the one-time Wi-Fi step.
void test_pairing_waiting_wifi()
{
    State state = someState();
    state.pairing = {pairing::Phase::WaitingForWifi, 1, pairing::Result::None};
    status_text::format(state, collect);
    TEST_ASSERT_EQUAL_STRING("status: pair=waiting_wifi pair_attempt=1 pair_result=none", lines[4]);
}

// 'pair prepare' (or a pairing that did not pair) keeps the GPS on for a pairing to come.
void test_gps_held_for_pairing()
{
    State state = someState();
    state.paired = false;
    state.link = link_control::State::Off;
    state.gpsFix = false;
    state.gpsSatellites = 4;
    state.gpsHoldSeconds = 600;
    status_text::format(state, collect);
    TEST_ASSERT_EQUAL_STRING("status: gps=on gps_fix=0 gps_sat=4 gps_hold=600", lines[2]);
}

// Every line fits a log line, also with the longest values.
void test_longest_lines_fit()
{
    State state = someState();
    state.batteryMillivolts = UINT32_MAX;
    state.batteryPercent = UINT8_MAX;
    state.charger = battery::Charger::NotCharging;
    state.gpsSatellites = UINT8_MAX;
    state.gpsHoldSeconds = UINT32_MAX;
    state.pairing = {pairing::Phase::WaitingForFix, 3, pairing::Result::NotInPairingMode};
    state.timeZone = "<+000000000000000000000000000000000000000000000000000000000000>0"; // 63, the most
    status_text::format(state, collect);
    TEST_ASSERT_EQUAL_size_t(11, count);
    for (size_t i = 0; i < count; i++)
        TEST_ASSERT_TRUE(strlen(lines[i]) < status_text::LINE_SIZE - 1); // nothing was cut
    TEST_ASSERT_EQUAL_STRING("status: clock=on tz=<+000000000000000000000000000000000000000000000000000000000000>0",
                             lines[5]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_lines);
    RUN_TEST(test_other_values);
    RUN_TEST(test_frequent_in_effect);
    RUN_TEST(test_pairing_results);
    RUN_TEST(test_pairing_waiting_wifi);
    RUN_TEST(test_gps_held_for_pairing);
    RUN_TEST(test_longest_lines_fit);
    return UNITY_END();
}
