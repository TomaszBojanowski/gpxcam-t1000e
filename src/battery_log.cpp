#include "battery_log.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

#include "battery.h"
#include "battery_rules.h"
#include "link_control.h"
#include "log.h"

// Appending: a file opened with FILE_O_WRITE starts at its end (Adafruit_LittleFS_File.cpp);
// littlefs commits it on close.

using namespace Adafruit_LittleFS_Namespace;

namespace {

constexpr const char *DIRECTORY = "/gpxcam";
constexpr const char *PATH = "/gpxcam/battery.bin";
constexpr uint32_t MS_PER_MINUTE = 60000;

bool logging = false;
bool onBattery = false; // the previous poll saw no USB
uint32_t lastEntryMs = 0;

uint32_t entryCount()
{
    File file(InternalFS);
    if (!file.open(PATH, FILE_O_READ))
        return 0;
    const uint32_t size = file.size();
    file.close();
    return size / battery_rules::ENTRY_SIZE;
}

bool append(uint32_t millivolts, uint8_t flags)
{
    if (entryCount() >= battery_log::MAX_ENTRIES)
        return false;
    if (!InternalFS.exists(DIRECTORY) && !InternalFS.mkdir(DIRECTORY))
        return false;
    const battery_rules::Entry entry = {millis() / MS_PER_MINUTE, (uint16_t)millivolts,
                                        (uint8_t)link_control::activity(), flags};
    uint8_t data[battery_rules::ENTRY_SIZE];
    battery_rules::encode(entry, data);
    File file(InternalFS);
    if (!file.open(PATH, FILE_O_WRITE))
        return false;
    const size_t written = file.write(data, sizeof(data));
    file.close();
    return written == sizeof(data);
}

} // namespace

namespace battery_log {

void start()
{
    if (logging) {
        logger::line("bat log: already on");
        return;
    }
    if (!InternalFS.begin()) {
        logger::line("bat log: file system not available");
        return;
    }
    logging = true;
    onBattery = false; // the first entry comes when the cable is unplugged
    logger::line("bat log: on - an entry every %lu min on the battery, %lu entries so far (max %lu)",
                 (unsigned long)(PERIOD_MS / MS_PER_MINUTE), (unsigned long)entryCount(),
                 (unsigned long)MAX_ENTRIES);
}

void stop()
{
    if (!logging) {
        logger::line("bat log: already off");
        return;
    }
    logging = false;
    logger::line("bat log: off, %lu entries", (unsigned long)entryCount());
}

void dump()
{
    File file(InternalFS);
    if (!InternalFS.begin() || !file.open(PATH, FILE_O_READ)) {
        logger::transient("bat log: no entries");
        return;
    }
    const uint32_t count = file.size() / battery_rules::ENTRY_SIZE;
    logger::transient("bat log: %lu entries, logging %s (uptime min, battery mV, link)", (unsigned long)count,
                      logging ? "on" : "off");
    uint8_t data[battery_rules::ENTRY_SIZE];
    for (uint32_t i = 0; i < count && file.read(data, sizeof(data)) == (int)sizeof(data); i++) {
        const battery_rules::Entry entry = battery_rules::decode(data);
        logger::transient("bat log: %5lu min %4u mV %s%s", (unsigned long)entry.uptimeMinutes, entry.millivolts,
                          link_control::activityName(entry.activity),
                          (entry.flags & battery_rules::FLAG_LOW_BATTERY_OFF) ? " - low battery, powered off" : "");
    }
    file.close();
}

void clear()
{
    if (InternalFS.begin() && InternalFS.exists(PATH) && !InternalFS.remove(PATH)) {
        logger::line("bat log: could not remove %s", PATH);
        return;
    }
    logger::line("bat log: cleared");
}

void poll()
{
    if (!logging)
        return;
    if (battery::usbPowered()) {
        onBattery = false;
        return;
    }
    const uint32_t now = millis();
    if (onBattery && now - lastEntryMs < PERIOD_MS)
        return;
    onBattery = true;
    lastEntryMs = now;
    const uint32_t millivolts = battery::measureMillivolts();
    if (!append(millivolts, 0)) {
        logger::line("bat log: full or not writable - logging stops (%lu entries)", (unsigned long)entryCount());
        logging = false;
    }
}

void addPowerOffEntry(uint32_t millivolts)
{
    if (InternalFS.begin())
        append(millivolts, battery_rules::FLAG_LOW_BATTERY_OFF);
}

} // namespace battery_log
