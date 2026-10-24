#include "battery_log.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

#include "battery.h"
#include "battery_batch.h"
#include "battery_rules.h"
#include "link_control.h"
#include "log.h"

// Appending: a file opened with FILE_O_WRITE starts at its end (Adafruit_LittleFS_File.cpp);
// littlefs commits it on close.

using namespace Adafruit_LittleFS_Namespace;

namespace {

constexpr const char *DIRECTORY = "/log";
constexpr const char *PATH = "/log/battery.bin";
// Firmware before 1.4.0: in the directory of the pairing, time zone and settings, which every
// entry then rewrote (battery_batch.h).
constexpr const char *OLD_PATH = "/gpxcam/battery.bin";
constexpr uint32_t MS_PER_MINUTE = 60000;

bool fileSystemReady = false;
bool logging = false;
bool onBattery = false; // the previous poll saw no USB
uint32_t lastEntryMs = 0;
uint32_t entriesInFile = 0; // read at start, then counted: no file opened for it per entry
battery_batch::Batch batch;

uint32_t countEntriesInFile()
{
    File file(InternalFS);
    if (!fileSystemReady || !file.open(PATH, FILE_O_READ))
        return 0;
    const uint32_t size = file.size();
    file.close();
    return size / battery_rules::ENTRY_SIZE;
}

// Appends the entries in RAM to the file in one write. They leave RAM either way: entries that
// could not be written are dropped, and the log says so.
bool writeBatch()
{
    const size_t count = batch.size();
    if (count == 0)
        return true;
    bool written = fileSystemReady && (InternalFS.exists(DIRECTORY) || InternalFS.mkdir(DIRECTORY));
    if (written) {
        File file(InternalFS);
        written = file.open(PATH, FILE_O_WRITE);
        if (written) {
            written = file.write(batch.bytes(), batch.byteCount()) == batch.byteCount();
            file.close();
        }
    }
    batch.clear();
    if (!written) {
        entriesInFile = countEntriesInFile(); // a part may have been written
        logger::line("bat log: could not write %u entries to %s - they are lost", (unsigned)count, PATH);
        return false;
    }
    entriesInFile += count;
    return true;
}

battery_rules::Entry entryNow(uint32_t millivolts, uint8_t flags)
{
    return {millis() / MS_PER_MINUTE, (uint16_t)millivolts, (uint8_t)link_control::activity(), flags};
}

void logEntry(const battery_rules::Entry &entry, bool saved)
{
    logger::transient("bat log: %5lu min %4u mV %s%s%s", (unsigned long)entry.uptimeMinutes, entry.millivolts,
                      link_control::activityName(entry.activity),
                      (entry.flags & battery_rules::FLAG_LOW_BATTERY_OFF) ? " - low battery, powered off" : "",
                      saved ? "" : " (not saved yet)");
}

void logOldFileLeft()
{
    logger::line("bat log: an older log is still in %s - 'bat log clear' removes it too", OLD_PATH);
}

// Once, on the first start of 1.4.0. littlefs 1.7 renames across directories safely over a
// power cut: lfs_rename() marks the entry as moving first, and after a cut the first write of
// the next start finishes or undoes the move (lfs_deorphan()).
void moveOldFile()
{
    switch (battery_batch::oldFile(InternalFS.exists(OLD_PATH), InternalFS.exists(PATH))) {
    case battery_batch::OldFile::None:
        return;
    case battery_batch::OldFile::Move:
        if ((InternalFS.exists(DIRECTORY) || InternalFS.mkdir(DIRECTORY)) && InternalFS.rename(OLD_PATH, PATH))
            logger::line("bat log: moved %s to %s", OLD_PATH, PATH);
        else
            logger::line("bat log: could not move %s to %s", OLD_PATH, PATH);
        return;
    case battery_batch::OldFile::KeepBoth:
        logOldFileLeft();
        return;
    }
}

// True when the file is gone or was not there; says so when it could not be removed.
bool removeIfThere(const char *path)
{
    if (!fileSystemReady || !InternalFS.exists(path) || InternalFS.remove(path))
        return true;
    logger::line("bat log: could not remove %s", path);
    return false;
}

// Ends logging when the file cannot take more entries, or a write failed.
void stopLogging(const char *why)
{
    logging = false;
    logger::line("bat log: %s - logging stops (%lu entries)", why, (unsigned long)entriesInFile);
}

void logEntries()
{
    if (entriesInFile + batch.size() == 0) {
        logger::transient("bat log: no entries");
        return;
    }
    logger::transient("bat log: %lu entries (%u not saved yet), logging %s (uptime min, battery mV, link)",
                      (unsigned long)(entriesInFile + batch.size()), (unsigned)batch.size(), logging ? "on" : "off");
    File file(InternalFS);
    if (entriesInFile > 0 && file.open(PATH, FILE_O_READ)) {
        uint8_t data[battery_rules::ENTRY_SIZE];
        for (uint32_t i = 0; i < entriesInFile && file.read(data, sizeof(data)) == (int)sizeof(data); i++)
            logEntry(battery_rules::decode(data), true);
        file.close();
    }
    for (size_t i = 0; i < batch.size(); i++)
        logEntry(batch.entry(i), false);
}

} // namespace

