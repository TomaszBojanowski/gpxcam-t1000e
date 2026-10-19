// Unit tests for src/lumix.cpp, run on the PC: pio test -e native
// All camera values are made up: serial number 12 34 56 78 9a. The service UUID is the one
// the camera advertises (docs/PROTOKOL.md), the same for every camera.
#include <string.h>
#include <unity.h>

#include "lumix.h"

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
    return UNITY_END();
}
