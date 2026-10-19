// NMEA 0183 helpers with no hardware dependencies (to be tested on the PC).
#pragma once

#include <stddef.h>

namespace nmea {

// "$...*hh": XOR of all characters between '$' and '*', as two hex digits at the end.
bool checksumOk(const char *sentence);

// True for "$xxTTT," where TTT is the sentence type and xx any talker (GP, GN, ...).
bool isType(const char *sentence, const char *type);

// True when the sentence starts with prefix (e.g. "$PAIR001,").
bool startsWith(const char *sentence, const char *prefix);

// Start of comma-separated field `index` (0 is the address field "$xxTTT"), or nullptr.
// A field ends at ',' or '*'.
const char *field(const char *sentence, unsigned index);

// Reads field `index` as a decimal number (optional leading '-'); false if it is
// missing, empty, not a number or outside [minValue, maxValue].
bool fieldInt(const char *sentence, unsigned index, long minValue, long maxValue, long *value);

// Writes "$<body>*hh\r\n" to out. Returns its length, or 0 if it does not fit.
size_t buildSentence(const char *body, char *out, size_t size);

} // namespace nmea
