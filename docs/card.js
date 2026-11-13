"use strict";

// The card's console as the card page (docs/index.html) uses it: its values and commands, its time zone
// table (zones.js), its answers and state, and the connection over Web Serial or the desktop app.

// The card's serial console (src/main.cpp). Every value is fixed: the page has no setting for it.
const BAUD_RATE = 115200; // any rate works over USB; only the bootloader's reset rate must never be used
const CARD_USB = { usbVendorId: 0x2886, usbProductId: 0x8057 }; // Seeed, T1000-E running gpxcam (boards/t1000-e.json)
// The answers the card gives at once: 'v', 'status' and 'p' (src/main.cpp), 'pair', 'pair off' and 'pair
// prepare' (src/pairing.cpp), 'log' with its last 64 lines (src/log.cpp dumpHistory()).
const ANSWER_TIMEOUT_MS = 3000;
const STATUS_PERIOD_MS = 5000; // the card's state on the page refreshes itself this often
const PAIRING_STATUS_PERIOD_MS = 2000; // ... and this often while the card pairs
const UNPAIR_TIMEOUT_MS = 10000; // the card first puts its GPS to sleep (up to 7.5 s), then writes flash
// Pairing as the card does it (src/pairing.cpp MAX_ATTEMPTS, FIX_WAIT_MS, SEARCH_MS; the page's test compares):
// the card looks for a camera in pairing mode PAIR_SEARCH_MS, and again as long after each quick retry and each
// pause between attempts; its first line says it ("up to 120 s").
const PAIR_ATTEMPTS = 3;
const PAIR_FIX_WAIT_MS = 120 * 1000;
const PAIR_SEARCH_MS = 120 * 1000;
// 'pair older': the card may wait for a GPS fix, then for the camera, then connect and introduce itself.
const OLDER_START_TIMEOUT_MS = PAIR_FIX_WAIT_MS + PAIR_SEARCH_MS + 30 * 1000;
// The commands that save a setting (time zone, clock, sound, location updates; 'tz' alone too): saving
// writes flash, about 1 s, more the first time.
const SAVE_TIMEOUT_MS = 6000;
const SOUND_SETTINGS = ["loud", "quiet", "off"]; // src/sound_rules.cpp settingName()
const UPDATES_SETTINGS = ["normal", "frequent"]; // src/link_policy.cpp updatesName()
const ANSWER_QUIET_MS = 400; // an answer is complete after this long without another of its lines
const SCAN_RUN_MS = 30000; // one 'diag scan' run (src/scan_dump.h DURATION_MS)
const SCAN_TIMEOUT_MS = SCAN_RUN_MS + 10000; // the run ends with "diag scan: done"; this much is waited at most
const MAX_RESULTS = 60;
const NO_ANSWER_TEXT = "No answer from the card. Try again.";

// ---- Zones (pure functions, tested with node --test test/web/card_page.test.mjs) ----

const ZONES = new Map(TZ_DATA.zones.map(([name, countries, comment]) => [name, { countries, comment }]));
const NAMES_LOWER = new Map(); // lower-case name: name as the card's table spells it
for (const name of ZONES.keys()) NAMES_LOWER.set(name.toLowerCase(), name);
for (const alias of Object.keys(TZ_DATA.aliases)) NAMES_LOWER.set(alias.toLowerCase(), alias);
const COUNTRY_OF_NAME = new Map(); // a zone name as named for a country (zone.tab): "Denmark", "United States, Eastern ..."
for (const [country, zones] of Object.values(TZ_DATA.countries)) {
  for (const [name, comment] of zones) {
    if (!COUNTRY_OF_NAME.has(name)) COUNTRY_OF_NAME.set(name, comment ? `${country}, ${comment}` : country);
  }
}

// A name the card knows, in any case: { name (as the table spells it), zone (the zone it stands for) }.
function findZone(text) {
  const name = NAMES_LOWER.get(String(text).trim().toLowerCase());
  if (!name) return null;
  return { name, zone: TZ_DATA.aliases[name] || name };
}

// A name the card knows but the page does not offer (TZ_DATA.legacy: the old POSIX-style names, such as
// PST8PDT), as findZone() gives a zone; null for any other text.
function legacyZone(name) {
  return Object.hasOwn(TZ_DATA.legacy, name) ? { name, zone: TZ_DATA.legacy[name] } : null;
}

