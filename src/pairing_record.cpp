#include "pairing_record.h"

#include <string.h>

namespace pairing_record {

namespace {

constexpr uint8_t MAGIC[] = {'G', 'X', 'P', 'R'};
constexpr uint8_t VERSION = 1;

constexpr size_t VERSION_OFFSET = 4;
constexpr size_t PAIRED_OFFSET = 5;
constexpr size_t UUID_OFFSET = 6;
constexpr size_t SERIAL_OFFSET = UUID_OFFSET + lumix::UUID_LENGTH;      // 22
constexpr size_t DIALECT_OFFSET = SERIAL_OFFSET + lumix::SERIAL_LENGTH; // 27
constexpr size_t CRC_OFFSET = DIALECT_OFFSET + 1;                       // 28
static_assert(CRC_OFFSET + 4 == SIZE, "record layout");

constexpr uint32_t CRC_POLYNOMIAL = 0xEDB88320; // IEEE 802.3, reflected

// RFC 9562, 4.1 and 4.2: version in the high 4 bits of byte 6, variant 10 in byte 8.
constexpr size_t UUID_VERSION_BYTE = 6;
constexpr uint8_t UUID_VERSION_4 = 0x40;
constexpr size_t UUID_VARIANT_BYTE = 8;
constexpr uint8_t UUID_VARIANT_RFC = 0x80;

void putU32(uint8_t *out, uint32_t value)
{
    for (size_t i = 0; i < 4; i++)
        out[i] = (uint8_t)(value >> (8 * i));
}

uint32_t getU32(const uint8_t *in)
{
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

} // namespace

void encode(const Record &record, uint8_t out[SIZE])
{
    memset(out, 0, SIZE);
    memcpy(out, MAGIC, sizeof(MAGIC));
    out[VERSION_OFFSET] = VERSION;
    out[PAIRED_OFFSET] = record.paired ? 1 : 0;
    memcpy(&out[UUID_OFFSET], record.uuid, lumix::UUID_LENGTH);
    if (record.paired)
        memcpy(&out[SERIAL_OFFSET], record.serial, lumix::SERIAL_LENGTH);
    out[DIALECT_OFFSET] = record.dialect;
    putU32(&out[CRC_OFFSET], crc32(out, CRC_OFFSET));
}

Status decode(const uint8_t *data, size_t length, Record *record)
{
    if (data == nullptr || length != SIZE)
        return Status::WrongSize;
    if (memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
        return Status::WrongMagic;
    if (data[VERSION_OFFSET] != VERSION)
        return Status::WrongVersion;
    if (getU32(&data[CRC_OFFSET]) != crc32(data, CRC_OFFSET))
        return Status::BadChecksum;
    if (data[PAIRED_OFFSET] > 1)
        return Status::BadFlag;
    record->paired = data[PAIRED_OFFSET] == 1;
    memcpy(record->uuid, &data[UUID_OFFSET], lumix::UUID_LENGTH);
    memcpy(record->serial, &data[SERIAL_OFFSET], lumix::SERIAL_LENGTH);
    record->dialect = data[DIALECT_OFFSET];
    return Status::Ok;
}

bool isOlderDialect(const Record &record)
{
    return record.dialect == DIALECT_OLDER;
}

const char *statusText(Status status)
{
    switch (status) {
    case Status::Ok:
        return "ok";
    case Status::WrongSize:
        return "wrong size";
    case Status::WrongMagic:
        return "not a pairing record";
    case Status::WrongVersion:
        return "unknown format version";
    case Status::BadChecksum:
        return "bad checksum";
    case Status::BadFlag:
        return "bad paired flag";
    }
    return "?";
}

uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 1) ? (crc >> 1) ^ CRC_POLYNOMIAL : crc >> 1;
    }
    return ~crc;
}

void makeRandomUuid(uint8_t uuid[lumix::UUID_LENGTH])
{
    uuid[UUID_VERSION_BYTE] = (uint8_t)((uuid[UUID_VERSION_BYTE] & 0x0F) | UUID_VERSION_4);
    uuid[UUID_VARIANT_BYTE] = (uint8_t)((uuid[UUID_VARIANT_BYTE] & 0x3F) | UUID_VARIANT_RFC);
}

size_t formatUuid(const uint8_t uuid[lumix::UUID_LENGTH], char *out, size_t size)
{
    static const char HEX_DIGITS[] = "0123456789abcdef";
    if (out == nullptr || size < UUID_TEXT_LENGTH + 1)
        return 0;
    char *p = out;
    for (size_t i = 0; i < lumix::UUID_LENGTH; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) // 8-4-4-4-12 hex digits
            *p++ = '-';
        *p++ = HEX_DIGITS[uuid[i] >> 4];
        *p++ = HEX_DIGITS[uuid[i] & 0x0F];
    }
    *p = '\0';
    return UUID_TEXT_LENGTH;
}

} // namespace pairing_record
