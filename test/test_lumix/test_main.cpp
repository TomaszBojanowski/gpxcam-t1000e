// Unit tests for src/lumix.cpp, run on the PC: pio test -e native
// All camera values are made up: serial number 12 34 56 78 9a. The service UUID is the one
// the camera advertises (docs/PROTOKOL.md), the same for every camera. The vectors of the
// byte formats come from docs/PROTOKOL.md; the other expected values were computed with a
// Python copy of the gpxcam app's LumixProtocol.kt.
#include <string.h>
#include <unity.h>

#include "lumix.h"
#include "pairing_record.h"

void setUp() {}
void tearDown() {}

namespace {

const uint8_t SERIAL[lumix::SERIAL_LENGTH] = {0x12, 0x34, 0x56, 0x78, 0x9a};

// A Camera filled with a marker, to see whether parseAdvert() wrote it.
lumix::Camera untouched()
{
    lumix::Camera camera;
    memset(&camera, 0xEE, sizeof(camera));
    return camera;
}

void assertCamera(const lumix::Camera &camera, uint8_t state)
{
    TEST_ASSERT_EQUAL_HEX8(state, camera.state);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SERIAL, camera.serial, lumix::SERIAL_LENGTH);
}

void assertUntouched(const lumix::Camera &camera)
{
    const lumix::Camera marker = untouched();
    TEST_ASSERT_EQUAL_MEMORY(&marker, &camera, sizeof(camera));
}

} // namespace

// ---- cameras ---------------------------------------------------------------------

void test_camera_with_flags()
{
    // flags 06, then manufacturer data 3a 00 <on> <serial>
    const uint8_t data[] = {0x02, 0x01, 0x06, 0x09, 0xff, 0x3a, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a};
    lumix::Camera camera = untouched();
    TEST_ASSERT_EQUAL(lumix::Advert::Camera, lumix::parseAdvert(data, sizeof(data), &camera));
    assertCamera(camera, lumix::STATE_ON);
}

void test_camera_full_31_bytes_with_service_uuid()
{
    // flags, the advertised 128-bit service 054AC620-3214-11E6-AC0D-0002A5D5C51B
    // (little-endian), manufacturer data in pairing state: 31 bytes, the legacy maximum.
    const uint8_t data[] = {0x02, 0x01, 0x06,
                            0x11, 0x07, 0x1b, 0xc5, 0xd5, 0xa5, 0x02, 0x00, 0x0d, 0xac,
                            0xe6, 0x11, 0x14, 0x32, 0x20, 0xc6, 0x4a, 0x05,
                            0x09, 0xff, 0x3a, 0x00, 0x03, 0x12, 0x34, 0x56, 0x78, 0x9a};
    TEST_ASSERT_EQUAL(31, sizeof(data));
    lumix::Camera camera = untouched();
    TEST_ASSERT_EQUAL(lumix::Advert::Camera, lumix::parseAdvert(data, sizeof(data), &camera));
    assertCamera(camera, lumix::STATE_PAIRING);
}

void test_camera_in_scan_response_only()
{
    const uint8_t data[] = {0x09, 0xff, 0x3a, 0x00, 0x06, 0x12, 0x34, 0x56, 0x78, 0x9a};
    lumix::Camera camera = untouched();
    TEST_ASSERT_EQUAL(lumix::Advert::Camera, lumix::parseAdvert(data, sizeof(data), &camera));
    assertCamera(camera, lumix::STATE_STANDBY);
}

void test_camera_after_other_manufacturer()
{
    // Apple's company identifier 0x004C first, then the camera
    const uint8_t data[] = {0x04, 0xff, 0x4c, 0x00, 0x01,
                            0x09, 0xff, 0x3a, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a};
    lumix::Camera camera = untouched();
    TEST_ASSERT_EQUAL(lumix::Advert::Camera, lumix::parseAdvert(data, sizeof(data), &camera));
    assertCamera(camera, lumix::STATE_ON);
}

void test_camera_then_zero_padding()
{
    const uint8_t data[] = {0x09, 0xff, 0x3a, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a, 0x00, 0x00, 0x00};
    lumix::Camera camera = untouched();
    TEST_ASSERT_EQUAL(lumix::Advert::Camera, lumix::parseAdvert(data, sizeof(data), &camera));
    assertCamera(camera, lumix::STATE_ON);
}

// ---- not a camera ------------------------------------------------------------------

