// Unit tests for src/scan_dump.cpp, run on the PC: pio test -e native
// Made-up values: addresses, and the camera serial 12 34 56 78 9a from docs/PROTOCOL.md.
#include <string.h>
#include <unity.h>

#include "scan_dump.h"

void setUp() {}
void tearDown() {}

namespace {

// address is least significant byte first; address[0] is the byte the line shows.
using advert_kind::Kind;

scan_dump::Advert make(const uint8_t *data, uint8_t length, uint8_t addr0, uint8_t type, int8_t rssi, Kind kind)
{
    scan_dump::Advert advert = {};
    advert.address[0] = addr0;
    advert.addressType = type;
    advert.rssi = rssi;
    advert.kind = kind;
    advert.channel = 38;
    advert.length = length;
    memcpy(advert.data, data, length);
    return advert;
}

} // namespace

void test_a_lumix_camera_shows_full_bytes()
{
    const uint8_t data[] = {0x02, 0x01, 0x06, 0x09, 0xff, 0x3a, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a};
    const scan_dump::Advert advert = make(data, sizeof(data), 0xe2, 0x02, -48, Kind::Connectable);
    char out[scan_dump::BODY_MAX + 1];
    size_t n = scan_dump::formatAdvert(advert, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("**:**:**:**:**:e2 RPA -48 dBm ch 38 adv connectable 13 B, camera", out);
    TEST_ASSERT_EQUAL(strlen(out), n);
    n = scan_dump::formatBytes(advert, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("bytes: 02 01 06 09 ff 3a 00 00 12 34 56 78 9a", out);
    TEST_ASSERT_EQUAL(strlen(out), n);
}

void test_other_panasonic_shows_full_bytes()
{
    const uint8_t data[] = {0x05, 0xff, 0x3a, 0x00, 0x01, 0x02};
    char out[scan_dump::BODY_MAX + 1];
    const scan_dump::Advert advert = make(data, sizeof(data), 0x11, 0x00, -60, Kind::ScanOnly);
    scan_dump::formatAdvert(advert, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("**:**:**:**:**:11 public -60 dBm ch 38 adv scan-only 6 B, Panasonic", out);
    scan_dump::formatBytes(advert, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("bytes: 05 ff 3a 00 01 02", out);
    scan_dump::formatAdvert(make(data, sizeof(data), 0x11, 0x00, -60, Kind::NonConnectable), out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, " dBm ch 38 adv non-connectable 6 B,"));
}

void test_a_non_panasonic_device_is_a_summary_without_bytes()
{
    const uint8_t data[] = {0x02, 0x01, 0x06, 0x03, 0x03, 0x0f, 0x18}; // flags + a service UUID
    char out[scan_dump::BODY_MAX + 1];
    const scan_dump::Advert advert = make(data, sizeof(data), 0x33, 0x02, -40, Kind::ScanResponse);
    scan_dump::formatAdvert(advert, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("**:**:**:**:**:33 RPA -40 dBm ch 38 rsp 7 B", out);
    TEST_ASSERT_EQUAL(0, scan_dump::formatBytes(advert, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// The longest lines fit in BODY_MAX, which bluetooth.cpp fits in a log line: nothing is cut.
void test_the_longest_lines_fit()
{
    uint8_t data[scan_dump::MAX_ADVERT_BYTES];
    memset(data, 0xAB, sizeof(data));
    const uint8_t panasonic[] = {0x1e, 0xff, 0x3a, 0x00}; // Panasonic manufacturer data over the whole advert
    memcpy(data, panasonic, sizeof(panasonic));
    scan_dump::Advert advert = make(data, sizeof(data), 0x44, 0x01, -128, Kind::NonConnectable);
    advert.channel = 255;
    char out[scan_dump::BODY_MAX + 1];
    TEST_ASSERT_EQUAL(83, scan_dump::formatAdvert(advert, out, sizeof(out)));
    TEST_ASSERT_NOT_NULL(strstr(out, "random static -128 dBm ch 255 adv non-connectable 31 B, Panasonic"));
    TEST_ASSERT_EQUAL(scan_dump::BODY_MAX, scan_dump::formatBytes(advert, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("ab ab ab", out + strlen(out) - 8);
    char tooShort[scan_dump::BODY_MAX]; // one short: refused, not cut
    TEST_ASSERT_EQUAL(0, scan_dump::formatBytes(advert, tooShort, sizeof(tooShort)));
}

void test_the_length_is_capped_and_a_short_buffer_is_refused()
{
    uint8_t data[scan_dump::MAX_ADVERT_BYTES];
    memset(data, 0xAB, sizeof(data));
    scan_dump::Advert advert = make(data, sizeof(data), 0x44, 0x02, -50, Kind::Connectable);
    advert.length = 255; // more than MAX_ADVERT_BYTES: capped
    char out[scan_dump::BODY_MAX + 1];
    const size_t n = scan_dump::formatAdvert(advert, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, " 31 B")); // capped to MAX_ADVERT_BYTES
    char tiny[10];
    TEST_ASSERT_EQUAL(0, scan_dump::formatAdvert(advert, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL(0, scan_dump::formatAdvert(advert, out, 0));
    TEST_ASSERT_EQUAL(0, scan_dump::formatBytes(advert, out, 0));
}

// The slots of the per-channel counts of a run: 37..39; another index is not counted.
void test_channel_slots()
{
    TEST_ASSERT_EQUAL_size_t(0, scan_dump::channelSlot(37));
    TEST_ASSERT_EQUAL_size_t(1, scan_dump::channelSlot(38));
    TEST_ASSERT_EQUAL_size_t(2, scan_dump::channelSlot(39));
    TEST_ASSERT_EQUAL_size_t(scan_dump::ADV_CHANNELS, scan_dump::channelSlot(36));
    TEST_ASSERT_EQUAL_size_t(scan_dump::ADV_CHANNELS, scan_dump::channelSlot(40));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_lumix_camera_shows_full_bytes);
    RUN_TEST(test_other_panasonic_shows_full_bytes);
    RUN_TEST(test_a_non_panasonic_device_is_a_summary_without_bytes);
    RUN_TEST(test_the_longest_lines_fit);
    RUN_TEST(test_the_length_is_capped_and_a_short_buffer_is_refused);
    RUN_TEST(test_channel_slots);
    return UNITY_END();
}
