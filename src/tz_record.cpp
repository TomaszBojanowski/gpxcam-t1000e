#include "tz_record.h"

#include <string.h>

#include "pairing_record.h" // crc32(): the same checksum as the pairing file
#include "posix_tz.h"
#include "tz_table.h"

namespace tz_record {

namespace {

constexpr uint8_t MAGIC[] = {'G', 'X', 'T', 'Z'};
constexpr uint8_t VERSION = 1;

constexpr size_t VERSION_OFFSET = 4;
constexpr size_t CLOCK_OFFSET = 5;
constexpr uint8_t CLOCK_ON = 0;
constexpr uint8_t CLOCK_OFF = 1;
constexpr size_t NAME_OFFSET = 8;
constexpr size_t RULE_OFFSET = NAME_OFFSET + NAME_SIZE; // 56
constexpr size_t CRC_OFFSET = RULE_OFFSET + RULE_SIZE;  // 120
static_assert(CRC_OFFSET + 4 == SIZE, "record layout");

void putU32(uint8_t *out, uint32_t value)
{
    for (size_t i = 0; i < 4; i++)
        out[i] = (uint8_t)(value >> (8 * i));
}

uint32_t getU32(const uint8_t *in)
{
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

// False, with *record unchanged, if a text does not fit in its field.
bool make(const char *name, const char *rule, Record *record)
{
    const size_t nameLength = strlen(name);
    const size_t ruleLength = strlen(rule);
    if (nameLength >= NAME_SIZE || ruleLength >= RULE_SIZE)
        return false;
    Record made = {};
    memcpy(made.name, name, nameLength);
    memcpy(made.rule, rule, ruleLength);
    *record = made;
    return true;
}

} // namespace

bool fromInput(const char *text, Record *record)
{
    const char *name = nullptr;
    const char *rule = nullptr;
    if (tz_table::find(text, &name, &rule))
        return make(name, rule, record);
    posix_tz::Rule parsed;
    return posix_tz::parse(text, &parsed) && make("", text, record);
}

Record defaultRecord()
{
    Record record = {};
    fromInput(DEFAULT_NAME, &record); // in the table (tested)
    return record;
}

bool updateFromTable(Record *record)
{
    const char *name = nullptr;
    const char *rule = nullptr;
    if (record->name[0] == '\0' || !tz_table::find(record->name, &name, &rule) || strcmp(rule, record->rule) == 0)
        return false;
    const bool clockOff = record->clockOff;
    if (!make(name, rule, record))
        return false;
    record->clockOff = clockOff;
    return true;
}

void encode(const Record &record, uint8_t out[SIZE])
{
    memset(out, 0, SIZE);
    memcpy(out, MAGIC, sizeof(MAGIC));
    out[VERSION_OFFSET] = VERSION;
    out[CLOCK_OFFSET] = record.clockOff ? CLOCK_OFF : CLOCK_ON;
    memcpy(&out[NAME_OFFSET], record.name, strnlen(record.name, NAME_SIZE - 1));
    memcpy(&out[RULE_OFFSET], record.rule, strnlen(record.rule, RULE_SIZE - 1));
    putU32(&out[CRC_OFFSET], pairing_record::crc32(out, CRC_OFFSET));
}

Status decode(const uint8_t *data, size_t length, Record *record)
{
    if (data == nullptr || length != SIZE)
        return Status::WrongSize;
    if (memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
        return Status::WrongMagic;
    if (data[VERSION_OFFSET] != VERSION)
        return Status::WrongVersion;
    if (getU32(&data[CRC_OFFSET]) != pairing_record::crc32(data, CRC_OFFSET))
        return Status::BadChecksum;
    const char *name = (const char *)&data[NAME_OFFSET];
    const char *rule = (const char *)&data[RULE_OFFSET];
    if (memchr(name, '\0', NAME_SIZE) == nullptr || memchr(rule, '\0', RULE_SIZE) == nullptr || rule[0] == '\0')
        return Status::BadText;
    if (data[CLOCK_OFFSET] != CLOCK_ON && data[CLOCK_OFFSET] != CLOCK_OFF)
        return Status::BadClockFlag;
    Record decoded;
    if (!make(name, rule, &decoded))
        return Status::BadText;
    decoded.clockOff = data[CLOCK_OFFSET] == CLOCK_OFF;
    *record = decoded;
    return Status::Ok;
}

const char *statusText(Status status)
{
    switch (status) {
    case Status::Ok:
        return "ok";
    case Status::WrongSize:
        return "wrong size";
    case Status::WrongMagic:
        return "not a time zone record";
    case Status::WrongVersion:
        return "unknown format version";
    case Status::BadChecksum:
        return "bad checksum";
    case Status::BadText:
        return "bad text";
    case Status::BadClockFlag:
        return "bad clock flag";
    }
    return "?";
}

} // namespace tz_record