void test_other_manufacturer()
{
    const uint8_t data[] = {0x02, 0x01, 0x06, 0x09, 0xff, 0x4c, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a};
    lumix::Camera camera = untouched();
    TEST_ASSERT_EQUAL(lumix::Advert::None, lumix::parseAdvert(data, sizeof(data), &camera));
    assertUntouched(camera);
}

void test_panasonic_in_another_layout()
{
    lumix::Camera camera = untouched();
    const uint8_t shorter[] = {0x05, 0xff, 0x3a, 0x00, 0x01, 0x02};
    TEST_ASSERT_EQUAL(lumix::Advert::OtherPanasonic, lumix::parseAdvert(shorter, sizeof(shorter), &camera));
    const uint8_t longer[] = {0x0a, 0xff, 0x3a, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    TEST_ASSERT_EQUAL(lumix::Advert::OtherPanasonic, lumix::parseAdvert(longer, sizeof(longer), &camera));
    const uint8_t idOnly[] = {0x03, 0xff, 0x3a, 0x00};
    TEST_ASSERT_EQUAL(lumix::Advert::OtherPanasonic, lumix::parseAdvert(idOnly, sizeof(idOnly), &camera));
    assertUntouched(camera);
}

void test_malformed_or_empty()
{
    lumix::Camera camera = untouched();
    // length 10 claimed, only 9 bytes follow
    const uint8_t pastEnd[] = {0x0a, 0xff, 0x3a, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a};
    TEST_ASSERT_EQUAL(lumix::Advert::None, lumix::parseAdvert(pastEnd, sizeof(pastEnd), &camera));
    // manufacturer data too short for a company identifier
    const uint8_t noCompany[] = {0x02, 0xff, 0x3a};
    TEST_ASSERT_EQUAL(lumix::Advert::None, lumix::parseAdvert(noCompany, sizeof(noCompany), &camera));
    // the camera hidden behind the early end of the data
    const uint8_t afterEnd[] = {0x00, 0x09, 0xff, 0x3a, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a};
    TEST_ASSERT_EQUAL(lumix::Advert::None, lumix::parseAdvert(afterEnd, sizeof(afterEnd), &camera));
    // a length byte with nothing after it
    const uint8_t lengthOnly[] = {0x02, 0x01, 0x06, 0x05};
    TEST_ASSERT_EQUAL(lumix::Advert::None, lumix::parseAdvert(lengthOnly, sizeof(lengthOnly), &camera));
    TEST_ASSERT_EQUAL(lumix::Advert::None, lumix::parseAdvert(pastEnd, 0, &camera));
    TEST_ASSERT_EQUAL(lumix::Advert::None, lumix::parseAdvert(nullptr, 5, &camera));
    assertUntouched(camera);
}

// ---- text ------------------------------------------------------------------------

void test_state_names()
{
    TEST_ASSERT_EQUAL_STRING("on", lumix::stateName(0x00));
    TEST_ASSERT_EQUAL_STRING("pairing", lumix::stateName(0x03));
    TEST_ASSERT_EQUAL_STRING("standby", lumix::stateName(0x06));
    TEST_ASSERT_EQUAL_STRING("unknown", lumix::stateName(0x01));
}

void test_format_serial()
{
    char text[11];
    TEST_ASSERT_EQUAL(10, lumix::formatSerial(SERIAL, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("123456789a", text);
    char small[10]; // no room for the terminator
    TEST_ASSERT_EQUAL(0, lumix::formatSerial(SERIAL, small, sizeof(small)));
}

// ---- introducing the card ---------------------------------------------------------

void test_word_swap()
{
    // docs/PROTOKOL.md: wordSwap(01 02 03 04 05) -> 0403020100000005
    const uint8_t in[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    const uint8_t expected[] = {0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x05};
    uint8_t out[8];
    TEST_ASSERT_EQUAL(8, lumix::wordSwap(in, sizeof(in), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof(expected));
    uint8_t small[7];
    TEST_ASSERT_EQUAL(0, lumix::wordSwap(in, sizeof(in), small, sizeof(small)));
    TEST_ASSERT_EQUAL(0, lumix::wordSwap(in, 0, out, sizeof(out)));
}

void test_apply_key()
{
    // docs/PROTOKOL.md: xor(00 00 00 00 0f, 01 02 03 04) -> 010203040e
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x0f};
    const uint8_t key[lumix::KEY_LENGTH] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t expected[] = {0x01, 0x02, 0x03, 0x04, 0x0e};
    lumix::applyKey(data, sizeof(data), key);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, data, sizeof(expected));
}

void test_device_id()
{
    // docs/PROTOKOL.md: UUID 00010203-0405-0607-0809-0a0b0c0d0e0f before XOR
    uint8_t uuid[lumix::UUID_LENGTH];
    for (uint8_t i = 0; i < lumix::UUID_LENGTH; i++)
        uuid[i] = i;
    const uint8_t expected[lumix::DEVICE_ID_LENGTH] = {0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04, 0x0b, 0x0a,
                                                       0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c, 0xff, 0xff, 0xff, 0xcd};
    uint8_t out[lumix::DEVICE_ID_LENGTH];
    lumix::deviceIdPlain(uuid, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof(expected));
}

void test_device_name()
{
    uint8_t out[lumix::DEVICE_NAME_MAX_LENGTH];
    // docs/PROTOKOL.md: "iPhone" -> 6850690600656e6fffffff00
    const uint8_t iphone[] = {0x68, 0x50, 0x69, 0x06, 0x00, 0x65, 0x6e, 0x6f, 0xff, 0xff, 0xff, 0x00};
    TEST_ASSERT_EQUAL(sizeof(iphone), lumix::deviceNamePlain("iPhone", out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(iphone, out, sizeof(iphone));
    // docs/PROTOKOL.md: "iPhone 17 Pro Max" -> 24 bytes
    const uint8_t longer[] = {0x68, 0x50, 0x69, 0x11, 0x20, 0x65, 0x6e, 0x6f, 0x50, 0x20, 0x37, 0x31,
                              0x4d, 0x20, 0x6f, 0x72, 0x00, 0x00, 0x78, 0x61, 0xff, 0xff, 0xff, 0x00};
    TEST_ASSERT_EQUAL(sizeof(longer), lumix::deviceNamePlain("iPhone 17 Pro Max", out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(longer, out, sizeof(longer));
    // the card's name
    const uint8_t card[] = {0x30, 0x31, 0x54, 0x07, 0x45, 0x2d, 0x30, 0x30, 0xff, 0xff, 0xff, 0x00};
    TEST_ASSERT_EQUAL(sizeof(card), lumix::deviceNamePlain("T1000-E", out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(card, out, sizeof(card));
    // 34 bytes of UTF-8: cut to 22, because byte 23 would split a character
    const uint8_t polish[] = {0xc5, 0x61, 0x5a, 0x16, 0xc5, 0xb3, 0xc3, 0xbc, 0x20, 0x87, 0xc4, 0x82, 0xc5, 0x99,
                              0xc4, 0x67, 0x85, 0xc4, 0x6c, 0x9b, 0x00, 0x61, 0x6a, 0x20, 0xff, 0xff, 0xff, 0x00};
    TEST_ASSERT_EQUAL(sizeof(polish), lumix::deviceNamePlain("Zażółć gęślą jaźń aparatu", out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(polish, out, sizeof(polish));
}

void test_app_name()
{
    uint8_t out[lumix::APP_NAME_LENGTH];
    uint8_t expected[lumix::APP_NAME_LENGTH] = {};
    memcpy(expected, "gpxcam T1000-E 0.0.1", 20);
    lumix::appName("gpxcam T1000-E 0.0.1", out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof(expected));
    // 31 ASCII bytes and a 2-byte letter that does not fit: cut before it
    memset(expected, 'a', 31);
    expected[31] = 0;
    lumix::appName("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\xc5\x82", out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof(expected));
}

void test_time_data()
{
    // docs/PROTOKOL.md: 2026-10-13 22:30:29 UTC, Europe/Warsaw in summer time
    const uint8_t summer[] = {0xea, 0x07, 0x0a, 0x0d, 0x16, 0x1e, 0x1d, 0x3c, 0x00, 0x01};
    uint8_t out[lumix::TIME_LENGTH];
    lumix::timeData(utc::DateTime{2026, 10, 13, 22, 30, 29}, 60, true, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(summer, out, sizeof(summer));
    // winter, and a negative offset (-300 min = 0xfed4) to check the sign
    const uint8_t winter[] = {0xea, 0x07, 0x0c, 0x18, 0x0c, 0x00, 0x00, 0xd4, 0xfe, 0x00};
    lumix::timeData(utc::DateTime{2026, 12, 24, 12, 0, 0}, -300, false, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(winter, out, sizeof(winter));
}

void test_location_data()
{
    uint8_t out[lumix::LOCATION_LENGTH];
    // docs/PROTOKOL.md: 2026-10-13 22:30:29 UTC, the made-up point at sea 54.6, 19.0, 0 m
    const uint8_t vector[] = {0x85, 0x73, 0xf9, 0x57, 0x80, 0x4c, 0x8b, 0x20,
                              0x80, 0x2b, 0x53, 0x0b, 0x00, 0x00, 0x41, 0x00};
    lumix::locationData(1791930629, 546000000, 190000000, 0, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vector, out, sizeof(vector));
    // the made-up S/W point, -12.4 m
    const uint8_t southWest[] = {0x85, 0x73, 0xf9, 0x57, 0xff, 0x0a, 0xd0, 0xeb,
                                 0x57, 0x44, 0xdf, 0xa5, 0xf4, 0xff, 0x41, 0x00};
    lumix::locationData(1791930629, -338687233, -1512094633, -124, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(southWest, out, sizeof(southWest));
    // altitude above the i16 range: limited to 32767 m
    const uint8_t high[] = {0x85, 0x73, 0xf9, 0x57, 0x80, 0x4c, 0x8b, 0x20,
                            0x80, 0x2b, 0x53, 0x0b, 0xff, 0x7f, 0x41, 0x00};
    lumix::locationData(1791930629, 546000000, 190000000, 400000, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(high, out, sizeof(high));
    // before the GPS epoch the time is 0; 545.4 m -> 545 (0x0221)
    const uint8_t early[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00, 0x21, 0x02, 0x41, 0x00};
    lumix::locationData(lumix::GPS_EPOCH_UNIX_SECONDS - 5, 0, 0, 5454, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(early, out, sizeof(early));
    // rounding half away from zero: 0.5 m -> 1, -12.5 m -> -13 (0xfff3)
    lumix::locationData(1791930629, 0, 0, 5, out);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[12]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[13]);
    lumix::locationData(1791930629, 0, 0, -125, out);
    TEST_ASSERT_EQUAL_HEX8(0xf3, out[12]);
    TEST_ASSERT_EQUAL_HEX8(0xff, out[13]);
}

void test_gatt_uuids()
{
    // the byte arrays against the text in docs/PROTOKOL.md (lower case)
    char text[pairing_record::UUID_TEXT_LENGTH + 1];
    pairing_record::formatUuid(lumix::SERVICE_UUID, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("1fe71b85-8344-4e6a-bf1a-ac7acdb3f8af", text);
    pairing_record::formatUuid(lumix::KEY_UUID, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("239c7203-922f-43e9-bdf1-6d1fedf6f43f", text);
    pairing_record::formatUuid(lumix::DEVICE_ID_UUID, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("bff12e7f-5944-4eb6-a90f-48d8786157ea", text);
    pairing_record::formatUuid(lumix::APP_NAME_UUID, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("ed8b1352-5aa7-4155-b7c9-521292dd1932", text);
    pairing_record::formatUuid(lumix::DEVICE_NAME_UUID, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("05a797b6-9c2f-4d18-99a7-fa61a3880bfe", text);
    pairing_record::formatUuid(lumix::TIME_UUID, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("39a02c41-c6e3-4b73-a3ea-537f23f0085a", text);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_camera_with_flags);
    RUN_TEST(test_camera_full_31_bytes_with_service_uuid);
    RUN_TEST(test_camera_in_scan_response_only);
    RUN_TEST(test_camera_after_other_manufacturer);
    RUN_TEST(test_camera_then_zero_padding);
    RUN_TEST(test_other_manufacturer);
    RUN_TEST(test_panasonic_in_another_layout);
    RUN_TEST(test_malformed_or_empty);
    RUN_TEST(test_state_names);
    RUN_TEST(test_format_serial);
    RUN_TEST(test_word_swap);
    RUN_TEST(test_apply_key);
    RUN_TEST(test_device_id);
    RUN_TEST(test_device_name);
    RUN_TEST(test_app_name);
    RUN_TEST(test_time_data);
    RUN_TEST(test_location_data);
    RUN_TEST(test_gatt_uuids);
    return UNITY_END();
}
