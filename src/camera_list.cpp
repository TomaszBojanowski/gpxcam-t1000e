#include "camera_list.h"

#include <string.h>

namespace camera_list {

namespace {

bool sameSerial(const lumix::Camera &a, const lumix::Camera &b)
{
    return memcmp(a.serial, b.serial, lumix::SERIAL_LENGTH) == 0;
}

bool sameStateAndAddress(const Report &a, const Report &b)
{
    return a.camera.state == b.camera.state && a.addressType == b.addressType &&
           memcmp(a.address, b.address, ADDRESS_LENGTH) == 0;
}

constexpr uint32_t MS_PER_SECOND = 1000;

void countRecent(Entry &entry, uint32_t nowMs)
{
    const uint32_t second = nowMs / MS_PER_SECOND;
    const size_t slot = second % RECENT_SECONDS;
    if (entry.recentSecond[slot] != second) {
        entry.recentSecond[slot] = second;
        entry.recentCount[slot] = 0;
    }
    if (entry.recentCount[slot] < UINT16_MAX)
        entry.recentCount[slot]++;
}

void store(Entry &entry, const Report &report, uint32_t nowMs)
{
    entry.last = report;
    entry.lastSeenMs = nowMs;
    if (report.kind == advert_kind::Kind::ScanResponse) {
        entry.scanResponseCount++;
    } else {
        entry.advertCount++;
        if (report.kind == advert_kind::Kind::Connectable)
            entry.connectableCount++;
    }
    countRecent(entry, nowMs);
}

} // namespace

Change CameraList::add(const Report &report, uint32_t nowMs)
{
    Entry *freeSlot = nullptr;
    for (Entry &entry : entries_) {
        if (!entry.used) {
            if (freeSlot == nullptr)
                freeSlot = &entry;
            continue;
        }
        if (!sameSerial(entry.last.camera, report.camera))
            continue;
        const bool changed = !sameStateAndAddress(entry.last, report);
        store(entry, report, nowMs);
        return changed ? Change::Changed : Change::None;
    }
    if (freeSlot == nullptr)
        return Change::TableFull;
    *freeSlot = Entry{};
    freeSlot->used = true;
    store(*freeSlot, report, nowMs);
    return Change::New;
}

bool CameraList::removeLost(uint32_t nowMs, Entry *lost)
{
    for (Entry &entry : entries_) {
        if (entry.used && nowMs - entry.lastSeenMs >= LOST_AFTER_MS) {
            *lost = entry;
            entry.used = false;
            return true;
        }
    }
    return false;
}

const Entry *CameraList::at(size_t i) const
{
    return (i < MAX_CAMERAS && entries_[i].used) ? &entries_[i] : nullptr;
}

void CameraList::clear()
{
    for (Entry &entry : entries_)
        entry.used = false;
}

uint32_t recentReports(const Entry &entry, uint32_t nowMs)
{
    const uint32_t second = nowMs / MS_PER_SECOND;
    uint32_t count = 0;
    for (size_t i = 0; i < RECENT_SECONDS; i++) {
        if (second - entry.recentSecond[i] < RECENT_SECONDS)
            count += entry.recentCount[i];
    }
    return count;
}

const char *addressTypeName(uint8_t type)
{
    switch (type) {
    case ADDRESS_PUBLIC:
        return "public";
    case ADDRESS_RANDOM_STATIC:
        return "random static";
    case ADDRESS_RANDOM_RESOLVABLE:
        return "RPA";
    case ADDRESS_RANDOM_NON_RESOLVABLE:
        return "random";
    case ADDRESS_ANONYMOUS:
        return "anonymous";
    default:
        return "unknown";
    }
}

size_t formatAddress(const uint8_t address[ADDRESS_LENGTH], char *out, size_t size)
{
    static const char HEX_DIGITS[] = "0123456789abcdef";
    constexpr size_t TEXT_LENGTH = 3 * ADDRESS_LENGTH - 1; // "aa:" x 5 + "ff"
    if (out == nullptr || size < TEXT_LENGTH + 1)
        return 0;
    char *p = out;
    for (size_t i = 0; i < ADDRESS_LENGTH; i++) {
        const uint8_t byte = address[ADDRESS_LENGTH - 1 - i];
        if (i > 0)
            *p++ = ':';
        *p++ = HEX_DIGITS[byte >> 4];
        *p++ = HEX_DIGITS[byte & 0x0F];
    }
    *p = '\0';
    return TEXT_LENGTH;
}

} // namespace camera_list
