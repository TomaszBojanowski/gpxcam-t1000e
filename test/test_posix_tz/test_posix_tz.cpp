// Unit tests for src/posix_tz.cpp, run on the PC: pio test -e native
// zone_vectors.inc holds UTC offsets from the full tz database (tools/gen_tz_table.py).
#include <stdint.h>
#include <stdio.h>
#include <unity.h>

#include "posix_tz.h"
#include "tz_table.h"
#include "utc.h"

#include "zone_vectors.inc"

using posix_tz::CameraZone;
using posix_tz::Rule;

namespace {

constexpr int32_t HOUR = 3600;
constexpr int32_t MINUTE = 60;

int64_t at(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute = 0, uint8_t second = 0)
{
    return utc::toUnixSeconds({year, month, day, hour, minute, second});
}

Rule parsed(const char *text)
{
    Rule rule = {};
    TEST_ASSERT_TRUE_MESSAGE(posix_tz::parse(text, &rule), text);
    return rule;
}

Rule zoneRule(const char *zone)
{
    const char *name = nullptr;
    const char *rule = nullptr;
    TEST_ASSERT_TRUE_MESSAGE(tz_table::find(zone, &name, &rule), zone);
    return parsed(rule);
}

void assertTransition(uint8_t month, uint8_t week, uint8_t weekday, int32_t timeSeconds,
                      const posix_tz::Transition &transition)
{
    TEST_ASSERT_EQUAL_UINT8(month, transition.month);
    TEST_ASSERT_EQUAL_UINT8(week, transition.week);
    TEST_ASSERT_EQUAL_UINT8(weekday, transition.weekday);
    TEST_ASSERT_EQUAL_INT32(timeSeconds, transition.timeSeconds);
}

void assertCameraZone(const char *zone, int64_t moment, int16_t standardOffsetMinutes, bool summerTime)
{
    const CameraZone camera = posix_tz::cameraZone(zoneRule(zone), moment);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(standardOffsetMinutes, camera.standardOffsetMinutes, zone);
    TEST_ASSERT_EQUAL_MESSAGE(summerTime, camera.summerTime, zone);
}

} // namespace

void setUp() {}
void tearDown() {}

// POSIX counts offsets west of UTC; the rule keeps them east.
void test_parse_fixed_offsets()
{
    struct {
        const char *text;
        int32_t offset;
    } const cases[] = {
        {"UTC0", 0},
        {"IST-5:30", 5 * HOUR + 30 * MINUTE},
        {"<+0545>-5:45", 5 * HOUR + 45 * MINUTE},
        {"<-05>5", -5 * HOUR},
        {"<+14>-14", 14 * HOUR},
        {"<-0930>+9:30", -(9 * HOUR + 30 * MINUTE)},
    };
    for (const auto &c : cases) {
        const Rule rule = parsed(c.text);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(c.offset, rule.standardOffsetSeconds, c.text);
        TEST_ASSERT_FALSE_MESSAGE(rule.hasSummerTime, c.text);
    }
}

void test_parse_summer_time()
{
    Rule rule = parsed("CET-1CEST,M3.5.0,M10.5.0/3");
    TEST_ASSERT_EQUAL_INT32(1 * HOUR, rule.standardOffsetSeconds);
    TEST_ASSERT_TRUE(rule.hasSummerTime);
    TEST_ASSERT_EQUAL_INT32(2 * HOUR, rule.summerOffsetSeconds);
    assertTransition(3, 5, 0, 2 * HOUR, rule.summerStart); // 02:00 when not given
    assertTransition(10, 5, 0, 3 * HOUR, rule.summerEnd);

    // Summer time given explicitly, one hour behind (Europe/Dublin).
    rule = parsed("IST-1GMT0,M10.5.0,M3.5.0/1");
    TEST_ASSERT_EQUAL_INT32(1 * HOUR, rule.standardOffsetSeconds);
    TEST_ASSERT_EQUAL_INT32(0, rule.summerOffsetSeconds);

    // Negative transition time (America/Nuuk) and one over 24 h (Asia/Gaza).
    rule = parsed("<-02>2<-01>,M3.5.0/-1,M10.5.0/0");
    TEST_ASSERT_EQUAL_INT32(-2 * HOUR, rule.standardOffsetSeconds);
    TEST_ASSERT_EQUAL_INT32(-1 * HOUR, rule.summerOffsetSeconds);
    assertTransition(3, 5, 0, -1 * HOUR, rule.summerStart);
    assertTransition(10, 5, 0, 0, rule.summerEnd);
    rule = parsed("EET-2EEST,M3.4.4/50,M10.4.4/50");
    assertTransition(3, 4, 4, 50 * HOUR, rule.summerStart);

    // Minutes in offsets and transition times (Pacific/Chatham, America/St_Johns).
    rule = parsed("<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45");
    TEST_ASSERT_EQUAL_INT32(12 * HOUR + 45 * MINUTE, rule.standardOffsetSeconds);
    TEST_ASSERT_EQUAL_INT32(13 * HOUR + 45 * MINUTE, rule.summerOffsetSeconds);
    assertTransition(9, 5, 0, 2 * HOUR + 45 * MINUTE, rule.summerStart);
    rule = parsed("NST3:30NDT,M3.2.0,M11.1.0");
    TEST_ASSERT_EQUAL_INT32(-(3 * HOUR + 30 * MINUTE), rule.standardOffsetSeconds);
    TEST_ASSERT_EQUAL_INT32(-(2 * HOUR + 30 * MINUTE), rule.summerOffsetSeconds);
}

