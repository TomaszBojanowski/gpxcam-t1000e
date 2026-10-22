// Unit tests for src/tz_table.cpp, run on the PC: pio test -e native
// The expected rules are the footers of the zoneinfo files of the tz database the table was
// generated from; tools/gen_tz_table.py checks them against the full database.
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "tz_table.h"

namespace data {
#include "tz_table_data.inc"
}

namespace {

void assertZone(const char *name, const char *expectedName, const char *expectedRule)
{
    const char *tableName = nullptr;
    const char *rule = nullptr;
    TEST_ASSERT_TRUE_MESSAGE(tz_table::find(name, &tableName, &rule), name);
    TEST_ASSERT_EQUAL_STRING(expectedName, tableName);
    TEST_ASSERT_EQUAL_STRING(expectedRule, rule);
}

void assertUnknown(const char *name)
{
    const char *tableName = "unchanged";
    const char *rule = "unchanged";
    TEST_ASSERT_FALSE_MESSAGE(tz_table::find(name, &tableName, &rule), name);
    TEST_ASSERT_EQUAL_STRING("unchanged", tableName);
    TEST_ASSERT_EQUAL_STRING("unchanged", rule);
}

} // namespace

void setUp() {}
void tearDown() {}

// The zones of the camera tests and the special cases of the rule parser.
void test_known_zones()
{
    assertZone("Europe/Warsaw", "Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3");
    assertZone("America/New_York", "America/New_York", "EST5EDT,M3.2.0,M11.1.0");
    assertZone("Asia/Kolkata", "Asia/Kolkata", "IST-5:30");
    assertZone("Asia/Kathmandu", "Asia/Kathmandu", "<+0545>-5:45");
    assertZone("Australia/Sydney", "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3");
    assertZone("Australia/Lord_Howe", "Australia/Lord_Howe", "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0");
    assertZone("Europe/Dublin", "Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1");
    assertZone("America/Nuuk", "America/Nuuk", "<-02>2<-01>,M3.5.0/-1,M10.5.0/0");
    assertZone("UTC", "UTC", "UTC0");
    assertZone("Etc/GMT+5", "Etc/GMT+5", "<-05>5");
}

// Console input may come in any case; the table's spelling is returned.
void test_case_is_ignored()
{
    assertZone("europe/warsaw", "Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3");
    assertZone("AMERICA/NEW_YORK", "America/New_York", "EST5EDT,M3.2.0,M11.1.0");
    assertZone("utc", "UTC", "UTC0");
}

// Aliases keep their own name and share the rule of their zone.
void test_aliases()
{
    assertZone("Poland", "Poland", "CET-1CEST,M3.5.0,M10.5.0/3");
    assertZone("US/Eastern", "US/Eastern", "EST5EDT,M3.2.0,M11.1.0");
    assertZone("Asia/Calcutta", "Asia/Calcutta", "IST-5:30");
}

void test_unknown_names()
{
    assertUnknown("");
    assertUnknown("Europe");
    assertUnknown("Europe/Warsa");
    assertUnknown("Europe/Warsaw2");
    assertUnknown("Europe/Warsaw ");
    assertUnknown("Factory");
    assertUnknown("Mars/Olympus_Mons");
}

// Every name of the generated data is found with its own rule; a name found under another
// spelling would mean two names that differ only in case.
void test_every_name_is_found()
{
    const char *name = data::ZONE_NAMES;
    for (size_t i = 0; i < data::ZONE_COUNT; i++) {
        const char *expectedRule = data::RULES;
        for (size_t r = 0; r < data::ZONE_RULES[i]; r++)
            expectedRule += strlen(expectedRule) + 1;
        TEST_ASSERT_TRUE(strlen(expectedRule) > 0);
        assertZone(name, name, expectedRule);
        name += strlen(name) + 1;
    }
}

// The version of the tz database: four digits of the year and a letter, e.g. "2026e".
void test_version()
{
    const char *version = tz_table::tzdataVersion();
    TEST_ASSERT_EQUAL_STRING(data::TZDATA_VERSION, version);
    TEST_ASSERT_EQUAL_size_t(5, strlen(version));
    for (size_t i = 0; i < 4; i++)
        TEST_ASSERT_TRUE(isdigit((unsigned char)version[i]));
    TEST_ASSERT_TRUE(islower((unsigned char)version[4]));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_zones);
    RUN_TEST(test_case_is_ignored);
    RUN_TEST(test_aliases);
    RUN_TEST(test_unknown_names);
    RUN_TEST(test_every_name_is_found);
    RUN_TEST(test_version);
    return UNITY_END();
}
