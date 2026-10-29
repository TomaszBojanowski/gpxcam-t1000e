// gpxcam-t1000e – firmware for the SenseCAP T1000-E that sends the position from its GPS to
// a Panasonic LUMIX S5II over Bluetooth LE: start-up, the main loop, the serial commands
// and the LED. Copyright (C) 2026 Tomasz Bojanowski. License: GPL-3.0-only (see LICENSE).
#include <Arduino.h>
// The core always uses Serial (TinyUSB CDC, included by Arduino.h). This include lets
// PlatformIO's library finder link the Adafruit TinyUSB library; it does not follow
// includes inside core headers.
#include <Adafruit_TinyUSB.h>

#include "battery.h"
#include "battery_log.h"
#include "battery_rules.h"
#include "bluetooth.h"
#include "board.h"
#include "button.h"
#include "camera_link.h"
#include "console_commands.h"
#include "diag.h"
#include "gps.h"
#include "link_control.h"
#include "link_policy.h"
#include "log.h"
#include "pairing.h"
#include "pairing_store.h"
#include "power.h"
#include "settings_store.h"
#include "sound.h"
#include "status_text.h"
#include "storage.h"
#include "tz_record.h"
#include "tz_store.h"
#include "usb_power.h"
#include "version.h"
#include "watchdog.h"