void test_parse_refuses()
{
    const char *const cases[] = {
        "",
        "CET",                            // no offset
        "CE-1",                           // name shorter than 3 letters
        "C1T-1",                          // digit in a name without <>
        "<+05-5",                         // no closing >
        "<+0>-5",                         // name in <> shorter than 3
        "CET-25",                         // offset over 24 h
        "CET-1:30:15",                    // not whole minutes
        "CET-1:60",                       // minutes over 59
        "CET-1 ",                         // trailing space
        ":Europe/Warsaw",                 // not a rule
        "Europe/Warsaw",                  // a zone name, not a rule
        "CET-1CEST",                      // summer time without its transitions
        "CET-1CEST,M3.5.0",               // only one transition
        "CET-1CEST,M3.5.0,M10.5.0/3,",    // something after the rule
        "CET-1CEST,M13.5.0,M10.5.0/3",    // month 13
        "CET-1CEST,M3.6.0,M10.5.0/3",     // week 6
        "CET-1CEST,M3.5.7,M10.5.0/3",     // weekday 7
        "CET-1CEST,M3.5.0/168,M10.5.0/3", // transition time over 167 h
        "CET-1CEST,M3.5.0/1000,M10.5.0",  // more than 3 digits
        "CET-1CEST,J60,J300",             // Jn form, not supported
        "CET-1CEST,59,299",               // n form, not supported
        "EST5EDT,M3.2.0,M11.1.0x",        // trailing character
    };
    for (const char *text : cases) {
        Rule rule = {};
        rule.standardOffsetSeconds = 12345;
        TEST_ASSERT_FALSE_MESSAGE(posix_tz::parse(text, &rule), text);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(12345, rule.standardOffsetSeconds, text); // unchanged
    }
}

// Every rule of the tz table, at every change of five years, against the full database.
void test_every_table_rule_matches_the_database()
{
    for (const ZoneVector &v : ZONE_VECTORS) {
        char message[80];
        snprintf(message, sizeof(message), "%s at %lld", v.zone, (long long)v.unixSeconds);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(v.utcOffsetSeconds, posix_tz::utcOffsetSeconds(zoneRule(v.zone), v.unixSeconds),
                                        message);
    }
}

