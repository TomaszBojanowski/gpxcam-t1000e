// Unit tests for src/nmea.cpp, run on the PC: pio test -e native
// All positions are made up (a point at sea, a made-up S/W point) or the textbook GGA
// example from NMEA documentation; the no-fix sentences come from the card and hold no position.
#include <string.h>
#include <unity.h>

#include "nmea.h"

void setUp() {}
void tearDown() {}

// ---- checksum, fields, commands --------------------------------------------------

void test_checksum()
{
    TEST_ASSERT_TRUE(nmea::checksumOk("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
    TEST_ASSERT_TRUE(nmea::checksumOk("$PAIR067*3B"));
    TEST_ASSERT_TRUE(nmea::checksumOk("$GNGGA,123519.000,,,,,0,0,,,M,,M,,*5b")); // lower-case hex
    TEST_ASSERT_FALSE(nmea::checksumOk("$PAIR067*3C"));                          // wrong sum
    TEST_ASSERT_FALSE(nmea::checksumOk("$PAIR067"));                             // no '*'
    TEST_ASSERT_FALSE(nmea::checksumOk("$PAIR067*3"));                           // one digit
    TEST_ASSERT_FALSE(nmea::checksumOk("$PAIR067*3B1"));                         // extra character
    TEST_ASSERT_FALSE(nmea::checksumOk("PAIR067*3B"));                           // no '$'
    TEST_ASSERT_FALSE(nmea::checksumOk(""));
}

void test_type_and_prefix()
{
    TEST_ASSERT_TRUE(nmea::isType("$GNRMC,1*00", "RMC"));
    TEST_ASSERT_TRUE(nmea::isType("$GPGGA,1*00", "GGA"));
    TEST_ASSERT_FALSE(nmea::isType("$GNGGA,1*00", "RMC"));
    TEST_ASSERT_FALSE(nmea::isType("$PAIR063,-1*0F", "063"));
    TEST_ASSERT_FALSE(nmea::isType("$GNRMC", "RMC"));
    TEST_ASSERT_TRUE(nmea::startsWith("$PAIR001,062,0*3F", "$PAIR001,"));
    TEST_ASSERT_FALSE(nmea::startsWith("$PAIR00", "$PAIR001,"));
}

void test_fields()
{
    const char *ack = "$PAIR001,062,0*3F";
    long v = -7;
    TEST_ASSERT_TRUE(nmea::fieldInt(ack, 1, 0, 999, &v));
    TEST_ASSERT_EQUAL(62, v);
    TEST_ASSERT_TRUE(nmea::fieldInt(ack, 2, 0, 5, &v));
    TEST_ASSERT_EQUAL(0, v);
    TEST_ASSERT_FALSE(nmea::fieldInt(ack, 3, 0, 5, &v)); // past the last field
    TEST_ASSERT_FALSE(nmea::fieldInt(ack, 1, 0, 50, &v)); // out of range
    TEST_ASSERT_TRUE(nmea::fieldInt("$PAIR063,-1*0F", 1, -1, 9, &v));
    TEST_ASSERT_EQUAL(-1, v);
    TEST_ASSERT_FALSE(nmea::fieldInt("$GNGGA,1,,2*00", 2, 0, 9, &v)); // empty field
    TEST_ASSERT_FALSE(nmea::fieldInt("$GNGGA,1,x,2*00", 2, 0, 9, &v)); // not a number
    TEST_ASSERT_EQUAL_STRING_LEN("A,", nmea::field("$GNRMC,1,A,*00", 2), 2);
    TEST_ASSERT_NULL(nmea::field("$GNRMC,1*00", 2));
}

void test_build_sentence()
{
    char out[32];
    // Both were sent to the card and acknowledged by the GPS (M2 step 2).
    TEST_ASSERT_EQUAL(16, nmea::buildSentence("PAIR063,-1", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("$PAIR063,-1*0F\r\n", out);
    TEST_ASSERT_EQUAL(13, nmea::buildSentence("PAIR067", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("$PAIR067*3B\r\n", out);
    // Quectel specification example for $PAIR066.
    TEST_ASSERT_EQUAL(25, nmea::buildSentence("PAIR066,1,1,1,1,0,0", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("$PAIR066,1,1,1,1,0,0*3A\r\n", out);
    char small[13]; // one byte short for "$PAIR067*3B\r\n" and its terminator
    TEST_ASSERT_EQUAL(0, nmea::buildSentence("PAIR067", small, sizeof(small)));
}

// ---- RMC -----------------------------------------------------------------------

void test_rmc_valid_north_east()
{
    // The made-up point at sea used for the LUMIX location test vector (54.6, 19.0).
    nmea::Rmc r;
    TEST_ASSERT_TRUE(nmea::parseRmc("$GNRMC,123519.000,A,5436.0000,N,01900.0000,E,0.00,0.00,191026,,,A,V*06", &r));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_TRUE(r.hasPosition);
    TEST_ASSERT_EQUAL_INT32(546000000, r.latitudeE7);
    TEST_ASSERT_EQUAL_INT32(190000000, r.longitudeE7);
    TEST_ASSERT_TRUE(r.hasTime);
    TEST_ASSERT_EQUAL(12, r.time.hour);
    TEST_ASSERT_EQUAL(35, r.time.minute);
    TEST_ASSERT_EQUAL(19, r.time.second);
    TEST_ASSERT_EQUAL(0, r.time.millisecond);
    TEST_ASSERT_TRUE(r.hasDate);
    TEST_ASSERT_EQUAL(2026, r.date.year);
    TEST_ASSERT_EQUAL(10, r.date.month);
    TEST_ASSERT_EQUAL(19, r.date.day);
}

void test_rmc_south_west_and_rounding()
{
    // 33 deg 52.1234' S = -33.868723333 deg, 151 deg 12.5678' W = -151.209463333 deg.
    nmea::Rmc r;
    TEST_ASSERT_TRUE(nmea::parseRmc("$GPRMC,000001.50,A,3352.1234,S,15112.5678,W,,,010125,,,A*5B", &r));
    TEST_ASSERT_TRUE(r.hasPosition);
    TEST_ASSERT_EQUAL_INT32(-338687233, r.latitudeE7);
    TEST_ASSERT_EQUAL_INT32(-1512094633, r.longitudeE7);
    TEST_ASSERT_EQUAL(1, r.time.second);
    TEST_ASSERT_EQUAL(500, r.time.millisecond);
    TEST_ASSERT_EQUAL(2025, r.date.year);
}

void test_rmc_no_fix_from_the_card()
{
    // Received from the card without a fix: time already set, default date 07.01.1980.
    nmea::Rmc r;
    TEST_ASSERT_TRUE(nmea::parseRmc("$GNRMC,190128.092,V,,,,,,,070180,,,N,V*2F", &r));
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_FALSE(r.hasPosition);
    TEST_ASSERT_TRUE(r.hasTime);
    TEST_ASSERT_EQUAL(19, r.time.hour);
    TEST_ASSERT_EQUAL(92, r.time.millisecond);
    TEST_ASSERT_TRUE(r.hasDate);
    TEST_ASSERT_EQUAL(1980, r.date.year);
    TEST_ASSERT_EQUAL(1, r.date.month);
    TEST_ASSERT_EQUAL(7, r.date.day);
}

void test_rmc_end_of_day_and_century()
{
    nmea::Rmc r;
    TEST_ASSERT_TRUE(nmea::parseRmc("$GNRMC,235959.999,V,,,,,,,311299,,,N,V*20", &r));
    TEST_ASSERT_EQUAL(23, r.time.hour);
    TEST_ASSERT_EQUAL(59, r.time.minute);
    TEST_ASSERT_EQUAL(59, r.time.second);
    TEST_ASSERT_EQUAL(999, r.time.millisecond);
    TEST_ASSERT_EQUAL(1999, r.date.year);
    TEST_ASSERT_EQUAL(12, r.date.month);
    TEST_ASSERT_EQUAL(31, r.date.day);
}

void test_rmc_rejects()
{
    nmea::Rmc r;
    // minutes 60: the sentence parses, the position does not
    TEST_ASSERT_TRUE(nmea::parseRmc("$GNRMC,101010.000,A,5460.0000,N,01900.0000,E,,,191026,,,A,V*09", &r));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FALSE(r.hasPosition);
    // malformed time
    TEST_ASSERT_FALSE(nmea::parseRmc("$GNRMC,1010,A,5436.0000,N,01900.0000,E,,,191026,,,A,V*15", &r));
    // wrong checksum
    TEST_ASSERT_FALSE(nmea::parseRmc("$GNRMC,123519.000,A,5436.0000,N,01900.0000,E,0.00,0.00,191026,,,A,V*07", &r));
    // a GGA is not an RMC
    TEST_ASSERT_FALSE(nmea::parseRmc("$GNGGA,123519.000,,,,,0,0,,,M,,M,,*5B", &r));
}

// ---- GGA -----------------------------------------------------------------------

void test_gga_textbook_example()
{
    nmea::Gga g;
    TEST_ASSERT_TRUE(nmea::parseGga("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", &g));
    TEST_ASSERT_EQUAL(1, g.quality);
    TEST_ASSERT_EQUAL(8, g.satellites);
    TEST_ASSERT_TRUE(g.hasAltitude);
    TEST_ASSERT_EQUAL_INT32(5454, g.altitudeDm);
    TEST_ASSERT_TRUE(g.hasTime);
    TEST_ASSERT_EQUAL(12, g.time.hour);
    TEST_ASSERT_EQUAL(19, g.time.second);
}

void test_gga_no_fix_from_the_card()
{
    nmea::Gga g;
    TEST_ASSERT_TRUE(nmea::parseGga("$GNGGA,190128.092,,,,,0,0,,,M,,M,,*5E", &g));
    TEST_ASSERT_EQUAL(0, g.quality);
    TEST_ASSERT_EQUAL(0, g.satellites);
    TEST_ASSERT_FALSE(g.hasAltitude);
}

void test_gga_negative_altitude_and_empty_satellites()
{
    nmea::Gga g;
    TEST_ASSERT_TRUE(nmea::parseGga("$GNGGA,123519.000,5436.0000,N,01900.0000,E,1,12,0.8,-12.35,M,30.1,M,,*62", &g));
    TEST_ASSERT_EQUAL(12, g.satellites);
    TEST_ASSERT_EQUAL_INT32(-124, g.altitudeDm); // -12.35 m, rounded away from zero
    TEST_ASSERT_TRUE(nmea::parseGga("$GNGGA,123519.000,5436.0000,N,01900.0000,E,1,,0.8,7.04,M,30.1,M,,*7A", &g));
    TEST_ASSERT_EQUAL(-1, g.satellites);
    TEST_ASSERT_EQUAL_INT32(70, g.altitudeDm);
    TEST_ASSERT_FALSE(nmea::parseGga("$GNRMC,190128.092,V,,,,,,,070180,,,N,V*2F", &g)); // not a GGA
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_checksum);
    RUN_TEST(test_type_and_prefix);
    RUN_TEST(test_fields);
    RUN_TEST(test_build_sentence);
    RUN_TEST(test_rmc_valid_north_east);
    RUN_TEST(test_rmc_south_west_and_rounding);
    RUN_TEST(test_rmc_no_fix_from_the_card);
    RUN_TEST(test_rmc_end_of_day_and_century);
    RUN_TEST(test_rmc_rejects);
    RUN_TEST(test_gga_textbook_example);
    RUN_TEST(test_gga_no_fix_from_the_card);
    RUN_TEST(test_gga_negative_altitude_and_empty_satellites);
    return UNITY_END();
}