function countryName(code) {
  const country = TZ_DATA.countries[code];
  return country ? country[0] : code;
}

function searchable(text) {
  return text.toLowerCase().replace(/_/g, " ");
}

// "Germany, Denmark, ..." for a zone, from zone1970.tab.
function zoneCountries(zone) {
  const entry = ZONES.get(zone);
  return entry ? entry.countries.map(countryName).join(", ") : "";
}

// What a name of the card's table stands for: the countries and IANA's comment of a zone, or
// the zone of an alias (with the country a zone.tab name belongs to).
function detailOf(name) {
  const zone = TZ_DATA.aliases[name];
  if (zone) {
    const country = COUNTRY_OF_NAME.get(name);
    return country ? `${country} (same as ${zone})` : `same as ${zone}`;
  }
  const entry = ZONES.get(name);
  return [zoneCountries(name), entry ? entry.comment : ""].filter(Boolean).join(" \u00b7 ");
}

// The list before anything is typed: the computer's zone first, then every zone of the card.
function startList(computer) {
  const results = [];
  if (computer) {
    const detail = [detailOf(computer.name), "your computer"].filter(Boolean).join(" \u00b7 ");
    results.push({ name: computer.name, zone: computer.zone, detail });
  }
  for (const name of ZONES.keys()) {
    if (!computer || name !== computer.name) results.push({ name, zone: name, detail: detailOf(name) });
  }
  return results;
}

// The city part of a zone name: "New_York" of "America/New_York".
function cityOf(name) {
  return name.slice(name.lastIndexOf("/") + 1);
}

// Search results, best first. First what starts with the text: countries by name (or their
// two-letter code), as named for the country (zone.tab: Denmark gives Europe/Copenhagen), then
// zones and aliases by city or full name. Then what only contains it, in the same order, zones
// also by their comment. Each name appears once.
function searchZones(query) {
  const q = searchable(String(query).trim());
  if (!q) return [];
  const results = [];
  const seen = new Set();
  const add = (name, detail) => {
    if (seen.has(name) || results.length >= MAX_RESULTS) return;
    seen.add(name);
    results.push({ name, zone: TZ_DATA.aliases[name] || name, detail });
  };
  for (const atStart of [true, false]) {
    const matches = (text) => (atStart ? text.startsWith(q) : text.includes(q));
    const nameMatches = (name) => matches(searchable(name)) || matches(searchable(cityOf(name)));
    for (const [code, [country, zones]] of Object.entries(TZ_DATA.countries)) {
      if (matches(searchable(country)) || (atStart && code.toLowerCase() === q)) {
        for (const [name, comment] of zones) add(name, comment ? `${country}, ${comment}` : country);
      }
    }
    for (const [name, { comment }] of ZONES) {
      if (nameMatches(name) || (!atStart && searchable(comment).includes(q))) add(name, detailOf(name));
    }
    for (const alias of Object.keys(TZ_DATA.aliases)) {
      if (nameMatches(alias)) add(alias, detailOf(alias));
    }
  }
  return results;
}

// "UTC+05:30" now, from the browser's own time zone data; "" if the browser does not know the zone.
function utcOffsetText(zone, date = new Date()) {
  try {
    const parts = new Intl.DateTimeFormat("en-US", { timeZone: zone, timeZoneName: "longOffset" }).formatToParts(date);
    const offset = parts.find((part) => part.type === "timeZoneName").value; // "GMT+05:30" or "GMT"
    return offset === "GMT" ? "UTC+00:00" : offset.replace("GMT", "UTC");
  } catch (error) {
    return "";
  }
}

// The name to offer for a zone the browser reports: browsers often give old aliases
// (Asia/Calcutta, Europe/Kiev); those become their zone, but a country's own name stays
// (Europe/Copenhagen, from zone.tab).
function preferredZone(text) {
  const found = findZone(text);
  if (found && found.name !== found.zone && !COUNTRY_OF_NAME.has(found.name)) return findZone(found.zone);
  return found;
}

// The computer's time zone, if the card knows it.
function computerZone() {
  try {
    return preferredZone(Intl.DateTimeFormat().resolvedOptions().timeZone);
  } catch (error) {
    return null;
  }
}

