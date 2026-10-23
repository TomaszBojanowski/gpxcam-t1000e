#include "settings_store.h"

#include <InternalFileSystem.h>

#include "log.h"
#include "record_file.h"
#include "settings_record.h"

namespace {

constexpr const char *PATH = "/gpxcam/settings.bin";
constexpr const char *TEMP_PATH = "/gpxcam/settings.tmp";
static_assert(settings_record::SIZE <= record_file::MAX_SIZE, "one block");

settings_record::Record current = settings_record::DEFAULTS;
bool fileSystemReady = false;
bool savedInFile = false; // current comes from the file

bool writeFile(const settings_record::Record &record)
{
    uint8_t data[settings_record::SIZE];
    settings_record::encode(record, data);
    return record_file::write(PATH, TEMP_PATH, data, sizeof(data));
}

} // namespace

namespace settings_store {

void begin(bool fileSystemMounted)
{
    current = settings_record::DEFAULTS;
    savedInFile = false;
    fileSystemReady = fileSystemMounted && InternalFS.begin(); // mounted by Bluefruit: true at once
    if (!fileSystemReady) {
        logger::line("settings: file system not available - using the defaults");
        return;
    }
    uint8_t data[settings_record::SIZE + 1]; // one byte more, to notice a longer file
    size_t length = 0;
    if (!record_file::read(PATH, data, sizeof(data), &length))
        return; // never set: the defaults
    const settings_record::Status status = settings_record::decode(data, length, &current);
    if (status != settings_record::Status::Ok) {
        logger::line("settings: %s is damaged (%s) - using the defaults", PATH, settings_record::statusText(status));
        return;
    }
    savedInFile = true;
}

sound_rules::Setting sound()
{
    return current.sound;
}

SetResult setSound(sound_rules::Setting setting)
{
    if (savedInFile && setting == current.sound)
        return SetResult::Unchanged;
    settings_record::Record wanted = current;
    wanted.sound = setting;
    if (!fileSystemReady || !writeFile(wanted))
        return SetResult::NotSaved;
    current = wanted;
    savedInFile = true;
    return SetResult::Saved;
}

void logState()
{
    logger::transient("sound: %s%s", sound_rules::settingName(current.sound), savedInFile ? "" : " (default)");
}

} // namespace settings_store
