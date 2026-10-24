#include "pairing_copy_record.h"

#include <string.h>

namespace pairing_copy_record {

namespace {

constexpr uint8_t MAGIC[4] = {'G', 'X', 'P', 'C'};
constexpr uint8_t VERSION = 1;
constexpr size_t VERSION_AT = 4;
constexpr size_t RESERVED_AT = 5;
constexpr size_t RESERVED_SIZE = 3;
constexpr size_t SEQUENCE_AT = 8;
constexpr size_t RECORD_AT = 12;
constexpr size_t CRC_AT = RECORD_AT + pairing_record::SIZE;
static_assert(CRC_AT + 4 == SIZE, "layout");
constexpr uint8_t ERASED = 0xFF;

void putU32(uint8_t *out, uint32_t value)
{
    for (int i = 0; i < 4; i++)
        out[i] = (uint8_t)(value >> (8 * i));
}

uint32_t getU32(const uint8_t *in)
{
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

bool allErased(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (data[i] != ERASED)
            return false;
    }
    return true;
}

} // namespace

void encode(const pairing_record::Record &record, uint32_t sequence, uint8_t out[SIZE])
{
    memcpy(out, MAGIC, sizeof(MAGIC));
    out[VERSION_AT] = VERSION;
    memset(&out[RESERVED_AT], 0, RESERVED_SIZE);
    putU32(&out[SEQUENCE_AT], sequence);
    pairing_record::encode(record, &out[RECORD_AT]);
    putU32(&out[CRC_AT], pairing_record::crc32(out, CRC_AT));
}

Page classify(const uint8_t *page, size_t size)
{
    Page result = {};
    if (allErased(page, size)) {
        result.kind = Kind::Empty;
        return result;
    }
    if (size < SIZE || memcmp(page, MAGIC, sizeof(MAGIC)) != 0) {
        result.kind = Kind::Foreign;
        return result;
    }
    result.kind = Kind::Broken;
    if (page[VERSION_AT] != VERSION || getU32(&page[CRC_AT]) != pairing_record::crc32(page, CRC_AT) ||
        !allErased(&page[SIZE], size - SIZE))
        return result;
    pairing_record::Record record;
    if (pairing_record::decode(&page[RECORD_AT], pairing_record::SIZE, &record) != pairing_record::Status::Ok)
        return result;
    result.kind = Kind::Copy;
    result.sequence = getU32(&page[SEQUENCE_AT]);
    result.record = record;
    return result;
}

bool usable(const Page &page)
{
    return page.kind != Kind::Foreign;
}

int newest(const Page pages[PAGES])
{
    int found = -1;
    for (size_t i = 0; i < PAGES; i++) {
        if (pages[i].kind == Kind::Copy && (found < 0 || pages[i].sequence > pages[found].sequence))
            found = (int)i;
    }
    return found;
}

int writeTarget(const Page pages[PAGES])
{
    const int keep = newest(pages);
    int onlyUsable = -1;
    for (size_t i = 0; i < PAGES; i++) {
        if (!usable(pages[i]))
            continue;
        if ((int)i != keep)
            return (int)i;
        onlyUsable = (int)i;
    }
    return onlyUsable;
}

uint32_t nextSequence(const Page pages[PAGES])
{
    const int last = newest(pages);
    return last < 0 ? 1 : pages[last].sequence + 1;
}

const char *kindText(Kind kind)
{
    switch (kind) {
    case Kind::Empty:
        return "empty";
    case Kind::Copy:
        return "copy";
    case Kind::Broken:
        return "broken copy";
    case Kind::Foreign:
        return "other data";
    }
    return "?";
}

} // namespace pairing_copy_record
