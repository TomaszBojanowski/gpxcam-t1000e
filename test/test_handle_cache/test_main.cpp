// Unit tests for src/handle_cache.cpp, run on the PC: pio test -e native
// Made-up camera serial numbers 12 34 56 78 9a / 9b and made-up handles.
#include <string.h>
#include <unity.h>

#include "handle_cache.h"

using handle_cache::Handles;
using handle_cache::Lookup;

namespace {

const uint8_t CAMERA_A[lumix::SERIAL_LENGTH] = {0x12, 0x34, 0x56, 0x78, 0x9a};
const uint8_t CAMERA_B[lumix::SERIAL_LENGTH] = {0x12, 0x34, 0x56, 0x78, 0x9b};

Handles someHandles()
{
    Handles handles = {};
    handles.serviceEnd = 0x0090;
    for (size_t i = 0; i < handle_cache::CHARACTERISTIC_COUNT; i++) {
        handles.valueHandle[i] = (uint16_t)(0x0042 + 3 * i);
        handles.properties[i] = (uint8_t)(i == 0 ? 0x01 : 0x02);
    }
    return handles;
}

} // namespace

void setUp()
{
    handle_cache::forget();
}

void tearDown() {}

// After a restart nothing is kept: the first session looks the handles up.
void test_empty_after_start()
{
    TEST_ASSERT_FALSE(handle_cache::forget());
    Handles found = {};
    TEST_ASSERT_TRUE(handle_cache::find(CAMERA_A, &found) == Lookup::Empty);
}

// The handles of camera A are not used for camera B, and are forgotten.
void test_other_camera_forgets()
{
    handle_cache::keep(CAMERA_A, someHandles());
    Handles found = {};
    TEST_ASSERT_TRUE(handle_cache::find(CAMERA_B, &found) == Lookup::OtherCameraForgotten);
    TEST_ASSERT_TRUE(handle_cache::find(CAMERA_A, &found) == Lookup::Empty);
}

// An error in the introduction once connected forgets the handles.
void test_error_in_introduction_forgets()
{
    handle_cache::keep(CAMERA_A, someHandles());
    TEST_ASSERT_TRUE(handle_cache::forgetAfterFailure(true, false));
    TEST_ASSERT_TRUE(handle_cache::forget());
    Handles found = {};
    TEST_ASSERT_TRUE(handle_cache::find(CAMERA_A, &found) == Lookup::Empty);
}

// No connection (HCI 0x3e, or no answer to connecting): the handles stay and are found
// unchanged for the same camera.
void test_no_connection_keeps()
{
    const Handles handles = someHandles();
    handle_cache::keep(CAMERA_A, handles);
    TEST_ASSERT_FALSE(handle_cache::forgetAfterFailure(true, true));
    TEST_ASSERT_FALSE(handle_cache::forgetAfterFailure(false, false));
    Handles found = {};
    TEST_ASSERT_TRUE(handle_cache::find(CAMERA_A, &found) == Lookup::Found);
    TEST_ASSERT_EQUAL_HEX16(handles.serviceEnd, found.serviceEnd);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(handles.valueHandle, found.valueHandle, handle_cache::CHARACTERISTIC_COUNT);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(handles.properties, found.properties, handle_cache::CHARACTERISTIC_COUNT);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_after_start);
    RUN_TEST(test_other_camera_forgets);
    RUN_TEST(test_error_in_introduction_forgets);
    RUN_TEST(test_no_connection_keeps);
    return UNITY_END();
}
