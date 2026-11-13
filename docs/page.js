"use strict";

// The card page (docs/index.html), after zones.js and card.js: the card's state as tiles, the camera
// section's pairing screens, diagnostics, and startPage(), which ties them to the page's elements.

const CONNECT_TO_CHANGE_TEXT = "Connect the card to change this setting.";

// ---- The card's state as tiles: [level, word, detail] ----

function satellitesText(count) {
  return count === 1 ? "1 satellite" : `${count} satellites`;
}
// level: good; warn (under way, or worth a look); bad (needs you); idle (nothing to do now). The word always
// says the state: the colour of the dot only repeats it.

const BATTERY_LOW_PERCENT = 20; // the card powers itself off at 0 % (3.40 V)
const BATTERY_VERY_LOW_PERCENT = 10;

// The card connects to the camera only with a GPS fix: the camera gets the time from it. While the camera is
// away a while, the card looks for it with the GPS asleep (link_policy::gpsNeeded): no fix, but nothing to wait for.
function cameraTile(state) {
  if (state.pair !== "idle") return ["warn", "Pairing", "See the camera section below"];
  if (!state.paired) return ["idle", "Not paired", "Pair it below"];
  switch (state.link) {
    case "connected": return ["good", "Connected", "Sends the position"];
    case "connecting": return ["warn", "Connecting", "A few seconds"];
    case "searching": return state.gpsOn && !state.gpsFix ?
      ["warn", "Waiting for GPS", "Connects once the GPS has a fix"] :
      ["idle", "Looking for it", "Camera off or out of range"];
    case "pausing": return ["warn", "Trying again", "In a few seconds"];
    case "stopped": return ["bad", "Refused the card", "Pair them again"];
    default: return ["idle", "Link off", "'link on' in a serial terminal"];
  }
}

function gpsTile(state) {
  if (!state.gpsOn) return ["idle", "Off", "Saves power"];
  const satellites = satellitesText(state.gpsSatellites);
  if (state.gpsFix) return ["good", "Fix", satellites];
  return ["warn", "No fix yet", `${satellites}; outdoors 1\u20133 min`];
}

// On USB the battery reads higher than it is, so the percent is only about. When the charger
// reports the charge complete, the battery is full whatever the voltage gives (about 98 % at
// 4.18 V): the tile says 100 %. The card's own percent ('status' battery_pct) does not change.
function batteryTile(state) {
  const volts = `${(state.batteryMv / 1000).toFixed(2)} V`;
  const percent = `${state.batteryPercent} %`;
  switch (state.charger) {
    case "charging": return ["good", "Charging", `about ${percent} \u00b7 ${volts}`];
    case "charged": return ["good", "Charged", `100 % \u00b7 ${volts}`];
    case "not_charging": return ["idle", "On USB", `not charging, about ${percent} \u00b7 ${volts}`];
    default:
      if (state.batteryPercent < BATTERY_VERY_LOW_PERCENT) return ["bad", "Very low", `${percent} \u00b7 ${volts}`];
      if (state.batteryPercent < BATTERY_LOW_PERCENT) return ["warn", "Low", `${percent} \u00b7 ${volts}`];
      return ["good", "Good", `${percent} \u00b7 ${volts}`];
  }
}

// The zone may break after a "/" (zero-width space); its offset now when the browser knows the name.
function zoneTile(state) {
  const name = state.timeZone.replaceAll("/", "/\u200b");
  if (!state.clockOn) return ["idle", name, "Clock off: set it on the camera"];
  const found = findZone(state.timeZone) || legacyZone(state.timeZone);
  const offset = found ? utcOffsetText(found.zone) : "";
  return ["good", name, offset ? `Clock on \u00b7 now ${offset}` : "Clock on \u00b7 a rule given directly"];
}

// 'pair older' (app only): the card prints one line for the app with the identity and device name to
// register over the camera's Wi-Fi (src/older_session.cpp): "older: app register identity <id> name <name>".
function parseOlderRegister(line) {
  const match = /^older: app register identity (\S+) name (\S+)/.exec(line);
  return match ? { identity: match[1], name: match[2] } : null;
}

// How the app's Wi-Fi registration (register_older, app/src-tauri/src/older_wifi.rs) ended: its outcome,
// or the word it failed with (app/src-tauri/src/lib.rs Problem).
function olderOutcomeText(outcome) {
  switch (outcome) {
    case "accepted": return ["The camera accepted the card. Pairing done.", "good"];
    case "not-confirmed": return ["The camera did not confirm. On the camera, allow the connection, then try the " +
      "Wi-Fi step again.", "bad"];
    case "not-on-network": return ["Could not reach the camera. Join the camera's Wi-Fi network on this computer, " +
      "then try the Wi-Fi step again.", "bad"];
    case "cancelled": return ["Cancelled.", ""];
    case "bad-identity":
    case "bad-name": return ["The app cannot register the identity this card gave. Install the newest gpxcam card " +
      "app and gpxcam, then start again.", "bad"];
    default: return ["The Wi-Fi step failed. Try it again, or cancel.", "bad"];
  }
}

// On connecting: an unpaired card that can ('pair prepare', gpxcam 1.5.0-beta.2) switches its GPS on
// for 10 minutes, so that pairing finds the time ready. The card is on USB then: it costs no battery.
function shouldPrepareGps(state) {
  return state.gpsHold !== null && !state.paired && state.pair === "idle";
}

// The card's identity in the older dialect and its device name, as the firmware makes them
// (src/device_identity.h, src/older_session.cpp DEVICE_NAME): the fixed prefix, then the seed - the last
// 6 bytes of the card's UUID - in upper case. What the card registers at the Wi-Fi step, so a pairing left
// waiting there can be resumed without the line the card printed for it.
const OLDER_IDENTITY_PREFIX = "4D454930-0100-1000-8001-";
const OLDER_DEVICE_NAME = "T1000-E";
const SEED_HEX_DIGITS = 12;
function olderIdentity(cardId) {
  const seed = cardId.replaceAll("-", "").slice(-SEED_HEX_DIGITS).toUpperCase();
  return { identity: OLDER_IDENTITY_PREFIX + seed, name: OLDER_DEVICE_NAME };
}

// ---- The camera section's pairing screens (pure functions) ----

// Whether the card's gpxcam can pair as with an older camera ('pair older', gpxcam 1.5.0).
function canPairOlder(version) {
  const [major, minor] = version.split(".").map(Number);
  return major > 1 || (major === 1 && minor >= 5);
}

// While an older camera's Wi-Fi step waits for the user (app only).
const WIFI_WAIT_TEXT = "Waiting for this computer to join\u2026 the app continues by itself (up to 5 minutes).";