// ---- The card's console (pure functions) ----

// The only commands the page may send (test/commands.json): 'p' only to read the card's id for resuming
// older pairing and how long a search for the camera has run, 'tz NAME' only with a name exactly as the
// table spells it.
const FIXED_COMMANDS = new Set(["v", "status", "pair", "pair off", "pair older", "pair prepare", "p", "unpair",
  "clock on", "clock off",
  ...UPDATES_SETTINGS.map((setting) => `updates ${setting}`), ...SOUND_SETTINGS.map((setting) => `sound ${setting}`),
  "log", "diag scan", "tz"]);
function allowedCommand(command) {
  if (FIXED_COMMANDS.has(command)) return true;
  const match = /^tz ([A-Za-z0-9/_+-]{1,40})$/.exec(command);
  return match !== null && findZone(match[1])?.name === match[1];
}

function tzCommand(text) {
  const found = findZone(text);
  return found ? `tz ${found.name}` : null;
}

// "1.2.0" from the card's version line: "gpxcam-t1000e 1.2.0 (git 1234567), uptime ...".
function parseVersion(line) {
  const match = /^gpxcam-t1000e (\d+\.\d+\.\d+)/.exec(line);
  return match ? match[1] : null;
}

// has(prefix) for these lines: whether one of them starts with prefix.
const lineFinder = (lines) => (prefix) => lines.some((line) => line.startsWith(prefix));

// Which lines answer a command: its own (prefix) and the console's ("command: unknown ...").
const answerOf = (prefix) => (line) => line.startsWith(prefix) || line.startsWith("command: ");

const isTzAnswer = answerOf("tz: ");

// What the card answered to 'tz' or 'tz NAME' (src/main.cpp setTimeZone(), tz_store::logState()).
function classifyTzAnswer(lines) {
  const has = lineFinder(lines);
  if (lines.length === 0) return "none";
  if (has("command: unknown")) return "no-tz"; // firmware before 1.2.0
  if (has("command: too long")) return "unknown";
  if (has("tz: saved")) return "saved";
  if (has("tz: already set")) return "unchanged";
  if (has("tz: unknown")) return "unknown";
  if (has("tz: could not write")) return "not-saved";
  return "state";
}

// [text, kind] after saving the time zone `name`; for any other answer the page shows the card's lines.
function tzAnswerText(name, answer) {
  switch (answer) {
    case "saved": return [`Saved: ${name}.`, "good"];
    case "unchanged": return [`${name} was already set on the card.`, "good"];
    case "unknown": return ["The card does not know this time zone. Is its firmware older than this page?", "bad"];
    case "not-saved": return ["The card could not save the time zone. Try again.", "bad"];
    case "none": return [NO_ANSWER_TEXT, "bad"];
    default: return ["The card answered:", ""];
  }
}

// A setting the card saves with one command, '<name> <value>': 'clock on|off', 'sound loud|quiet|off',
// 'updates normal|frequent' (src/main.cpp setClock(), setSound(), setUpdates()). isAnswer: the lines of
// its answer; classify(lines): what the card answered; text(value, answer): [text, kind] for the page,
// with savedText(value) when it was saved.
function settingAnswers(name, values, savedText) {
  return {
    name,
    isAnswer: answerOf(`${name}: `),
    classify(lines) {
      const has = lineFinder(lines);
      if (lines.length === 0) return "none";
      if (has("command: unknown")) return `no-${name}`;
      if (values.some((value) => has(`${name}: ${value} saved`))) return "saved";
      if (has(`${name}: already`)) return "unchanged";
      if (has(`${name}: could not write`)) return "not-saved";
      return "state";
    },
    text(value, answer) {
      switch (answer) {
        case "saved": return [savedText(value), "good"];
        case "unchanged": return ["The card already had this setting.", "good"];
        case "not-saved": return ["The card could not save the setting. Try again.", "bad"];
        default: return [NO_ANSWER_TEXT, "bad"];
      }
    },
  };
}

const CLOCK_ANSWERS = settingAnswers("clock", ["on", "off"], (value) => (value === "on"
  ? "Saved. The camera gets the time at the next connection: switch the camera off and on."
  : "Saved. It counts from the next connection."));
