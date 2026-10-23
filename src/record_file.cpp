#include "record_file.h"

#include <InternalFileSystem.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

namespace record_file {

bool read(const char *path, uint8_t *data, size_t capacity, size_t *length)
{
    File file(InternalFS);
    if (!file.open(path, FILE_O_READ))
        return false;
    const uint16_t wanted = (uint16_t)(capacity <= MAX_SIZE + 1 ? capacity : MAX_SIZE + 1);
    const int count = file.read(data, wanted);
    file.close();
    *length = count < 0 ? 0 : (size_t)count;
    return true;
}

bool write(const char *path, const char *tempPath, const uint8_t *data, size_t size)
{
    if (size > MAX_SIZE)
        return false;
    if (!InternalFS.exists(DIRECTORY) && !InternalFS.mkdir(DIRECTORY))
        return false;
    if (InternalFS.exists(tempPath) && !InternalFS.remove(tempPath)) // left by a power loss
        return false;
    File file(InternalFS);
    if (!file.open(tempPath, FILE_O_WRITE))
        return false;
    const size_t written = file.write(data, size);
    file.close();
    if (written != size || !InternalFS.rename(tempPath, path))
        return false;
    uint8_t check[MAX_SIZE + 1];
    size_t length = 0;
    return read(path, check, sizeof(check), &length) && length == size && memcmp(check, data, size) == 0;
}

} // namespace record_file
