#include "tz_store.h"

#include <InternalFileSystem.h>
#include <string.h>

#include "log.h"

// Written like the pairing file (pairing_store.cpp): a temporary file renamed over the
// record, which littlefs 1.7 does atomically, also on power loss, then read back, because
// File::close() reports no errors.

using namespace Adafruit_LittleFS_Namespace;

namespace {

constexpr const char *DIRECTORY = "/gpxcam";
constexpr const char *PATH = "/gpxcam/timezone.bin";
constexpr const char *TEMP_PATH = "/gpxcam/timezone.tmp";

tz_record::Record current = {};
posix_tz::Rule currentRule = {};
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
    File file(InternalFS);
    if (!file.open(PATH, FILE_O_READ))
        return false;
    uint8_t data[tz_record::SIZE + 1]; // one byte more, to notice a longer file
    const int length = file.read(data, sizeof(data));
    file.close();
    *status = tz_record::decode(data, length < 0 ? 0 : (size_t)length, record);
    return true;
}

bool sameZone(const tz_record::Record &a, const tz_record::Record &b)
{
    return strcmp(a.name, b.name) == 0 && strcmp(a.rule, b.rule) == 0;
}

bool writeFile(const tz_record::Record &record)
{
    uint8_t data[tz_record::SIZE];
    tz_record::encode(record, data);
    if (!InternalFS.exists(DIRECTORY) && !InternalFS.mkdir(DIRECTORY))
        return false;
    if (InternalFS.exists(TEMP_PATH) && !InternalFS.remove(TEMP_PATH)) // left by a power loss
        return false;
    File file(InternalFS);
    if (!file.open(TEMP_PATH, FILE_O_WRITE))
        return false;
    const size_t written = file.write(data, sizeof(data));
    file.close();
    if (written != sizeof(data) || !InternalFS.rename(TEMP_PATH, PATH))
        return false;
    tz_record::Record check;
    tz_record::Status status;
    return readFile(&check, &status) && status == tz_record::Status::Ok && sameZone(check, record);
}

} // namespace

namespace tz_store {

void begin()
{
    useDefault();
    if (!InternalFS.begin()) { // already mounted by Bluefruit: returns true at once
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

const tz_record::Record &record()
{
    return current;
}

const posix_tz::Rule &rule()
{
    return currentRule;
}

SetResult set(const char *text)
{
    tz_record::Record wanted;
    posix_tz::Rule parsed;
    if (!tz_record::fromInput(text, &wanted) || !posix_tz::parse(wanted.rule, &parsed))
        return SetResult::Unknown;
    if (savedInFile && sameZone(wanted, current))
        return SetResult::Unchanged;
    if (!writeFile(wanted))
        return SetResult::NotSaved;
    current = wanted;
    currentRule = parsed;
    savedInFile = true;
    return SetResult::Saved;
}

} // namespace tz_store