const SOUND_ANSWERS = settingAnswers("sound", SOUND_SETTINGS, () => "Saved.");
const UPDATES_ANSWERS = settingAnswers("updates", UPDATES_SETTINGS,
  () => "Saved. The card uses it from the next position it sends.");

// What the setting means. version: gpxcam that cannot change it (before 1.3.0), or null.
function clockNoteText(clockOn, version = null) {
  if (version) return `gpxcam ${version} always sets the camera's clock. Switching it off needs version 1.3.0.`;
  if (clockOn) return "Each time it connects, the card sets the camera's clock from its GPS, in the time zone below.";
  return "The card leaves the camera's clock alone: set the time and the time zone in the camera's menu. " +
    "Pairing still sets the clock once.";
}

// What a sound setting means (src/sound_rules.h); null: none shown yet.
function soundText(setting) {
  switch (setting) {
    case "loud": return "All sounds.";
    case "quiet": return "All sounds, quieter.";
    case "off": return "Only quiet beeps when you hold the button, so you know when to let go.";
    default: return "How loud the card is.";
  }
}

// Under the choice. version: gpxcam that cannot change it (before 1.3.0), or null.
function soundNoteText(version = null) {
  if (version) return `gpxcam ${version} always plays its sounds loud. Choosing needs version 1.3.0.`;
  return "Choosing Loud or Quiet plays two short beeps at that volume.";
}

// What a setting means (src/link_policy.h NORMAL_TIMING, FREQUENT_TIMING); null: none shown yet.
function updatesText(setting) {
  switch (setting) {
    case "normal": return "Every 25 seconds, sooner when you move 20 m. Enough for photos from one place.";
    case "frequent":
      return "Every 5 seconds, sooner when you move 5 m. For photos on the move: on foot, by bike, by car.";
    default: return "How often the card sends the camera its position.";
  }
}

// Under the choice. version: gpxcam that cannot change it (before 1.4.0), or null.
function updatesNoteText(version = null) {
  if (version) return `gpxcam ${version} sends the position every 25 seconds. Choosing needs version 1.4.0.`;
  return "The camera keeps the last position it got: on the move, Frequent puts photos closer to where you took them.";
}

// While the card's guard holds a connection at the normal timing (src/link_policy.h RateGuard):
// the camera answered late, or the connection before broke off. Null otherwise.
function updatesNowText(state) {
  if (state.updates !== "frequent" || state.updatesNow !== "normal") return null;
  return "For now every 25 seconds: the camera answers slowly. Frequent comes back when you switch the camera " +
    "off and on.";
}

// While the card cannot read its saved settings it refuses to pair or forget the camera: it could
// not save the change (src/pairing_store.h writable()).
const STORAGE_PAIRING_TEXT = "The card cannot change the pairing while it cannot read its saved settings " +
  "(see the note above).";

// When the card cannot mount its file system (src/storage.h): it works with the default settings and
// the pairing from its copy, if it has one (src/pairing_copy.h); only the console command
// 'storage format yes' erases what is there. Null otherwise.
function storageNoteText(state) {
  if (state.storage !== "damaged") return null;
  const pairing = state.paired ? "The pairing comes from its copy, so it still connects to the camera."
    : "It has no pairing and cannot connect to the camera.";
  return `The card cannot read its saved settings and works with the defaults. ${pairing} Repairing erases ` +
    "the saved settings; the README says how.";
}

const isPairAnswer = answerOf("pair: ");

// What the card answered to 'unpair' (src/main.cpp forgetCamera()).
function classifyUnpairAnswer(lines) {
  const has = lineFinder(lines);
  if (lines.length === 0) return "none";
  if (has("pair: camera forgotten")) return "forgotten";
  if (has("pair: no camera paired")) return "not-paired";
  if (has("pair: not possible - the file system is damaged")) return "storage-damaged"; // src/main.cpp
  return "failed"; // "could not write the pairing file", "not available"
}

function unpairAnswerText(answer) {
  switch (answer) {
    case "forgotten": return ["The camera is forgotten.", "good"];
    case "not-paired": return ["No camera was paired.", "good"];
    case "failed": return ["The card could not forget the camera. Try again.", "bad"];
    case "storage-damaged": return [STORAGE_PAIRING_TEXT, "bad"];
    default: return [NO_ANSWER_TEXT, "bad"];
  }
}

