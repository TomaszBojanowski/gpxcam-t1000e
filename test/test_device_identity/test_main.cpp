// Unit tests for src/device_identity.cpp, run on the PC: pio test -e native
// The made-up values and the expected bytes are in test/device_identity_vectors.txt, shared with the
// app's tests; they were computed with a separate Python implementation of docs/PROTOCOL.md.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "device_identity.h"

void setUp() {}
void tearDown() {}

namespace {

// 'pio test' runs the tests in the project folder.
constexpr const char *VECTORS = "test/device_identity_vectors.txt";
constexpr size_t LINE_LENGTH = 128;

// The value of "name = value" in the vectors file; false if the file or the name is missing.
bool vector(const char *name, char *value, size_t size)
{
    FILE *file = fopen(VECTORS, "r");
    if (file == nullptr)
        return false;
    char line[LINE_LENGTH];
    const size_t nameLength = strlen(name);
    bool found = false;
    while (!found && fgets(line, sizeof(line), file) != nullptr) {
        if (strncmp(line, name, nameLength) != 0 || strncmp(&line[nameLength], " = ", 3) != 0)
            continue;
        const char *start = &line[nameLength + 3];
        const size_t length = strcspn(start, "\r\n");
        found = length < size;
        if (found) {
            memcpy(value, start, length);
            value[length] = '\0';
        }
    }
    fclose(file);
    return found;
}

uint8_t hexDigit(char c)
{
    return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10);
}

// A vector given in hex as exactly 'length' bytes; fails the test otherwise.
void hexVector(const char *name, uint8_t *bytes, size_t length)
{
    char text[LINE_LENGTH];
    TEST_ASSERT_TRUE_MESSAGE(vector(name, text, sizeof(text)), name);
    TEST_ASSERT_EQUAL_MESSAGE(2 * length, strlen(text), name);
    for (size_t i = 0; i < length; i++)
        bytes[i] = (uint8_t)(hexDigit(text[2 * i]) << 4 | hexDigit(text[2 * i + 1]));
}

} // namespace

void test_seed_is_the_end_of_the_card_uuid()
{
    uint8_t uuid[lumix::UUID_LENGTH], expected[device_identity::SEED_LENGTH], seed[device_identity::SEED_LENGTH];
    hexVector("card_uuid", uuid, sizeof(uuid));
    hexVector("seed", expected, sizeof(expected));
    device_identity::cardSeed(uuid, seed);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, seed, sizeof(seed));
}

void test_identity_bytes()
{
    uint8_t seed[device_identity::SEED_LENGTH], expected[device_identity::IDENTITY_LENGTH];
    uint8_t identity[device_identity::IDENTITY_LENGTH];
    hexVector("seed", seed, sizeof(seed));
    hexVector("identity", expected, sizeof(expected));
    device_identity::identityBytes(seed, identity);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, identity, sizeof(identity));
}

void test_identity_text()
{
    uint8_t seed[device_identity::SEED_LENGTH];
    char expected[LINE_LENGTH], text[device_identity::TEXT_LENGTH + 1];
    hexVector("seed", seed, sizeof(seed));
    TEST_ASSERT_TRUE(vector("identity_text", expected, sizeof(expected)));
    TEST_ASSERT_EQUAL(device_identity::TEXT_LENGTH, device_identity::identityText(seed, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING(expected, text);
    char small[device_identity::TEXT_LENGTH];
    TEST_ASSERT_EQUAL(0, device_identity::identityText(seed, small, sizeof(small)));
}

void test_identity_field()
{
    uint8_t identity[device_identity::IDENTITY_LENGTH], key[lumix::KEY_LENGTH];
    uint8_t expected[device_identity::FIELD_LENGTH], field[device_identity::FIELD_LENGTH];
    hexVector("identity", identity, sizeof(identity));
    hexVector("key", key, sizeof(key));
    hexVector("identity_field", expected, sizeof(expected));
    device_identity::identityField(identity, key, field);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, field, sizeof(field));
}

void test_name_field()
{
    uint8_t key[lumix::KEY_LENGTH], expected[device_identity::FIELD_LENGTH], field[device_identity::FIELD_LENGTH];
    char name[LINE_LENGTH];
    hexVector("key", key, sizeof(key));
    hexVector("name_field", expected, sizeof(expected));
    TEST_ASSERT_TRUE(vector("name", name, sizeof(name)));
    TEST_ASSERT_TRUE(device_identity::nameField(name, key, field));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, field, sizeof(field));
}

void test_name_field_takes_only_short_printable_ascii()
{
    const uint8_t key[lumix::KEY_LENGTH] = {0x11, 0x22, 0x33, 0x44};
    uint8_t field[device_identity::FIELD_LENGTH];
    TEST_ASSERT_TRUE(device_identity::nameField("abcdefghijklmnop", key, field)); // 16 characters
    memset(field, 0xAA, sizeof(field));
    TEST_ASSERT_FALSE(device_identity::nameField("abcdefghijklmnopq", key, field)); // 17
    TEST_ASSERT_FALSE(device_identity::nameField("", key, field));
    TEST_ASSERT_FALSE(device_identity::nameField(nullptr, key, field));
    TEST_ASSERT_FALSE(device_identity::nameField("Zażółć", key, field));
    TEST_ASSERT_FALSE(device_identity::nameField("T1000\tE", key, field));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xAA, field, sizeof(field)); // not written when refused
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_seed_is_the_end_of_the_card_uuid);
    RUN_TEST(test_identity_bytes);
    RUN_TEST(test_identity_text);
    RUN_TEST(test_identity_field);
    RUN_TEST(test_name_field);
    RUN_TEST(test_name_field_takes_only_short_printable_ascii);
    return UNITY_END();
}