// Changes moved into another year by their transition time count in that year (RFC 8536,
// 3.3.1; tzcode works so, glibc does not). Summer time +01 starts on the first Friday of
// January at -24:00 and ends on the last Saturday of December at 72:00:
// - 2026: ends Sat 26 Dec + 72 h = 29 Dec 00:00 +01 = 28 Dec 23:00 UTC;
// - 2027: starts Fri 1 Jan - 24 h = 31 Dec 2026 00:00 UTC, ends Sat 25 Dec + 72 h = 27 Dec 23:00 UTC.
void test_changes_across_the_new_year()
{
    const Rule rule = parsed("<+00>0<+01>,M1.1.5/-24,M12.5.6/72");
    TEST_ASSERT_EQUAL_INT32(HOUR, posix_tz::utcOffsetSeconds(rule, at(2026, 12, 28, 22, 59, 59)));
    TEST_ASSERT_EQUAL_INT32(0, posix_tz::utcOffsetSeconds(rule, at(2026, 12, 28, 23)));
    TEST_ASSERT_EQUAL_INT32(0, posix_tz::utcOffsetSeconds(rule, at(2026, 12, 30, 23, 59, 59)));
    TEST_ASSERT_EQUAL_INT32(HOUR, posix_tz::utcOffsetSeconds(rule, at(2026, 12, 31, 0)));
    TEST_ASSERT_EQUAL_INT32(HOUR, posix_tz::utcOffsetSeconds(rule, at(2027, 1, 1, 12)));
    TEST_ASSERT_EQUAL_INT32(HOUR, posix_tz::utcOffsetSeconds(rule, at(2027, 12, 27, 22, 59, 59)));
    TEST_ASSERT_EQUAL_INT32(0, posix_tz::utcOffsetSeconds(rule, at(2027, 12, 27, 23)));
}

// The default zone gives the camera what versions up to 1.1 sent with their fixed EU rule
// (60 min, summer time from 01:00 UTC on the last Sunday of March until 01:00 UTC on the
// last Sunday of October): the cases of that rule's former tests.
void test_warsaw_as_before()
{
    struct {
        int64_t moment;
        bool summerTime;
    } const cases[] = {
        {at(2026, 3, 29, 0, 59, 59), false}, // 2026: 29 March and 25 October
        {at(2026, 3, 29, 1), true},
        {at(2026, 10, 13, 22, 30, 29), true}, // the moment of the time vector in docs/PROTOCOL.md
        {at(2026, 10, 25, 0, 59, 59), true},
        {at(2026, 10, 25, 1), false},
        {at(2026, 1, 15, 12), false},
        {at(2026, 12, 31, 23, 59, 59), false},
        {at(2027, 3, 28, 0, 59, 59), false}, // 2027: 28 March and 31 October, the last day of the month
        {at(2027, 3, 28, 1), true},
        {at(2027, 10, 31, 0, 59, 59), true},
        {at(2027, 10, 31, 1), false},
    };
    const Rule warsaw = zoneRule("Europe/Warsaw");
    for (const auto &c : cases) {
        const CameraZone camera = posix_tz::cameraZone(warsaw, c.moment);
        TEST_ASSERT_EQUAL_INT16(60, camera.standardOffsetMinutes);
        TEST_ASSERT_EQUAL(c.summerTime, camera.summerTime);
    }
}

// What the camera gets: the standard offset in minutes and the flag that adds one hour.
void test_camera_zone()
{
    const int64_t january = at(2027, 1, 15, 12);
    const int64_t july = at(2027, 7, 15, 12);
    assertCameraZone("Europe/Warsaw", july, 60, true);
    assertCameraZone("Europe/Warsaw", january, 60, false);
    assertCameraZone("America/New_York", july, -300, true);
    assertCameraZone("America/New_York", january, -300, false);
    assertCameraZone("America/St_Johns", july, -210, true);
    assertCameraZone("Australia/Sydney", january, 600, true);
    assertCameraZone("Australia/Sydney", july, 600, false);
    assertCameraZone("Pacific/Chatham", january, 765, true);
    assertCameraZone("Asia/Kolkata", july, 330, false);
    assertCameraZone("Asia/Kathmandu", july, 345, false);
    assertCameraZone("UTC", july, 0, false);
    // Summer time one hour behind: Irish winter (GMT) is the standard time for the camera.
    assertCameraZone("Europe/Dublin", july, 0, true);
    assertCameraZone("Europe/Dublin", january, 0, false);
    // Summer time 30 min or 2 h ahead: the offset in effect, without the flag.
    assertCameraZone("Australia/Lord_Howe", january, 660, false);
    assertCameraZone("Australia/Lord_Howe", july, 630, false);
    assertCameraZone("Antarctica/Troll", july, 120, false);
    assertCameraZone("Antarctica/Troll", january, 0, false);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_fixed_offsets);
    RUN_TEST(test_parse_summer_time);
    RUN_TEST(test_parse_refuses);
    RUN_TEST(test_every_table_rule_matches_the_database);
    RUN_TEST(test_changes_across_the_new_year);
    RUN_TEST(test_warsaw_as_before);
    RUN_TEST(test_camera_zone);
    return UNITY_END();
}
