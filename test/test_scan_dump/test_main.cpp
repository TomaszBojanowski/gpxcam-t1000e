// Unit tests for src/scan_dump.cpp, run on the PC: pio test -e native
// Made-up values: addresses, and the camera serial 12 34 56 78 9a from docs/PROTOCOL.md.
#include <string.h>
#include <unity.h>

#include "scan_dump.h"

void setUp() {}
void tearDown() {}

namespace {

// address is least significant byte first; address[0] is the byte the line shows.
scan_dump::Advert make(const uint8_t *data, uint8_t length, uint8_t addr0, uint8_t type, int8_t rssi, bool rsp)
{
    scan_dump::Advert advert = {};
    advert.address[0] = addr0;
    advert.addressType = type;
    advert.rssi = rssi;
    advert.scanResponse = rsp;
    advert.length = length;
    memcpy(advert.data, data, length);
    return advert;
}

} // namespace

void test_a_lumix_camera_shows_full_bytes()
{
    const uint8_t data[] = {0x02, 0x01, 0x06, 0x09, 0xff, 0x3a, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a};
    char out[200];
    const size_t n = scan_dump::formatAdvert(make(data, sizeof(data), 0xe2, 0x02, -48, false), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "**:**:**:**:**:e2 RPA -48 dBm adv 13 B, camera, bytes: 02 01 06 09 ff 3a 00 00 12 34 56 78 9a", out);
    TEST_ASSERT_EQUAL(strlen(out), n);
}

void test_other_panasonic_shows_full_bytes()
{
    const uint8_t data[] = {0x05, 0xff, 0x3a, 0x00, 0x01, 0x02};
    char out[200];
    scan_dump::formatAdvert(make(data, sizeof(data), 0x11, 0x00, -60, false), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("**:**:**:**:**:11 public -60 dBm adv 6 B, Panasonic, bytes: 05 ff 3a 00 01 02", out);
}

void test_a_non_panasonic_device_is_a_summary_without_bytes()
{
    const uint8_t data[] = {0x02, 0x01, 0x06, 0x03, 0x03, 0x0f, 0x18}; // flags + a service UUID
    char out[200];
    scan_dump::formatAdvert(make(data, sizeof(data), 0x33, 0x02, -40, true), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("**:**:**:**:**:33 RPA -40 dBm rsp 7 B", out);
}

void test_the_length_is_capped_and_a_short_buffer_is_refused()
{
    uint8_t data[scan_dump::MAX_ADVERT_BYTES];
    memset(data, 0xAB, sizeof(data));
    scan_dump::Advert advert = make(data, sizeof(data), 0x44, 0x02, -50, false);
    advert.length = 255; // more than MAX_ADVERT_BYTES: capped
    char out[256];
    const size_t n = scan_dump::formatAdvert(advert, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, " 31 B")); // capped to MAX_ADVERT_BYTES
    char tiny[10];
    TEST_ASSERT_EQUAL(0, scan_dump::formatAdvert(advert, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL(0, scan_dump::formatAdvert(advert, out, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_lumix_camera_shows_full_bytes);
    RUN_TEST(test_other_panasonic_shows_full_bytes);
    RUN_TEST(test_a_non_panasonic_device_is_a_summary_without_bytes);
    RUN_TEST(test_the_length_is_capped_and_a_short_buffer_is_refused);
    return UNITY_END();
}
