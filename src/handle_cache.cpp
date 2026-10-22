#include "handle_cache.h"

#include <string.h>

namespace {

bool valid = false;
uint8_t keptSerial[lumix::SERIAL_LENGTH] = {};
handle_cache::Handles kept = {};

} // namespace

namespace handle_cache {

void keep(const uint8_t serial[lumix::SERIAL_LENGTH], const Handles &handles)
{
    valid = true;
    memcpy(keptSerial, serial, sizeof(keptSerial));
    kept = handles;
}

Lookup find(const uint8_t serial[lumix::SERIAL_LENGTH], Handles *handles)
{
    if (!valid)
        return Lookup::Empty;
    if (memcmp(keptSerial, serial, sizeof(keptSerial)) != 0) {
        forget();
        return Lookup::OtherCameraForgotten;
    }
    *handles = kept;
    return Lookup::Found;
}

bool forget()
{
    const bool had = valid;
    valid = false;
    memset(keptSerial, 0, sizeof(keptSerial));
    kept = {};
    return had;
}

bool forgetAfterFailure(bool connected, bool notEstablished)
{
    return connected && !notEstablished;
}

} // namespace handle_cache
