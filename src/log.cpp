#include "log.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

// Serial.write() in the core loops until everything is queued while a terminal
// is open (Adafruit_USBD_CDC::write), so a host that keeps the port open without
// reading would block the caller forever. We wait at most this long for space.
constexpr uint32_t LOG_WAIT_MS = 50;

char buffer[logger::LINE_MAX];
uint32_t droppedLines = 0;

bool waitForTxSpace(size_t len)
{
    const uint32_t start = millis();
    while (Serial.availableForWrite() < (int)len) {
        if (!Serial || millis() - start >= LOG_WAIT_MS)
            return false;
        delay(1); // lets the USB task send queued data
    }
    return true;
}

} // namespace

namespace logger {

void line(const char *fmt, ...)
{
    constexpr size_t EOL_LEN = 2; // "\r\n"

    va_list args;
    va_start(args, fmt);
    const int formatted = vsnprintf(buffer, sizeof(buffer) - EOL_LEN, fmt, args);
    va_end(args);
    if (formatted < 0)
        return;

    // vsnprintf returns the untruncated length; clamp to what fits in the buffer.
    size_t len = min((size_t)formatted, sizeof(buffer) - EOL_LEN - 1);
    buffer[len++] = '\r';
    buffer[len++] = '\n';

    if (!Serial || !waitForTxSpace(len)) {
        droppedLines++;
        return;
    }
    Serial.write(buffer, len);
    Serial.flush(); // starts the USB transfer, does not wait for it
}

uint32_t dropped()
{
    return droppedLines;
}

} // namespace logger