namespace {

constexpr uint32_t LED_PERIOD_MS = 1000;      // heartbeat: one short flash per second
constexpr uint32_t LED_ON_MS = 100;
constexpr uint32_t COUNTDOWN_LED_PERIOD_MS = 200; // fast blinking while 'off' counts down
// Link: looking for the camera - two short flashes every 2 s; connected -
// one short flash every 4 s.
constexpr uint32_t LINK_FLASH_MS = 50;
constexpr uint32_t SEARCH_LED_PERIOD_MS = 2000;
constexpr uint32_t SEARCH_SECOND_FLASH_MS = 200; // the second flash starts here
constexpr uint32_t CONNECTED_LED_PERIOD_MS = 4000;
constexpr uint32_t STATUS_PERIOD_MS = 10000;  // version/uptime line in the log
constexpr uint32_t BATTERY_PERIOD_MS = 60000; // battery line in the log
constexpr uint32_t SERIAL_WAIT_MS = 2000;     // at start: wait this long for a terminal
constexpr uint32_t DFU_MESSAGE_DELAY_MS = 200; // let the last log line leave before reset
constexpr uint32_t LOOP_DELAY_MS = 10;        // loop period; the CPU sleeps in between
                                              // (FreeRTOS tickless idle, configUSE_TICKLESS_IDLE in the core)

// Longest accepted command, longer input is rejected: "tz", a space and a POSIX rule as long
// as the time zone file holds (63 characters).
constexpr size_t COMMAND_MAX = (sizeof(console_commands::TIME_ZONE_SET) - 1) + (tz_record::RULE_SIZE - 1);
static_assert(COMMAND_MAX == 66, "tz with the longest rule");

// Terminal editing keys. PlatformIO's send_on_enter filter sends the typed line as is,
// including these, so a corrected typo arrives as text + 0x7F + text.
constexpr int KEY_BACKSPACE = 0x08;
constexpr int KEY_DELETE = 0x7F;
constexpr int FIRST_PRINTABLE = 0x20; // space
constexpr int LAST_PRINTABLE = 0x7E;  // '~'

// Terminal escape sequences (arrow keys, replies such as ESC [ c), ECMA-48: ESC followed
// by one character, or ESC '[' then parameter bytes and one final byte 0x40..0x7E.
constexpr int KEY_ESCAPE = 0x1B;
constexpr int CSI_INTRODUCER = '[';
constexpr int CSI_FINAL_FIRST = 0x40;
constexpr int CSI_FINAL_LAST = 0x7E;
enum class Escape { None, Started, Sequence };
Escape escape = Escape::None;

char command[COMMAND_MAX + 1];
size_t commandLen = 0;
bool commandTooLong = false;

// The periodic status line is not kept in the log history (it would push the
// events out); the version shown at start and with 'v' is.
void logVersion(bool keepInHistory)
{
    // No build date/time: the same source must give the same UF2 image.
    char text[logger::LINE_MAX];
    snprintf(text, sizeof(text), "%s %s (git %s), uptime %lu s, log dropped %lu", FW_NAME, FW_VERSION,
             FW_GIT, (unsigned long)(millis() / 1000), (unsigned long)logger::dropped());
    if (keepInHistory)
        logger::line("%s", text);
    else
        logger::transient("%s", text);
}

bool ledLit(uint32_t now) // not ledOn(): the core has one
{
    if (power::countdownActive())
        return now % COUNTDOWN_LED_PERIOD_MS < LED_ON_MS;
    switch (link_control::indication()) {
    case link_control::Indication::Connected:
        return now % CONNECTED_LED_PERIOD_MS < LINK_FLASH_MS;
    case link_control::Indication::Searching: {
        const uint32_t t = now % SEARCH_LED_PERIOD_MS;
        return t < LINK_FLASH_MS || (t >= SEARCH_SECOND_FLASH_MS && t < SEARCH_SECOND_FLASH_MS + LINK_FLASH_MS);
    }
    case link_control::Indication::Off:
        break;
    }
    return now % LED_PERIOD_MS < LED_ON_MS;
}

// At start and every 60 s the reading also goes to the low-battery check; the one of
// 'p' does not, so it cannot add to the count of low readings.
enum class BatteryCheck { None, AtStart, Periodic };

void logBattery(BatteryCheck check)
{
    const battery::Status status = battery::read();
    // In the history at start only; every 60 s the battery log ('bat log') keeps the record.
    if (check == BatteryCheck::AtStart)
        logger::line("battery: %lu mV, %s", (unsigned long)status.batteryMv, battery::chargerText(status.charger));
    else
        logger::transient("battery: %lu mV, %s", (unsigned long)status.batteryMv, battery::chargerText(status.charger));
    if (check != BatteryCheck::None)
        power::checkBattery(status.batteryMv, status.charger != battery::Charger::NoUsb,
                            check == BatteryCheck::AtStart);
}

static_assert(status_text::LINE_SIZE == logger::LINE_MAX - 2, "a status line is one log line");

// 'status': the state for the card page as key=value lines (status_text.h). Not in the
// history: the page asks every few seconds.
void logStatus()
{
    const battery::Status battery = battery::read();
    status_text::State state = {};
    state.version = FW_VERSION;
    state.git = FW_GIT;
    state.watchdogRestart = watchdog::restartedLastTime();
    state.storage = storage::stateName();
    state.batteryMillivolts = battery.batteryMv;
    state.batteryPercent = battery_rules::percentFromMillivolts(battery.batteryMv);
    state.charger = battery.charger;
    state.gpsOn = gps::isOn();
    state.gpsFix = gps::hasFix();
    state.gpsSatellites = (uint8_t)max(0, gps::satellites());
    state.gpsHoldSeconds = pairing::gpsHoldSeconds();
    state.paired = pairing_store::ready() && pairing_store::record().paired;
    state.link = link_control::state();
    state.pairing = pairing::progress();
    state.clockOn = tz_store::clockOn();
    state.timeZone = tz_store::zoneText();
    state.sound = settings_store::sound();
    state.updates = settings_store::updates();
    state.updatesNow = link_control::updatesNow();
    status_text::format(state, [](const char *line) { logger::transient("%s", line); });
}

// The RAM left, for 'p' (Adafruit core utility/debug.cpp): the heap is malloc's, from the end of
// .bss to the main stack, and the FreeRTOS task stacks come from it; the loop task's stack never
// used is its high water mark (FreeRTOS, INCLUDE_uxTaskGetStackHighWaterMark); the main stack is
// the interrupts', painted with a pattern at start-up (gcc_startup_nrf52840.S).
void logMemory()
{
    logger::transient("memory: heap %d B free of %d B, loop stack %lu B never used, main stack %d B used of %d B",
                      dbgHeapTotal() - dbgHeapUsed(), dbgHeapTotal(),
                      (unsigned long)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)), dbgStackUsed(),
                      dbgStackTotal());
}

