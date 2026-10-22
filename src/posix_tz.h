// Time zone rules in the POSIX TZ form, as the tz table holds them: "CET-1CEST,M3.5.0,M10.5.0/3".
// POSIX.1-2017, section 8.3, with the tz database's extension of RFC 8536, section 3.3.1
// (transition times from -167 to 167 hours). No hardware dependencies (tested on the PC).
//
// Supported: names of letters or in <>, offsets in whole minutes (as the camera takes them) and
// summer-time transitions in the Mm.w.d form, which all rules of the tz database use. Refused:
// the Jn and n forms of a transition date, and summer time without its transitions.
#pragma once

#include <stdint.h>

namespace posix_tz {

// A summer-time change: weekday d of week w of month m (week 5 = the last such day of the
// month), at a time of day in the local time in effect before the change.
struct Transition {
    uint8_t month;       // 1..12
    uint8_t week;        // 1..5
    uint8_t weekday;     // 0 = Sunday .. 6 = Saturday
    int32_t timeSeconds; // -167 h .. 167 h; 02:00 when the rule gives none
};

struct Rule {
    int32_t standardOffsetSeconds; // east of UTC: +3600 for "CET-1" (POSIX counts west)
    bool hasSummerTime;
    int32_t summerOffsetSeconds; // east of UTC; one hour more than standard when not given
    Transition summerStart;      // in standard time
    Transition summerEnd;        // in summer time
};

// Parses a rule. False, with *rule unchanged, for anything not valid or not supported.
bool parse(const char *text, Rule *rule);

// The offset from UTC in effect at a moment.
int32_t utcOffsetSeconds(const Rule &rule, int64_t unixSeconds);

// The time zone as the LUMIX time packet carries it (docs/PROTOCOL.md): a standard offset and
// a flag that adds one hour.
struct CameraZone {
    int16_t standardOffsetMinutes; // east of UTC
    bool summerTime;
};

// The camera's zone at a moment. Summer time one hour ahead of standard time maps directly.
// One hour behind (Europe/Dublin: its standard time is the summer one, winter is "summer
// time" one hour behind) swaps the two. Any other difference (Australia/Lord_Howe: 30 min,
// Antarctica/Troll: 2 h) goes as a standard offset equal to the offset in effect, without the
// flag: the camera's local time is right, only its summer-time mark is not shown.
CameraZone cameraZone(const Rule &rule, int64_t unixSeconds);

} // namespace posix_tz
