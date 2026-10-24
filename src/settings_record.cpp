#include "settings_record.h"

#include <string.h>

#include "bytes.h"
#include "pairing_record.h" // crc32(): the same checksum as the other files

namespace settings_record {

namespace {

constexpr uint8_t MAGIC[] = {'G', 'X', 'S', 'T'};
constexpr uint8_t VERSION = 1;

constexpr size_t VERSION_OFFSET = 4;
constexpr size_t SOUND_OFFSET = 5;
constexpr size_t UPDATES_OFFSET = 6;
constexpr size_t CRC_OFFSET = 12;
static_assert(CRC_OFFSET + 4 == SIZE, "record layout");

bool isSound(uint8_t value)
{
    return value == (uint8_t)sound_rules::Setting::Loud || value == (uint8_t)sound_rules::Setting::Quiet ||
           value == (uint8_t)sound_rules::Setting::Off;
}

// Only frequent is read as frequent: 0 and values this firmware does not know are normal.
link_policy::Updates updatesFrom(uint8_t value)
{
    return value == (uint8_t)link_policy::Updates::Frequent ? link_policy::Updates::Frequent
                                                             : link_policy::Updates::Normal;
}

} // namespace

void encode(const Record &record, uint8_t out[SIZE])
{
    memset(out, 0, SIZE);
    memcpy(out, MAGIC, sizeof(MAGIC));
    out[VERSION_OFFSET] = VERSION;
    out[SOUND_OFFSET] = (uint8_t)record.sound;
    out[UPDATES_OFFSET] = (uint8_t)record.updates;
    bytes::putU32(&out[CRC_OFFSET], pairing_record::crc32(out, CRC_OFFSET));
}

Status decode(const uint8_t *data, size_t length, Record *record)
{
    if (data == nullptr || length != SIZE)
        return Status::WrongSize;
    if (memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
        return Status::WrongMagic;
    if (data[VERSION_OFFSET] != VERSION)
        return Status::WrongVersion;
    if (bytes::getU32(&data[CRC_OFFSET]) != pairing_record::crc32(data, CRC_OFFSET))
        return Status::BadChecksum;
    if (!isSound(data[SOUND_OFFSET]))
        return Status::BadSound;
    record->sound = (sound_rules::Setting)data[SOUND_OFFSET];
    record->updates = updatesFrom(data[UPDATES_OFFSET]);
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
        return "not a settings file";
    case Status::WrongVersion:
        return "unknown format version";
    case Status::BadChecksum:
        return "checksum error";
    case Status::BadSound:
        return "unknown sound setting";
    }
    return "?";
}

} // namespace settings_record