// The screens of the camera section: prepare (1), camera (2: start pairing on the camera now), connect (3),
// wifi (4: the older cameras' Wi-Fi step), done (5), failed (6). state: the card's last 'status'. ended: how
// the last pairing the page followed ended (the card's pair_result, or "wifi-given-up"), null for none.
// pairAgain: Pair again was chosen on a paired card. introducing: an older camera's introduction the page
// saw start has not reached the Wi-Fi step yet (the card is in waiting_wifi for all of it).
// { screen, variant }: camera "gps", "search" or "pause"; failed "no-camera" or "other".
// What the page knows of one pairing (startPage()): a new object when a pairing starts and when the card goes.
//   kind: what Try again repeats, "pair" or "older" (Pair as an older camera);
//   watching: the card is pairing, or was asked to: how it ends is shown;
//   ended, endedReason: how it ended (pairingScreen(), failedText()), and the card's own words when it gave up;
//   pairAgain: Pair again was chosen on a paired card: Prepare shows;
//   older, introducing: the camera is an older one, and its introduction runs before its Wi-Fi step;
//   nearby: the card sees a camera, but not in pairing mode;
//   search, searchMs: the card's search for the camera, { startsAt, lengthMs }, and how long one lasts;
//   stagesDone: of the connection (connectStages());
//   note: [text, kind] on the status line (cancelled, forgotten, no answer);
//   running, cancelled: a run of the older-camera wizard holds the buttons, and Cancel was pressed in it;
//   wifiText, wifiAccepted, wifiRetry: the Wi-Fi step's progress, the camera took the card, it failed and
//   can be tried again.
function newPairing(kind = "pair") {
  return { kind, watching: false, ended: null, endedReason: null, pairAgain: false, older: false,
    introducing: false, nearby: false, search: null, searchMs: PAIR_SEARCH_MS, stagesDone: 0, note: null,
    running: false, cancelled: false, wifiText: null, wifiAccepted: false, wifiRetry: false };
}

function pairingScreen(state, { ended = null, pairAgain = false, introducing = false } = {}) {
  switch (state.pair) {
    case "idle": break;
    case "waiting_gps": return { screen: "camera", variant: "gps" };
    case "searching": return { screen: "camera", variant: "search" };
    case "pausing": return { screen: "camera", variant: "pause" };
    case "waiting_wifi": return { screen: introducing ? "connect" : "wifi", variant: null };
    default: return { screen: "connect", variant: null }; // connecting, or a phase of a newer gpxcam
  }
  if (ended === "paired") return { screen: "done", variant: null };
  if (ended === "no_camera" || ended === "not_in_pairing_mode") return { screen: "failed", variant: "no-camera" };
  if (ended !== null && ended !== "none" && ended !== "cancelled") return { screen: "failed", variant: "other" };
  return { screen: state.paired && !pairAgain ? "done" : "prepare", variant: null };
}

const PAIRING_STEPS = [["prepare", "Prepare"], ["camera", "Pair on camera"], ["connect", "Connect"],
  ["wifi", "Camera Wi-Fi"], ["done", "Done"]];

// The row of steps while a pairing is prepared or under way, with the Wi-Fi step only for an older camera;
// null on done and failed. Each step: { name, label, state: "done" | "current" | "later" }.
function pairingSteps(screen, older) {
  if (screen === "done" || screen === "failed") return null;
  const steps = PAIRING_STEPS.filter(([name]) => name !== "wifi" || older || screen === "wifi");
  const at = steps.findIndex(([name]) => name === screen);
  return steps.map(([name, label], index) => ({ name, label, state: stepState(index, at) }));
}

// The short state beside the section's title: { text, kind }, kind the badge's class.
function pairingBadge(state) {
  if (state.pair !== "idle") return { text: "Pairing\u2026", kind: "pairing" };
  return state.paired ? { text: "Paired", kind: "paired" } : { text: "Not paired yet", kind: "" };
}

// The state of the step at index in a row whose current step is at `at`.
function stepState(index, at) {
  return index < at ? "done" : index === at ? "current" : "later";
}

// What a line of the card says about its pairing (src/pairing.cpp, newer_session.cpp, older_session.cpp),
// received at receivedAt (ms); null for any other line. searchMs: how long a search lasts, as far as known.
//   { search: { startsAt, lengthMs } } a search for the camera starts (or started: 'p' gives its age);
//   { nearby: true }    the card sees a camera, but not in pairing mode;
//   { stages: n }       n stages of the connection are done (connectStages());
//   { older: true }     no newer-dialect service: the card goes on as with an older camera;
//   { wifiStep: true }  an older camera waits for the Wi-Fi step;
//   { gaveUp: reason }  the card gave up, in its own words.
function pairingEvent(line, receivedAt, searchMs = PAIR_SEARCH_MS) {
  let match = /^pair: GPS time ok - looking for a camera in pairing mode \(up to (\d+) s\)/.exec(line);
  if (match) return { search: { startsAt: receivedAt, lengthMs: Number(match[1]) * 1000 } };
  if (/^pair: quick retry \d+\/\d+/.test(line)) return { search: { startsAt: receivedAt, lengthMs: searchMs } };
  match = /^pair: next attempt in (\d+) s/.exec(line);
  if (match) return { search: { startsAt: receivedAt + Number(match[1]) * 1000, lengthMs: searchMs } };
  match = /^pair: looking for a camera in pairing mode for (\d+) s, attempt/.exec(line);
  if (match) return { search: { startsAt: receivedAt - Number(match[1]) * 1000, lengthMs: searchMs } };
  if (/^pair: camera \S+ is .*, not in pairing mode/.test(line)) return { nearby: true };
  if (/^pair: attempt \d+\/\d+ - camera \S+ in pairing mode/.test(line)) return { stages: 1 };
  if (/^(camera|older): connected$/.test(line)) return { stages: 2 };
  if (/^(camera: device name accepted|older: device name written)$/.test(line)) return { stages: 3 };
  if (/^(camera: time accepted|older: time written)$/.test(line)) return { stages: 4 };
  if (/^pair: camera \S+ has no newer-dialect service/.test(line)) return { older: true };
  if (/^older: introduced - waiting up to/.test(line) || parseOlderRegister(line)) return { wifiStep: true };
  match = /^pair: given up - (.+)$/.exec(line);
  return match ? { gaveUp: match[1] } : null;
}

// Of a search { startsAt, lengthMs }: the ms left at now (its whole length before it starts), or null.
function searchLeftMs(search, now) {
  if (!search) return null;
  return Math.min(search.lengthMs, Math.max(0, search.startsAt + search.lengthMs - now));
}

// "2 minutes": a limit of whole minutes, as the texts say it.
function minutesText(ms) {
  const minutes = ms / 60000;
  return `${minutes} minute${minutes === 1 ? "" : "s"}`;
}

// "2:00" down to "0:00", in whole seconds rounded up.
function countdownText(ms) {
  const seconds = Math.ceil(Math.max(0, ms) / 1000);
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
}

// The same for a screen reader: "1 minute 32 seconds left".
function countdownLabel(ms) {
  const seconds = Math.ceil(Math.max(0, ms) / 1000);
  const parts = [[Math.floor(seconds / 60), "minute"], [seconds % 60, "second"]]
    .filter(([count], index) => count > 0 || (index === 1 && seconds === 0))
    .map(([count, unit]) => `${count} ${unit}${count === 1 ? "" : "s"}`);
  return `${parts.join(" ")} left`;
}

const CONNECT_STAGES = ["Camera in pairing mode", "Connected over Bluetooth", "Introducing the card to the camera",
  "Setting the camera's clock"];

// Screen 3: the stages with `done` of them done and the next one current.
function connectStages(done) {
  return CONNECT_STAGES.map((label, index) => ({ label, state: stepState(index, done) }));
}

