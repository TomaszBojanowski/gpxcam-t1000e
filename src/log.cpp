#include "log.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

// Serial.write() in the core loops until everything is queued while a terminal
// is open (Adafruit_USBD_CDC::write), so a host that keeps the port open without
// reading would block the caller forever. We wait at most this long for space.
constexpr uint32_t LOG_WAIT_MS = 50;
constexpr size_t EOL_LEN = 2; // "\r\n"

// History line as shown by dumpHistory(): "[  12345.6 s] " + text + "\r\n".
constexpr size_t STAMP_MAX = 16;

struct Entry {
    uint32_t uptimeMs;
    char text[logger::LINE_MAX - EOL_LEN]; // without "\r\n", zero-terminated
};

char buffer[logger::LINE_MAX];
char stampedBuffer[STAMP_MAX + logger::LINE_MAX];
Entry history[logger::HISTORY_LINES];
size_t historyNext = 0;  // slot for the next line
size_t historyCount = 0; // filled slots
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

void send(const char *data, size_t len)
{
    if (!Serial || !waitForTxSpace(len)) {
        droppedLines++;
        return;
    }
    Serial.write(data, len);
    Serial.flush(); // starts the USB transfer, does not wait for it
}

// Formats into buffer and returns the text length (without "\r\n"), or -1.
int format(const char *fmt, va_list args)
{
    const int formatted = vsnprintf(buffer, sizeof(buffer) - EOL_LEN, fmt, args);
    if (formatted < 0)
        return -1;
    // vsnprintf returns the untruncated length; clamp to what fits in the buffer.
    return (int)min((size_t)formatted, sizeof(buffer) - EOL_LEN - 1);
}

void remember(const char *text, size_t len)
{
    Entry &entry = history[historyNext];
    entry.uptimeMs = millis();
    memcpy(entry.text, text, len); // len < sizeof(entry.text), see format()
    entry.text[len] = '\0';
    historyNext = (historyNext + 1) % logger::HISTORY_LINES;
    if (historyCount < logger::HISTORY_LINES)
        historyCount++;
}

void sendFormatted(size_t len)
{
    buffer[len++] = '\r';
    buffer[len++] = '\n';
    send(buffer, len);
}

} // namespace

namespace logger {

void line(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int len = format(fmt, args);
    va_end(args);
    if (len < 0)
        return;
    remember(buffer, (size_t)len);
    sendFormatted((size_t)len);
}

void transient(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int len = format(fmt, args);
    va_end(args);
    if (len >= 0)
        sendFormatted((size_t)len);
}

void dumpHistory()
{
    const size_t first = (historyNext + HISTORY_LINES - historyCount) % HISTORY_LINES;
    for (size_t i = 0; i < historyCount; i++) {
        const Entry &entry = history[(first + i) % HISTORY_LINES];
        const int len = snprintf(stampedBuffer, sizeof(stampedBuffer), "[%7lu.%01lu s] %s\r\n",
                                 (unsigned long)(entry.uptimeMs / 1000),
                                 (unsigned long)(entry.uptimeMs % 1000 / 100), entry.text);
        if (len > 0)
            send(stampedBuffer, min((size_t)len, sizeof(stampedBuffer) - 1));
    }
}

uint32_t dropped()
{
    return droppedLines;
}

} // namespace logger
