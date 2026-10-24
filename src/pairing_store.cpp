#include "pairing_store.h"

#include <Arduino.h>
#include <InternalFileSystem.h>
#include <nrf_soc.h>
#include <string.h>

#include "log.h"
#include "storage.h"

// Sources: InternalFileSytem and Adafruit_LittleFS in the core (littlefs 1.7). littlefs
// commits a file only when it is closed, and rename is atomic, also on power loss, and
// replaces an existing file (littlefs README, lfs.h lfs_rename()).

using namespace Adafruit_LittleFS_Namespace;

namespace {

constexpr const char *DIRECTORY = "/gpxcam";
constexpr const char *PATH = "/gpxcam/pairing.bin";
constexpr const char *TEMP_PATH = "/gpxcam/pairing.tmp";

constexpr uint32_t RANDOM_WAIT_MS = 100; // the SoftDevice fills its pool within milliseconds

bool loaded = false;
pairing_record::Record current = {};

// False if the file is missing; otherwise *status says whether *record was read.
bool readFile(pairing_record::Record *record, pairing_record::Status *status)
{
    File file(InternalFS);
    if (!file.open(PATH, FILE_O_READ))
        return false;
    uint8_t data[pairing_record::SIZE + 1]; // one byte more, to notice a longer file
    const int length = file.read(data, sizeof(data));
    file.close();
    *status = pairing_record::decode(data, length < 0 ? 0 : (size_t)length, record);
    return true;
}

// Writes a temporary file and renames it over the record, then reads it back.
bool writeFile(const pairing_record::Record &record)
{
    uint8_t data[pairing_record::SIZE];
    pairing_record::encode(record, data);
    if (!InternalFS.exists(DIRECTORY) && !InternalFS.mkdir(DIRECTORY))
        return false;
    if (InternalFS.exists(TEMP_PATH) && !InternalFS.remove(TEMP_PATH)) // left by a power loss
        return false;
    File file(InternalFS);
    if (!file.open(TEMP_PATH, FILE_O_WRITE))
        return false;
    const size_t written = file.write(data, sizeof(data));
    file.close(); // reports no errors, hence the read-back below
    if (written != sizeof(data) || !InternalFS.rename(TEMP_PATH, PATH))
        return false;
    pairing_record::Record check;
    pairing_record::Status status;
    return readFile(&check, &status) && status == pairing_record::Status::Ok &&
           memcmp(check.uuid, record.uuid, sizeof(check.uuid)) == 0 && check.paired == record.paired &&
           memcmp(check.serial, record.serial, sizeof(check.serial)) == 0;
}

// Hardware random numbers through the SoftDevice, which owns the RNG while it runs.
bool randomBytes(uint8_t *out, uint8_t length)
{
    const uint32_t start = millis();
    for (;;) {
        uint8_t available = 0;
        sd_rand_application_bytes_available_get(&available);
        if (available >= length)
            return sd_rand_application_vector_get(out, length) == NRF_SUCCESS;
        if (millis() - start >= RANDOM_WAIT_MS)
            return false;
        delay(1);
    }
}

bool createRecord()
{
    pairing_record::Record fresh = {};
    if (!randomBytes(fresh.uuid, lumix::UUID_LENGTH)) {
        logger::line("pair: no random numbers from the SoftDevice");
        return false;
    }
    pairing_record::makeRandomUuid(fresh.uuid);
    if (!writeFile(fresh)) {
        logger::line("pair: could not write %s", PATH);
        return false;
    }
    current = fresh;
    char uuid[pairing_record::UUID_TEXT_LENGTH + 1];
    pairing_record::formatUuid(current.uuid, uuid, sizeof(uuid));
    logger::line("pair: new card id %s saved in %s", uuid, PATH);
    return true;
}

} // namespace

namespace pairing_store {

bool begin()
{
    if (!storage::ready()) {
        logger::line("pair: no pairing - the file system is damaged (see 'storage' in 'p')");
        return false;
    }
    pairing_record::Status status = pairing_record::Status::Ok;
    if (readFile(&current, &status) && status == pairing_record::Status::Ok) {
        loaded = true;
        return true;
    }
    if (status != pairing_record::Status::Ok)
        logger::line("pair: %s is damaged (%s) - new card id, pair the camera again", PATH,
                     pairing_record::statusText(status));
    loaded = createRecord();
    return loaded;
}

bool ready()
{
    return loaded;
}

const pairing_record::Record &record()
{
    return current;
}

bool saveCamera(const uint8_t serial[lumix::SERIAL_LENGTH])
{
    if (!loaded)
        return false;
    pairing_record::Record changed = current;
    changed.paired = true;
    memcpy(changed.serial, serial, sizeof(changed.serial));
    if (!writeFile(changed))
        return false;
    current = changed;
    return true;
}

bool forgetCamera()
{
    if (!loaded)
        return false;
    pairing_record::Record changed = current;
    changed.paired = false;
    memset(changed.serial, 0, sizeof(changed.serial));
    if (!writeFile(changed))
        return false;
    current = changed;
    return true;
}

void logState()
{
    if (!loaded) {
        logger::transient("pair: not available");
        return;
    }
    char uuid[pairing_record::UUID_TEXT_LENGTH + 1];
    pairing_record::formatUuid(current.uuid, uuid, sizeof(uuid));
    if (!current.paired) {
        logger::transient("pair: card id %s, no camera paired", uuid);
        return;
    }
    char serial[2 * lumix::SERIAL_LENGTH + 1];
    lumix::formatSerial(current.serial, serial, sizeof(serial));
    logger::transient("pair: card id %s, camera %s", uuid, serial);
}

} // namespace pairing_store
