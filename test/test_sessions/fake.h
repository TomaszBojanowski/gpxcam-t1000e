// The fakes of the host tests of the state machines (pio test -e native_sessions): the test clock and
// loop, the log, the GPS, the sounds, the scanner, the stores, and a camera behind camera_link that
// answers as the S5II does (docs/PROTOCOL.md), or as an older-dialect camera. The real
// camera_session, older_session, pairing and link_control run against them.
// Made-up values only: the camera serial 12 34 56 78 9a, the card UUID of
// test/device_identity_vectors.txt, the position 5436.0000N 01900.0000E.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include <ble_gatt.h>

#include "camera_link.h"
#include "link_policy.h"
#include "lumix.h"
#include "pairing_record.h"

namespace fake {

// ---- Clock and loop

constexpr uint32_t LOOP_STEP_MS = 10;

uint32_t now();
// Moves the clock on without running anything.
void advance(uint32_t ms);
// Runs the card's loop (the scanner, pairing, the link) for ms, LOOP_STEP_MS at a time.
void loop(uint32_t ms);
// Runs it until done() is true, at most timeoutMs; true if done.
bool loopUntil(bool (*done)(), uint32_t timeoutMs);
// Every fake back to its start, and the state machines idle: no camera, nothing logged, GPS off.
void reset();

// ---- Log (logger::line and logger::transient)

bool logged(const char *part);
int loggedCount(const char *part);
bool kept(const char *part); // a line with part that the card keeps in its history (logger::line)
// Lines longer than the 125 characters a line of the card can have (log.cpp cuts them).
int longLines();
void clearLog();
// Prints the log, for a failing test.
void printLog();

// ---- GPS (gps.h)

extern bool gpsOn;
extern bool gpsFix; // a fix with fresh time and position while the GPS is on
extern int gpsOffCount;

// ---- Sounds (sound.h)

extern int pairedSounds;
extern int refuseSounds;

// ---- Scanner (bluetooth.h)

extern bool scanning;
extern bool scanForDump; // the scan is on only for a 'diag scan' run
extern int keptScanLines; // scan on/off lines that go into the history
extern uint16_t scanIntervalMs;
// The test camera advertises in this state: the scan sees it at every loop step while it is on.
void advertise(uint8_t state, int8_t rssi = -50);
void stopAdvertising();

// ---- Stores (pairing_store.h, settings_store.h, tz_store.h)

extern pairing_record::Record record;
extern bool storeWritable;
extern bool saveOk;
extern link_policy::Updates updates;
extern bool clockOn;
// The record as after pairing the test camera in this dialect.
void pairCamera(uint8_t dialect);

// ---- The camera behind camera_link

extern const uint8_t SERIAL[lumix::SERIAL_LENGTH];

enum class Dialect { Newer, Older };
enum class Answer { Connects, NoAnswer, NotEstablished }; // to a connect
enum class Request : uint8_t { None, Mtu, Service, Characteristics, Read, Write, Descriptors };
enum class Descriptors { NoCccd, Cccd, CccdInSecondAnswer }; // after the GPS notify characteristic

// A request, and for Read and Write the value handle (0: any).
struct Trigger {
    Request request = Request::None;
    uint16_t handle = 0;
};

struct Camera {
    Dialect dialect = Dialect::Newer; // a newer camera has the older characteristics too (as the S5II)
    Answer answer = Answer::Connects;
    Trigger silent;       // gets no answer: its step times out
    Trigger disconnectAt; // the camera ends the connection on it, with disconnectReason
    uint8_t disconnectReason = 0x13;
    Trigger refuse; // a refused Write, Read or Service search, with refuseStatus
    uint16_t refuseStatus = BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED;
    bool knowsCard = true; // older dialect: 01 once the card wrote knownAfter, on knownOn
    uint16_t knownAfter = 0x0046;  // OLDER_DEVICE_NAME (fake.h's value handles, below)
    uint16_t knownOn = 0x0052;     // OLDER_LOCATION_NOTIFY
    Descriptors descriptors = Descriptors::NoCccd;
};
extern Camera camera;
// Milliseconds the clock moves on at every request to camera_link, as time passes on the card while it
// works through events (0: none).
extern uint32_t requestMs;
// Milliseconds after which an event can be read (0: at once): a camera that answers in a later loop step.
extern uint32_t answerMs;

// The test camera's value handles.
constexpr uint16_t SERVICE_START = 0x0010;
constexpr uint16_t SERVICE_END = 0x0030;
constexpr uint16_t KEY = 0x0012;
constexpr uint16_t DEVICE_ID = 0x0014;
constexpr uint16_t APP_NAME = 0x0016;
constexpr uint16_t DEVICE_NAME = 0x0018;
constexpr uint16_t TIME = 0x001A;
constexpr uint16_t GPS_MODE = 0x001C;
constexpr uint16_t GPS_NOTIFY = 0x001E;
constexpr uint16_t LOCATION = 0x0022;
constexpr uint16_t OLDER_KEY = 0x0042;
constexpr uint16_t OLDER_DEVICE_ID = 0x0044;
constexpr uint16_t OLDER_DEVICE_NAME = 0x0046;
constexpr uint16_t OLDER_CAMERA_NAME = 0x0048;
constexpr uint16_t OLDER_CAMERA_TEXT = 0x004A;
constexpr uint16_t OLDER_TIME = 0x004C;
constexpr uint16_t OLDER_TIME_NOTIFY = 0x004E;
constexpr uint16_t OLDER_LOCATION = 0x0050;
constexpr uint16_t OLDER_LOCATION_NOTIFY = 0x0052;
static_assert(Camera().knownAfter == OLDER_DEVICE_NAME && Camera().knownOn == OLDER_LOCATION_NOTIFY, "the default 01");

// What the card asked camera_link, in order: "connect", "cancel connect", "disconnect", "mtu",
// "service", "characteristics", "older characteristics", "descriptors 001f", "read 0012", "write 001a".
extern std::vector<std::string> requests;
int askedCount(const char *request);
bool linkUp(); // a connection is open
// The camera sends a notification, ends the connection, or an event does not fit camera_link's queue.
void notify(uint16_t handle, uint8_t value);
void endConnection(uint8_t reason);
void loseNextEvent();

} // namespace fake
