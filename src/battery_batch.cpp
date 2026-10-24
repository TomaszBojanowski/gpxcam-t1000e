#include "battery_batch.h"

namespace battery_batch {

bool Batch::add(const battery_rules::Entry &entry, uint32_t entriesInFile)
{
    if (count >= BATCH_ENTRIES || entriesInFile + count >= MAX_ENTRIES)
        return false;
    battery_rules::encode(entry, &data[count * battery_rules::ENTRY_SIZE]);
    count++;
    return true;
}

bool Batch::due(uint32_t entriesInFile) const
{
    return count >= BATCH_ENTRIES || (count > 0 && entriesInFile + count >= MAX_ENTRIES);
}

battery_rules::Entry Batch::entry(size_t index) const
{
    return battery_rules::decode(&data[index * battery_rules::ENTRY_SIZE]);
}

bool StableUsb::update(bool usbNow, uint32_t nowMs)
{
    if (!usbNow) {
        present = false;
        return false;
    }
    if (!present) {
        present = true;
        sinceMs = nowMs;
    }
    return nowMs - sinceMs >= USB_STABLE_MS;
}

OldFile oldFile(bool oldExists, bool newExists)
{
    if (!oldExists)
        return OldFile::None;
    return newExists ? OldFile::KeepBoth : OldFile::Move;
}

} // namespace battery_batch