namespace battery_log {

void begin(bool fileSystemMounted)
{
    fileSystemReady = fileSystemMounted && InternalFS.begin(); // mounted by Bluefruit: true at once
    if (fileSystemReady)
        moveOldFile();
    entriesInFile = countEntriesInFile();
}

void start()
{
    if (logging) {
        logger::line("bat log: already on");
        return;
    }
    if (!fileSystemReady) {
        logger::line("bat log: file system not available");
        return;
    }
    logging = true;
    onBattery = false; // the first entry comes when the cable is unplugged
    logger::line("bat log: on - an entry every %lu min on the battery, saved %u at a time, %lu entries so far "
                 "(max %lu)",
                 (unsigned long)(PERIOD_MS / MS_PER_MINUTE), (unsigned)battery_batch::BATCH_ENTRIES,
                 (unsigned long)(entriesInFile + batch.size()), (unsigned long)battery_batch::MAX_ENTRIES);
}

void stop()
{
    if (!logging) {
        logger::line("bat log: already off");
        return;
    }
    logging = false;
    writeBatch();
    logger::line("bat log: off, %lu entries", (unsigned long)entriesInFile);
}

void dump()
{
    logEntries();
    if (fileSystemReady && InternalFS.exists(OLD_PATH))
        logOldFileLeft();
}

void clear()
{
    batch.clear();
    const bool removed = removeIfThere(PATH) && removeIfThere(OLD_PATH);
    entriesInFile = countEntriesInFile();
    if (!removed)
        return;
    logger::line("bat log: cleared");
}

void poll()
{
    if (!logging)
        return;
    if (battery::usbPowered()) {
        if (onBattery && !writeBatch()) // the run on the battery ended
            stopLogging("not writable");
        onBattery = false;
        return;
    }
    const uint32_t now = millis();
    if (onBattery && now - lastEntryMs < PERIOD_MS)
        return;
    onBattery = true;
    lastEntryMs = now;
    if (!batch.add(entryNow(battery::measureMillivolts(), 0), entriesInFile)) {
        stopLogging("full");
        return;
    }
    if (batch.due(entriesInFile) && !writeBatch())
        stopLogging("not writable");
    else if (entriesInFile >= battery_batch::MAX_ENTRIES)
        stopLogging("full");
}

void addPowerOffEntry(uint32_t millivolts)
{
    if (batch.add(entryNow(millivolts, battery_rules::FLAG_LOW_BATTERY_OFF), entriesInFile))
        writeBatch();
}

} // namespace battery_log