// Screen 2: the line under the countdown, [text, kind]. nearby: the card saw a camera, but not in pairing
// mode; pauseLeftMs: of the pause between attempts, null when not known.
function cameraStepText(variant, state, { nearby = false, pauseLeftMs = null } = {}) {
  switch (variant) {
    case "gps": return [`Waiting for the GPS time \u2013 ${satellitesText(state.gpsSatellites)} so far. Do not ` +
      "start pairing on the camera yet.", "busy"];
    case "pause": {
      const when = pauseLeftMs === null ? "in a moment" : `in ${Math.ceil(pauseLeftMs / 1000)} s`;
      return [`Attempt ${state.pairAttempt} of ${PAIR_ATTEMPTS} did not work \u2013 looking again ${when}. Keep ` +
        "the camera in pairing mode.", "warn"];
    }
    default:
      if (nearby) return ["A camera is nearby, but not in pairing mode yet.", "warn"];
      return [state.pairAttempt > 0 ? `Looking for the camera again (attempt ${state.pairAttempt + 1} of ` +
        `${PAIR_ATTEMPTS})\u2026` : "Looking for the camera\u2026", "busy"];
  }
}

// Screen 1: the GPS row, checked by itself from the card's state. { done, detail }
function gpsCheck(state) {
  const satellites = satellitesText(state.gpsSatellites);
  if (state.gpsFix) return { done: true, detail: `${satellites} \u2013 the card knows the time.` };
  if (!state.gpsOn) return { done: false, detail: "The GPS switches on when pairing starts." };
  return { done: false, detail: `Waiting for a fix \u2013 ${satellites} so far.` };
}

// Screen 6: { title, text, older } for a pairing that ended without a camera; older: the box that offers
// pairing as with an older camera goes with it. result: the card's pair_result, or "wifi-given-up";
// reason: the card's own words when it gave up (pairingEvent() gaveUp).
function failedText(result, reason = null) {
  switch (result) {
    case "no_camera": return { title: "The camera did not answer", text: `The card looked for ` +
      `${minutesText(PAIR_SEARCH_MS)} and did not find a camera in pairing mode.`, older: true };
    case "not_in_pairing_mode": return { title: "The camera did not answer", text: "The card saw the camera, " +
      `but not in pairing mode, for ${minutesText(PAIR_SEARCH_MS)}.`, older: true };
    case "failed":
      if (reason !== null && reason.startsWith("no connection to the camera")) {
        return { title: "Pairing did not work", text: "The card could not connect to the camera: the connection " +
          "never came up.", older: true };
      }
      return { title: "Pairing did not work", text: `The camera did not take the card in ${PAIR_ATTEMPTS} attempts. ` +
        "If it lists T1000\u2011E from an earlier pairing, delete it there.", older: true };
    case "no_gps_fix": return { title: "No GPS time", text: `The card got no GPS time within ${minutesText(PAIR_FIX_WAIT_MS)}. ` +
      "Put it outdoors or by a window.", older: false };
    case "not_saved": return { title: "Pairing not saved", text: "The camera took the card, but the card could " +
      "not save the pairing.", older: false };
    case "not_possible": return { title: "The card cannot pair now", text: "Its Bluetooth or its storage is not " +
      "ready.", older: false };
    case "wifi-given-up": return { title: "The Wi-Fi step was not done", text: "The card no longer waits for the " +
      "Wi-Fi step.", older: false };
    default: return { title: "Pairing did not work", text: `The card ended the pairing: ${result}.`, older: false };
  }
}

// ---- Diagnostics (pure functions) ----

// While 'diag scan' runs, every line of the card belongs to it (its devices and the card's log
// around them), but not the answers to the page's own 'status'.
function belongsToScan(line) {
  return !line.startsWith("status: ");
}

// The last line of a 'diag scan' run (src/bluetooth.cpp): its summary, a refusal, or a card
// without the command (gpxcam before 1.5.0-beta.1).
function scanEnded(line) {
  return line.startsWith("diag scan: done") || line.startsWith("diag scan: not possible") ||
    line.startsWith("diag scan: already running") || line.startsWith("command: unknown");
}

// How a 'diag scan' run ended, from its lines: [text, kind].
function scanResultText(lines, version) {
  const has = lineFinder(lines);
  if (has("command: unknown")) return [`gpxcam ${version} cannot scan. Scanning needs version 1.5.0.`, "bad"];
  if (has("diag scan: not possible")) return ["The card's Bluetooth did not start, so it cannot scan.", "bad"];
  if (has("diag scan: already running")) {
    return [`A scan is already running on the card. Try again in ${SCAN_RUN_MS / 1000} s.`, "bad"];
  }
  if (!has("diag scan: done")) return ["The scan did not end in time. What came so far is below.", "bad"];
  return ["Scan done.", "good"];
}

// The identifiers in what Diagnostics shows and copies, hidden: Bluetooth addresses keep their last
// byte, UUIDs their last 4 hex digits (the card's older-dialect identity also its known prefix,
// docs/PROTOCOL.md), camera serial numbers (10 hex digits, and the bytes after "ff 3a 00 <state>" in
// a 'diag scan' line) their last 2, the camera's Wi-Fi name nothing. The card logs no position.
const MASK_RULES = [
  [new RegExp(`(register identity ${OLDER_IDENTITY_PREFIX})[0-9A-Fa-f]{8}([0-9A-Fa-f]{4})`, "g"), "$1********$2"],
  [/\b(?:[0-9A-Fa-f]{2}:){5}([0-9A-Fa-f]{2})\b/g, "**:**:**:**:**:$1"],
  [/\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{8}([0-9A-Fa-f]{4})\b/g,
    "********-****-****-****-********$1"],
  [/\b[0-9A-Fa-f]{8}([0-9A-Fa-f]{2})\b/g, "********$1"],
  [/S5M2-[0-9A-Fa-f]{6}/g, "S5M2-******"],
  [/(ff 3a 00 [0-9a-f]{2})(?: [0-9a-f]{2}){4} ([0-9a-f]{2})/g, "$1 ** ** ** ** $2"],
];
function maskIdentifiers(text) {
  return MASK_RULES.reduce((masked, [rule, replacement]) => masked.replace(rule, replacement), text);
}

// What the user sends: a heading with what it is, the firmware and the time, then the card's lines.
function diagnosticsText(title, firmware, lines, date = new Date()) {
  return [`${title} - ${firmware} - ${date.toISOString()}`, ...lines].join("\n") + "\n";
}

// ---- The page ----