static_assert(console_commands::HELP_LINE_MAX == logger::TEXT_MAX, "a help line is one log line");
static_assert(logger::HISTORY_LINES == 64, "the help of 'log' (console_commands.cpp)");

void logHelp()
{
    console_commands::help([](const char *line) { logger::transient("%s", line); });
}

void rebootToUf2Bootloader()
{
    // The command came over USB, so USB works: a safe moment for the entries still in RAM,
    // which wait for USB_STABLE_MS after the cable is plugged in.
    battery_log::save();
    logger::line("dfu: rebooting to the UF2 bootloader (drive T1000-E)");
    delay(DFU_MESSAGE_DELAY_MS);
    // Core (cores/nRF5/wiring.c): disables the SoftDevice, sets GPREGRET to
    // DFU_MAGIC_UF2_RESET (0x57) and resets. The bootloader then starts with
    // the USB drive. Flash is not changed. Does not return.
    enterUf2Dfu();
}

// "diag clk S": the crystal check in S seconds.
void runClockCheck(const char *argument)
{
    char *end = nullptr;
    const unsigned long delayS = strtoul(argument, &end, 10);
    if (end == argument || *end != '\0' || delayS > diag::CLOCK_CHECK_MAX_DELAY_S)
        logger::line("diag: usage: diag clk [S], S = 0..%lu (seconds before the check)",
                     (unsigned long)diag::CLOCK_CHECK_MAX_DELAY_S);
    else
        diag::checkClock((uint32_t)delayS);
}

// "diag snd N": plays all sounds at N % PWM duty to choose the volume by ear.
void runSoundTest(const char *argument)
{
    char *end = nullptr;
    const unsigned long duty = strtoul(argument, &end, 10);
    if (end == argument || *end != '\0' || duty > UINT8_MAX || !sound::playTest((uint8_t)duty))
        logger::line("sound: usage: diag snd N, N = 1..50 (%% PWM duty)");
    else
        logger::line("sound: test at %lu %% duty: click, start, shutdown, pairing, paired, connected, disconnected",
                     duty);
}

void forgetCamera()
{
    if (!pairing_store::ready()) {
        logger::line("pair: not available");
        return;
    }
    if (!pairing_store::record().paired) {
        logger::line("pair: no camera paired");
        return;
    }
    if (!pairing_store::writable()) {
        logger::line("%s", pairing_store::NOT_WRITABLE_LINE);
        return;
    }
    link_control::stop(false); // it would go on with the forgotten camera
    pairing::keepLinkOff();    // and a pairing that runs would start it again
    if (pairing_store::forgetCamera())
        logger::line("pair: camera forgotten (the card id stays)");
    else
        logger::line("pair: could not write the pairing file");
}

// The camera gets the time zone with the time when the card connects to it (camera_session, older_session).
void setTimeZone(const char *nameOrRule)
{
    switch (tz_store::set(nameOrRule)) {
    case tz_store::SetResult::Saved:
        logger::line("tz: saved - the camera gets it at the next connection");
        if (!tz_store::clockOn())
            logger::transient("tz: the clock is off - the card sends the zone only after 'clock on'");
        break;
    case tz_store::SetResult::Unchanged:
        logger::line("tz: already set");
        break;
    case tz_store::SetResult::Unknown:
        logger::line("tz: unknown time zone or rule '%s'", nameOrRule);
        logger::transient("tz: e.g. tz Europe/Warsaw, tz America/New_York, tz Asia/Kolkata");
        return;
    case tz_store::SetResult::NotSaved:
        logger::line("tz: could not write the time zone file");
        return;
    }
    tz_store::logState();
}

void logClock()
{
    if (tz_store::clockOn())
        logger::transient("clock: on - the card sets the camera's clock and time zone ('tz') when it connects");
    else
        logger::transient("clock: off - the card leaves the camera's clock alone; set it in the camera's menu");
}

