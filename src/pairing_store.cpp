#include "pairing_store.h"

#include <Arduino.h>
#include <nrf_soc.h>
#include <string.h>

#include "log.h"
#include "pairing_copy.h"
#include "record_file.h"
#include "storage.h"

namespace {

// Written through a temporary file and read back (record_file.h), as the time zone and the settings.
constexpr const char *PATH = "/gpxcam/pairing.bin";
static_assert(pairing_record::SIZE <= record_file::MAX_SIZE, "the record fits a record file");
constexpr const char *TEMP_PATH = "/gpxcam/pairing.tmp";

constexpr uint32_t RANDOM_WAIT_MS = 100; // the SoftDevice fills its pool within milliseconds

bool loaded = false;
pairing_record::Record current = {};

bool sameRecord(const pairing_record::Record &a, const pairing_record::Record &b)
{
    return memcmp(a.uuid, b.uuid, sizeof(a.uuid)) == 0 && a.paired == b.paired &&
           memcmp(a.serial, b.serial, sizeof(a.serial)) == 0 && a.dialect == b.dialect;
}

// False if the file is missing; otherwise *status says whether *record was read.
bool readFile(pairing_record::Record *record, pairing_record::Status *status)
{
    uint8_t data[pairing_record::SIZE + 1]; // one byte more, to notice a longer file
    size_t length = 0;
    if (!record_file::read(PATH, data, sizeof(data), &length))
        return false;
    *status = pairing_record::decode(data, length, record);
    return true;
}

bool writeFile(const pairing_record::Record &record)
{
    uint8_t data[pairing_record::SIZE];
    pairing_record::encode(record, data);
    return record_file::write(PATH, TEMP_PATH, data, sizeof(data));
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
    pairing_copy::write(fresh);
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
    pairing_copy::begin();
    pairing_record::Record copy;
    const bool haveCopy = pairing_copy::read(&copy);
    if (!storage::ready()) {
        if (!haveCopy) {
            logger::line("pair: no pairing - the file system is damaged and there is no copy");
            return false;
        }
        current = copy;
        loaded = true;
        logger::line("pair: the file system is damaged - the pairing comes from its copy");
        return true;
    }
    pairing_record::Status status = pairing_record::Status::Ok;
    const bool found = readFile(&current, &status);
    if (found && status == pairing_record::Status::Ok) {
        loaded = true;
        if (!haveCopy || !sameRecord(copy, current))
            pairing_copy::write(current); // the first start with a copy, or a copy older than the file
        return true;
    }
    if (haveCopy) { // after 'storage format yes', or a damaged file
        logger::line("pair: %s %s - restored from its copy", PATH,
                     found ? pairing_record::statusText(status) : "missing");
        current = copy;
        loaded = true;
        if (!writeFile(current))
            logger::line("pair: could not write %s - the copy is used", PATH);
        return true;
    }
    if (found)
        logger::line("pair: %s is damaged (%s) - new card id, pair the camera again", PATH,
                     pairing_record::statusText(status));
    loaded = createRecord();
    return loaded;
}

bool ready()
{
    return loaded;
}

bool writable()
{
    return loaded && storage::ready();
}

const pairing_record::Record &record()
{
    return current;
}

bool saveCamera(const uint8_t serial[lumix::SERIAL_LENGTH], uint8_t dialect)
{
    if (!writable())
        return false;
    pairing_record::Record changed = current;
    changed.paired = true;
    memcpy(changed.serial, serial, sizeof(changed.serial));
    changed.dialect = dialect;
    if (!writeFile(changed))
        return false;
    pairing_copy::write(changed); // after the file: a copy older than the file is replaced at start
    current = changed;
    return true;
}

bool forgetCamera()
{
    if (!writable())
        return false;
    pairing_record::Record changed = current;
    changed.paired = false;
    memset(changed.serial, 0, sizeof(changed.serial));
    if (!writeFile(changed))
        return false;
    pairing_copy::write(changed);
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
    } else {
        char serial[2 * lumix::SERIAL_LENGTH + 1];
        lumix::formatSerial(current.serial, serial, sizeof(serial));
        logger::transient("pair: card id %s, camera %s", uuid, serial);
    }
    pairing_copy::logState();
}

} // namespace pairing_store
