// gpxcam-t1000e – M0 skeleton: peripherals off, LED heartbeat, version log,
// serial commands. Copyright (C) 2026 Tomasz Bojanowski. License: GPL-3.0 (see LICENSE).
#include <Arduino.h>
// The core always uses Serial (TinyUSB CDC, included by Arduino.h). This include lets
// PlatformIO's library finder link the Adafruit TinyUSB library; it does not follow
// includes inside core headers.
#include <Adafruit_TinyUSB.h>

#include "board.h"
#include "diag.h"
#include "log.h"
#include "version.h"

namespace {

constexpr uint32_t LED_PERIOD_MS = 1000;      // heartbeat: one short flash per second
constexpr uint32_t LED_ON_MS = 100;
constexpr uint32_t STATUS_PERIOD_MS = 10000;  // version/uptime line in the log
constexpr uint32_t SERIAL_WAIT_MS = 2000;     // at start: wait this long for a terminal
constexpr uint32_t DFU_MESSAGE_DELAY_MS = 200; // let the last log line leave before reset
constexpr uint32_t LOOP_DELAY_MS = 10;        // loop period; the CPU sleeps in between
                                              // (FreeRTOS tickless idle, configUSE_TICKLESS_IDLE in the core)

constexpr size_t COMMAND_MAX = 16; // longest accepted command, longer input is rejected

char command[COMMAND_MAX + 1];
size_t commandLen = 0;
bool commandTooLong = false;

void logVersion()
{
    // No build date/time: the same source must give the same UF2 image.
    logger::line("%s %s (git %s), uptime %lu s, log dropped %lu", FW_NAME, FW_VERSION, FW_GIT,
                 (unsigned long)(millis() / 1000), (unsigned long)logger::dropped());
}

void logHelp()
{
    logger::line("commands: v = version and reset reason, p = power state, dfu = UF2 bootloader");
    logger::line("diagnostics: diag btn (15 s), diag bat, diag p004 - read-only");
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

void runCommand(const char *cmd)
{
    if (strcmp(cmd, "v") == 0) {
        logVersion();
        diag::logResetInfo();
    } else if (strcmp(cmd, "p") == 0) {
        board::logPowerState();
    } else if (strcmp(cmd, "dfu") == 0) {
        rebootToUf2Bootloader();
    } else if (strcmp(cmd, "diag btn") == 0) {
        diag::startButton();
    } else if (strcmp(cmd, "diag bat") == 0) {
        diag::battery();
    } else if (strcmp(cmd, "diag p004") == 0) {
        diag::p004();
    } else if (cmd[0] != '\0') {
        logger::line("unknown command '%s'", cmd);
        logHelp();
    }
}

// Collects characters into a command; runs it on CR or LF.
void readSerial()
{
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c == '\r' || c == '\n') {
            command[commandLen] = '\0';
            if (commandTooLong)
                logger::line("command too long (max %u characters)", (unsigned)COMMAND_MAX);
            else
                runCommand(command);
            commandLen = 0;
            commandTooLong = false;
        } else if (commandLen < COMMAND_MAX) {
            command[commandLen++] = (char)tolower(c);
        } else {
            commandTooLong = true;
        }
    }
}

} // namespace

void setup()
{
    board::init();

    Serial.begin(115200); // USB CDC: the baud rate is ignored
    const uint32_t start = millis();
    while (!Serial && millis() - start < SERIAL_WAIT_MS)
        delay(LOOP_DELAY_MS);

    logVersion();
    diag::logResetInfo();
    board::logPowerState();
    logHelp();
}

void loop()
{
    static uint32_t lastStatus = 0;
    const uint32_t now = millis();

    board::led(now % LED_PERIOD_MS < LED_ON_MS);

    if (now - lastStatus >= STATUS_PERIOD_MS) {
        lastStatus = now;
        logVersion();
    }

    readSerial();
    diag::poll();
    delay(LOOP_DELAY_MS);
}