// Whether the card sets the camera's clock; it counts from the next connection.
void setClock(bool on)
{
    switch (tz_store::setClock(on)) {
    case tz_store::SetResult::Saved:
        logger::line("clock: %s saved - from the next connection", on ? "on" : "off");
        break;
    case tz_store::SetResult::Unchanged:
        logger::line("clock: already %s", on ? "on" : "off");
        break;
    case tz_store::SetResult::Unknown: // not returned by setClock()
    case tz_store::SetResult::NotSaved:
        logger::line("clock: could not write the time zone file");
        return;
    }
    logClock();
}

// What 'off' still plays (sound_rules.h): without it the user cannot tell when to let go.
void logSoundOffNote()
{
    logger::transient("sound: off - the card only answers a held button (switching on and off, pairing), quietly");
}

void logSound()
{
    settings_store::logSound();
    if (settings_store::sound() == sound_rules::Setting::Off)
        logSoundOffNote();
}

// "sound loud|quiet|off": saved at once and in effect for the next sound; a sample plays at
// the new volume.
void setSound(const char *name)
{
    sound_rules::Setting setting;
    if (!sound_rules::settingFromName(name, &setting)) {
        logger::line("sound: unknown setting '%s' - sound loud, sound quiet or sound off", name);
        return;
    }
    switch (settings_store::setSound(setting)) {
    case settings_store::SetResult::Saved:
        logger::line("sound: %s saved", name);
        break;
    case settings_store::SetResult::Unchanged:
        logger::line("sound: already %s", name);
        break;
    case settings_store::SetResult::NotSaved:
        logger::line("sound: could not write the settings file");
        return;
    }
    sound::setSetting(settings_store::sound());
    if (setting == sound_rules::Setting::Off)
        logSoundOffNote();
    else
        sound::playConnected(); // the sample: two short beeps
}

void logUpdates()
{
    settings_store::logUpdates();
    if (settings_store::updates() == link_policy::Updates::Frequent)
        logger::transient("updates: normal for the rest of a connection with %u camera answers over %lu ms, "
                          "and for the one after a broken one",
                          (unsigned)link_policy::LATE_ANSWERS_TO_SLOW_DOWN, (unsigned long)link_policy::LATE_ANSWER_MS);
}

// "updates normal|frequent": saved at once and in effect for the next location, also in the
// connection under way.
void setUpdates(const char *name)
{
    link_policy::Updates updates;
    if (!link_policy::updatesFromName(name, &updates)) {
        logger::line("updates: unknown setting '%s' - updates normal or updates frequent", name);
        return;
    }
    switch (settings_store::setUpdates(updates)) {
    case settings_store::SetResult::Saved:
        logger::line("updates: %s saved", name);
        break;
    case settings_store::SetResult::Unchanged:
        logger::line("updates: already %s", name);
        break;
    case settings_store::SetResult::NotSaved:
        logger::line("updates: could not write the settings file");
        return;
    }
    logUpdates();
}

void handleButton()
{
    const button::Event event = button::poll();
    if (event.type == button::Type::ShortPress) {
        logger::line("button: short press (%lu ms)", (unsigned long)event.heldMs);
        power::cancelCountdown(); // does nothing without a countdown
        sound::playClick();
        pairing::stop();          // does nothing without a session
    } else if (event.type == button::Type::ClickHold) {
        logger::line("button: click + hold (%lu ms) - pairing", (unsigned long)event.heldMs);
        pairing::start();
    } else if (event.type == button::Type::LongPress) {
        logger::line("button: long press (%lu ms) - powering off", (unsigned long)event.heldMs);
        power::powerOff(); // plays the shutdown melody; returns only if refused (e.g. USB)
    }
}

// 'p': the state of every part of the card, for a person reading it.
void logCardState()
{
    storage::logState();
    board::logPowerState();
    gps::logState();
    bluetooth::logState();
    pairing_store::logState();
    tz_store::logState();
    settings_store::logState();
    pairing::logState();
    link_control::logState();
    watchdog::logState();
    usb_power::logState();
    logBattery(BatteryCheck::None);
    logMemory();
}

