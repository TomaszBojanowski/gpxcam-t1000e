#include "gps_setup.h"

#include <Arduino.h>
#include <stdio.h>

#include "gps.h"
#include "log.h"
#include "nmea.h"

// Commands from the Quectel LC26G/LC76G/LC86G Series GNSS Protocol Specification V1.4
// (modules built on Airoha AG3335/AG3352):
//   2.4.1   $PAIR001,<CommandID>,<Result>  acknowledgement: 0 = done, 1 = in progress, 2..5 = failed
//   2.4.9   $PAIR011,001                   GNSS system started
//   2.4.14  $PAIR062,<Type>,<Rate>         set NMEA output rate (0 = off, N = every N fixes)
//   2.4.15  $PAIR063,-1                    get all output rates; replies $PAIR063,<Type>,<Rate>
//   2.4.16  $PAIR066,GPS,GLO,GAL,BDS,QZSS,0 set constellations - the module reboots
//   2.4.17  $PAIR067                       get constellations; replies $PAIR067,GPS,GLO,GAL,BDS,QZSS,0
// Settings live in the GPS RTC RAM, kept by the backup supply (2.4.49: $PAIR513 copies
// them from RTC RAM to flash - never sent here).

namespace {

constexpr uint32_t BOOT_TIMEOUT_MS = 3000;  // $PAIR011,001 arrives about 0.3 s after power-on
constexpr uint32_t REPLY_TIMEOUT_MS = 1000;
constexpr int MAX_ATTEMPTS = 3;

constexpr long ACK_DONE = 0;
constexpr long ACK_IN_PROGRESS = 1;
constexpr long ACK_RESULT_MAX = 5;
constexpr long STARTUP_NOTIFICATION = 1;
constexpr long COMMAND_SET_RATE = 62; // $PAIR062, as reported in $PAIR001
constexpr long COMMAND_SET_MODE = 66; // $PAIR066

constexpr int NMEA_TYPES = 10; // $PAIR062/063 types 0..9
const char *const TYPE_NAMES[NMEA_TYPES] = {"GGA", "GLL", "GSA", "GSV", "RMC", "VTG", "ZDA", "GRS", "GST", "GNS"};
constexpr long WANTED_RATE[NMEA_TYPES] = {1, 0, 0, 0, 1, 0, 0, 0, 0, 0}; // GGA and RMC every fix
constexpr long MAX_RATE = 20;

// GPS, GLONASS, Galileo, BeiDou (M2 decision). QZSS (field 5) is not compared.
constexpr int COMPARED_SYSTEMS = 4;
constexpr const char *SET_MODE = "PAIR066,1,1,1,1,0,0"; // spec example, also used by Meshtastic

enum class Step { Idle, WaitBoot, QueryRates, SetRate, QueryMode, SetMode, Done };

Step step = Step::Idle;
uint32_t deadline = 0;
int attempts = 0;
long rates[NMEA_TYPES];
bool rateKnown[NMEA_TYPES];
int rateType = 0; // type being set in Step::SetRate
int changedRates = 0;
bool modeSent = false; // $PAIR066 at most once per power-on

void wait(Step next, uint32_t timeoutMs)
{
    step = next;
    deadline = millis() + timeoutMs;
}

void send(Step next, const char *body)
{
    attempts++;
    gps::sendCommand(body);
    wait(next, REPLY_TIMEOUT_MS);
}

void finish(const char *summary)
{
    step = Step::Done;
    logger::line("gps: setup done - %s", summary);
}

void queryMode()
{
    attempts = 0;
    send(Step::QueryMode, "PAIR067");
}

// Next output rate that differs from the wanted one, or NMEA_TYPES.
int nextRateToSet(int from)
{
    for (int t = from; t < NMEA_TYPES; t++)
        if (rateKnown[t] && rates[t] != WANTED_RATE[t])
            return t;
    return NMEA_TYPES;
}

void sendRate()
{
    char body[24];
    snprintf(body, sizeof(body), "PAIR062,%d,%ld", rateType, WANTED_RATE[rateType]);
    send(Step::SetRate, body);
}

void setRate(int type)
{
    rateType = type;
    if (rateType >= NMEA_TYPES) {
        if (changedRates > 0)
            logger::line("gps: %d NMEA output rates changed", changedRates);
        queryMode();
        return;
    }
    attempts = 0;
    sendRate();
}

void logRates()
{
    char text[96];
    size_t used = snprintf(text, sizeof(text), "gps: NMEA rates");
    for (int t = 0; t < NMEA_TYPES && used < sizeof(text); t++)
        used += snprintf(text + used, sizeof(text) - used, " %s%ld", TYPE_NAMES[t], rates[t]);
    logger::line("%s", text);
}

void queryRates()
{
    for (int t = 0; t < NMEA_TYPES; t++)
        rateKnown[t] = false;
    attempts = 0;
    send(Step::QueryRates, "PAIR063,-1");
}

void afterBoot()
{
    if (modeSent)
        queryMode(); // after $PAIR066 the module rebooted: check the result
    else
        queryRates();
}

void handleRate(const char *s)
{
    long type = 0;
    long rate = 0;
    if (!nmea::fieldInt(s, 1, 0, NMEA_TYPES - 1, &type) || !nmea::fieldInt(s, 2, 0, MAX_RATE, &rate))
        return;
    rates[type] = rate;
    rateKnown[type] = true;
    for (int t = 0; t < NMEA_TYPES; t++)
        if (!rateKnown[t])
            return;
    logRates();
    setRate(nextRateToSet(0));
}

void handleMode(const char *s)
{
    for (int i = 0; i < COMPARED_SYSTEMS; i++) {
        long enabled = 0;
        if (!nmea::fieldInt(s, i + 1, 0, 1, &enabled))
            return; // malformed reply: wait for a retry
        if (enabled != 1) {
            if (modeSent) {
                finish("constellations still differ after $PAIR066, left as they are");
            } else {
                modeSent = true;
                logger::line("gps: setting constellations GPS+GLONASS+Galileo+BeiDou (the GPS restarts)");
                attempts = 0;
                send(Step::SetMode, SET_MODE);
            }
            return;
        }
    }
    finish("constellations GPS+GLONASS+Galileo+BeiDou");
}

void handleAck(const char *s)
{
    long command = 0;
    long result = 0;
    if (!nmea::fieldInt(s, 1, 0, 999, &command) || !nmea::fieldInt(s, 2, 0, ACK_RESULT_MAX, &result))
        return;
    if (result == ACK_IN_PROGRESS)
        return;
    if (step == Step::SetRate && command == COMMAND_SET_RATE) {
        if (result == ACK_DONE)
            changedRates++;
        else
            logger::line("gps: $PAIR062 for %s failed (result %ld)", TYPE_NAMES[rateType], result);
        setRate(nextRateToSet(rateType + 1));
    } else if (step == Step::SetMode && command == COMMAND_SET_MODE) {
        if (result == ACK_DONE)
            wait(Step::WaitBoot, BOOT_TIMEOUT_MS); // the module reboots now
        else
            finish("$PAIR066 failed, constellations left as they are");
    }
}

void retryOrGiveUp()
{
    switch (step) {
    case Step::WaitBoot:
        logger::line("gps: no startup message, continuing");
        afterBoot();
        break;
    case Step::QueryRates:
        if (attempts < MAX_ATTEMPTS)
            send(Step::QueryRates, "PAIR063,-1");
        else {
            logger::line("gps: no reply to $PAIR063, output rates left as they are");
            queryMode();
        }
        break;
    case Step::SetRate:
        if (attempts < MAX_ATTEMPTS) {
            sendRate();
        } else {
            logger::line("gps: no reply to $PAIR062 for %s", TYPE_NAMES[rateType]);
            setRate(nextRateToSet(rateType + 1));
        }
        break;
    case Step::QueryMode:
        if (attempts < MAX_ATTEMPTS)
            send(Step::QueryMode, "PAIR067");
        else
            finish("no reply to $PAIR067, constellations left as they are");
        break;
    case Step::SetMode:
        // The acknowledgement may be lost in the reboot; wait for the startup message.
        wait(Step::WaitBoot, BOOT_TIMEOUT_MS);
        break;
    case Step::Idle:
    case Step::Done:
        break;
    }
}

} // namespace

