// The fakes of the hardware modules the state machines use (fake.h): their functions as the card's
// headers declare them, with the state in namespace fake.
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bluetooth.h"
#include "camera_session.h"
#include "fake.h"
#include "gps.h"
#include "link_control.h"
#include "log.h"
#include "older_session.h"
#include "pairing.h"
#include "pairing_store.h"
#include "settings_store.h"
#include "sound.h"
#include "tz_store.h"

namespace fake {

namespace {

uint32_t nowMs = 0;
bool fixSeen = false;  // the GPS had a fix since it was switched on
uint32_t lastFixMs = 0; // when, for gps::utcSinceFix()

struct LogLine {
    std::string text;
    bool kept; // logger::line (kept in the history) or logger::transient
};
std::vector<LogLine> logLines;
int tooLong = 0;
constexpr size_t CARD_TEXT_MAX = logger::TEXT_MAX;

// The test camera as the scan sees it.
camera_list::Entry seen = {};
bool advertising = false;
uint8_t advertState = 0;
int8_t advertRssi = 0;

// The card UUID of test/device_identity_vectors.txt (made up).
const uint8_t CARD_UUID[lumix::UUID_LENGTH] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x46, 0x77,
                                               0x88, 0x99, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6};

void refreshScan()
{
    if (seen.used && nowMs - seen.lastSeenMs >= camera_list::LOST_AFTER_MS)
        seen.used = false; // as camera_list: not seen for 30 s
    if (!scanning || !advertising)
        return;
    seen.used = true;
    memcpy(seen.last.camera.serial, SERIAL, lumix::SERIAL_LENGTH);
    seen.last.camera.state = advertState;
    seen.last.rssi = advertRssi;
    seen.last.kind = advert_kind::Kind::Connectable;
    seen.lastSeenMs = nowMs;
}

void addLine(const char *format, va_list args, bool kept)
{
    char text[512];
    vsnprintf(text, sizeof(text), format, args);
    if (strlen(text) > CARD_TEXT_MAX)
        tooLong++;
    logLines.push_back({text, kept});
}

} // namespace

bool gpsOn = false;
bool gpsFix = false;
int gpsOffCount = 0;
int pairedSounds = 0;
int refuseSounds = 0;
int connectedSounds = 0;
int disconnectedSounds = 0;
bool scanning = false;
bool scanForDump = false;
int keptScanLines = 0;
uint16_t scanIntervalMs = 100;
pairing_record::Record record = {};
bool storeWritable = true;
bool saveOk = true;
link_policy::Updates updates = link_policy::Updates::Normal;
bool clockOn = true;

void resetCameraLink(); // fake_camera_link.cpp

uint32_t now()
{
    return nowMs;
}

void advance(uint32_t ms)
{
    nowMs += ms;
}

void loop(uint32_t ms)
{
    for (uint32_t done = 0; done < ms; done += LOOP_STEP_MS) {
        nowMs += LOOP_STEP_MS;
        if (gpsOn && gpsFix) {
            fixSeen = true;
            lastFixMs = nowMs;
        }
        refreshScan();
        pairing::poll();
        link_control::poll();
    }
}

bool loopUntil(bool (*done)(), uint32_t timeoutMs)
{
    for (uint32_t waited = 0; waited < timeoutMs; waited += LOOP_STEP_MS) {
        if (done())
            return true;
        loop(LOOP_STEP_MS);
    }
    return done();
}

void reset()
{
    pairing::stop(false);
    link_control::stop(false);
    camera_session::cancel();
    older_session::cancel();
    camera_session::forgetHandles("test reset");
    nowMs += 3600000; // past every hold and pause of the last test
    pairing::poll();  // releases a GPS hold
    logLines.clear();
    tooLong = 0;
    seen = {};
    advertising = false;
    gpsOn = false;
    gpsFix = false;
    fixSeen = false;
    gpsOffCount = 0;
    pairedSounds = 0;
    refuseSounds = 0;
    connectedSounds = 0;
    disconnectedSounds = 0;
    scanning = false;
    scanForDump = false;
    keptScanLines = 0;
    scanIntervalMs = 100;
    record = {};
    memcpy(record.uuid, CARD_UUID, sizeof(record.uuid));
    storeWritable = true;
    saveOk = true;
    updates = link_policy::Updates::Normal;
    clockOn = true;
    resetCameraLink();
}

bool logged(const char *part)
{
    return loggedCount(part) > 0;
}

bool kept(const char *part)
{
    for (const LogLine &line : logLines)
        if (line.kept && line.text.find(part) != std::string::npos)
            return true;
    return false;
}

