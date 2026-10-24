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

// Saves wanted and puts it in effect. sameSetting: the changed setting already has this value;
// then nothing is written if the file holds it too.
settings_store::SetResult save(const settings_record::Record &wanted, bool sameSetting)
{
    if (savedInFile && sameSetting)
        return settings_store::SetResult::Unchanged;
    if (!fileSystemReady || !writeFile(wanted))
        return settings_store::SetResult::NotSaved;
    current = wanted;
    savedInFile = true;
    return settings_store::SetResult::Saved;
}

const char *defaultMark()
{
    return savedInFile ? "" : " (default)";
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

link_policy::Updates updates()
{
    return current.updates;
}

SetResult setSound(sound_rules::Setting setting)
{
    settings_record::Record wanted = current;
    wanted.sound = setting;
    return save(wanted, setting == current.sound);
}

SetResult setUpdates(link_policy::Updates updates)
{
    settings_record::Record wanted = current;
    wanted.updates = updates;
    return save(wanted, updates == current.updates);
}

void logSound()
{
    logger::transient("sound: %s%s", sound_rules::settingName(current.sound), defaultMark());
}

void logUpdates()
{
    const link_policy::LocationTiming &timing = link_policy::timingOf(current.updates);
    logger::transient("updates: %s%s - a location every %lu s, sooner after %lu m or when the camera asks "
                      "(at most every %lu s)",
                      link_policy::updatesName(current.updates), defaultMark(),
                      (unsigned long)(timing.periodMs / 1000), (unsigned long)timing.moveMetres,
                      (unsigned long)(timing.minGapMs / 1000));
}

void logState()
{
    logSound();
    logUpdates();
}

} // namespace settings_store
