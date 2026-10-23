#include "tz_store.h"

#include <InternalFileSystem.h>
#include <string.h>

#include "gps.h"
#include "log.h"
#include "record_file.h"
#include "tz_table.h"

namespace {

constexpr const char *PATH = "/gpxcam/timezone.bin";
constexpr const char *TEMP_PATH = "/gpxcam/timezone.tmp";
static_assert(tz_record::SIZE <= record_file::MAX_SIZE, "one block");

constexpr int32_t SECONDS_PER_MINUTE = 60;
constexpr int32_t MINUTES_PER_HOUR = 60;

tz_record::Record current = {};
posix_tz::Rule currentRule = {};
bool fileSystemReady = false;
bool savedInFile = false; // current comes from the file (its rule maybe from a newer tz table)

void useDefault()
{
    current = tz_record::defaultRecord();
    posix_tz::parse(current.rule, &currentRule); // the table's rule for the default zone (tested)
    savedInFile = false;
}

// False if the file is missing; otherwise *status says whether *record was read.
bool readFile(tz_record::Record *record, tz_record::Status *status)
{
    uint8_t data[tz_record::SIZE + 1]; // one byte more, to notice a longer file
    size_t length = 0;
    if (!record_file::read(PATH, data, sizeof(data), &length))
        return false;
    *status = tz_record::decode(data, length, record);
    return true;
}

bool sameRecord(const tz_record::Record &a, const tz_record::Record &b)
{
    return strcmp(a.name, b.name) == 0 && strcmp(a.rule, b.rule) == 0 && a.clockOff == b.clockOff;
}

bool writeFile(const tz_record::Record &record)
{
    uint8_t data[tz_record::SIZE];
    tz_record::encode(record, data);
    return record_file::write(PATH, TEMP_PATH, data, sizeof(data));
}

} // namespace

namespace tz_store {

void begin(bool fileSystemMounted)
{
    useDefault();
    fileSystemReady = fileSystemMounted && InternalFS.begin(); // mounted by Bluefruit: true at once
    if (!fileSystemReady) {
        logger::line("tz: file system not available - using %s", current.name);
        return;
    }
    tz_record::Record loaded;
    tz_record::Status status = tz_record::Status::Ok;
    if (!readFile(&loaded, &status))
        return; // never set: the default zone
    if (status != tz_record::Status::Ok) {
        logger::line("tz: %s is damaged (%s) - using %s", PATH, tz_record::statusText(status), current.name);
        return;
    }
    tz_record::updateFromTable(&loaded);
    posix_tz::Rule parsed;
    if (!posix_tz::parse(loaded.rule, &parsed)) {
        logger::line("tz: the rule in %s cannot be used - using %s", PATH, current.name);
        return;
    }
    current = loaded;
    currentRule = parsed;
    savedInFile = true;
}

const posix_tz::Rule &rule()
{
    return currentRule;
}

const char *zoneText()
{
    return current.name[0] != '\0' ? current.name : current.rule;
}

SetResult set(const char *text)
{
    tz_record::Record wanted;
    posix_tz::Rule parsed;
    if (!tz_record::fromInput(text, &wanted) || !posix_tz::parse(wanted.rule, &parsed))
        return SetResult::Unknown;
    wanted.clockOff = current.clockOff;
    if (savedInFile && sameRecord(wanted, current))
        return SetResult::Unchanged;
    if (!fileSystemReady || !writeFile(wanted))
        return SetResult::NotSaved;
    current = wanted;
    currentRule = parsed;
    savedInFile = true;
    return SetResult::Saved;
}

bool clockOn()
{
    return !current.clockOff;
}

SetResult setClock(bool on)
{
    tz_record::Record wanted = current;
    wanted.clockOff = !on;
    if (savedInFile && sameRecord(wanted, current))
        return SetResult::Unchanged;
    if (!fileSystemReady || !writeFile(wanted))
        return SetResult::NotSaved;
    current = wanted;
    savedInFile = true;
    return SetResult::Saved;
}

void logState()
{
    if (current.name[0] != '\0')
        logger::transient("tz: %s%s, rule %s (tz database %s)", current.name, savedInFile ? "" : " (default)",
                          current.rule, tz_table::tzdataVersion());
    else
        logger::transient("tz: rule %s, given directly", current.rule);
    if (current.clockOff) {
        logger::transient("tz: clock off - the card does not set the camera's clock or time zone");
        return;
    }
    int64_t now = 0;
    if (!gps::utcNow(&now))
        return;
    const int32_t offset = posix_tz::utcOffsetSeconds(currentRule, now);
    const int32_t minutes = (offset < 0 ? -offset : offset) / SECONDS_PER_MINUTE;
    const posix_tz::CameraZone camera = posix_tz::cameraZone(currentRule, now);
    logger::transient("tz: now UTC%c%02ld:%02ld, the camera gets %d min%s", offset < 0 ? '-' : '+',
                      (long)(minutes / MINUTES_PER_HOUR), (long)(minutes % MINUTES_PER_HOUR),
                      camera.standardOffsetMinutes, camera.summerTime ? " + summer time" : "");
}

} // namespace tz_store