void stopLink()
{
    if (link_control::active())
        link_control::stop(false);
    else if (pairing::keepLinkOff())
        logger::line("link: off - it stays off after the pairing");
    else
        logger::line("link: already off");
}

void stopPairing()
{
    if (pairing::active())
        pairing::stop();
    else
        logger::line("pair: not running");
}

// The commands and their help: src/console_commands.cpp.
void runCommand(char *line)
{
    using console_commands::Id;
    const console_commands::Command found = console_commands::find(line);
    switch (found.id) {
    case Id::Empty:
        break;
    case Id::Unknown:
        logger::line("command: unknown '%s'", found.argument);
        logHelp();
        break;
    case Id::Version:
        logVersion(true);
        diag::logResetInfo();
        break;
    case Id::State:
        logCardState();
        break;
    case Id::Status:
        logStatus();
        break;
    case Id::Log:
        logger::dumpHistory();
        break;
    case Id::Off:
        power::startCountdown();
        break;
    case Id::Dfu:
        rebootToUf2Bootloader();
        break;
    case Id::Pair:
        pairing::start();
        break;
    case Id::PairOff:
        stopPairing();
        break;
    case Id::PairOlder:
        pairing::startOlder();
        break;
    case Id::PairPrepare:
        pairing::prepare();
        break;
    case Id::Unpair:
        forgetCamera();
        break;
    case Id::TimeZone:
        tz_store::logState();
        break;
    case Id::TimeZoneSet:
        setTimeZone(found.argument);
        break;
    case Id::Clock:
        logClock();
        break;
    case Id::ClockOn:
        setClock(true);
        break;
    case Id::ClockOff:
        setClock(false);
        break;
    case Id::Sound:
        logSound();
        break;
    case Id::SoundSet:
        setSound(found.argument);
        break;
    case Id::Updates:
        logUpdates();
        break;
    case Id::UpdatesSet:
        setUpdates(found.argument);
        break;
    case Id::LinkOn:
        link_control::start();
        break;
    case Id::LinkOff:
        stopLink();
        break;
    case Id::BatteryLogOn:
        battery_log::start();
        break;
    case Id::BatteryLogOff:
        battery_log::stop();
        break;
    case Id::BatteryLog:
        battery_log::dump();
        break;
    case Id::BatteryLogClear:
        battery_log::clear();
        break;
    case Id::DiagBattery:
        diag::battery();
        break;
    case Id::DiagSound:
        runSoundTest(found.argument);
        break;
    case Id::DiagHang:
        diag::hang();
        break;
    case Id::DiagScan:
        bluetooth::startScanDump();
        break;
    case Id::DiagClock:
        diag::checkClock(0);
        break;
    case Id::DiagClockLater:
        runClockCheck(found.argument);
        break;
    case Id::StorageFormat:
        logger::line("storage: 'storage format yes' erases the file system and restarts (the pairing comes back "
                     "from its copy)");
        break;
    case Id::StorageFormatYes:
        storage::format();
        break;
#ifdef GPXCAM_TEST_COMMANDS
    case Id::DiagStorageBreak:
        diag::breakFileSystem();
        break;
    case Id::DiagStorageBreakMiddle:
        diag::breakFileSystemPage();
        break;
    case Id::DiagScanStall:
        bluetooth::stallScan();
        break;
    case Id::DiagGpsStale:
        gps::setStaleForTest(true);
        break;
    case Id::DiagGpsFresh:
        gps::setStaleForTest(false);
        break;
#endif
    }
}

void finishCommand()
{
    command[commandLen] = '\0';
    if (commandTooLong)
        logger::line("command: too long (max %u characters)", (unsigned)COMMAND_MAX);
    else
        runCommand(command);
    commandLen = 0;
    commandTooLong = false;
}