// Splits the card's output into lines; a line may come in several pieces.
class LineSplitter {
  constructor() {
    this.rest = "";
  }
  push(text) {
    const parts = (this.rest + text).split("\n");
    this.rest = parts.pop();
    return parts.map((line) => line.replace(/\r$/, ""));
  }
}

// Why the card did not answer 'v'. In the browser, a read that fails at once while the card is plugged
// in comes from Linux: a program using pyserial (a serial monitor such as pio device monitor) leaves the
// port with VMIN=0, which the kernel keeps for the next program, and Chromium then takes the first empty
// read for a lost device. Plugging the card in again resets the port's settings. The app opens the port
// raw (app/src-tauri/src/card.rs), so its read errors get only the plain advice.
function noAnswerText(readError) {
  if (readError instanceof AppReadError) {
    return `Reading from the card failed (${readErrorText(readError)}). Unplug the card, plug it in again and ` +
      "click Connect.";
  }
  if (readError) {
    return `Reading from the card failed (${readError.message}). On Linux this happens after another serial ` +
      "program, such as a serial monitor, used the card: unplug the card, plug it in again and click Connect.";
  }
  return "No answer from the card. Is it a T1000\u2011E with gpxcam? Unplug it, plug it in again and retry.";
}

// What stops the page after the card's answer to 'tz' when connecting, or null if it can go on.
function tzProblemText(version, answer) {
  if (answer === "no-tz") return `gpxcam ${version} cannot be set up from this page. Install the newest gpxcam first.`;
  if (answer === "none") return "No answer from the card to 'tz'. Unplug it, plug it in again and retry.";
  return null;
}

// What stops the page after the card's answer to 'status' when connecting (Card.status() kind), or null.
function statusProblemText(kind) {
  if (kind === "none") return "No answer from the card to 'status'. Unplug it, plug it in again and retry.";
  if (kind === "newer") return "The card's gpxcam is newer than this page. Reload the page.";
  return null;
}

// A line of the card's answer to 'log' (src/log.cpp dumpHistory()): "[  12345.6 s] text".
function isLogLine(line) {
  return /^\[ *\d+\.\d s\] /.test(line);
}

// ---- The card's state (pure functions) ----

// The keys of the answer to 'status' in format 1 (src/status_text.h). Not among them, as older gpxcam does
// not send them: updates, updates_now, watchdog_restart and storage (gpxcam 1.4.0), gps_hold (1.5.0-beta.2).
const STATUS_KEYS = ["format", "fw", "git", "battery_mv", "battery_pct", "charger", "gps", "gps_fix", "gps_sat",
  "paired", "link", "pair", "pair_attempt", "pair_result", "clock", "tz", "sound"];

const isStatusAnswer = answerOf("status: ");

function statusAnswerDone(lines) {
  const last = lines[lines.length - 1];
  return last === "status: end" || last.startsWith("command: ");
}

// The card's answer to 'status': "status: key=value ..." lines, then "status: end". Gives
// { kind: "ok", state }, or the kind alone: "no-status" (gpxcam before 1.3.0), "newer" (a
// format this page does not know) or "none" (no answer, or lines missing).
function parseStatus(lines) {
  if (lines.some((line) => line.startsWith("command: unknown"))) return { kind: "no-status" };
  if (lines.length === 0 || lines[lines.length - 1] !== "status: end") return { kind: "none" };
  const values = {};
  for (const line of lines.slice(0, -1)) {
    for (const pair of line.slice("status: ".length).split(" ")) {
      const at = pair.indexOf("=");
      if (at > 0) values[pair.slice(0, at)] = pair.slice(at + 1);
    }
  }
  if ("format" in values && values.format !== "1") return { kind: "newer" };
  if (!STATUS_KEYS.every((key) => key in values)) return { kind: "none" };
  return {
    kind: "ok",
    state: {
      version: values.fw,
      git: values.git,
      batteryMv: Number(values.battery_mv),
      batteryPercent: Number(values.battery_pct),
      charger: values.charger,
      gpsOn: values.gps === "on",
      gpsFix: values.gps_fix === "1",
      gpsSatellites: Number(values.gps_sat),
      paired: values.paired === "1",
      link: values.link,
      pair: values.pair,
      pairAttempt: Number(values.pair_attempt),
      pairResult: values.pair_result,
      clockOn: values.clock === "on",
      timeZone: values.tz,
      sound: values.sound,
      updates: values.updates ?? null, // null: gpxcam before 1.4.0, always normal
      updatesNow: values.updates_now ?? null,
      storage: values.storage ?? null, // null: gpxcam before 1.4.0
      // null: gpxcam before 1.5.0-beta.2, without 'pair prepare'
      gpsHold: values.gps_hold !== undefined ? Number(values.gps_hold) : null,
    },
  };
}