int loggedCount(const char *part)
{
    int count = 0;
    for (const LogLine &line : logLines)
        count += line.text.find(part) != std::string::npos;
    return count;
}

int longLines()
{
    return tooLong;
}

void clearLog()
{
    logLines.clear();
}

void printLog()
{
    for (const LogLine &line : logLines)
        printf("%s %s\n", line.kept ? "[log]" : "[   ]", line.text.c_str());
}

void advertise(uint8_t state, int8_t rssi)
{
    advertising = true;
    advertState = state;
    advertRssi = rssi;
}

void stopAdvertising()
{
    advertising = false;
}

void pairCamera(uint8_t dialect)
{
    record.paired = true;
    memcpy(record.serial, SERIAL, sizeof(record.serial));
    record.dialect = dialect;
}

} // namespace fake

uint32_t millis()
{
    return fake::now();
}

namespace logger {

void line(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fake::addLine(fmt, args, true);
    va_end(args);
}

void transient(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fake::addLine(fmt, args, false);
    va_end(args);
}

} // namespace logger

namespace gps {

void on()
{
    fake::gpsOn = true;
    fake::fixSeen = false; // as gps.cpp: a GPS switched on again forgets the last fix
}

void off()
{
    fake::gpsOn = false;
    fake::gpsOffCount++;
}

bool isOn()
{
    return fake::gpsOn;
}

bool utcNow(int64_t *unixSeconds)
{
    if (!fake::gpsOn || !fake::gpsFix)
        return false;
    *unixSeconds = 1791930000 + fake::now() / 1000; // a made-up moment in 2026
    return true;
}

bool utcSinceFix(uint32_t maxAgeMs, int64_t *unixSeconds, uint32_t *ageMs)
{
    const uint32_t age = fake::now() - fake::lastFixMs;
    if (!fake::fixSeen || age > maxAgeMs)
        return false;
    *unixSeconds = 1791930000 + fake::now() / 1000;
    *ageMs = age;
    return true;
}

bool position(int32_t *latitudeE7, int32_t *longitudeE7, int32_t *altitudeDm)
{
    if (!fake::gpsOn || !fake::gpsFix)
        return false;
    *latitudeE7 = 546000000; // 5436.0000N
    *longitudeE7 = 190000000; // 01900.0000E
    *altitudeDm = 100;
    return true;
}

} // namespace gps

namespace sound {

void playRefuse(Kind)
{
    fake::refuseSounds++;
}

void playPairing() {}

void playPaired()
{
    fake::pairedSounds++;
}

void playConnected()
{
    fake::connectedSounds++;
}

void playDisconnected()
{
    fake::disconnectedSounds++;
}

} // namespace sound

namespace bluetooth {

bool ready()
{
    return true;
}

void scanOn(Lines lines)
{
    if (lines == Lines::Kept)
        fake::keptScanLines++;
    fake::scanning = true;
    fake::scanForDump = false;
    fake::seen.used = false; // as bluetooth::scanOn(): the camera list starts empty
}

void scanOff(Lines lines)
{
    if (fake::scanning && lines == Lines::Kept)
        fake::keptScanLines++;
    fake::scanning = false;
}

bool scanning()
{
    return fake::scanning;
}

bool scanForDumpOnly()
{
    return fake::scanning && fake::scanForDump;
}

bool setScanTiming(uint16_t intervalMs, uint16_t)
{
    fake::scanIntervalMs = intervalMs;
    return true;
}

void setNormalScanTiming()
{
    fake::scanIntervalMs = 100;
}

const camera_list::Entry *camera(size_t i)
{
    return i == 0 && fake::seen.used ? &fake::seen : nullptr;
}

} // namespace bluetooth

namespace tz_store {

const posix_tz::Rule &rule()
{
    static posix_tz::Rule utc;
    static const bool parsed = posix_tz::parse("UTC0", &utc);
    (void)parsed;
    return utc;
}

bool clockOn()
{
    return fake::clockOn;
}

} // namespace tz_store

namespace settings_store {

link_policy::Updates updates()
{
    return fake::updates;
}

} // namespace settings_store

namespace pairing_store {

bool ready()
{
    return true;
}

bool writable()
{
    return fake::storeWritable;
}

const pairing_record::Record &record()
{
    return fake::record;
}

bool saveCamera(const uint8_t serial[lumix::SERIAL_LENGTH], uint8_t dialect)
{
    if (!fake::saveOk)
        return false;
    memcpy(fake::record.serial, serial, sizeof(fake::record.serial));
    fake::record.paired = true;
    fake::record.dialect = dialect;
    return true;
}

} // namespace pairing_store
