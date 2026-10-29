// The commands of the serial console: one table of what can be typed, how a typed line is matched
// to a command, and the help, which lists the same table. No hardware dependencies (tested on the PC:
// test/test_console); main.cpp runs the commands.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace console_commands {

enum class Id : uint8_t {
    Empty,   // an empty line (e.g. the LF of CRLF): nothing to do
    Unknown, // the argument is the whole line, in lower case
    Version,
    State,
    Status,
    Log,
    Off,
    Dfu,
    Pair,
    PairOff,
    PairOlder,
    PairPrepare,
    Unpair,
    TimeZone,
    TimeZoneSet, // the argument is a zone name or a POSIX rule, as typed
    Clock,
    ClockOn,
    ClockOff,
    Sound,
    SoundSet, // the argument is the setting
    Updates,
    UpdatesSet, // the argument is the setting
    LinkOn,
    LinkOff,
    BatteryLogOn,
    BatteryLogOff,
    BatteryLog,
    BatteryLogClear,
    DiagBattery,
    DiagSound, // the argument is the PWM duty
    DiagHang,
    DiagScan,
    DiagClock,
    DiagClockLater, // the argument is the delay in seconds
    StorageFormat,  // without "yes": only says what it would do
    StorageFormatYes,
#ifdef GPXCAM_TEST_COMMANDS
    // Test builds only (platformio.ini, env t1000e_test).
    DiagStorageBreak,
    DiagStorageBreakMiddle,
    DiagScanStall,
    DiagGpsStale,
    DiagGpsFresh,
#endif
};

// "tz" and the longest POSIX rule make the longest command (main.cpp).
constexpr char TIME_ZONE_SET[] = "tz ";

struct Command {
    Id id;
    const char *argument; // the rest of the line for a command that takes one, otherwise ""
};

// The command of one line as typed, without its line ending. Changes the line in place: spaces at
// its start and end go, and it is lower-cased, except the argument of 'tz' (POSIX rules need
// their case: "M3.5.0"). The argument points into the line.
Command find(char *line);

// What is typed for the command (lower case); for one with an argument, up to and with the space
// before it. nullptr for Empty and Unknown.
const char *text(Id id);

// Longest line of the help, without the line ending: one log line (logger::TEXT_MAX, checked in
// main.cpp: log.h does not build on the PC next to <limits.h>, which has its own LINE_MAX).
constexpr size_t HELP_LINE_MAX = 125;

// The help, one line at a time.
void help(void (*print)(const char *line));

} // namespace console_commands