// The card's UUID from its answer to 'p': "pair: card id 00112233-4455-4677-8899-a1b2c3d4e5f6, ..."
// (src/pairing_store.cpp, lower case with dashes), or null.
function parseCardId(line) {
  const match = /^pair: card id ([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})\b/.exec(line);
  return match ? match[1] : null;
}

// ---- The connection to the card ----

class Card {
  constructor(port) {
    this.port = port;
    this.splitter = new LineSplitter();
    this.waiters = new Set();
    this.reader = null;
    this.readError = null; // why reading ended, if it failed
    this.onClosed = null; // called once when reading ends (unplugged, closed)
    this.queue = Promise.resolve(); // one command at a time: answers are told apart only by their lines
    this.writing = Promise.resolve(); // one write at a time: a second writer on the port would throw
  }

  async open() {
    await this.port.open({ baudRate: BAUD_RATE });
    await this.port.setSignals({ dataTerminalReady: true }); // the card writes its console only with DTR
    this.readLoop();
  }

  async readLoop() {
    const decoder = new TextDecoder();
    try {
      this.reader = this.port.readable.getReader();
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        for (const line of this.splitter.push(decoder.decode(value, { stream: true }))) this.deliver(line);
      }
    } catch (error) {
      this.readError = error; // unplugged, or see noAnswerText()
    } finally {
      if (this.reader) this.reader.releaseLock();
      this.reader = null;
      if (this.onClosed) this.onClosed();
    }
  }

  deliver(line) {
    for (const waiter of this.waiters) waiter(line);
  }

  // Writes one command after any write still under way. Commands sent outside the one-command queue
  // ('pair older', 'diag scan') would otherwise meet a status request being written, and taking a
  // second writer of the port's stream throws while the first one holds it.
  send(command) {
    if (!allowedCommand(command)) return Promise.reject(new Error(`not sent: ${command}`));
    const written = this.writing.then(async () => {
      const writer = this.port.writable.getWriter();
      try {
        await writer.write(new TextEncoder().encode(command + "\n"));
      } finally {
        writer.releaseLock();
      }
    });
    this.writing = written.catch(() => {});
    return written;
  }

  // Sends a command, after the answers to the commands before it, and collects the lines that
  // belong to its answer: until done(lines) says so, ANSWER_QUIET_MS after the last such line,
  // or timeoutMs without any.
  ask(command, belongs, timeoutMs, done = () => false) {
    const asked = this.queue.then(() => this.askNow(command, belongs, timeoutMs, done));
    this.queue = asked.catch(() => {});
    return asked;
  }

  askNow(command, belongs, timeoutMs, done) {
    return new Promise((resolve, reject) => {
      const lines = [];
      let quiet = null;
      const finish = () => {
        clearTimeout(timeout);
        clearTimeout(quiet);
        this.waiters.delete(waiter);
        resolve(lines);
      };
      const waiter = (line) => {
        if (!belongs(line)) return;
        lines.push(line);
        clearTimeout(quiet);
        if (done(lines)) finish();
        else quiet = setTimeout(finish, ANSWER_QUIET_MS);
      };
      const timeout = setTimeout(finish, timeoutMs);
      this.waiters.add(waiter);
      this.send(command).catch((error) => {
        clearTimeout(timeout);
        this.waiters.delete(waiter);
        reject(error);
      });
    });
  }

  async version() {
    const lines = await this.ask("v", (line) => parseVersion(line) !== null, ANSWER_TIMEOUT_MS, () => true);
    return lines.length ? parseVersion(lines[0]) : null;
  }

  async status() {
    return parseStatus(await this.ask("status", isStatusAnswer, ANSWER_TIMEOUT_MS, statusAnswerDone));
  }

  pair() {
    return this.ask("pair", isPairAnswer, ANSWER_TIMEOUT_MS, () => true);
  }

  cancelPairing() {
    return this.ask("pair off", isPairAnswer, ANSWER_TIMEOUT_MS, () => true);
  }

  unpair() {
    return this.ask("unpair", isPairAnswer, UNPAIR_TIMEOUT_MS, () => true);
  }

  log() {
    return this.ask("log", isLogLine, ANSWER_TIMEOUT_MS);
  }

  // The card's UUID (parseCardId()) from 'p', or null.
  async cardId() {
    const lines = await this.ask("p", (line) => parseCardId(line) !== null, ANSWER_TIMEOUT_MS, () => true);
    return lines.length ? parseCardId(lines[0]) : null;
  }

  // The pairing lines of 'p' (the card's state for a person); the page reads only how long a search for the
  // camera has run (pairingEvent(), through watchLines()).
  pairingDetails() {
    return this.ask("p", (line) => line.startsWith("pair: "), ANSWER_TIMEOUT_MS);
  }

  prepareGps() {
    return this.ask("pair prepare", isPairAnswer, ANSWER_TIMEOUT_MS, () => true);
  }

  // Calls onLine for every line the card sends, until the returned stop() is called. Unlike ask(),
  // it does not use the one-command queue, so the status refresh keeps running meanwhile: the
  // older-pairing wizard watches the card's progress while it waits for the Wi-Fi step, and Scan for
  // cameras collects a 'diag scan' run.
  watchLines(onLine) {
    this.waiters.add(onLine);
    return () => this.waiters.delete(onLine);
  }

  // One of the settings saved with one command: answers is CLOCK_ANSWERS, SOUND_ANSWERS or UPDATES_ANSWERS.
  save(answers, value) {
    return this.ask(`${answers.name} ${value}`, answers.isAnswer, SAVE_TIMEOUT_MS);
  }

  timeZone(name) {
    const command = name ? tzCommand(name) : "tz";
    if (!command) return Promise.reject(new Error(`unknown time zone: ${name}`));
    return this.ask(command, isTzAnswer, SAVE_TIMEOUT_MS);
  }

  async close() {
    try {
      if (this.reader) await this.reader.cancel();
    } catch (error) {
      // already gone
    }
    try {
      await this.port.close();
    } catch (error) {
      // already closed or unplugged
    }
  }
}