namespace gps_setup {

void begin()
{
    attempts = 0;
    changedRates = 0;
    modeSent = false;
    wait(Step::WaitBoot, BOOT_TIMEOUT_MS);
}

void stop()
{
    step = Step::Idle;
}

void handleSentence(const char *s)
{
    if (step == Step::Idle || step == Step::Done)
        return;
    long type = 0;
    if (nmea::startsWith(s, "$PAIR011,")) {
        if (step == Step::WaitBoot && nmea::fieldInt(s, 1, 0, 999, &type) && type == STARTUP_NOTIFICATION)
            afterBoot();
    } else if (nmea::startsWith(s, "$PAIR063,")) {
        if (step == Step::QueryRates)
            handleRate(s);
    } else if (nmea::startsWith(s, "$PAIR067,")) {
        if (step == Step::QueryMode)
            handleMode(s);
    } else if (nmea::startsWith(s, "$PAIR001,")) {
        handleAck(s);
    }
}

void poll()
{
    if (step == Step::Idle || step == Step::Done)
        return;
    if ((int32_t)(millis() - deadline) >= 0)
        retryOrGiveUp();
}

const char *stateText()
{
    switch (step) {
    case Step::Idle:
        return "setup idle";
    case Step::WaitBoot:
        return "setup: waiting for start";
    case Step::QueryRates:
        return "setup: reading NMEA rates";
    case Step::SetRate:
        return "setup: setting NMEA rates";
    case Step::QueryMode:
        return "setup: reading constellations";
    case Step::SetMode:
        return "setup: setting constellations";
    case Step::Done:
        break;
    }
    return "setup done";
}

} // namespace gps_setup
