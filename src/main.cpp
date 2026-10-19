// gpxcam-t1000e – firmware for the SenseCAP T1000-E that sends the position from its GPS to
// a Panasonic LUMIX S5II over Bluetooth LE: start-up, the main loop, the serial commands
// and the LED. Copyright (C) 2026 Tomasz Bojanowski. License: GPL-3.0 (see LICENSE).
#include <Arduino.h>
// The core always uses Serial (TinyUSB CDC, included by Arduino.h). This include lets
// PlatformIO's library finder link the Adafruit TinyUSB library; it does not follow
// includes inside core headers.
#include <Adafruit_TinyUSB.h>

#include "battery.h"
#include "battery_log.h"
#include "bluetooth.h"
#include "board.h"
#include "button.h"
#include "diag.h"
#include "gps.h"
#include "link_control.h"
#include "log.h"
#include "pairing.h"
#include "pairing_store.h"
#include "power.h"
#include "sound.h"
#include "version.h"

namespace {

constexpr uint32_t LED_PERIOD_MS = 1000;      // heartbeat: one short flash per second
constexpr uint32_t LED_ON_MS = 100;
constexpr uint32_t COUNTDOWN_LED_PERIOD_MS = 200; // fast blinking while 'off' counts down
// Link (M5 decision): looking for the camera - two short flashes every 2 s; connected -
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

constexpr size_t COMMAND_MAX = 16; // longest accepted command, longer input is rejected

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

// At start and every 60 s the reading also goes to the low-battery check (M6); the one of
// 'p' does not, so it cannot add to the count of low readings.
enum class BatteryCheck { None, AtStart, Periodic };

void logBattery(BatteryCheck check)
{
    const battery::Status status = battery::read();
    logger::line("battery: %lu mV, %s", (unsigned long)status.batteryMv, battery::chargerText(status.charger));
    if (check != BatteryCheck::None)
        power::checkBattery(status.batteryMv, status.charger != battery::Charger::NoUsb,
                            check == BatteryCheck::AtStart);
}

void logHelp()
{
    logger::transient("commands: v = version and reset reason, p = power state, dfu = UF2 bootloader,");
    logger::transient("          off = System OFF after a 15 s countdown (refused with USB connected),");
    logger::transient("          log = last %lu log lines, also those from times without USB,",
                      (unsigned long)logger::HISTORY_LINES);
    logger::transient("          link on / link off = work with the paired camera (on after start when paired),");
    logger::transient("          pair = pair with a camera in pairing mode (or click + hold 3 s), pair off = cancel,");
    logger::transient("          unpair = forget the paired camera ('p' shows the card id and the camera)");
    logger::transient("          bat log on / bat log off = battery log in flash every 10 min on the battery,");
    logger::transient("          bat log = show it, bat log clear = delete it");
    logger::transient("diagnostics: diag bat (battery divider settling, ~4 s), diag snd 1..50 (volume, %% duty)");
}

void rebootToUf2Bootloader()
{
    logger::line("rebooting to UF2 bootloader (drive T1000-E)...");
    delay(DFU_MESSAGE_DELAY_MS);
    // Core (cores/nRF5/wiring.c): disables the SoftDevice, sets GPREGRET to
    // DFU_MAGIC_UF2_RESET (0x57) and resets. The bootloader then starts with
    // the USB drive. Flash is not changed. Does not return.
    enterUf2Dfu();
}

constexpr const char *SOUND_TEST_COMMAND = "diag snd ";

// "diag snd N": plays all sounds at N % PWM duty to choose the volume by ear.
void runSoundTest(const char *argument)
{
    char *end = nullptr;
    const unsigned long duty = strtoul(argument, &end, 10);
    if (end == argument || *end != '\0' || duty > UINT8_MAX || !sound::playTest((uint8_t)duty))
        logger::line("usage: diag snd N, N = 1..50 (%% PWM duty)");
    else
        logger::line("sound test at %lu %% duty: click, start, shutdown, pairing, paired, connected, disconnected",
                     duty);
}

void forgetCamera()
{
    if (!pairing_store::ready()) {
        logger::line("pairing: not available");
        return;
    }
    if (!pairing_store::record().paired) {
        logger::line("pairing: no camera paired");
        return;
    }
    link_control::stop(false); // it would go on with the forgotten camera
    if (pairing_store::forgetCamera())
        logger::line("pairing: camera forgotten (the card id stays)");
    else
        logger::line("pairing: could not write the pairing file");
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

void runCommand(const char *cmd)
{
    if (strcmp(cmd, "v") == 0) {
        logVersion(true);
        diag::logResetInfo();
    } else if (strcmp(cmd, "p") == 0) {
        board::logPowerState();
        gps::logState();
        bluetooth::logState();
        pairing_store::logState();
        pairing::logState();
        link_control::logState();
        logBattery(BatteryCheck::None);
    } else if (strcmp(cmd, "dfu") == 0) {
        rebootToUf2Bootloader();
    } else if (strcmp(cmd, "off") == 0) {
        power::startCountdown();
    } else if (strcmp(cmd, "log") == 0) {
        logger::dumpHistory();
    } else if (strcmp(cmd, "link on") == 0) {
        link_control::start();
    } else if (strcmp(cmd, "link off") == 0) {
        if (link_control::active())
            link_control::stop(false);
        else
            logger::line("link: already off");
    } else if (strcmp(cmd, "pair") == 0) {
        pairing::start();
    } else if (strcmp(cmd, "pair off") == 0) {
        if (pairing::active())
            pairing::stop();
        else
            logger::line("pair: not running");
    } else if (strcmp(cmd, "unpair") == 0) {
        forgetCamera();
    } else if (strcmp(cmd, "bat log on") == 0) {
        battery_log::start();
    } else if (strcmp(cmd, "bat log off") == 0) {
        battery_log::stop();
    } else if (strcmp(cmd, "bat log") == 0) {
        battery_log::dump();
    } else if (strcmp(cmd, "bat log clear") == 0) {
        battery_log::clear();
    } else if (strncmp(cmd, SOUND_TEST_COMMAND, strlen(SOUND_TEST_COMMAND)) == 0) {
        runSoundTest(cmd + strlen(SOUND_TEST_COMMAND));
    } else if (strcmp(cmd, "diag bat") == 0) {
        diag::battery();
    } else if (cmd[0] != '\0') {
        logger::line("unknown command '%s'", cmd);
        logHelp();
    }
}

// Removes leading and trailing spaces in place; returns the start of the text.
char *trimSpaces(char *text)
{
    while (*text == ' ')
        text++;
    size_t len = strlen(text);
    while (len > 0 && text[len - 1] == ' ')
        text[--len] = '\0';
    return text;
}

void finishCommand()
{
    command[commandLen] = '\0';
    if (commandTooLong)
        logger::line("command too long (max %u characters)", (unsigned)COMMAND_MAX);
    else
        runCommand(trimSpaces(command)); // an empty line (e.g. the LF of CRLF) does nothing
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
            command[commandLen++] = (char)tolower(c);
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
    power::handleWakeUp(); // may go straight back to System OFF after a short button press
    board::sleepRadio();   // about 0.27 s
    const bool bleReady = bluetooth::begin(); // a few ms; up to about 1 s on the very first start
    button::begin();
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
    board::logPowerState();
    gps::logState();
    logBattery(BatteryCheck::AtStart); // a flat battery powers off here, before GPS and radio start
    // After the start logs: the very first start writes flash here.
    if (bleReady && pairing_store::begin())
        pairing_store::logState();
    link_control::begin(); // starts when a camera is paired
    logHelp();
}

void loop()
{
    static uint32_t lastStatus = 0;
    static uint32_t lastBattery = millis(); // measured once in setup()
    const uint32_t now = millis();

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
    pairing::poll();
    link_control::poll();
    battery_log::poll();
    handleButton();
    power::poll();
    sound::poll();
    delay(LOOP_DELAY_MS);
}