// ---- How the page reaches the card ----
// A transport finds the card's port and tells when a port is unplugged. The port is an object
// like Web Serial's SerialPort (open, setSignals, readable, writable, close), and Card uses only
// that, whatever the transport.

// Web Serial in Chrome and Edge: the browser's chooser, filtered by the card's USB id.
function webSerialTransport(navigatorObject, windowObject) {
  if (!("serial" in navigatorObject) || !windowObject.isSecureContext) return null;
  return {
    name: "web-serial",
    // The port the user chose; rejects when the chooser was closed without one.
    requestPort: () => navigatorObject.serial.requestPort({ filters: [CARD_USB] }),
    // Calls listener(port) when a port is unplugged.
    onDisconnect: (listener) =>
      navigatorObject.serial.addEventListener("disconnect", (event) => listener(event.target)),
    // Calls listener(port) when a card the user chose before is plugged in; gives the function that stops it.
    watchForCard: (listener) => {
      const pluggedIn = (event) => listener(event.target);
      navigatorObject.serial.addEventListener("connect", pluggedIn);
      return () => navigatorObject.serial.removeEventListener("connect", pluggedIn);
    },
  };
}

// The desktop app (app/): its Rust side opens the card's port at 115200 baud with DTR, sends the
// card only the commands of the page's list (test/commands.json) and passes the card's lines
// over a Tauri channel. Tauri gives window.__TAURI__ (app.withGlobalTauri). The page sees a port
// like Web Serial's.
class AppPort {
  constructor(core, path) {
    this.core = core;
    this.path = path;
    this.readable = null;
    this.writable = null;
  }

