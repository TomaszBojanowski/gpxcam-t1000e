// Unit tests for src/utc.cpp, run on the PC: pio test -e native
// Expected values computed independently with Python's calendar.timegm().
#include <unity.h>

#include "utc.h"

void setUp() {}
void tearDown() {}

namespace {

utc::DateTime at(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    return utc::DateTime{year, month, day, hour, minute, second};
}

void assertDateTime(const utc::DateTime &expected, const utc::DateTime &actual)
{
    TEST_ASSERT_EQUAL(expected.year, actual.year);
    TEST_ASSERT_EQUAL(expected.month, actual.month);
    TEST_ASSERT_EQUAL(expected.day, actual.day);
    TEST_ASSERT_EQUAL(expected.hour, actual.hour);
    TEST_ASSERT_EQUAL(expected.minute, actual.minute);
    TEST_ASSERT_EQUAL(expected.second, actual.second);
}

} // namespace

void test_unix_seconds()
{
    TEST_ASSERT_EQUAL_INT64(0, utc::toUnixSeconds(at(1970, 1, 1, 0, 0, 0)));
    // the GPS epoch: 315964800 s, as in docs/PROTOKOL.md
    TEST_ASSERT_EQUAL_INT64(315964800, utc::toUnixSeconds(at(1980, 1, 6, 0, 0, 0)));
    // the moment of the LUMIX test vectors
    TEST_ASSERT_EQUAL_INT64(1791930629, utc::toUnixSeconds(at(2026, 10, 13, 22, 30, 29)));
    TEST_ASSERT_EQUAL_INT64(1709208000, utc::toUnixSeconds(at(2024, 2, 29, 12, 0, 0)));
    TEST_ASSERT_EQUAL_INT64(951782400, utc::toUnixSeconds(at(2000, 2, 29, 0, 0, 0)));
    TEST_ASSERT_EQUAL_INT64(3471292799, utc::toUnixSeconds(at(2079, 12, 31, 23, 59, 59))); // last NMEA year
}

void test_round_trip_and_rollover()
{
    assertDateTime(at(2026, 10, 13, 22, 30, 29), utc::fromUnixSeconds(1791930629));
    assertDateTime(at(2024, 2, 29, 12, 0, 0), utc::fromUnixSeconds(1709208000));
    // one second after the end of 2026
    assertDateTime(at(2027, 1, 1, 0, 0, 0), utc::fromUnixSeconds(1798761599 + 1));
    assertDateTime(at(1970, 1, 1, 0, 0, 0), utc::fromUnixSeconds(0));
    assertDateTime(at(1969, 12, 31, 23, 59, 59), utc::fromUnixSeconds(-1));
}

void test_valid_dates()
{
    TEST_ASSERT_TRUE(utc::isValid(at(2024, 2, 29, 0, 0, 0)));  // leap year
    TEST_ASSERT_TRUE(utc::isValid(at(2000, 2, 29, 0, 0, 0)));  // divisible by 400
    TEST_ASSERT_FALSE(utc::isValid(at(2026, 2, 29, 0, 0, 0)));
    TEST_ASSERT_FALSE(utc::isValid(at(2100, 2, 29, 0, 0, 0))); // divisible by 100
    TEST_ASSERT_FALSE(utc::isValid(at(2026, 4, 31, 0, 0, 0)));
    TEST_ASSERT_TRUE(utc::isValid(at(2026, 12, 31, 23, 59, 59)));
    TEST_ASSERT_FALSE(utc::isValid(at(2026, 0, 1, 0, 0, 0)));
    TEST_ASSERT_FALSE(utc::isValid(at(2026, 13, 1, 0, 0, 0)));
    TEST_ASSERT_FALSE(utc::isValid(at(2026, 1, 0, 0, 0, 0)));
    TEST_ASSERT_FALSE(utc::isValid(at(2026, 1, 1, 24, 0, 0)));
    TEST_ASSERT_FALSE(utc::isValid(at(2026, 1, 1, 0, 60, 0)));
    TEST_ASSERT_FALSE(utc::isValid(at(2026, 1, 1, 0, 0, 60))); // no leap seconds
}

void test_eu_summer_time()
{
    // 2026: last Sundays are 29 March and 25 October; the change is at 01:00 UTC.
    TEST_ASSERT_FALSE(utc::euSummerTime(utc::toUnixSeconds(at(2026, 3, 29, 0, 59, 59))));
    TEST_ASSERT_TRUE(utc::euSummerTime(utc::toUnixSeconds(at(2026, 3, 29, 1, 0, 0))));
    TEST_ASSERT_TRUE(utc::euSummerTime(utc::toUnixSeconds(at(2026, 10, 13, 22, 30, 29)))); // vector moment
    TEST_ASSERT_TRUE(utc::euSummerTime(utc::toUnixSeconds(at(2026, 10, 25, 0, 59, 59))));
    TEST_ASSERT_FALSE(utc::euSummerTime(utc::toUnixSeconds(at(2026, 10, 25, 1, 0, 0))));
    TEST_ASSERT_FALSE(utc::euSummerTime(utc::toUnixSeconds(at(2026, 1, 15, 12, 0, 0))));
    TEST_ASSERT_FALSE(utc::euSummerTime(utc::toUnixSeconds(at(2026, 12, 31, 23, 59, 59))));
    // 2027: 28 March and 31 October (the last day of the month is a Sunday)
    TEST_ASSERT_FALSE(utc::euSummerTime(utc::toUnixSeconds(at(2027, 3, 28, 0, 59, 59))));
    TEST_ASSERT_TRUE(utc::euSummerTime(utc::toUnixSeconds(at(2027, 3, 28, 1, 0, 0))));
    TEST_ASSERT_TRUE(utc::euSummerTime(utc::toUnixSeconds(at(2027, 10, 31, 0, 59, 59))));
    TEST_ASSERT_FALSE(utc::euSummerTime(utc::toUnixSeconds(at(2027, 10, 31, 1, 0, 0))));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_unix_seconds);
    RUN_TEST(test_round_trip_and_rollover);
    RUN_TEST(test_valid_dates);
    RUN_TEST(test_eu_summer_time);
    return UNITY_END();
}