function startPage() {
  const element = (id) => document.getElementById(id);
  const ui = {
    connect: element("connect"),
    chooseCard: element("choose-card"),
    cardChoices: element("card-choices"),
    disconnect: element("disconnect"),
    cardStatus: element("card-status"),
    state: element("state"),
    stateFirmware: element("state-firmware"),
    cardLines: element("card-lines"),
    cardNote: element("card-note"),
    storageNote: element("storage-note"),
    refreshRow: element("refresh-row"),
    refresh: element("refresh"),
    stateTime: element("state-time"),
    cameraNote: element("camera-note"),
    cameraOld: element("camera-old"),
    intro: element("intro"),
    pairing: element("pairing"),
    pairBadge: element("pair-badge"),
    pairSteps: element("pair-steps"),
    checkGps: element("check-gps"),
    checkGpsDetail: element("check-gps-detail"),
    pairStart: element("pair-start"),
    pairKeep: element("pair-keep"),
    pairWaitNote: element("pair-wait-note"),
    pairCallout: element("pair-callout"),
    pairCountdown: element("pair-countdown"),
    pairCountdownTime: element("pair-countdown-time"),
    pairCountdownBar: element("pair-countdown-bar"),
    wifiApp: element("wifi-app"),
    wifiBrowser: element("wifi-browser"),
    wifiRetry: element("wifi-retry"),
    pairAgain: element("pair-again"),
    forget: element("forget"),
    forgetConfirm: element("forget-confirm"),
    forgetYes: element("forget-yes"),
    forgetNo: element("forget-no"),
    failedTitle: element("failed-title"),
    failedText: element("failed-text"),
    failedPaired: element("failed-paired"),
    pairingHelp: element("pairing-help"),
    failedTips: element("failed-tips"),
    pairRetry: element("pair-retry"),
    olderBox: element("older-box"),
    pairOlder: element("pair-older"),
    olderBrowser: element("older-browser"),
    pairStatus: element("pair-status"),
    pairStopRow: element("pair-stop-row"),
    pairStop: element("pair-stop"),
    clock: element("clock"),
    clockNote: element("clock-note"),
    clockAnswer: element("clock-answer"),
    zones: element("zones"),
    search: element("search"),
    selected: element("selected"),
    results: element("results"),
    save: element("save"),
    saveAnswer: element("save-answer"),
    saveStatus: element("save-status"),
    saveLines: element("save-lines"),
    saveNote: element("save-note"),
    soundNote: element("sound-note"),
    soundSays: element("sound-says"),
    soundAnswer: element("sound-answer"),
    updatesNote: element("updates-note"),
    updatesSays: element("updates-says"),
    updatesNow: element("updates-now"),
    updatesAnswer: element("updates-answer"),
    copyLog: element("copy-log"),
    scan: element("scan"),
    diagAnswer: element("diag-answer"),
    diagStatus: element("diag-status"),
    diagProgress: element("diag-progress"),
    diagText: element("diag-text"),
    diagCopy: element("diag-copy"),
    diagUnmasked: element("diag-unmasked"),
  };
  const soundChoices = new Map(SOUND_SETTINGS.map((setting) => [setting, element(`sound-${setting}`)]));
  const updatesChoices = new Map(UPDATES_SETTINGS.map((setting) => [setting, element(`updates-${setting}`)]));
  let card = null;
  const computer = computerZone();
  let selected = computer;
  let busy = false;
  let refreshTimer = null;
  let refreshing = false;
  let clockOn = null; // as the card has it; null: not connected, or it cannot be changed
  let soundSetting = null; // as the card has it; null: not connected, or it cannot be changed
  let updatesSetting = null; // the same
  let oldVersion = null; // gpxcam before 1.3.0: the page only sets its time zone
  let lastState = null; // the card's last answer to 'status'
  let pairing = newPairing(); // the card's 'status' gives its phase, its lines and the wizard the rest
  let countdownTimer = null;
  let helpScreen = null; // the screen How pairing works was last shown on: folded again on another one
  const transport = chooseTransport(navigator, window);
  const inApp = transport !== null && transport.name === "app"; // only the app can do the Wi-Fi step

  if (inApp) {
    element("intro-choose").classList.add("hidden"); // no chooser: the app takes the card by itself
    element("about-title").textContent = "About this app";
    element("about-runs").textContent = "The app sends";
    element("about-link").classList.add("hidden"); // the app opens no web pages
    element("about-address").classList.remove("hidden");
  }

  if (transport === null) {
    element("unsupported").classList.remove("hidden");
    element("app").classList.add("hidden"); // nothing here would work
    ui.connect.disabled = true;
  }

  const setStatus = (target, text, kind) => {
    target.textContent = text;
    target.className = kind || "";
  };

  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

  const pairingUnderWay = () => lastState !== null && lastState.pair !== "idle";
  const storageDamaged = () => lastState !== null && lastState.storage === "damaged"; // it refuses to pair

  const refreshButtons = () => {
    ui.connect.disabled = card !== null || busy || transport === null;
    const locked = card === null || busy; // no card, or the page waits for its answer
    ui.disconnect.disabled = locked;
    ui.save.disabled = locked || selected === null;
    ui.refresh.disabled = card === null || refreshing;
    ui.clock.disabled = locked || clockOn === null;
    for (const choice of soundChoices.values()) choice.disabled = locked || soundSetting === null;
    for (const choice of updatesChoices.values()) choice.disabled = locked || updatesSetting === null;
    const canPair = !locked && lastState !== null && !pairingUnderWay() && !storageDamaged();
    for (const button of [ui.pairStart, ui.pairRetry, ui.pairOlder]) button.disabled = !canPair;
    ui.pairAgain.disabled = locked || pairingUnderWay() || storageDamaged();
    ui.pairKeep.disabled = locked;
    ui.wifiRetry.disabled = locked;
    ui.pairStop.disabled = card === null || (busy && !pairing.running); // it also stops a Wi-Fi step
    for (const button of [ui.forget, ui.forgetYes]) button.disabled = locked || storageDamaged();
    ui.copyLog.disabled = locked;
    ui.scan.disabled = locked;
    ui.diagCopy.disabled = ui.diagText.value === ""; // what was collected stays after disconnecting
  };

  // Screen 2's countdown, while it shows a search whose start the page saw.
  const showCountdown = () => {
    const left = searchLeftMs(pairing.search, Date.now());
    ui.pairCountdown.classList.toggle("hidden", left === null);
    if (left === null) return;
    ui.pairCountdownTime.textContent = countdownText(left);
    ui.pairCountdownBar.max = pairing.search.lengthMs / 1000;
    ui.pairCountdownBar.value = Math.ceil(left / 1000);
    ui.pairCountdownBar.setAttribute("aria-valuetext", countdownLabel(left));
  };

  const runCountdown = (on) => {
    clearTimeout(countdownTimer);
    countdownTimer = null;
    if (!on) {
      ui.pairCountdown.classList.add("hidden");
      return;
    }
    const tick = () => {
      showCountdown();
      countdownTimer = setTimeout(tick, 1000);
    };
    tick();
  };

  const showSteps = (steps) => {
    ui.pairSteps.classList.toggle("hidden", steps === null);
    for (const [name] of PAIRING_STEPS) {
      const item = element(`pair-step-${name}`);
      const step = steps && steps.find((one) => one.name === name);
      item.classList.toggle("hidden", !step);
      if (!step) continue;
      item.textContent = step.label;
      item.className = step.state;
      if (step.state === "current") item.setAttribute("aria-current", "step");
      else item.removeAttribute("aria-current");
    }
  };

  const showPrepare = () => {
    const gps = gpsCheck(lastState);
    ui.checkGps.className = gps.done ? "check-done" : "";
    ui.checkGpsDetail.textContent = gps.detail;
    ui.pairKeep.classList.toggle("hidden", !lastState.paired);
    ui.pairWaitNote.textContent = `Do not start pairing on the camera yet \u2013 ${inApp ? "the app" : "this page"} ` +
      "tells you when.";
  };

  // Screen 2: the call to start pairing on the camera (not while the card waits for GPS time); its status line.
  const showCamera = (variant) => {
    ui.pairCallout.classList.toggle("hidden", variant === "gps");
    const { search, nearby } = pairing;
    const pauseLeftMs = variant === "pause" && search ? Math.max(0, search.startsAt - Date.now()) : null;
    return cameraStepText(variant, lastState, { nearby, pauseLeftMs });
  };

  const showStages = () => {
    connectStages(Math.max(pairing.stagesDone, 1)).forEach((stage, index) => {
      const item = element(`connect-stage-${index + 1}`);
      item.textContent = stage.label;
      item.className = stage.state;
    });
  };

  // Screen 4: in the app the step itself, in a browser what it needs; its status line.
  const showWifiStep = () => {
    ui.wifiApp.classList.toggle("hidden", !inApp);
    ui.wifiBrowser.classList.toggle("hidden", inApp);
    ui.wifiRetry.classList.toggle("hidden", !inApp || !pairing.wifiRetry || pairing.running || pairing.wifiAccepted);
    if (pairing.wifiText) return pairing.wifiText;
    return inApp ? [WIFI_WAIT_TEXT, "warn"] : ["", ""];
  };

  const showFailed = (variant) => {
    const failed = failedText(pairing.ended, pairing.endedReason);
    ui.failedTitle.textContent = failed.title;
    ui.failedText.textContent = failed.text;
    ui.failedPaired.classList.toggle("hidden", !lastState.paired); // a failed pairing keeps the camera paired
    ui.failedTips.classList.toggle("hidden", variant !== "no-camera");
    ui.olderBox.classList.toggle("hidden", !failed.older || !canPairOlder(lastState.version));
    ui.pairOlder.classList.toggle("hidden", !inApp);
    ui.olderBrowser.classList.toggle("hidden", inApp);
  };

  const PAIRING_SCREENS = ["prepare", "camera", "connect", "wifi", "done", "failed"];

  // The camera section from the card's last state and what the page saw of the pairing: one screen.
  const showPairing = () => {
    const known = card !== null && lastState !== null && oldVersion === null;
    ui.cameraOld.classList.toggle("hidden", oldVersion === null);
    ui.cameraNote.textContent = card === null ? "Connect the card to pair it with your camera." :
      storageDamaged() ? STORAGE_PAIRING_TEXT : "";
    ui.cameraNote.classList.toggle("hidden", ui.cameraNote.textContent === "");
    ui.pairing.classList.toggle("hidden", !known);
    ui.pairBadge.classList.toggle("hidden", !known);
    if (!known) {
      runCountdown(false);
      refreshButtons();
      return;
    }
    const { screen, variant } = pairingScreen(lastState, pairing);
    const badge = pairingBadge(lastState);
    ui.pairBadge.textContent = badge.text;
    ui.pairBadge.className = `pair-badge ${badge.kind}`.trimEnd();
    showSteps(pairingSteps(screen, pairing.older));
    for (const name of PAIRING_SCREENS) element(`pair-screen-${name}`).classList.toggle("hidden", name !== screen);
    if (screen !== helpScreen) ui.pairingHelp.open = false; // opened by the user, it stays open on this screen only
    helpScreen = screen;
    let status = pairing.note || ["", ""];
    if (screen === "prepare") showPrepare();
    else if (screen === "camera") status = showCamera(variant);
    else if (screen === "connect") status = ["", ""];
    else if (screen === "wifi") status = showWifiStep();
    else if (screen === "failed") showFailed(variant);
    if (screen === "connect") showStages();
    if (screen !== "done") ui.forgetConfirm.classList.add("hidden");
    runCountdown(screen === "camera" && variant === "search" && pairing.search !== null);
    setStatus(ui.pairStatus, ...status);
    ui.pairStopRow.classList.toggle("hidden", lastState.pair === "idle" && !pairing.running);
    refreshButtons();
  };

  // The Wi-Fi step's progress, shown until the pairing ends.
  const showWifi = (text, kind) => {
    pairing.wifiText = [text, kind];
    showPairing();
  };

  // The card's lines say what its 'status' does not: when a search starts, a camera nearby, the stages.
  const followCardLine = (line) => {
    const event = pairingEvent(line, Date.now(), pairing.searchMs);
    if (!event) return;
    if (event.search) {
      pairing.search = event.search;
      pairing.searchMs = event.search.lengthMs;
    }
    if (event.nearby) pairing.nearby = true;
    if (event.stages) { // a new attempt starts again; the camera is in pairing mode now
      pairing.stagesDone = event.stages === 1 ? 1 : Math.max(pairing.stagesDone, event.stages);
      pairing.nearby = false;
    }
    if (event.older) {
      pairing.older = true;
      pairing.introducing = true;
    }
    if (event.wifiStep) {
      pairing.older = true;
      pairing.introducing = false;
    }
    if (event.gaveUp) pairing.endedReason = event.gaveUp;
    if (lastState !== null) showPairing();
  };

  // Follows a pairing, also one started with the card's button, to its end, shown until the next one.
  const followPairing = (state) => {
    if (state.pair !== "idle") {
      if (!pairing.watching) pairing.note = null;
      pairing.watching = true;
      if (state.pair === "waiting_wifi") pairing.older = true;
      return;
    }
    if (!pairing.watching) return;
    // After a failed Wi-Fi step the card waits up to 300 s for another; then it gives up.
    const ended = pairing.wifiRetry && state.pairResult !== "paired" ? "wifi-given-up" : state.pairResult;
    const note = ended === "cancelled" ? ["Pairing cancelled.", ""] : null;
    pairing = { ...newPairing(pairing.kind), ended, endedReason: pairing.endedReason, note };
  };

  // The switch as the card has it; the time zone can be chosen unless the clock is off.
  const showClock = () => {
    ui.clock.checked = clockOn === true || oldVersion !== null; // gpxcam 1.2 always sets it
    ui.clockNote.textContent = card === null ? CONNECT_TO_CHANGE_TEXT :
      clockNoteText(clockOn !== false, oldVersion);
    ui.zones.disabled = clockOn === false;
    refreshButtons();
  };

  // The choice as the card has it.
  const showSound = () => {
    const shown = oldVersion !== null ? "loud" : soundSetting; // gpxcam 1.2 always plays loud
    for (const [setting, choice] of soundChoices) choice.checked = setting === shown;
    ui.soundSays.textContent = soundText(shown);
    ui.soundNote.textContent = card === null ? CONNECT_TO_CHANGE_TEXT : soundNoteText(oldVersion);
    refreshButtons();
  };

  // gpxcam that cannot change the location updates (before 1.4.0), or null.
  const updatesOldVersion = () => {
    if (card === null) return null;
    if (oldVersion !== null) return oldVersion;
    return lastState !== null && lastState.updates === null ? lastState.version : null;
  };

  // The choice as the card has it, and a note while the card sends less often than chosen.
  const showUpdates = () => {
    const old = updatesOldVersion();
    const shown = old !== null ? "normal" : updatesSetting; // before 1.4.0 always normal
    for (const [setting, choice] of updatesChoices) choice.checked = setting === shown;
    ui.updatesSays.textContent = updatesText(shown);
    ui.updatesNote.textContent = card === null ? CONNECT_TO_CHANGE_TEXT : updatesNoteText(old);
    const slow = card !== null && lastState !== null ? updatesNowText(lastState) : null;
    ui.updatesNow.textContent = slow || "";
    ui.updatesNow.classList.toggle("hidden", slow === null);
    refreshButtons();
  };

  const showSettings = () => {
    showClock();
    showSound();
    showUpdates();
  };

  // A tile: its level as a class (the colour of the dot), the word and the detail.
  const showTile = (name, [level, word, detail]) => {
    element(`tile-${name}`).className = `tile level-${level}`;
    element(`state-${name}`).textContent = word;
    element(`state-${name}-detail`).textContent = detail;
  };

  const showState = (state) => {
    ui.stateFirmware.textContent = `gpxcam ${state.version} \u00b7 git ${state.git}`;
    showTile("camera", cameraTile(state));
    showTile("gps", gpsTile(state));
    showTile("battery", batteryTile(state));
    showTile("zone", zoneTile(state));
    const storage = storageNoteText(state);
    ui.storageNote.textContent = storage || "";
    ui.storageNote.classList.toggle("hidden", storage === null);
    ui.stateTime.textContent = `Updated ${new Date().toLocaleTimeString()}.`;
    ui.state.classList.remove("hidden");
    ui.refreshRow.classList.remove("hidden");
    // In the app the Wi-Fi step goes on by itself: when a pairing reaches it (Pair found an older camera),
    // and when the page finds the card waiting there (a reconnect).
    const reachedWifiStep = inApp && (lastState === null || lastState.pair !== "waiting_wifi") &&
      state.pair === "waiting_wifi";
    lastState = state;
    if (!busy) { // a change being saved is shown when it is done
      clockOn = state.clockOn;
      soundSetting = state.sound;
      updatesSetting = state.updates;
      showSettings();
    }
    showSelected();
    followPairing(state);
    if (reachedWifiStep && !pairing.running) resumeOlder(); // shows its first step at once
    showPairing();
  };

  // Asks the card for its state now and then every STATUS_PERIOD_MS while it stays connected.
  const refresh = async () => {
    const asked = card;
    if (!asked || refreshing) return;
    clearTimeout(refreshTimer);
    refreshing = true;
    refreshButtons();
    const status = await asked.status().catch(() => ({ kind: "none" }));
    refreshing = false;
    if (card !== asked) return;
    if (status.kind === "ok") showState(status.state);
    else ui.stateTime.textContent = `No answer from the card at ${new Date().toLocaleTimeString()}; trying again.`;
    refreshButtons();
    refreshTimer = setTimeout(refresh, pairingUnderWay() ? PAIRING_STATUS_PERIOD_MS : STATUS_PERIOD_MS);
  };

  const hideCard = () => {
    clearTimeout(refreshTimer);
    for (const part of [ui.state, ui.cardNote, ui.refreshRow, ui.storageNote]) part.classList.add("hidden");
    ui.cardLines.textContent = "";
    ui.intro.classList.remove("hidden");
    clockOn = null;
    soundSetting = null;
    updatesSetting = null;
    oldVersion = null;
    setStatus(ui.clockAnswer, "");
    setStatus(ui.soundAnswer, "");
    setStatus(ui.updatesAnswer, "");
    lastState = null;
    helpScreen = null;
    if (pairing.running) transport.cancelOlder(); // a Wi-Fi step for this card stops with it
    pairing = newPairing();
    if (ui.diagStatus.className === "busy") { // a log or a scan cut short
      ui.diagProgress.classList.add("hidden");
      setStatus(ui.diagStatus, "Stopped: the connection to the card ended.", "bad");
    }
  };

  // The zone that Save would send; the computer's zone is offered first, and it may not be the card's.
  const showSelected = () => {
    if (!selected) {
      ui.selected.textContent = "No time zone selected.";
      return;
    }
    const offset = utcOffsetText(selected.zone);
    const same = selected.name !== selected.zone ? ` (same as ${selected.zone})` : "";
    const onCard = lastState !== null && lastState.timeZone === selected.name ? " (already on the card)" : "";
    ui.selected.textContent = `To save: ${selected.name}${same}${offset ? `, now ${offset}` : ""}${onCard}`;
  };

  const showResults = () => {
    ui.results.replaceChildren();
    const typed = ui.search.value.trim() !== "";
    for (const result of typed ? searchZones(ui.search.value) : startList(computer)) {
      const item = document.createElement("li");
      const label = document.createElement("label");
      const radio = document.createElement("input");
      radio.type = "radio";
      radio.name = "zone";
      radio.value = result.name;
      radio.checked = selected !== null && selected.name === result.name;
      radio.addEventListener("change", () => {
        selected = findZone(result.name);
        showSelected();
        refreshButtons();
      });
      const text = document.createElement("span");
      const name = document.createElement("span");
      name.className = "name";
      result.name.split("/").forEach((part, i) => { // breaks after a "/", not inside a word
        if (i > 0) name.append("/", document.createElement("wbr"));
        name.append(part);
      });
      const detail = document.createElement("div");
      detail.className = "detail";
      detail.textContent = result.detail;
      text.append(name, detail);
      const offset = document.createElement("span");
      offset.className = "offset";
      offset.textContent = utcOffsetText(result.zone);
      label.append(radio, text, offset);
      item.append(label);
      ui.results.append(item);
    }
  };

  const disconnected = (message, kind) => {
    card = null;
    busy = false;
    hideCard();
    showSelected();
    setStatus(ui.cardStatus, message, kind);
    showSettings();
    showPairing();
  };

  // A card before gpxcam 1.3.0 answers 'tz' but not 'status'. Null if the page can go on.
  const connectOldCard = async (opened, version) => {
    const lines = await opened.timeZone().catch(() => []);
    const problem = tzProblemText(version, classifyTzAnswer(lines));
    if (problem) return problem;
    setStatus(ui.cardStatus, `Connected: gpxcam ${version}. Its time zone now:`, "good");
    ui.cardLines.textContent = lines.join("\n");
    ui.cardNote.textContent = `gpxcam ${version} can only set the time zone from this page. From version 1.3.0 ` +
      "it also shows the card's state here.";
    ui.cardNote.classList.remove("hidden");
    oldVersion = version;
    return null;
  };

  // With more than one card plugged in (only in the app): a button for each; the path chosen,
  // or null after Cancel.
  const chooseCard = (paths) => new Promise((resolve) => {
    const done = (path) => {
      ui.chooseCard.classList.add("hidden");
      ui.cardChoices.replaceChildren();
      resolve(path);
    };
    const buttons = paths.map((path) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "secondary";
      button.textContent = path;
      button.addEventListener("click", () => done(path));
      return button;
    });
    const cancel = document.createElement("button");
    cancel.type = "button";
    cancel.className = "secondary";
    cancel.textContent = "Cancel";
    cancel.addEventListener("click", () => done(null));
    ui.cardChoices.replaceChildren(...buttons, cancel);
    ui.chooseCard.classList.remove("hidden");
  });

  const connect = async () => {
    let port;
    try {
      port = await transport.requestPort(chooseCard);
    } catch (error) {
      if (error instanceof NoCardError) {
        setStatus(ui.cardStatus, "No card found. Plug the card in with a USB cable and click Connect.", "bad");
      }
      return; // no card chosen
    }
    busy = true;
    refreshButtons();
    const opened = new Card(port);
    try {
      setStatus(ui.cardStatus, "Connecting\u2026", "busy");
      await opened.open();
    } catch (error) {
      disconnected(openProblemText(error), "bad");
      return;
    }
    const version = await opened.version().catch(() => null);
    if (!version) {
      await opened.close();
      disconnected(noAnswerText(opened.readError), "bad");
      return;
    }
    const status = await opened.status().catch(() => ({ kind: "none" }));
    const problem = status.kind === "no-status" ? await connectOldCard(opened, version)
      : statusProblemText(status.kind);
    if (problem) {
      await opened.close();
      disconnected(problem, "bad");
      return;
    }
    card = opened;
    opened.watchLines(followCardLine);
    ui.intro.classList.add("hidden");
    card.onClosed = () => {
      const why = opened.readError ? ` (${readErrorText(opened.readError)})` : "";
      if (card === opened) disconnected(`The connection to the card ended${why}.`, "bad");
    };
    busy = false;
    if (status.kind === "ok") {
      setStatus(ui.cardStatus, "Connected to the card", "good");
      showState(status.state);
      if (shouldPrepareGps(status.state)) opened.prepareGps().catch(() => {}); // the next refresh shows it
      // A search under way since before: 'p' says how long it has run, for the countdown.
      if (status.state.pair === "searching") opened.pairingDetails().catch(() => {});
      refreshTimer = setTimeout(refresh, STATUS_PERIOD_MS);
    } else { // a card before 1.3.0: showState() did not run
      showSettings();
      showPairing();
    }
  };

  const disconnect = async () => {
    if (!card) return;
    const closing = card;
    card = null;
    refreshButtons();
    await closing.close();
    disconnected("Not connected.");
  };

  const saveZone = async () => {
    const name = selected.name; // the list may change while the card answers
    busy = true;
    refreshButtons();
    ui.saveAnswer.classList.remove("hidden");
    setStatus(ui.saveStatus, `Saving ${name}\u2026`, "busy");
    ui.saveNote.classList.add("hidden");
    ui.saveLines.textContent = "";
    const lines = await card.timeZone(name).catch(() => []);
    const [text, kind] = tzAnswerText(name, classifyTzAnswer(lines));
    setStatus(ui.saveStatus, text, kind);
    ui.saveNote.classList.toggle("hidden", kind !== "good");
    ui.saveLines.textContent = kind === "good" ? "" : lines.join("\n"); // "Saved: X." says it all
    busy = false;
    refreshButtons();
    if (!ui.state.classList.contains("hidden")) refresh(); // the new zone in the card's state
  };

  // Saves one of the settings with its command (answers: CLOCK_ANSWERS, SOUND_ANSWERS, UPDATES_ANSWERS); the
  // choice shows the card's setting again once the card has answered (show).
  const saveSetting = async (answers, value, answerElement, show) => {
    const asked = card;
    if (!asked) return;
    busy = true;
    refreshButtons();
    setStatus(answerElement, "Saving\u2026", "busy");
    const lines = await asked.save(answers, value).catch(() => []);
    if (card !== asked) return;
    setStatus(answerElement, ...answers.text(value, answers.classify(lines)));
    busy = false;
    await refresh();
    show();
  };

  // Sends a pairing command and then shows the card's state; the stages and the result come
  // with it (showPairing()).
  // Sends a pairing command and follows the card's answer; the screen stays as it is until then. kind: a new
  // pairing of that kind starts (Pair, Try again); null: the one under way goes on (Cancel).
  const pairCommand = async (send, kind = null) => {
    const asked = card;
    if (!asked) return;
    busy = true;
    ui.forgetConfirm.classList.add("hidden");
    pairing.note = null;
    showPairing();
    const lines = await send(asked).catch(() => []);
    if (card !== asked) return;
    busy = false;
    if (kind !== null) pairing = newPairing(kind);
    if (lines.length === 0) pairing.note = [NO_ANSWER_TEXT, "bad"];
    else pairing.watching = true; // also when the card refused at once: the result says why
    await refresh();
    showPairing();
  };

  const forgetCamera = async () => {
    const asked = card;
    if (!asked) return;
    busy = true;
    refreshButtons();
    setStatus(ui.pairStatus, "Forgetting the camera\u2026", "busy");
    const answer = classifyUnpairAnswer(await asked.unpair().catch(() => []));
    if (card !== asked) return;
    busy = false;
    ui.forgetConfirm.classList.add("hidden");
    pairing = newPairing();
    pairing.note = unpairAnswerText(answer);
    await refresh();
    showPairing();
  };

  // The one-time Wi-Fi step of an older camera (app only): the card has introduced itself and waits for
  // it; the app registers the same identity and name over the camera's own Wi-Fi. show(text, kind) puts
  // the progress where the step was started from. The outcome of register_older, or the word it failed
  // with (both for olderOutcomeText()), or null when the card went away meanwhile.
  const runWifiStep = async (asked, info, show) => {
    show(WIFI_WAIT_TEXT, "warn");
    const outcome = await transport.registerOlder(info.identity, info.name, (stage) => {
      if (card !== asked || pairing.cancelled) return;
      if (stage === "on-network") show("On the camera's network \u2013 registering the card\u2026", "busy");
      else if (stage === "registering") {
        show("Registering \u2013 allow the connection on the camera if it asks\u2026", "busy");
      }
    }).catch((problem) => String(problem));
    return card === asked ? outcome : null;
  };

  // Cancel is a direct command (not queued behind the card's console), so it stops the Wi-Fi wait at
  // once; register_older returns "cancelled". A cancel before the Wi-Fi wait is seen at the wizard's
  // next check (Start: within 0.4 s; Resume: after the card's answer to 'p').
  const cancelOlder = () => {
    pairing.cancelled = true;
    setStatus(ui.pairStatus, "Cancelling\u2026", "busy");
    transport.cancelOlder();
  };

  // A run of the wizard (Start, Resume) holds the buttons; when it ends they follow the card's state.
  const olderStarted = () => {
    busy = true;
    pairing.running = true;
    pairing.cancelled = false;
    pairing.wifiText = null;
    pairing.wifiAccepted = false;
    pairing.note = null;
    showPairing();
  };

  // The end of a run for the card it was started with; the card's pairing went with it if the card went.
  const olderEnded = (asked) => {
    if (card !== asked) return;
    busy = false;
    pairing.running = false;
    showPairing();
  };

  // The Wi-Fi step and what follows it. When it fails the card keeps waiting (up to 300 s from its
  // introduction, src/older_session.cpp), so it can be tried again; a cancel stops the card's pairing.
  const finishWifiStep = async (asked, info) => {
    const outcome = await runWifiStep(asked, info, showWifi);
    if (outcome === null) return;
    pairing.wifiAccepted = outcome === "accepted";
    pairing.wifiRetry = !pairing.wifiAccepted && outcome !== "cancelled";
    showWifi(...olderOutcomeText(outcome));
    if (outcome === "cancelled") await asked.cancelPairing().catch(() => {});
    await refresh();
  };

  // Pair as an older camera (app only): 'pair older', then the Wi-Fi step with the identity the card prints.
  // The screen stays as it is until the card answers; a card that refuses at once ends at once, saying why.
  const pairOlder = async () => {
    const asked = card;
    if (!asked || busy) return;
    olderStarted();
    let info = null;
    // The card prints one line with the identity and name when it reaches the Wi-Fi step; catch it
    // without the ask queue, so the status refresh keeps running and shows the card's progress.
    const stopWatch = asked.watchLines((line) => {
      info = parseOlderRegister(line) || info;
    });
    try {
      await asked.send("pair older");
      if (card !== asked) return;
      pairing = { ...newPairing("older"), older: true, introducing: true, watching: true, running: true,
        cancelled: pairing.cancelled };
      await refresh(); // the card's console answers in order: its state after 'pair older'
      // Wait as long as the card does (a GPS fix, then the camera, then the introduction); each status
      // refresh shows its phase. Stop when it reaches the Wi-Fi step, ends, is cancelled, or the time runs out.
      const deadline = Date.now() + OLDER_START_TIMEOUT_MS;
      while (!info && !pairing.cancelled && pairingUnderWay() && Date.now() < deadline) {
        await sleep(400);
        if (card !== asked) return;
      }
      if (info && !pairing.cancelled) {
        stopWatch();
        await finishWifiStep(asked, info);
        return;
      }
      const ranOut = !pairing.cancelled && pairingUnderWay();
      if (pairingUnderWay()) await asked.cancelPairing().catch(() => {}); // the card may still be waiting
      await refresh();
      if (ranOut) pairing.note = ["The card did not reach the Wi-Fi step. Try again.", "bad"];
    } catch (error) {
      if (card === asked) pairing.note = ["Pairing failed. Try again.", "bad"];
    } finally {
      stopWatch();
      olderEnded(asked);
    }
  };

  // Resume (and try the Wi-Fi step again): the card waits at the Wi-Fi step, and the app registers the
  // identity it computes from the card's UUID ('p'), as the firmware does.
  const resumeOlder = async () => {
    const asked = card;
    if (!asked || busy) return;
    olderStarted();
    showWifi("Reading the card's identity\u2026", "busy");
    try {
      const cardId = await asked.cardId();
      if (card !== asked) return;
      if (pairing.cancelled) {
        showWifi("Cancelled.", "");
        await asked.cancelPairing().catch(() => {});
        await refresh();
      } else if (cardId === null) {
        showWifi("The card did not give its identity. Cancel, then start again.", "bad");
      } else {
        await finishWifiStep(asked, olderIdentity(cardId));
      }
    } catch (error) {
      if (card === asked) showWifi("Pairing failed. Try again.", "bad");
    } finally {
      olderEnded(asked);
    }
  };

  const startPair = () => pairCommand((asked) => asked.pair(), "pair");

  // Cancel while no run of the wizard is under way: the card's pairing stops, also one that waits for a
  // failed Wi-Fi step to be tried again, and its result follows like any pairing's.
  const cancelPairing = () => {
    pairing.wifiRetry = false;
    return pairCommand((asked) => asked.cancelPairing());
  };

  // ---- Diagnostics: the card's log and a scan, shown and copied for a message ----

  // The firmware for the heading of what is copied.
  const firmwareText = () => {
    if (lastState !== null) return `gpxcam ${lastState.version} (git ${lastState.git})`;
    return oldVersion !== null ? `gpxcam ${oldVersion}` : "gpxcam";
  };

  // False where the page may not write the clipboard: no clipboard (the app's web view), or the
  // window lost focus during a 30 s scan.
  const copyToClipboard = async (text) => {
    try {
      await navigator.clipboard.writeText(text);
      return true;
    } catch (error) {
      return false;
    }
  };

  // What the card sent, as it sent it; the text field shows it masked unless Copy unmasked is on.
  let diagRaw = "";
  const diagShown = () => (ui.diagUnmasked.checked ? diagRaw : maskIdentifiers(diagRaw));
  const copiedText = () => (ui.diagUnmasked.checked ? "Copied unmasked: send it only privately." :
    "Copied: paste it into a message.");

  const startDiagnostics = (text) => {
    busy = true;
    diagRaw = "";
    ui.diagText.value = "";
    refreshButtons();
    ui.diagAnswer.classList.remove("hidden");
    setStatus(ui.diagStatus, text, "busy");
  };

  // Shows what the card sent and copies it; result: [text, kind] of how the run ended.
  const showDiagnostics = async (text, [resultText, kind]) => {
    busy = false;
    ui.diagProgress.classList.add("hidden");
    diagRaw = text;
    ui.diagText.value = diagShown();
    refreshButtons();
    const copied = await copyToClipboard(ui.diagText.value);
    setStatus(ui.diagStatus, `${resultText} ${copied ? copiedText() :
      "Click Copy, or select the text below and copy it."}`, kind);
  };

  const copyLog = async () => {
    const asked = card;
    if (!asked || busy) return;
    startDiagnostics("Reading the card's log\u2026");
    const lines = await asked.log().catch(() => []);
    if (card !== asked) return;
    if (lines.length === 0) {
      busy = false;
      refreshButtons();
      setStatus(ui.diagStatus, NO_ANSWER_TEXT, "bad");
      return;
    }
    await showDiagnostics(diagnosticsText("Card log", firmwareText(), lines),
      [`The card's last ${lines.length} events.`, "good"]);
  };

  // 'diag scan' outside the one-command queue, as pairOlder(): the status refresh keeps running,
  // and its answers are left out. A bar shows the card's 30 s run.
  const scanForCameras = async () => {
    const asked = card;
    if (!asked || busy) return;
    startDiagnostics(`Scanning for ${SCAN_RUN_MS / 1000} seconds \u2013 keep the camera next to the card\u2026`);
    ui.diagProgress.max = SCAN_RUN_MS;
    ui.diagProgress.value = 0;
    ui.diagProgress.classList.remove("hidden");
    const lines = [];
    let ended = false;
    const stopWatch = asked.watchLines((line) => {
      if (ended || !belongsToScan(line)) return;
      lines.push(line);
      ended = scanEnded(line);
    });
    try {
      const started = Date.now();
      await asked.send("diag scan");
      while (!ended && Date.now() - started < SCAN_TIMEOUT_MS) {
        ui.diagProgress.value = Math.min(Date.now() - started, SCAN_RUN_MS);
        await sleep(250);
        if (card !== asked) return;
      }
    } catch (error) {
      // not sent: the card is gone, and its end is shown by onClosed
    } finally {
      stopWatch();
    }
    if (card !== asked) return;
    const version = lastState !== null ? lastState.version : oldVersion;
    await showDiagnostics(diagnosticsText("Scan for cameras", firmwareText(), lines), scanResultText(lines, version));
  };

  const copyAgain = async () => {
    const copied = await copyToClipboard(ui.diagText.value);
    setStatus(ui.diagStatus, copied ? copiedText() :
      "Copying did not work here: select the text below and copy it (Ctrl+C, on a Mac Cmd+C).", copied ? "good" : "");
  };

  ui.copyLog.addEventListener("click", copyLog);
  ui.scan.addEventListener("click", scanForCameras);
  ui.diagCopy.addEventListener("click", copyAgain);
  ui.diagUnmasked.addEventListener("change", () => {
    ui.diagText.value = diagShown();
    refreshButtons();
  });
  ui.pairStart.addEventListener("click", startPair);
  ui.pairRetry.addEventListener("click", () => (pairing.kind === "older" ? pairOlder() : startPair()));
  ui.pairOlder.addEventListener("click", pairOlder);
  ui.pairAgain.addEventListener("click", () => {
    pairing = { ...newPairing(), pairAgain: true };
    showPairing();
  });
  ui.pairKeep.addEventListener("click", () => {
    pairing.pairAgain = false;
    showPairing();
  });
  ui.pairStop.addEventListener("click", () => (pairing.running ? cancelOlder() : cancelPairing()));
  ui.wifiRetry.addEventListener("click", resumeOlder);
  ui.forget.addEventListener("click", () => ui.forgetConfirm.classList.remove("hidden"));
  ui.forgetNo.addEventListener("click", () => ui.forgetConfirm.classList.add("hidden"));
  ui.forgetYes.addEventListener("click", forgetCamera);
  ui.connect.addEventListener("click", connect);
  ui.disconnect.addEventListener("click", disconnect);
  ui.save.addEventListener("click", saveZone);
  ui.refresh.addEventListener("click", refresh);
  ui.clock.addEventListener("change",
    () => saveSetting(CLOCK_ANSWERS, ui.clock.checked ? "on" : "off", ui.clockAnswer, showClock));
  for (const [setting, choice] of soundChoices) {
    choice.addEventListener("change", () => saveSetting(SOUND_ANSWERS, setting, ui.soundAnswer, showSound));
  }
  for (const [setting, choice] of updatesChoices) {
    choice.addEventListener("change", () => saveSetting(UPDATES_ANSWERS, setting, ui.updatesAnswer, showUpdates));
  }
  ui.search.addEventListener("input", showResults);
  if (transport !== null) {
    transport.onDisconnect((port) => {
      if (card && port === card.port) card.close();
    });
  }
  showSelected();
  showResults();
  showSettings();
  showPairing();
}

if (typeof document !== "undefined") startPage();