  // The app always opens at 115200 baud, so the options are not passed on.
  async open() {
    const encoder = new TextEncoder();
    let lines;
    this.readable = new ReadableStream({ start: (controller) => { lines = controller; } });
    const settle = (change) => {
      try {
        change();
      } catch (error) {
        // the reader was cancelled: nothing waits for it
      }
    };
    const channel = new this.core.Channel();
    channel.onmessage = (event) => {
      if (event.kind === "line") settle(() => lines.enqueue(encoder.encode(event.text + "\n")));
      else if (event.kind === "closed" && event.error) settle(() => lines.error(new AppReadError(event.error)));
      else if (event.kind === "closed") settle(() => lines.close());
    };
    await this.core.invoke("open_card", { path: this.path, onEvent: channel });
    this.writable = new WritableStream({
      write: (chunk) => this.core.invoke("send", { command: new TextDecoder().decode(chunk).replace(/\n$/, "") }),
    });
  }

  async setSignals() {} // the app sets DTR itself when it opens the port

  async close() {
    await this.core.invoke("close_card");
  }
}

// Why the app's reading of the card ended, with the app's word as its message (app/src-tauri/src/lib.rs
// Problem): port-closed, or port-failed when the card was unplugged.
class AppReadError extends Error {}

// Why reading from the card ended, for the page's messages: the app's word as text, or the browser's
// own message (Web Serial).
function readErrorText(error) {
  if (!(error instanceof AppReadError)) return error.message;
  if (error.message === "port-closed") return "the card's port closed";
  if (error.message === "port-failed") return "the card's port failed";
  return "the connection failed";
}

// Thrown when no card is plugged in: the app has no chooser that would show an empty list.
class NoCardError extends Error {}

// How soon the app notices a card plugged in again: its list of cards is read this often meanwhile.
const CARD_RETURN_POLL_MS = 1000;

// The app lists the cards itself (USB 2886:8057): one is taken at once; with more, choose(paths)
// lets the user pick one and gives its path, or null. It also does an older camera's Wi-Fi step.
function appTransport(windowObject) {
  const core = windowObject.__TAURI__ && windowObject.__TAURI__.core;
  if (!core) return null;
  return {
    name: "app",
    requestPort: async (choose) => {
      const paths = (await core.invoke("list_cards")).map((card) => card.path);
      if (paths.length === 0) throw new NoCardError("no card");
      const path = paths.length === 1 ? paths[0] : await choose(paths);
      if (!path) throw new Error("no card chosen");
      return new AppPort(core, path);
    },
    // An unplugged card ends the app's channel with an error, and with it the reading.
    onDisconnect: () => {},
    // Calls listener(port) once one card is listed (with more, the user chooses); gives the function that
    // stops it.
    watchForCard: (listener) => {
      let stopped = false;
      let timer = null;
      const look = async () => {
        const paths = (await core.invoke("list_cards").catch(() => [])).map((card) => card.path);
        if (stopped) return;
        if (paths.length === 1) {
          listener(new AppPort(core, paths[0]));
          return;
        }
        timer = setTimeout(look, CARD_RETURN_POLL_MS);
      };
      timer = setTimeout(look, CARD_RETURN_POLL_MS);
      return () => {
        stopped = true;
        clearTimeout(timer);
      };
    },
    // Registers the identity and name over the camera's Wi-Fi (app/src-tauri/src/older_wifi.rs); onStage
    // gets each stage. Gives the outcome, or fails with the word of the app's problem.
    registerOlder: (identity, name, onStage) => {
      const channel = new core.Channel();
      channel.onmessage = (event) => onStage(event.kind);
      return core.invoke("register_older", { identity, name, onEvent: channel });
    },
    // Stops a running registerOlder(), which then gives "cancelled".
    cancelOlder: () => core.invoke("cancel_older").catch(() => {}),
  };
}

// The transport of this page, or null where it cannot reach the card: the app first, as its web
// view may also offer Web Serial one day.
function chooseTransport(navigatorObject, windowObject) {
  return appTransport(windowObject) || webSerialTransport(navigatorObject, windowObject);
}

// Why the card's port did not open. The app answers with a word (app/src-tauri/src/card.rs).
function openProblemText(error) {
  const reason = error instanceof Error ? error.message : String(error);
  if (reason === "permission") {
    return "This computer does not let the app use the card's port. On Linux, add yourself to the dialout " +
      "group (sudo usermod -aG dialout $USER), log out and in again.";
  }
  if (reason === "not-found") return "The card is gone. Plug it in again and click Connect.";
  return "Could not open the card. Close other programs that use it (a serial monitor) and try again.";
}
