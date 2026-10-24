// Rules of the normal work with the paired camera (M5): when to connect, how long to wait
// after a disconnect and when to send a location. No hardware dependencies (tested on the PC).
#pragma once

#include <stdint.h>

#include "camera_list.h"
#include "pairing_record.h"

namespace link_policy {

constexpr uint32_t MS_PER_MINUTE = 60000;

// Connect only on a report this recent, so its state is current.
constexpr uint32_t REPORT_FRESH_MS = 1000;

// Weaker reports are not worth a connection attempt (M6 decision; at -90 dBm one failed).
constexpr int8_t MIN_RSSI_DBM = -85;

// True only for the paired camera (the same serial number), switched on (state 00), with
// a report at most REPORT_FRESH_MS old and at least MIN_RSSI_DBM strong. Never in standby
// (06) or pairing (03), never to another camera.
bool shouldConnect(const pairing_record::Record &pairing, const camera_list::Entry &camera, uint32_t nowMs);

// How the card looks for the camera while not connected (M6 decisions). In both standby
// modes the camera's last report says standby (06) and the card only waits for state 00.
//   Normal      - scan 50 ms of every 100 ms, GPS on;
//   Rare        - the camera not seen for RARE_AFTER_MS: scan 100 ms every 2 s, GPS in Backup;
//   Standby     - the first LONG_STANDBY_AFTER_MS of standby (a break between photos):
//                 scan 100 ms every 1 s, the GPS stays on, so the location is there at once;
//   LongStandby - after that: scan 100 ms every 10 s, GPS in Backup.
enum class SearchMode { Normal, Rare, Standby, LongStandby };
constexpr uint32_t RARE_AFTER_MS = 5 * MS_PER_MINUTE;
constexpr uint32_t LONG_STANDBY_AFTER_MS = 20 * MS_PER_MINUTE; // a break between photos (10 min in M6)

// cameraSeen: a report of the paired camera since the link started; msSinceSeen and
// lastState: of its last report; msSearching: since the search started; msInStandby:
// since the camera went to standby.
SearchMode searchMode(bool cameraSeen, uint32_t msSinceSeen, uint8_t lastState, uint32_t msSearching,
                      uint32_t msInStandby);

// Whether the GPS may stay on in this mode. (It is switched on only in normal mode.)
bool gpsNeeded(SearchMode mode);

// Pause before looking for the camera again after a disconnect or a failed connection
// (M5 decision): 5 s, 15 s, then 60 s for every further one. failuresInARow starts at 1.
uint32_t pauseMs(uint32_t failuresInARow);

// A connection that lasted this long starts the pause sequence again.
constexpr uint32_t STABLE_CONNECTION_MS = 60000;

// When locations go to the camera: at least every periodMs; earlier after moving more than
// moveMetres or when the camera asks, but never more often than every minGapMs.
struct LocationTiming {
    uint32_t periodMs;
    uint32_t minGapMs;
    uint32_t moveMetres;
};

// M5 decisions, as the gpxcam app: every 25 s, after 20 m, at most every 5 s.
constexpr LocationTiming NORMAL_TIMING = {25000, 5000, 20};
// Every 5 s, after 5 m (GPS noise is 2-5 m), at most every 2 s (the GPS gives a fix every
// second; 2 s halves the writes a car would cause). Endurance test with the S5II on
// 2026-10-24: 8 h in one connection, about 5950 locations, answers 58-244 ms.
constexpr LocationTiming FREQUENT_TIMING = {5000, 2000, 5};

// The 'updates' setting: the timing the card asks for. Stored as a byte in the settings file:
// keep the values.
enum class Updates : uint8_t { Normal = 0, Frequent = 1 };

const char *updatesName(Updates updates); // "normal", "frequent"

// A setting from its name (lower case); false if there is none.
bool updatesFromName(const char *name, Updates *updates);

inline const LocationTiming &timingOf(Updates updates)
{
    return updates == Updates::Frequent ? FREQUENT_TIMING : NORMAL_TIMING;
}

enum class Send { No, First, Periodic, Moved, Asked };

// What to do now. sentBefore: a location was sent in this connection; msSinceLast: since
// that one; metresMoved: from the position sent then; cameraAsked: a notification came.
Send locationDue(const LocationTiming &timing, bool sentBefore, uint32_t msSinceLast, uint32_t metresMoved,
                 bool cameraAsked);

// The guard of the frequent timing. The camera answers a location write in 1-2 connection
// events (about 50-100 ms); an answer later than LATE_ANSWER_MS counts as late. After
// LATE_ANSWERS_TO_SLOW_DOWN late answers in one connection the card goes back to the normal
// timing until the end of that connection. A connection that ends other than by the camera's
// own close (HCI 0x13: switched off) cannot go on, so the next one uses the normal timing, to
// its end. With the old connection parameters such ends (HCI 0x08) came along with the hangs
// of the camera's Bluetooth after 2-4 h; this keeps a lagging camera from more load.
constexpr uint32_t LATE_ANSWER_MS = 1000;
constexpr uint8_t LATE_ANSWERS_TO_SLOW_DOWN = 2;

class RateGuard {
public:
    // A connection is ready for locations: not slowed down, unless the one before ended badly.
    void connectionStarted();
    // The camera answered a location write after answerMs; true when this made it slow down.
    bool answered(uint32_t answerMs);
    // The connection ended; closedByCamera: HCI 0x13 (the camera switched off).
    void connectionEnded(bool closedByCamera);

    // The setting in effect now: frequent only when chosen and not slowed down.
    Updates inEffect(Updates chosen) const { return slow ? Updates::Normal : chosen; }
    const LocationTiming &timing(Updates chosen) const { return timingOf(inEffect(chosen)); }
    bool slowedDown() const { return slow; }
    uint8_t lateAnswers() const { return late; }

private:
    uint8_t late = 0;
    bool slow = false;
    bool nextSlow = false;
};

// The writes of the introduction (camera_session) and the first ones of the link, in order.
enum class IntroductionWrite { Identifier, AppName, DeviceName, Time, GpsMode, FirstLocation };

// Whether a GATT refusal of this write means that the camera does not know the card (never
// paired, or forgotten), so that the card stops connecting instead of trying again: after a
// few refused connections the S5II stops advertising. The S5II takes the identifier, app name
// and device name of an unknown device and refuses the time (gpxcam notes, 2026-10-14);
// without the time write ('clock off', timeWritten false) nothing shows it so clearly, so any
// refusal in the introduction counts.
bool refusalMeansUnknownCard(IntroductionWrite write, bool timeWritten);

// Without the time written, a camera that does not know the card may also just end the
// connection: the S5II ends it itself after refusing the time (gpxcam notes). So this many
// connections in a row that ended after they were made but before the camera took a location
// count as a refusal.
constexpr uint8_t EARLY_ENDS_FOR_UNKNOWN_CARD = 3;

// Counts such an early end in *earlyEndsInARow (link_control sets it to 0 when the camera takes
// a location) and says whether the card should stop as after a refusal. With the time written
// its refusal shows an unknown card, so early ends do not count and the count goes back to 0.
bool earlyEndMeansUnknownCard(bool timeWritten, uint8_t *earlyEndsInARow);

// Distance in metres between two positions in degrees * 10^7 (equirectangular
// approximation; good to well under 1 % for the short distances it is used for).
uint32_t distanceMetres(int32_t latitude1E7, int32_t longitude1E7, int32_t latitude2E7, int32_t longitude2E7);

} // namespace link_policy
