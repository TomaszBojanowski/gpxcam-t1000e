// Unit tests for src/settings_record.cpp, run on the PC: pio test -e native
// The expected bytes were computed with Python's zlib.crc32.
#include <string.h>
#include <unity.h>

#include "settings_record.h"

using link_policy::Updates;
using settings_record::Record;
using settings_record::Status;
using sound_rules::Setting;

namespace {

// Loud and normal (the defaults) and off, as firmware 1.3 also writes them (byte 6 reserved, 0);
// quiet with made-up values in the reserved bytes.
constexpr uint8_t LOUD[] = {0x47, 0x58, 0x53, 0x54, 0x01, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0xde, 0x89, 0x64, 0x4c};
constexpr uint8_t OFF[] = {0x47, 0x58, 0x53, 0x54, 0x01, 0x02, 0x00, 0x00,
                           0x00, 0x00, 0x00, 0x00, 0xf7, 0x98, 0xfb, 0xdb};
constexpr uint8_t QUIET_RESERVED_SET[] = {0x47, 0x58, 0x53, 0x54, 0x01, 0x01, 0x00, 0x07,
                                          0x00, 0x00, 0x00, 0x09, 0xde, 0xe6, 0xef, 0x21};
constexpr uint8_t SOUND_3[] = {0x47, 0x58, 0x53, 0x54, 0x01, 0x03, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x43, 0x93, 0x8c, 0x7d};
// Quiet and frequent; loud with updates 2, a value this firmware does not know.
constexpr uint8_t QUIET_FREQUENT[] = {0x47, 0x58, 0x53, 0x54, 0x01, 0x01, 0x01, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0xcf, 0x51, 0x4f, 0x21};
constexpr uint8_t LOUD_UPDATES_2[] = {0x47, 0x58, 0x53, 0x54, 0x01, 0x00, 0x02, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0xd5, 0x28, 0xac, 0x01};

} // namespace

void setUp() {}

void tearDown() {}

void test_encode_gives_the_known_bytes()
{
    uint8_t out[settings_record::SIZE];
    settings_record::encode(settings_record::DEFAULTS, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LOUD, out, sizeof(LOUD));
    settings_record::encode(Record{Setting::Off, Updates::Normal}, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(OFF, out, sizeof(OFF));
    settings_record::encode(Record{Setting::Quiet, Updates::Frequent}, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(QUIET_FREQUENT, out, sizeof(QUIET_FREQUENT));
}

void test_every_setting_round_trips()
{
    const Setting SETTINGS[] = {Setting::Loud, Setting::Quiet, Setting::Off};
    const Updates UPDATES[] = {Updates::Normal, Updates::Frequent};
    for (Setting setting : SETTINGS) {
        for (Updates updates : UPDATES) {
            uint8_t out[settings_record::SIZE];
            settings_record::encode(Record{setting, updates}, out);
            Record read = {Setting::Loud, updates == Updates::Normal ? Updates::Frequent : Updates::Normal};
            TEST_ASSERT_EQUAL(Status::Ok, settings_record::decode(out, sizeof(out), &read));
            TEST_ASSERT_EQUAL(setting, read.sound);
            TEST_ASSERT_EQUAL(updates, read.updates);
        }
    }
}

// A file of firmware 1.3 (byte 6 reserved, 0) gives normal updates and keeps its sound.
void test_a_file_of_firmware_1_3_gives_normal_updates()
{
    Record read = {Setting::Loud, Updates::Frequent};
    TEST_ASSERT_EQUAL(Status::Ok, settings_record::decode(OFF, sizeof(OFF), &read));
    TEST_ASSERT_EQUAL(Setting::Off, read.sound);
    TEST_ASSERT_EQUAL(Updates::Normal, read.updates);
}

// A value of a later firmware reads as normal; the rest of the file still applies.
void test_an_unknown_updates_value_reads_as_normal()
{
    Record read = {Setting::Quiet, Updates::Frequent};
    TEST_ASSERT_EQUAL(Status::Ok, settings_record::decode(LOUD_UPDATES_2, sizeof(LOUD_UPDATES_2), &read));
    TEST_ASSERT_EQUAL(Setting::Loud, read.sound);
    TEST_ASSERT_EQUAL(Updates::Normal, read.updates);
}

// A later firmware may use the reserved bytes; this one reads the file all the same.
void test_reserved_bytes_are_not_checked()
{
    Record read = {Setting::Loud, Updates::Frequent};
    TEST_ASSERT_EQUAL(Status::Ok, settings_record::decode(QUIET_RESERVED_SET, sizeof(QUIET_RESERVED_SET), &read));
    TEST_ASSERT_EQUAL(Setting::Quiet, read.sound);
    TEST_ASSERT_EQUAL(Updates::Normal, read.updates);
}

void test_damaged_records_are_refused_and_change_nothing()
{
    Record read = {Setting::Quiet, Updates::Frequent};
    TEST_ASSERT_EQUAL(Status::WrongSize, settings_record::decode(nullptr, settings_record::SIZE, &read));
    TEST_ASSERT_EQUAL(Status::WrongSize, settings_record::decode(OFF, sizeof(OFF) - 1, &read));
    TEST_ASSERT_EQUAL(Status::WrongSize, settings_record::decode(OFF, 0, &read));
    uint8_t changed[settings_record::SIZE];
    memcpy(changed, OFF, sizeof(changed));
    changed[3] = 'Z';
    TEST_ASSERT_EQUAL(Status::WrongMagic, settings_record::decode(changed, sizeof(changed), &read));
    memcpy(changed, OFF, sizeof(changed));
    changed[4] = 2;
    TEST_ASSERT_EQUAL(Status::WrongVersion, settings_record::decode(changed, sizeof(changed), &read));
    memcpy(changed, OFF, sizeof(changed));
    changed[5] = 0; // loud, with the checksum of off
    TEST_ASSERT_EQUAL(Status::BadChecksum, settings_record::decode(changed, sizeof(changed), &read));
    TEST_ASSERT_EQUAL(Status::BadSound, settings_record::decode(SOUND_3, sizeof(SOUND_3), &read));
    TEST_ASSERT_EQUAL(Setting::Quiet, read.sound); // unchanged
    TEST_ASSERT_EQUAL(Updates::Frequent, read.updates);
    TEST_ASSERT_EQUAL_STRING("unknown sound setting", settings_record::statusText(Status::BadSound));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_gives_the_known_bytes);
    RUN_TEST(test_every_setting_round_trips);
    RUN_TEST(test_a_file_of_firmware_1_3_gives_normal_updates);
    RUN_TEST(test_an_unknown_updates_value_reads_as_normal);
    RUN_TEST(test_reserved_bytes_are_not_checked);
    RUN_TEST(test_damaged_records_are_refused_and_change_nothing);
    return UNITY_END();
}