// True while the character belongs to a terminal escape sequence (and is skipped).
bool inEscapeSequence(int c)
{
    switch (escape) {
    case Escape::Started:
        escape = (c == CSI_INTRODUCER) ? Escape::Sequence : Escape::None;
        return true;
    case Escape::Sequence:
        if (c >= CSI_FINAL_FIRST && c <= CSI_FINAL_LAST)
            escape = Escape::None;
        return true;
    case Escape::None:
        break;
    }
    if (c != KEY_ESCAPE)
        return false;
    escape = Escape::Started;
    return true;
}

// CR, LF and CRLF all end a command (also inside a broken escape sequence).
// Backspace/Delete remove the last character; escape sequences, other control
// characters and non-ASCII bytes are ignored.
void handleChar(int c)
{
    if (c == '\r' || c == '\n') {
        escape = Escape::None;
        finishCommand();
    } else if (inEscapeSequence(c)) {
        return;
    } else if (c == KEY_BACKSPACE || c == KEY_DELETE) {
        if (commandLen > 0)
            commandLen--;
    } else if (c >= FIRST_PRINTABLE && c <= LAST_PRINTABLE) {
        if (commandLen < COMMAND_MAX)
            command[commandLen++] = (char)c;
        else
            commandTooLong = true;
    }
}

void readSerial()
{
    while (Serial.available() > 0)
        handleChar(Serial.read());
}

} // namespace

void setup()
{
    board::init();         // all peripherals off; takes microseconds
    watchdog::begin();     // from here on a hang restarts the card
    power::handleWakeUp(); // may go straight back to System OFF after a short button press
    board::sleepRadio();   // about 0.27 s
    storage::begin();      // before Bluefruit, which would format a file system it cannot mount
    // A few ms; up to about 1 s on the very first start.
    const bool bleReady = storage::safeForBluefruit() && bluetooth::begin();
    button::begin();
    settings_store::begin(storage::ready()); // reads a small file: a few ms
    // Before the start sound: the notes change in sound::poll(), so the one-time move of a 1.3
    // log (flash writes, done at 1.0 s after the start on the card) would hold its first note.
    // Otherwise it only reads: a few ms.
    battery_log::begin(storage::ready());
    sound::setSetting(settings_store::sound());
    const bool soundReady = sound::begin();
    sound::playStart(); // right away: after a wake-up it tells the user to let go

    Serial.begin(115200); // USB CDC: the baud rate is ignored
    // Without USB no terminal can connect, so there is nothing to wait for.
    const uint32_t start = millis();
    while (battery::usbPowered() && !Serial && millis() - start < SERIAL_WAIT_MS) {
        sound::poll();
        delay(LOOP_DELAY_MS);
    }

    logVersion(true);
    if (!soundReady)
        logger::line("sound: PWM3 is in use, buzzer disabled");
    if (bleReady)
        logger::line("ble: SoftDevice on (central role, not scanning)");
    else
        logger::line("ble: SoftDevice did not start, BLE off");
    diag::logResetInfo();
    storage::logStart();
    board::logPowerState();
    gps::logState();
    logBattery(BatteryCheck::AtStart); // a flat battery powers off here, before GPS and radio start
    // After the start logs: the very first start writes flash here.
    if (bleReady && pairing_store::begin())
        pairing_store::logState();
    tz_store::begin(storage::ready());
    tz_store::logState();
    settings_store::logState();
    link_control::begin(); // starts when a camera is paired
    logHelp();
}

void loop()
{
    static uint32_t lastStatus = 0;
    static uint32_t lastBattery = millis(); // measured once in setup()
    const uint32_t now = millis();

    watchdog::feed();
    board::led(ledLit(now));

    if (now - lastStatus >= STATUS_PERIOD_MS) {
        lastStatus = now;
        logVersion(false);
    }
    if (now - lastBattery >= BATTERY_PERIOD_MS) {
        lastBattery = now;
        logBattery(BatteryCheck::Periodic);
    }

    readSerial();
    gps::poll();
    bluetooth::poll();
    camera_link::poll();
    pairing::poll();
    link_control::poll();
    battery_log::poll();
    handleButton();
    power::poll();
    diag::poll();
    sound::poll();
    delay(LOOP_DELAY_MS);
}
