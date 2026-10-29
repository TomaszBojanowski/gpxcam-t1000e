#include "console_commands.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "battery_log.h"
#include "power.h"
#include "scan_dump.h"
#include "watchdog.h"

namespace console_commands {

namespace {

enum class Form : uint8_t {
    Exact,           // the whole line
    Argument,        // the text, then the argument (lower case)
    ArgumentAsTyped, // the text, then the argument in its case, without the spaces before it
};

// The help shows the table row by row: an entry with a help text starts a row, the entries after it
// with SAME_ROW join it ("clock / clock on / clock off = ..."), HIDDEN ones are left out. A '\n' in
// a help text goes on in a new line.
constexpr const char *SAME_ROW = nullptr;
constexpr const char *HIDDEN = "";

struct Entry {
    const char *text;  // lower case; with an argument, up to and with its space
    Id id;
    Form form;
    const char *usage; // how the help shows it; nullptr: the text
    const char *help;  // what it does, or SAME_ROW or HIDDEN
};

// In the order of the console's table in README.md, with the same rows (test/test_console).
constexpr Entry ENTRIES[] = {
    {"v", Id::Version, Form::Exact, nullptr, "version and reset reason"},
    {"p", Id::State, Form::Exact, nullptr,
     "state of GPS, BLE, pairing, time zone, settings, link, battery and memory"},
    {"status", Id::Status, Form::Exact, nullptr, "the same in short key=value lines, for the card page"},
    {"log", Id::Log, Form::Exact, nullptr, "the last 64 log lines, also those from times without USB"},
    {"off", Id::Off, Form::Exact, nullptr, "System OFF after a 15 s countdown (refused with USB connected)"},
    {"dfu", Id::Dfu, Form::Exact, nullptr, "UF2 bootloader (for flashing)"},
    {"pair", Id::Pair, Form::Exact, nullptr,
     "start / cancel pairing with a camera in pairing mode (or click + hold 3 s)"},
    {"pair off", Id::PairOff, Form::Exact, nullptr, SAME_ROW},
    {"pair older", Id::PairOlder, Form::Exact, nullptr,
     "pair an older LUMIX camera (the app does a one-time Wi-Fi step)"},
    {"pair prepare", Id::PairPrepare, Form::Exact, nullptr, "GPS on for 10 min, so that pairing finds the time ready"},
    {"unpair", Id::Unpair, Form::Exact, nullptr, "forget the paired camera ('p' shows the card id and the camera)"},
    {"tz", Id::TimeZone, Form::Exact, nullptr,
     "show / set the time zone of the camera's clock, e.g. tz America/New_York, tz <+0545>-5:45"},
    {TIME_ZONE_SET, Id::TimeZoneSet, Form::ArgumentAsTyped, "tz NAME / tz RULE", SAME_ROW},
    {"clock", Id::Clock, Form::Exact, nullptr, "show / set whether the card sets the camera's clock"},
    {"clock on", Id::ClockOn, Form::Exact, nullptr, SAME_ROW},
    {"clock off", Id::ClockOff, Form::Exact, nullptr, SAME_ROW},
    {"sound", Id::Sound, Form::Exact, nullptr,
     "show / set the card's volume (off: it only answers a held button, quietly)"},
    {"sound ", Id::SoundSet, Form::Argument, "sound loud / sound quiet / sound off", SAME_ROW},
    {"updates", Id::Updates, Form::Exact, nullptr,
     "show / set how often the card sends the position:\n"
     "locations every 25 s or every 5 s (frequent: for photos on the move)"},
    {"updates ", Id::UpdatesSet, Form::Argument, "updates normal / updates frequent", SAME_ROW},
    {"link on", Id::LinkOn, Form::Exact, nullptr, "work with the paired camera (on after start when paired)"},
    {"link off", Id::LinkOff, Form::Exact, nullptr, SAME_ROW},
    {"bat log on", Id::BatteryLogOn, Form::Exact, nullptr, "battery log in flash every 10 min on the battery"},
    {"bat log off", Id::BatteryLogOff, Form::Exact, nullptr, SAME_ROW},
    {"bat log", Id::BatteryLog, Form::Exact, nullptr, "show / delete the battery log"},
    {"bat log clear", Id::BatteryLogClear, Form::Exact, nullptr, SAME_ROW},
    {"diag bat", Id::DiagBattery, Form::Exact, nullptr, "battery divider settling (about 4 s)"},
    {"diag snd ", Id::DiagSound, Form::Argument, "diag snd N",
     "all sounds at N % PWM duty (1..50), to choose the volume by ear"},
    {"diag hang", Id::DiagHang, Form::Exact, nullptr, "test of the watchdog: the card restarts after 30 s"},
    {"diag scan", Id::DiagScan, Form::Exact, nullptr,
     "30 s dump of nearby advertisements, to see a camera's dialect\n"
     "(it has the camera's serial number, so send the log privately)"},
    {"diag clk", Id::DiagClock, Form::Exact, nullptr,
     "how much of 1 s the 64 MHz crystal runs, now or in S s (kept for 'log')"},
    {"diag clk ", Id::DiagClockLater, Form::Argument, "diag clk S", SAME_ROW},
    {"storage format yes", Id::StorageFormatYes, Form::Exact, nullptr,
     "erase the file system, for a damaged one ('p' says when; the settings are lost,\n"
     "the pairing comes back from its copy; the card restarts)"},
    {"storage format", Id::StorageFormat, Form::Exact, nullptr, HIDDEN},
#ifdef GPXCAM_TEST_COMMANDS
    {"diag storage break yes", Id::DiagStorageBreak, Form::Exact, nullptr, HIDDEN},
    {"diag storage break middle yes", Id::DiagStorageBreakMiddle, Form::Exact, nullptr, HIDDEN},
    {"diag scan stall", Id::DiagScanStall, Form::Exact, nullptr, HIDDEN},
    {"diag gps stale", Id::DiagGpsStale, Form::Exact, nullptr, HIDDEN},
    {"diag gps fresh", Id::DiagGpsFresh, Form::Exact, nullptr, HIDDEN},
#endif
};

// The numbers in the help texts ('log': main.cpp).
static_assert(power::OFF_COUNTDOWN_MS == 15000, "help of 'off'");
static_assert(battery_log::PERIOD_MS == 10 * 60000, "help of 'bat log on'");
static_assert(watchdog::TIMEOUT_S == 30, "help of 'diag hang'");
static_assert(scan_dump::DURATION_MS == 30000, "help of 'diag scan'");

// Removes the spaces at the start and the end in place; returns the start of the text.
char *trimSpaces(char *text)
{
    while (*text == ' ')
        text++;
    size_t length = strlen(text);
    while (length > 0 && text[length - 1] == ' ')
        text[--length] = '\0';
    return text;
}

void toLowerCase(char *text, const char *end)
{
    for (; text < end; text++)
        *text = (char)tolower((unsigned char)*text);
}

// The argument if the line is this entry's command ("" for a command without one), otherwise nullptr.
// The text is matched in any case.
char *argumentOf(const Entry &entry, char *line)
{
    const size_t length = strlen(entry.text);
    if (entry.form == Form::Exact)
        return strcasecmp(line, entry.text) == 0 ? line + length : nullptr;
    if (strncasecmp(line, entry.text, length) != 0)
        return nullptr;
    char *argument = line + length;
    if (entry.form == Form::ArgumentAsTyped)
        while (*argument == ' ')
            argument++;
    return argument;
}

// One line of the help being put together. One character longer than a help line may be, so that
// a text too long for it shows in the test (test/test_console) instead of being cut unseen.
struct HelpLine {
    char text[HELP_LINE_MAX + 2];
    size_t length;
};

void add(HelpLine &line, const char *text, size_t length)
{
    const size_t room = sizeof(line.text) - 1 - line.length;
    const size_t copied = length < room ? length : room;
    memcpy(&line.text[line.length], text, copied);
    line.length += copied;
    line.text[line.length] = '\0';
}

void add(HelpLine &line, const char *text)
{
    add(line, text, strlen(text));
}

constexpr const char *ROW_INDENT = "  ";
constexpr const char *CONTINUATION_INDENT = "    ";

// "  usage / usage = help", the entries from first up to end in one row.
void printRow(const Entry *first, const Entry *end, void (*print)(const char *line))
{
    HelpLine line = {};
    add(line, ROW_INDENT);
    for (const Entry *entry = first; entry < end; entry++) {
        if (entry != first)
            add(line, " / ");
        add(line, entry->usage != nullptr ? entry->usage : entry->text);
    }
    add(line, " = ");
    const char *help = first->help;
    size_t length = strcspn(help, "\n");
    add(line, help, length);
    print(line.text);
    while (help[length] == '\n') {
        help += length + 1;
        length = strcspn(help, "\n");
        line = {};
        add(line, CONTINUATION_INDENT);
        add(line, help, length);
        print(line.text);
    }
}

} // namespace

Command find(char *line)
{
    char *const typed = trimSpaces(line);
    if (typed[0] == '\0')
        return {Id::Empty, typed};
    for (const Entry &entry : ENTRIES) {
        char *const argument = argumentOf(entry, typed);
        if (argument == nullptr)
            continue;
        toLowerCase(typed, entry.form == Form::ArgumentAsTyped ? argument : argument + strlen(argument));
        return {entry.id, argument};
    }
    toLowerCase(typed, typed + strlen(typed));
    return {Id::Unknown, typed};
}

const char *text(Id id)
{
    for (const Entry &entry : ENTRIES)
        if (entry.id == id)
            return entry.text;
    return nullptr;
}

void help(void (*print)(const char *line))
{
    print("commands:");
    const Entry *const end = ENTRIES + sizeof(ENTRIES) / sizeof(ENTRIES[0]);
    for (const Entry *row = ENTRIES; row < end;) {
        const Entry *rowEnd = row + 1;
        while (rowEnd < end && rowEnd->help == SAME_ROW)
            rowEnd++;
        if (row->help[0] != '\0') // not HIDDEN
            printRow(row, rowEnd, print);
        row = rowEnd;
    }
}

} // namespace console_commands
