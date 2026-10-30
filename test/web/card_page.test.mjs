// Tests of the card page, docs/index.html, on the PC:
// node --test test/web/card_page.test.mjs
// The page's scripts run in a separate context without a browser; the card is simulated with
// answers in the form of the real card's console (src/main.cpp, src/tz_store.cpp), made-up values.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import vm from "node:vm";

import { simulatedApp } from "./simulated_app.mjs";

const PAGE = new URL("../../docs/index.html", import.meta.url);
const html = readFileSync(PAGE, "utf8");
// The page's scripts in its order: the zone data it loads from docs/zones.js, then its own.
const zonesScript = readFileSync(new URL("../../docs/zones.js", import.meta.url), "utf8");
assert.match(html, /<script src="zones\.js"><\/script>\s*<script>/);
const scripts = [zonesScript, ...[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((match) => match[1])];
const appCode = scripts[1];
const page = vm.runInContext(
  scripts.join("\n") +
    "\n;({ TZ_DATA, BAUD_RATE, CARD_USB, MAX_RESULTS, findZone, searchZones, utcOffsetText, allowedCommand," +
    " tzCommand, parseVersion, classifyTzAnswer, LineSplitter, Card, preferredZone, noAnswerText, tzProblemText," +
    " startList, parseStatus, cameraTile, gpsTile, batteryTile, zoneTile, CLOCK_ANSWERS, SOUND_ANSWERS," +
    " UPDATES_ANSWERS, clockNoteText, classifyUnpairAnswer, unpairAnswerText," +
    " SOUND_SETTINGS, soundNoteText, soundText, UPDATES_SETTINGS, updatesNoteText, updatesText, updatesNowText," +
    " storageNoteText, chooseTransport, tzAnswerText, statusProblemText," +
    " openProblemText, parseOlderRegister, olderOutcomeText, FIXED_COMMANDS, isLogLine," +
    " belongsToScan, scanEnded, scanResultText, diagnosticsText, SCAN_RUN_MS, shouldPrepareGps," +
    " maskIdentifiers, parseCardId, olderIdentity, PAIRING_STATUS_PERIOD_MS, AppReadError, readErrorText," +
    " PAIR_SEARCH_MS, pairingScreen, pairingSteps, pairingBadge, pairingEvent, searchLeftMs, countdownText," +
    " countdownLabel, connectStages, cameraStepText, gpsCheck, failedText, canPairOlder, WIFI_WAIT_TEXT })",
  vm.createContext({ TextEncoder, TextDecoder, ReadableStream, WritableStream, setTimeout, clearTimeout, console }),
);

// Values made in the page's context: compared as JSON, since their prototypes are not ours.
const json = (value) => JSON.parse(JSON.stringify(value));

const VERSION_ANSWER = ["gpxcam-t1000e 1.2", ".0 (git 1234567), uptime 12 s, log dropped 0\r\nreset: 0x00000004 (SREQ)\r\n"];
const TZ_STATE = [
  "tz: Europe/Warsaw, rule CET-1CEST,M3.5.0,M10.5.0/3 (tz database 2026e)\r\n",
  "battery: 4100 mV, USB, charging\r\n",
  "tz: now UTC+02:00, the camera gets 60 min + summer time\r\n",
];
const TZ_SAVED = [
  "tz: saved - the camera gets it at the next connection\r\n",
  "tz: America/New_York, rule EST5EDT,M3.2.0,M11.1.0 (tz database 2026e)\r\ntz: now UTC-04:00, the camera ",
  "gets -300 min + summer time\r\n",
];
const VERSION_1_3 = ["gpxcam-t1000e 1.3.0 (git 1234567), uptime 12 s, log dropped 0\r\n"];
const OLD_FIRMWARE = ["command: unknown 'tz'\r\n", "commands: v = version and reset reason,\r\n"];
const STATUS_ANSWER = [
  "status: format=1 fw=1.3.0 git=1234567\r\nstatus: battery_mv=4100 battery_pct=89 charger=charging\r\n",
  "gps: fix yes, 11 sat, alt 157 m, 19:05:12 UTC 2027-07-01\r\n", // a log line in between
  "status: gps=on gps_fix=1 gps_sat=11\r\nstatus: paired=1 link=connected\r\n",
  "status: pair=idle pair_attempt=0 pair_result=none\r\nstatus: clock=on tz=Europe/Warsaw\r\n",
  "status: sound=loud\r\nsta",
  "tus: end\r\n",
];
const STATE = {
  version: "1.3.0", git: "1234567", batteryMv: 4100, batteryPercent: 89, charger: "charging", gpsOn: true,
  gpsFix: true, gpsSatellites: 11, paired: true, link: "connected", pair: "idle", pairAttempt: 0,
  pairResult: "none", clockOn: true, timeZone: "Europe/Warsaw", sound: "loud", updates: null, updatesNow: null,
  storage: null, gpsHold: null,
};
const NO_STATUS = ["command: unknown 'status'\r\n", "commands: v = version and reset reason,\r\n"];
const statusLines = (text) => text.trim().split("\n");
const STATUS_CLOCK_OFF = STATUS_ANSWER.map((piece) => piece.replace("clock=on", "clock=off"));
const CLOCK_OFF_SAVED = [
  "clock: off saved - from the next connection\r\n",
  "clock: off - the card leaves the camera's clock alone; set it in the camera's menu\r\n",
];
const CLOCK_NOT_SAVED = ["clock: could not write the time zone file\r\n"];
// The keys of the card's answer to 'status', from its description in src/status_text.h.
const FIRMWARE_STATUS_KEYS = new Set(
  [...readFileSync(new URL("../../src/status_text.h", import.meta.url), "utf8").matchAll(/^\/\/ {3}status: (.+)$/gm)]
    .flatMap((match) => match[1].split(" ").filter((pair) => pair.includes("=")).map((pair) => pair.split("=")[0])),
);
// STATUS_ANSWER (or another answer) with other values, e.g. statusWith({ paired: 0, link: "off" }). A key the
// answer does not have yet gets a line of its own before 'status: end'; one the firmware does not know is a
// mistake in the test.
const statusWith = (changes, answer = STATUS_ANSWER) => [Object.entries(changes).reduce((text, [key, value]) => {
  if (!FIRMWARE_STATUS_KEYS.has(key)) throw new Error(`no key '${key}' in the card's status (src/status_text.h)`);
  const current = new RegExp(` ${key}=\\S+`);
  return current.test(text) ? text.replace(current, ` ${key}=${value}`)
    : text.replace("status: end", `status: ${key}=${value}\r\nstatus: end`);
}, answer.join(""))];
// The 'status' lines of an answer as parseStatus() gets them: without the log line in between.
const statusLinesOf = (answer) => statusLines(answer.join("").replace(/gps: .*\r\n/, "").replaceAll("\r", ""));
// A card with gpxcam 1.4.0: the same state and the location updates.
const STATUS_1_4 = statusWith({ fw: "1.4.0", updates: "normal", updates_now: "normal" });
const VERSION_1_4 = ["gpxcam-t1000e 1.4.0 (git 1234567), uptime 12 s, log dropped 0\r\n"];
const NOT_PAIRED = { paired: 0, link: "off" };
const PAIR_STARTED = ["link: off\r\n", "pair: started - waiting for GPS time (up to 120 s)\r\n"];
const PAIR_OFF_ANSWER = Object.freeze(["pair: cancelled\r\n"]);
const PREPARE_ANSWER = Object.freeze(["pair: GPS on for 600 s, ready for pairing\r\n"]);
// A card with gpxcam 1.2 (no 'status'; its time zone in the answer to 'tz') and one with 1.3 (its state).
// Shared, so frozen: a test that changes the answers makes its own copy ({ ...CARD_1_3 }).
const CARD_1_2 = Object.freeze({ v: VERSION_ANSWER, status: NO_STATUS, tz: TZ_STATE });
const CARD_1_3 = Object.freeze({ v: VERSION_1_3, status: STATUS_ANSWER });
const FORGOTTEN = ["link: off\r\n", "pair: camera forgotten (the card id stays)\r\n"];
const SOUND_OFF_SAVED = [
  "sound: off saved\r\n",
  "sound: off - the card only answers a held button (switching on and off, pairing), quietly\r\n",
];

// A serial port that answers commands with chunks of text; it records what the page does, and pending
// counts the pieces of answers still on their way. readFails: reading fails at once, as in Chromium on
// Linux after a pyserial program.
function simulatedCard(answers, readFails = false) {
  const encoder = new TextEncoder();
  let output;
  const port = {
    openedWith: null,
    signals: null,
    closed: false,
    written: [],
    pending: 0,
    readable: new ReadableStream({
      start: (controller) => { output = controller; },
      pull: (controller) => {
        if (readFails) controller.error(new DOMException("The device has been lost.", "NetworkError"));
      },
    }),
    writable: new WritableStream({
      write(chunk) {
        const command = new TextDecoder().decode(chunk);
        port.written.push(command);
        for (const piece of answers[command.replace(/\n$/, "")] || []) {
          port.pending++;
          setTimeout(() => { port.pending--; output.enqueue(encoder.encode(piece)); }, 5);
        }
      },
    }),
    async open(options) { port.openedWith = options; },
    // A line the card sends by itself (its log), not as an answer.
    say(text) { output.enqueue(encoder.encode(text)); },
    async setSignals(signals) { port.signals = signals; },
    async close() { port.closed = true; },
  };
  return port;
}

// ---- The page in a document made of stand-ins: what it shows, without a browser ----

class FakeElement {
  constructor(className = "") {
    this.classes = new Set(className.split(/\s+/).filter(Boolean));
    this.classList = {
      add: (name) => this.classes.add(name),
      remove: (name) => this.classes.delete(name),
      contains: (name) => this.classes.has(name),
      toggle: (name, on) => (on ? this.classes.add(name) : this.classes.delete(name)),
    };
    this.textContent = "";
    this.disabled = false;
    this.value = "";
    this.checked = false;
    this.children = [];
    this.listeners = {};
    this.attributes = {};
  }
  setAttribute(name, value) { this.attributes[name] = String(value); }
  removeAttribute(name) { delete this.attributes[name]; }
  get className() { return [...this.classes].join(" "); }
  set className(value) {
    this.classes.clear();
    for (const name of String(value).split(/\s+/).filter(Boolean)) this.classes.add(name);
  }
  get hidden() { return this.classes.has("hidden"); }
  addEventListener(type, listener) { (this.listeners[type] ||= []).push(listener); }
  // Runs the listeners and waits for them (the page's are async).
  fire(type) { return Promise.all((this.listeners[type] || []).map((listener) => listener({ target: this }))); }
  replaceChildren(...children) { this.children = children; }
  append(...children) { this.children.push(...children); }
}

// Starts the page with every element that has an id, with its classes from the HTML, and a
// browser whose port chooser gives the simulated card (serial: false - a browser without Web Serial).
// serialEvents collects the listeners the page adds to navigator.serial. app: the desktop app
// (simulatedApp()) instead of a browser.
// clipboard: navigator.clipboard (none by default, as in a web view without it).
function openPage(port, { serial = true, serialEvents = {}, app = null, clipboard = null } = {}) {
  const elements = {};
  for (const [tag] of html.matchAll(/<[a-z][a-z0-9]*\b[^>]*>/g)) {
    const id = / id="([^"]+)"/.exec(tag);
    if (id) elements[id[1]] = new FakeElement((/ class="([^"]*)"/.exec(tag) || [])[1]);
  }
  const document = {
    getElementById: (id) => elements[id],
    createElement: () => new FakeElement(),
  };
  const navigator = serial && !app ? {
    serial: { requestPort: async () => port, addEventListener: (type, listener) => { serialEvents[type] = listener; } },
  } : {};
  if (clipboard) navigator.clipboard = clipboard;
  vm.runInContext(scripts.join("\n"), vm.createContext({
    TextEncoder, TextDecoder, ReadableStream, WritableStream, setTimeout, clearTimeout, console, document, navigator,
    window: app ? app.window : { isSecureContext: true },
  }));
  return elements;
}

const tick = (ms = 20) => new Promise((resolve) => setTimeout(resolve, ms));

// Waits until check() is true, for up to ms.
async function until(check, ms = 3000) {
  for (const end = Date.now() + ms; !check(); await tick(10)) {
    if (Date.now() > end) throw new Error(`still not so after ${ms} ms: ${check}`);
  }
}

// Chooses a zone in the list as a click does; returns its name element.
async function chooseZone(ui, zone) {
  const item = ui.results.children.find((li) => li.children[0].children[0].value === zone);
  const [radio, text] = item.children[0].children;
  radio.checked = true;
  await radio.fire("change");
  return text.children[0];
}

// The page connected to a simulated card: { port, ui }.
async function connected(answers) {
  const port = simulatedCard(answers);
  const ui = openPage(port);
  await ui.connect.fire("click");
  return { port, ui };
}

// The page in the desktop app with one simulated card, connected: { app, ui }.
async function connectedApp(answers) {
  const app = simulatedApp(["/dev/ttyACM0"], answers);
  const ui = openPage(null, { app });
  await ui.connect.fire("click");
  return { app, ui };
}

async function openCard(answers, readFails = false) {
  const port = simulatedCard(answers, readFails);
  const card = new page.Card(port);
  await card.open();
  return { port, card };
}

test("the data holds the card's zones, aliases and countries", () => {
  assert.equal(page.TZ_DATA.version, "2026e");
  assert.equal(page.TZ_DATA.zones.length, 340); // the card's 344 but 4 old POSIX-style ones
  assert.equal(Object.keys(page.TZ_DATA.aliases).length, 246); // the card's 253 but 7 old POSIX-style ones
  assert.equal(page.TZ_DATA.aliases["Europe/Copenhagen"], "Europe/Berlin");
  assert.deepEqual(json(page.TZ_DATA.countries.DK), ["Denmark", [["Europe/Copenhagen", ""]]]);
});

// The card's table keeps the old POSIX-style names (tools/gen_tz_table.py LEGACY_NAMES); the page does not
// offer them, but shows one the card was set to as its zone, with the time it gives.
test("the old POSIX-style names are not offered, but a card set to one shows it as its zone", async () => {
  assert.deepEqual(json(Object.keys(page.TZ_DATA.legacy)),
    ["CET", "CST6CDT", "EET", "EST", "EST5EDT", "HST", "MET", "MST", "MST7MDT", "PST8PDT", "WET"]);
  for (const name of Object.keys(page.TZ_DATA.legacy)) {
    assert.equal(page.findZone(name), null, name);
    assert.equal(page.allowedCommand(`tz ${name}`), false, name);
    assert.ok(page.searchZones(name).every((result) => result.name !== name), name);
  }
  const zone = (timeZone) => json(page.zoneTile({ ...STATE, timeZone }));
  assert.deepEqual(zone("PST8PDT").slice(0, 2), ["good", "PST8PDT"]);
  assert.match(zone("PST8PDT")[2], /^Clock on \u00b7 now UTC-0[78]:00$/); // summer or winter time
  assert.equal(zone("EST")[2], "Clock on \u00b7 now UTC-05:00"); // as America/Panama
  const { ui } = await connected({ ...CARD_1_3, status: statusWith({ tz: "PST8PDT" }) });
  assert.equal(ui["tile-zone"].className, "tile level-good");
  assert.equal(ui["state-zone"].textContent, "PST8PDT");
  assert.match(ui["state-zone-detail"].textContent, /^Clock on \u00b7 now UTC-0[78]:00$/);
  await ui.disconnect.fire("click");
});

test("names are found in any case, as the table spells them", () => {
  assert.deepEqual(json(page.findZone("europe/warsaw")), { name: "Europe/Warsaw", zone: "Europe/Warsaw" });
  assert.deepEqual(json(page.findZone("POLAND")), { name: "Poland", zone: "Europe/Warsaw" });
  assert.deepEqual(json(page.findZone("Asia/Calcutta")), { name: "Asia/Calcutta", zone: "Asia/Kolkata" });
  for (const unknown of ["", "Europe", "Factory", "Mars/Olympus_Mons", "<+0545>-5:45"]) {
    assert.equal(page.findZone(unknown), null, unknown);
  }
});

test("search finds countries first, with their own zone names", () => {
  assert.equal(page.searchZones("denmark")[0].name, "Europe/Copenhagen");
  assert.equal(page.searchZones("pl")[0].name, "Europe/Warsaw"); // the country code
  assert.equal(page.searchZones("new york")[0].name, "America/New_York");
  const berlin = page.searchZones("berlin").find((result) => result.name === "Europe/Berlin");
  assert.match(berlin.detail, /Denmark/);
  const calcutta = page.searchZones("calcutta").find((result) => result.name === "Asia/Calcutta");
  assert.equal(calcutta.detail, "same as Asia/Kolkata");
  // A country's own name for a zone (zone.tab) says the country, also when found by the name.
  assert.equal(page.searchZones("Europe/Copenhagen")[0].detail, "Denmark (same as Europe/Berlin)");
  assert.equal(page.searchZones("kolkata")[0].detail, "India");
  assert.equal(page.searchZones("  ").length, 0);
  assert.equal(page.searchZones("a").length, page.MAX_RESULTS);
  const names = page.searchZones("a").map((result) => result.name);
  assert.equal(new Set(names).size, names.length);
});

test("the computer's zone is offered under the name people know", () => {
  assert.deepEqual(json(page.preferredZone("Asia/Calcutta")), { name: "Asia/Kolkata", zone: "Asia/Kolkata" });
  assert.deepEqual(json(page.preferredZone("Europe/Kiev")), { name: "Europe/Kyiv", zone: "Europe/Kyiv" });
  assert.deepEqual(json(page.preferredZone("Europe/Copenhagen")), { name: "Europe/Copenhagen", zone: "Europe/Berlin" });
  assert.deepEqual(json(page.preferredZone("Europe/Warsaw")), { name: "Europe/Warsaw", zone: "Europe/Warsaw" });
  assert.equal(page.preferredZone("Mars/Olympus_Mons"), null);
});

test("before anything is typed: the computer's zone first, then every zone", () => {
  const warsaw = page.startList(page.preferredZone("Europe/Warsaw"));
  assert.equal(warsaw[0].name, "Europe/Warsaw");
  assert.equal(warsaw[0].detail, "Poland \u00b7 your computer");
  assert.equal(warsaw.length, page.TZ_DATA.zones.length); // Warsaw is not listed twice
  assert.equal(new Set(warsaw.map((result) => result.name)).size, warsaw.length);
  const copenhagen = page.startList(page.preferredZone("Europe/Copenhagen"));
  assert.equal(copenhagen[0].detail, "Denmark (same as Europe/Berlin) \u00b7 your computer");
  assert.ok(copenhagen.some((result) => result.name === "Europe/Berlin"));
  const unknown = page.startList(null);
  assert.equal(unknown.length, page.TZ_DATA.zones.length);
  assert.equal(unknown[0].name, "Africa/Abidjan");
  assert.equal(page.searchZones("").length, 0); // searching starts with the first letter
  assert.match(html, /placeholder="e\.g\. Poland, New York, Tokyo"/);
});

test("what starts with the text comes before what only contains it", () => {
  const names = (query) => page.searchZones(query).map((result) => result.name);
  const pol = names("pol");
  assert.equal(pol[0], "Europe/Warsaw"); // Poland
  assert.equal(page.searchZones("pol")[0].detail, "Poland");
  for (const containing of ["Pacific/Tahiti", "Africa/Tripoli", "America/Indiana/Indianapolis"]) {
    assert.ok(pol.indexOf(containing) > pol.indexOf("Europe/Warsaw"), containing); // French Polynesia, Tripoli
  }
  assert.equal(names("kol")[0], "Asia/Kolkata"); // a city at the start
  // Countries, then cities that start with the text; then those that only contain it (Austria).
  assert.deepEqual(json(names("tri").slice(0, 2)), ["America/Port_of_Spain", "Africa/Tripoli"]);
  assert.ok(names("tri").indexOf("Africa/Tripoli") < names("tri").indexOf("Europe/Vienna"));
});

test("offsets come from the browser's own data", () => {
  const july = new Date(Date.UTC(2027, 6, 1));
  assert.equal(page.utcOffsetText("Asia/Kolkata", july), "UTC+05:30");
  assert.equal(page.utcOffsetText("America/New_York", july), "UTC-04:00");
  assert.equal(page.utcOffsetText("Etc/UTC", july), "UTC+00:00");
  assert.equal(page.utcOffsetText("Mars/Olympus_Mons", july), "");
});

// The same vectors as the desktop app's Rust side (cargo test).
const COMMANDS = JSON.parse(readFileSync(new URL("../commands.json", import.meta.url), "utf8"));

test("only the commands of test/commands.json can be sent, and About names each fixed one", () => {
  assert.deepEqual([...page.FIXED_COMMANDS].sort(), [...COMMANDS.fixed].sort());
  for (const allowed of [...COMMANDS.fixed, ...COMMANDS.allowed]) {
    assert.equal(page.allowedCommand(allowed), true, allowed);
  }
  for (const command of COMMANDS.refused) assert.equal(page.allowedCommand(command), false, JSON.stringify(command));
  const about = /<details class="about">([\s\S]*?)<\/details>/.exec(html)[1];
  const named = new Set([...about.matchAll(/<code>([^<]+)<\/code>/g)].map((match) => match[1]));
  assert.deepEqual([...named].sort(), [...COMMANDS.fixed].sort());
  for (const [name] of page.TZ_DATA.zones) assert.equal(page.allowedCommand(`tz ${name}`), true, name);
  for (const alias of Object.keys(page.TZ_DATA.aliases)) assert.equal(page.allowedCommand(`tz ${alias}`), true, alias);
  assert.equal(page.tzCommand("america/new_york"), "tz America/New_York");
  assert.equal(page.tzCommand("Mars/Olympus_Mons"), null);
});

test("the older-camera wizard reads the card's line and explains the Wi-Fi step's outcome", () => {
  assert.deepEqual(
    json(page.parseOlderRegister("older: app register identity 4D454930-0100-1000-8001-A1B2C3D4E5F6 name T1000-E")),
    { identity: "4D454930-0100-1000-8001-A1B2C3D4E5F6", name: "T1000-E" },
  );
  assert.equal(page.parseOlderRegister("older: introduced - waiting up to 300 s for the camera's 01"), null);
  assert.equal(page.olderOutcomeText("accepted")[1], "good");
  assert.equal(page.olderOutcomeText("cancelled")[1], "");
  for (const bad of ["not-confirmed", "not-on-network", "bad-identity", "bad-name", "failed", "anything"]) {
    assert.equal(page.olderOutcomeText(bad)[1], "bad");
  }
  const general = page.olderOutcomeText("anything")[0]; // a word the page does not know
  assert.equal(page.olderOutcomeText("failed")[0], general);
  for (const word of ["bad-identity", "bad-name"]) {
    assert.match(page.olderOutcomeText(word)[0], /^The app cannot register the identity this card gave\./, word);
  }
});

test("pairing as with an older camera is offered to gpxcam 1.5.0 and later", () => {
  for (const version of ["1.5.0", "1.5.0-beta.6", "1.6.0", "2.0.0"]) assert.equal(page.canPairOlder(version), true, version);
  for (const version of ["1.3.0", "1.4.0"]) assert.equal(page.canPairOlder(version), false, version);
});

// The made-up card UUID and its identity in the older dialect (test/device_identity_vectors.txt).
const VECTORS = Object.fromEntries(readFileSync(new URL("../device_identity_vectors.txt", import.meta.url), "utf8")
  .split("\n").filter((line) => line && !line.startsWith("#")).map((line) => line.split(" = ")));
const CARD_ID = VECTORS.card_uuid.replace(/^(.{8})(.{4})(.{4})(.{4})(.{12})$/, "$1-$2-$3-$4-$5");
const CARD_ID_ANSWER = [`pair: card id ${CARD_ID}, no camera paired\r\n`, "pair: copy - none\r\n"];

test("parseCardId reads the card's id from 'p', and olderIdentity makes the identity the card registers", () => {
  assert.equal(page.parseCardId(`pair: card id ${CARD_ID}, no camera paired`), CARD_ID);
  assert.equal(page.parseCardId(`pair: card id ${CARD_ID}, camera 123456789a`), CARD_ID);
  for (const other of ["pair: not available", `pair: new card id ${CARD_ID} saved in /gpxcam/pairing.bin`,
    `pair: card id ${CARD_ID.toUpperCase()}, no camera paired`, "pair: card id 0011, no camera paired"]) {
    assert.equal(page.parseCardId(other), null, other);
  }
  assert.deepEqual(json(page.olderIdentity(CARD_ID)), { identity: VECTORS.identity_text, name: VECTORS.name });
});

test("the port is opened only at 115200 baud, with DTR", async () => {
  assert.equal(page.BAUD_RATE, 115200);
  assert.deepEqual(json(page.CARD_USB), { usbVendorId: 0x2886, usbProductId: 0x8057 });
  assert.doesNotMatch(html, /1200/);
  // Every open() with options is the port's (Card.open() itself takes none).
  const opens = [...appCode.matchAll(/\.open\(([^)]*)\)/g)].map((match) => match[1]).filter(Boolean);
  assert.deepEqual(opens, ["{ baudRate: BAUD_RATE }"]);
  assert.equal(appCode.match(/getWriter\(/g).length, 1); // only Card.send() writes
  const { port } = await openCard({});
  assert.deepEqual(json(port.openedWith), { baudRate: 115200 });
  assert.deepEqual(json(port.signals), { dataTerminalReady: true });
});

test("the version line is found among other lines, also in pieces", async () => {
  const { port, card } = await openCard({ v: VERSION_ANSWER });
  assert.equal(await card.version(), "1.2.0");
  assert.deepEqual(port.written, ["v\n"]);
  assert.equal(page.parseVersion("gpxcam-t1000e 1.1.0 (git 4a1f79a), uptime 2080 s, log dropped 607"), "1.1.0");
  assert.equal(page.parseVersion("[      0.7 s] reset: 0x00010000 (OFF-GPIO)"), null);
});

test("tz shows the zone; other lines are left out", async () => {
  const { card } = await openCard({ tz: TZ_STATE });
  const lines = await card.timeZone();
  assert.deepEqual(json(lines), [
    "tz: Europe/Warsaw, rule CET-1CEST,M3.5.0,M10.5.0/3 (tz database 2026e)",
    "tz: now UTC+02:00, the camera gets 60 min + summer time",
  ]);
  assert.equal(page.classifyTzAnswer(lines), "state");
});

test("saving sends the name as the table spells it and reads the answer", async () => {
  const { port, card } = await openCard({ "tz America/New_York": TZ_SAVED });
  const lines = await card.timeZone("america/new_york");
  assert.deepEqual(port.written, ["tz America/New_York\n"]);
  assert.equal(lines.length, 3);
  assert.equal(page.classifyTzAnswer(lines), "saved");
});

test("the card's other answers to 'tz' are told apart, also from firmware without it", async () => {
  const classify = (lines) => page.classifyTzAnswer(lines);
  assert.equal(classify([]), "none");
  assert.equal(classify(["tz: already set", "tz: Europe/Warsaw, rule CET-1CEST,M3.5.0,M10.5.0/3 (tz database 2026e)"]), "unchanged");
  assert.equal(classify(["tz: unknown time zone or rule 'Mars/Olympus_Mons'"]), "unknown");
  assert.equal(classify(["tz: could not write the time zone file"]), "not-saved");
  const { card } = await openCard({ tz: OLD_FIRMWARE });
  assert.equal(classify(await card.timeZone()), "no-tz");
});

test("saving a time zone and connecting are explained in the page's words", () => {
  assert.deepEqual(json(page.tzAnswerText("Asia/Tokyo", "saved")), ["Saved: Asia/Tokyo.", "good"]);
  assert.deepEqual(json(page.tzAnswerText("Asia/Tokyo", "unchanged")), ["Asia/Tokyo was already set on the card.",
    "good"]);
  for (const answer of ["unknown", "not-saved", "none"]) {
    assert.equal(page.tzAnswerText("Asia/Tokyo", answer)[1], "bad", answer);
  }
  assert.deepEqual(json(page.tzAnswerText("Asia/Tokyo", "state")), ["The card answered:", ""]);
  assert.match(page.statusProblemText("none"), /^No answer from the card to 'status'/);
  assert.match(page.statusProblemText("newer"), /newer than this page/);
  assert.equal(page.statusProblemText("ok"), null);
});

test("a card without tz and a card that does not answer tz get different messages", () => {
  assert.match(page.tzProblemText("1.1.0", "no-tz"), /^gpxcam 1\.1\.0 cannot be set up from this page/);
  assert.match(page.tzProblemText("1.2.0", "none"), /^No answer from the card to 'tz'/);
  assert.equal(page.tzProblemText("1.2.0", "state"), null);
});

// The colours of a theme from the page's style: light from :root, dark from the media query.
function themeColours(dark) {
  const style = /<style>([\s\S]*?)<\/style>/.exec(html)[1];
  const darkStart = style.indexOf("@media (prefers-color-scheme: dark)");
  const light = Object.fromEntries([...style.slice(0, darkStart).matchAll(/(--[a-z-]+): (#[0-9a-f]{6});/g)]
    .map((match) => [match[1], match[2]]));
  if (!dark) return light;
  const darkBlock = style.slice(darkStart, style.indexOf("\n}\n", darkStart));
  const darkColours = [...darkBlock.matchAll(/(--[a-z-]+): (#[0-9a-f]{6});/g)].map((match) => [match[1], match[2]]);
  return { ...light, ...Object.fromEntries(darkColours) };
}

// WCAG 2 contrast ratio of two #rrggbb colours.
function contrast(a, b) {
  const luminance = (hex) => {
    const [r, g, b2] = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255)
      .map((c) => (c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4));
    return 0.2126 * r + 0.7152 * g + 0.0722 * b2;
  };
  const [high, low] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return (high + 0.05) / (low + 0.05);
}

test("text and the edges of fields stand out enough in both themes", () => {
  for (const dark of [false, true]) {
    const c = themeColours(dark);
    for (const ground of [c["--bg"], c["--panel"], c["--field"]]) {
      assert.ok(contrast(c["--field-line"], ground) >= 3, `field edge on ${ground}, dark ${dark}`); // WCAG 1.4.11
      assert.ok(contrast(c["--text"], ground) >= 4.5, `text on ${ground}, dark ${dark}`);
      assert.ok(contrast(c["--muted"], ground) >= 4.5, `muted text on ${ground}, dark ${dark}`);
      assert.ok(contrast(c["--link"], ground) >= 4.5, `red text on ${ground}, dark ${dark}`);
    }
    assert.ok(contrast(c["--accent"], c["--panel"]) >= 3, `icons on a card, dark ${dark}`); // WCAG 1.4.11
  }
});

test("each part of the page is a card with its heading and icon; the favicon is drawn in the page", () => {
  // The camera's heading also holds the short pairing state (pair-badge).
  const card = new RegExp(String.raw`<section class="panel section">\s*<h2><svg class="icon"[^>]*>[\s\S]*?<\/svg>` +
    String.raw`([^<]+)(?:<span id="pair-badge"[^>]*><\/span>)?<\/h2>`, "g");
  const cards = [...html.matchAll(card)];
  assert.deepEqual(cards.map((card) => card[1]), ["The card", "The camera", "The camera's clock", "Location updates",
    "Sound", "Diagnostics"]);
  assert.match(html, /<link rel="icon" href="data:image\/svg\+xml,/);
});

test("motion is short, changes only colours and the toggle's knob, and stops with reduced motion", () => {
  const style = /<style>([\s\S]*?)<\/style>/.exec(html)[1];
  const durations = [...style.matchAll(/(\d*\.?\d+)s\b/g)].map((match) => Number(match[1]));
  assert.ok(durations.length > 0);
  for (const duration of durations) assert.ok(duration <= 0.25 || duration === 1.2, `${duration} s`); // 1.2: the pulse
  for (const [, list] of style.matchAll(/transition: ([^;]+);/g)) {
    if (list.startsWith("none")) continue; // the reduced-motion rule
    for (const property of list.split(",").map((part) => part.trim().split(" ")[0])) {
      assert.ok(["background-color", "border-color", "color", "transform"].includes(property), property);
    }
  }
  const pulse = /@keyframes pulse \{([\s\S]*?)\n\}/.exec(style)[1];
  assert.deepEqual([...new Set([...pulse.matchAll(/([a-z-]+):/g)].map((match) => match[1]))], ["opacity"]);
  const reduced = /@media \(prefers-reduced-motion: reduce\) \{([^}]*)\}/.exec(style)[1];
  assert.match(reduced, /\*, \*::before, \*::after \{ animation: none !important; transition: none !important;/);
});

test("what a returning reader does not need is folded: How pairing works, and About", () => {
  assert.match(html, /<details id="pairing-help" class="help">\s*<summary>How pairing works<\/summary>/);
  assert.match(html, /<details class="about">\s*<summary id="about-title">About this page<\/summary>/);
});

test("hidden parts stay hidden whatever display their own rule gives", () => {
  assert.match(html, /^\.hidden \{ display: none !important; \}/m);
});

test("the page fetches nothing but its zone data, next to it: content security policy", () => {
  const policy = /<meta http-equiv="Content-Security-Policy" content="([^"]*)">/.exec(html);
  assert.ok(policy);
  assert.match(policy[1], /^default-src 'none';/);
  assert.match(policy[1], /script-src 'self' 'unsafe-inline';/); // docs/zones.js and the page's own script
  assert.doesNotMatch(policy[1], /connect-src|https?:/);
  assert.match(policy[1], /img-src data:/); // the favicon is a data: image
  assert.deepEqual([...html.matchAll(/<script[^>]+src="([^"]*)"/g)].map((match) => match[1]), ["zones.js"]);
  assert.doesNotMatch(html, /<link[^>]+href="(?!data:)/);
});

test("a refused command is not written; no answer ends after the time limit", async () => {
  const { port, card } = await openCard({});
  await assert.rejects(card.send("dfu"));
  await assert.rejects(card.timeZone("Mars/Olympus_Mons"));
  assert.deepEqual(port.written, []);
  assert.deepEqual(json(await card.ask("v", () => true, 50)), []);
});

test("a read that fails at once is explained", async () => {
  const { card } = await openCard({ v: VERSION_ANSWER }, true);
  await until(() => card.reader === null); // reading has ended
  assert.equal(card.readError.message, "The device has been lost.");
  assert.equal(card.reader, null); // reading has ended
  assert.match(page.noAnswerText(card.readError), /^Reading from the card failed \(The device has been lost\.\)/);
  assert.match(page.noAnswerText(card.readError), /unplug the card, plug it in again/);
  assert.match(page.noAnswerText(null), /^No answer from the card/);
});

test("in the app, the end of reading is explained from the app's word", () => {
  assert.equal(page.readErrorText(new page.AppReadError("port-closed")), "the card's port closed");
  assert.equal(page.readErrorText(new page.AppReadError("port-failed")), "the card's port failed");
  assert.equal(page.readErrorText(new page.AppReadError("anything")), "the connection failed");
  assert.equal(page.readErrorText(new Error("The device has been lost.")), "The device has been lost.");
  const text = page.noAnswerText(new page.AppReadError("port-failed"));
  assert.equal(text, "Reading from the card failed (the card's port failed). Unplug the card, plug it in again " +
    "and click Connect.");
});

test("closing ends reading and closes the port", async () => {
  const { port, card } = await openCard({});
  let closed = false;
  card.onClosed = () => { closed = true; };
  await card.close();
  await until(() => closed);
  assert.equal(port.closed, true);
  assert.equal(closed, true);
});

test("the old address of the time zone page leads here and fetches nothing", () => {
  const old = readFileSync(new URL("../../docs/timezone/index.html", import.meta.url), "utf8");
  assert.match(old, /<meta http-equiv="refresh" content="0; url=\.\.\/">/);
  // img-src data: for its empty favicon (data:,), which keeps the browser from asking for /favicon.ico
  assert.match(old, /<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:">/);
  assert.match(old, /<link rel="icon" href="data:,">/);
  assert.match(old, /<a href="\.\.\/">/);
  assert.doesNotMatch(old, /<script|https?:/);
});

test("the card's state is read from 'status', also in pieces and between log lines", async () => {
  const { port, card } = await openCard({ status: STATUS_ANSWER });
  const status = await card.status();
  assert.deepEqual(port.written, ["status\n"]);
  assert.equal(status.kind, "ok");
  assert.deepEqual(json(status.state), STATE);
});

test("a card before 1.3.0, a missing line, no end and a newer format are told apart", async () => {
  const { card } = await openCard({ status: NO_STATUS });
  assert.equal((await card.status()).kind, "no-status");
  const lines = statusLinesOf(STATUS_ANSWER);
  assert.equal(page.parseStatus(lines).kind, "ok");
  assert.equal(page.parseStatus(lines.filter((line) => !line.includes("battery"))).kind, "none");
  assert.equal(page.parseStatus(lines.slice(0, -1)).kind, "none"); // no "status: end"
  assert.equal(page.parseStatus([]).kind, "none");
  assert.equal(page.parseStatus(["status: format=2 fw=2.0.0", "status: end"]).kind, "newer");
  // A key added later is skipped.
  const more = [...lines.slice(0, -1), "status: later_key=1", "status: end"];
  assert.deepEqual(json(page.parseStatus(more).state), STATE);
});

test("the state is shown as tiles, each state in a word, not only by the colour of its dot", () => {
  const state = (changes) => ({ ...STATE, ...changes });
  const tile = (make, changes = {}) => json(make(state(changes)));
  assert.deepEqual(tile(page.cameraTile), ["good", "Connected", "Sends the position"]);
  assert.deepEqual(tile(page.cameraTile, { paired: false, link: "off" }), ["idle", "Not paired", "Pair it below"]);
  assert.deepEqual(tile(page.cameraTile, { link: "searching" }),
    ["idle", "Looking for it", "Camera off or out of range"]);
  assert.deepEqual(tile(page.cameraTile, { link: "searching", gpsFix: false }),
    ["warn", "Waiting for GPS", "Connects once the GPS has a fix"]);
  // The camera away a while: the GPS sleeps, the card only looks for the camera (the GPS tile says "Off")
  assert.deepEqual(tile(page.cameraTile, { link: "searching", gpsOn: false, gpsFix: false }),
    ["idle", "Looking for it", "Camera off or out of range"]);
  assert.deepEqual(tile(page.cameraTile, { link: "stopped" }), ["bad", "Refused the card", "Pair them again"]);
  assert.deepEqual(tile(page.cameraTile, { link: "off", pair: "searching" }),
    ["warn", "Pairing", "See the camera section below"]);
  assert.deepEqual(tile(page.gpsTile), ["good", "Fix", "11 satellites"]);
  assert.deepEqual(tile(page.gpsTile, { gpsOn: false }), ["idle", "Off", "Saves power"]);
  assert.deepEqual(tile(page.gpsTile, { gpsFix: false, gpsSatellites: 1 }),
    ["warn", "No fix yet", "1 satellite; outdoors 1\u20133 min"]);
  assert.deepEqual(tile(page.batteryTile), ["good", "Charging", "about 89 % \u00b7 4.10 V"]);
  assert.deepEqual(tile(page.batteryTile, { charger: "charged", batteryMv: 4180, batteryPercent: 98 }),
    ["good", "Charged", "100 % \u00b7 4.18 V"]); // charged is full, whatever the voltage gives
  assert.deepEqual(tile(page.batteryTile, { charger: "not_charging" }),
    ["idle", "On USB", "not charging, about 89 % \u00b7 4.10 V"]);
  assert.deepEqual(tile(page.batteryTile, { charger: "no_usb" }), ["good", "Good", "89 % \u00b7 4.10 V"]);
  assert.deepEqual(tile(page.batteryTile, { charger: "no_usb", batteryPercent: 19, batteryMv: 3620 }),
    ["warn", "Low", "19 % \u00b7 3.62 V"]);
  assert.deepEqual(tile(page.batteryTile, { charger: "no_usb", batteryPercent: 9, batteryMv: 3480 }),
    ["bad", "Very low", "9 % \u00b7 3.48 V"]);
  const [level, name, detail] = tile(page.zoneTile, { timeZone: "Asia/Kolkata" });
  assert.deepEqual([level, name], ["good", "Asia/\u200bKolkata"]); // it may break after the slash
  assert.equal(detail, "Clock on \u00b7 now UTC+05:30");
  assert.equal(tile(page.zoneTile, { timeZone: "<+0545>-5:45" })[2], "Clock on \u00b7 a rule given directly");
  assert.deepEqual(tile(page.zoneTile, { clockOn: false }),
    ["idle", "Europe/\u200bWarsaw", "Clock off: set it on the camera"]);
});

test("commands go one at a time: the next after the answer to the one before", async () => {
  const { port, card } = await openCard({ v: VERSION_ANSWER, status: STATUS_ANSWER });
  const version = card.version();
  const status = card.status();
  await tick(1); // the second is not written yet
  assert.deepEqual(port.written, ["v\n"]);
  assert.equal(await version, "1.2.0");
  assert.equal((await status).kind, "ok");
  assert.deepEqual(port.written, ["v\n", "status\n"]);
  await assert.rejects(card.ask("dfu", () => true, 50));
  assert.equal((await card.status()).kind, "ok"); // a refused command does not stop the queue
});

test("the page shows the state of a card with 'status'", async () => {
  const port = simulatedCard(CARD_1_3);
  const ui = openPage(port);
  assert.equal(ui.intro.hidden, false); // how to connect, until connected
  assert.equal(ui.connect.disabled, false);
  await ui.connect.fire("click");
  assert.deepEqual(port.written, ["v\n", "status\n"]);
  assert.equal(ui["card-status"].textContent, "Connected to the card");
  assert.equal(ui.intro.hidden, true);
  assert.equal(ui["card-status"].className, "good");
  assert.equal(ui.state.hidden, false);
  assert.equal(ui["state-firmware"].textContent, "gpxcam 1.3.0 \u00b7 git 1234567");
  assert.equal(ui["tile-camera"].className, "tile level-good");
  assert.equal(ui["state-camera"].textContent, "Connected");
  assert.equal(ui["state-camera-detail"].textContent, "Sends the position");
  assert.equal(ui["state-zone"].textContent, "Europe/\u200bWarsaw");
  assert.match(ui["state-zone-detail"].textContent, /^Clock on · now UTC\+0[12]:00$/);
  assert.equal(ui["refresh-row"].hidden, false);
  assert.equal(ui["card-note"].hidden, true);
  await ui.refresh.fire("click");
  assert.deepEqual(port.written, ["v\n", "status\n", "status\n"]);
  await ui.disconnect.fire("click");
  assert.equal(port.closed, true);
  assert.equal(ui.state.hidden, true);
  assert.equal(ui["card-status"].textContent, "Not connected.");
  assert.equal(ui.intro.hidden, false);
});

test("the page sets the time zone of a card with gpxcam 1.2 and says what 1.3.0 adds", async () => {
  const { port, ui } = await connected(CARD_1_2);
  assert.deepEqual(port.written, ["v\n", "status\n", "tz\n"]);
  assert.equal(ui["card-status"].textContent, "Connected: gpxcam 1.2.0. Its time zone now:");
  assert.match(ui["card-lines"].textContent, /^tz: Europe\/Warsaw/);
  assert.equal(ui.state.hidden, true);
  assert.equal(ui["refresh-row"].hidden, true);
  assert.match(ui["card-note"].textContent, /^gpxcam 1\.2\.0 can only set the time zone from this page/);
  assert.equal(ui.save.disabled, false); // the computer's zone, or nothing, is selected
  await ui.disconnect.fire("click");
});

test("Web Serial is the transport only where the browser offers it to this page", async () => {
  assert.equal(page.chooseTransport({}, { isSecureContext: true }), null); // Firefox, Safari
  assert.equal(page.chooseTransport({ serial: {} }, { isSecureContext: false }), null); // not https
  const asked = [];
  const listeners = {};
  const serial = {
    requestPort: async (options) => { asked.push(options); return "the port"; },
    addEventListener: (type, listener) => { listeners[type] = listener; },
  };
  const transport = page.chooseTransport({ serial }, { isSecureContext: true });
  assert.equal(transport.name, "web-serial");
  assert.equal(await transport.requestPort(), "the port");
  assert.deepEqual(json(asked), [{ filters: [{ usbVendorId: 0x2886, usbProductId: 0x8057 }] }]);
  const unplugged = [];
  transport.onDisconnect((port) => unplugged.push(port));
  listeners.disconnect({ target: "the port" });
  assert.deepEqual(unplugged, ["the port"]);
});

test("the desktop app is the transport where it runs, before Web Serial", () => {
  const app = simulatedApp([], {});
  assert.equal(page.chooseTransport({}, app.window).name, "app");
  assert.equal(page.chooseTransport({ serial: {} }, app.window).name, "app");
  assert.equal(page.chooseTransport({}, { isSecureContext: true, __TAURI__: {} }), null); // no core
});

test("the app's port reads lines from its channel and sends commands without the newline", async () => {
  const app = simulatedApp(["/dev/ttyACM0"], { v: VERSION_1_4, status: STATUS_1_4 });
  const transport = page.chooseTransport({}, app.window);
  const card = new page.Card(await transport.requestPort(() => assert.fail("one card: no choice")));
  await card.open();
  assert.equal(await card.version(), "1.4.0");
  const status = await card.status();
  assert.equal(status.kind, "ok");
  assert.equal(status.state.updates, "normal");
  await assert.rejects(card.ask("dfu", () => true, 50)); // refused before the app sees it
  await card.close();
  assert.deepEqual(app.calls, ["list_cards", "open_card /dev/ttyACM0", "send v", "send status", "close_card"]);
});

test("the app says why the card's port did not open", () => {
  assert.match(page.openProblemText("permission"), /dialout group \(sudo usermod -aG dialout \$USER\)/);
  assert.match(page.openProblemText("not-found"), /^The card is gone\./);
  assert.match(page.openProblemText("busy"), /^Could not open the card\. Close other programs/);
  assert.match(page.openProblemText(new DOMException("Failed to open serial port.", "NetworkError")),
    /^Could not open the card\./); // Web Serial
});

test("in the app: no chooser for one card, the same state, and words without the browser", async () => {
  const app = simulatedApp(["/dev/ttyACM0"], { v: VERSION_1_4, status: STATUS_1_4 });
  const ui = openPage(null, { app });
  assert.equal(ui.unsupported.hidden, true);
  assert.equal(ui["intro-choose"].hidden, true);
  assert.equal(ui["about-title"].textContent, "About this app");
  assert.equal(ui["about-runs"].textContent, "The app sends");
  assert.equal(ui["about-link"].hidden, true);
  assert.equal(ui["about-address"].hidden, false);
  await ui.connect.fire("click");
  assert.equal(ui["choose-card"].hidden, true);
  assert.equal(ui["card-status"].textContent, "Connected to the card");
  assert.equal(ui["state-firmware"].textContent, "gpxcam 1.4.0 \u00b7 git 1234567");
  assert.equal(ui["updates-normal"].checked, true);
  app.unplug();
  await tick();
  assert.equal(ui["card-status"].textContent, "The connection to the card ended (the card's port failed).");
  assert.equal(ui.connect.disabled, false);
});

test("in the app: a choice between two cards, Cancel, and no card at all", async () => {
  const app = simulatedApp(["/dev/ttyACM0", "/dev/ttyACM1"], { v: VERSION_1_4, status: STATUS_1_4 });
  const ui = openPage(null, { app });
  let connecting = ui.connect.fire("click");
  await tick();
  assert.equal(ui["choose-card"].hidden, false);
  const choices = ui["card-choices"].children;
  assert.deepEqual(choices.map((button) => button.textContent), ["/dev/ttyACM0", "/dev/ttyACM1", "Cancel"]);
  await choices[2].fire("click"); // Cancel
  await connecting;
  assert.equal(ui["choose-card"].hidden, true);
  assert.equal(ui["card-status"].className, ""); // nothing to report: the page stays as it was
  assert.equal(ui.connect.disabled, false);
  connecting = ui.connect.fire("click");
  await tick();
  await ui["card-choices"].children[1].fire("click");
  await connecting;
  assert.equal(app.calls[app.calls.length - 3], "open_card /dev/ttyACM1");
  assert.equal(ui["card-status"].textContent, "Connected to the card");
  await ui.disconnect.fire("click");

  const none = openPage(null, { app: simulatedApp([], {}) });
  await none.connect.fire("click");
  assert.equal(none["card-status"].textContent, "No card found. Plug the card in with a USB cable and click Connect.");
  assert.equal(none["card-status"].className, "bad");
});

test("in the app: a port without permission or in use is explained", async () => {
  for (const [word, text] of [["permission", /dialout/], ["busy", /Close other programs/]]) {
    const ui = openPage(null, { app: simulatedApp(["/dev/ttyACM0"], {}, { openError: word }) });
    await ui.connect.fire("click");
    assert.match(ui["card-status"].textContent, text, word);
    assert.equal(ui["card-status"].className, "bad");
    assert.equal(ui.connect.disabled, false);
  }
});

test("unplugging the card ends the connection on the page", async () => {
  const port = simulatedCard(CARD_1_3);
  const serialEvents = {};
  const ui = openPage(port, { serialEvents });
  await ui.connect.fire("click");
  assert.equal(ui["card-status"].textContent, "Connected to the card");
  serialEvents.disconnect({ target: {} }); // another port: nothing happens
  assert.equal(port.closed, false);
  serialEvents.disconnect({ target: port });
  await until(() => port.closed);
  assert.equal(port.closed, true);
  assert.match(ui["card-status"].textContent, /^The connection to the card ended/);
  assert.equal(ui.connect.disabled, false);
});

test("a browser without Web Serial gets only the explanation", () => {
  const ui = openPage(simulatedCard({}), { serial: false });
  assert.equal(ui.unsupported.hidden, false);
  assert.equal(ui.app.hidden, true);
  assert.equal(ui.connect.disabled, true);
});

test("zone names break after a slash, not inside a word", () => {
  const ui = openPage(simulatedCard({}));
  const name = ui.results.children.find((li) => li.children[0].children[0].value === "America/Argentina/Rio_Gallegos")
    .children[0].children[1].children[0];
  assert.equal(name.children.filter((part) => typeof part === "string").join(""), "America/Argentina/Rio_Gallegos");
  assert.equal(name.children.filter((part) => typeof part !== "string").length, 2); // a <wbr> after each "/"
  assert.equal(name.children.indexOf("/") + 1, name.children.findIndex((part) => typeof part !== "string"));
});

test("the zone to save says whether the card has it already", async () => {
  const port = simulatedCard(CARD_1_3); // the card has Europe/Warsaw
  const ui = openPage(port);
  await chooseZone(ui, "Europe/Warsaw");
  assert.match(ui.selected.textContent, /^To save: Europe\/Warsaw, now UTC\+0[12]:00$/);
  await ui.connect.fire("click");
  assert.match(ui.selected.textContent, /^To save: Europe\/Warsaw, now UTC\+0[12]:00 \(already on the card\)$/);
  await chooseZone(ui, "America/New_York");
  assert.match(ui.selected.textContent, /^To save: America\/New_York, now UTC-0[45]:00$/);
  await chooseZone(ui, "Europe/Warsaw");
  await ui.disconnect.fire("click");
  assert.match(ui.selected.textContent, /^To save: Europe\/Warsaw, now UTC\+0[12]:00$/);
});

test("saving a zone: the note about the next connection only when it was saved", async () => {
  const answers = { v: VERSION_1_3, status: STATUS_ANSWER, "tz America/New_York": TZ_SAVED };
  const { port, ui } = await connected(answers);
  await chooseZone(ui, "America/New_York");
  // After saving, the page asks for the card's state, with the new zone: wait for its answer.
  const statusAsks = () => port.written.filter((command) => command === "status\n").length;
  const stateRead = (before) => until(() => statusAsks() > before && port.pending === 0);
  let asked = statusAsks();
  await ui.save.fire("click");
  assert.ok(port.written.includes("tz America/New_York\n"));
  assert.equal(ui["save-status"].textContent, "Saved: America/New_York.");
  assert.equal(ui["save-note"].hidden, false);
  assert.equal(ui["save-lines"].textContent, ""); // "Saved: ..." says what the card's lines say
  await stateRead(asked);
  asked = statusAsks();
  answers["tz America/New_York"] = ["tz: could not write the time zone file\r\n"];
  await ui.save.fire("click");
  assert.equal(ui["save-status"].className, "bad");
  assert.equal(ui["save-note"].hidden, true);
  assert.equal(ui["save-lines"].textContent, "tz: could not write the time zone file");
  await stateRead(asked);
  await ui.disconnect.fire("click");
});

test("a card that does not answer 'status' is closed with a message", async () => {
  const { port, ui } = await connected({ v: VERSION_1_3 });
  assert.equal(port.closed, true);
  assert.match(ui["card-status"].textContent, /^No answer from the card to 'status'/);
  assert.equal(ui["card-status"].className, "bad");
  assert.equal(ui.connect.disabled, false);
});

// The settings saved with one command each: what is sent, and the card's answers told apart and explained.
test("the answers to 'clock', 'sound' and 'updates' are told apart and explained", async () => {
  const updatesSaved = [
    "updates: frequent saved\r\n",
    "updates: frequent - a location every 5 s, sooner after 5 m or when the camera asks (at most every 2 s)\r\n",
  ];
  for (const [answers, value, saved, other] of [
    [page.CLOCK_ANSWERS, "off", CLOCK_OFF_SAVED, "on"],
    [page.SOUND_ANSWERS, "off", SOUND_OFF_SAVED, "quiet"],
    [page.UPDATES_ANSWERS, "frequent", updatesSaved, "normal"],
  ]) {
    const name = answers.name;
    const { port, card } = await openCard({ [`${name} ${value}`]: saved });
    assert.equal(answers.classify(await card.save(answers, value)), "saved", name);
    assert.deepEqual(port.written, [`${name} ${value}\n`]);
    assert.equal(answers.classify([]), "none", name);
    assert.equal(answers.classify([`${name}: ${other} saved`]), "saved", name);
    assert.equal(answers.classify([`${name}: already ${value}`]), "unchanged", name);
    assert.equal(answers.classify([`${name}: could not write the settings file`]), "not-saved", name);
    assert.equal(answers.classify([`command: unknown '${name} ${value}'`]), `no-${name}`, name);
    assert.deepEqual(json(answers.text(value, "unchanged")), ["The card already had this setting.", "good"]);
    for (const answer of ["not-saved", "none", `no-${name}`]) assert.equal(answers.text(value, answer)[1], "bad");
  }
  assert.match(page.CLOCK_ANSWERS.text("on", "saved")[0], /switch the camera off and on\.$/);
  assert.equal(page.CLOCK_ANSWERS.text("off", "saved")[0], "Saved. It counts from the next connection.");
  assert.deepEqual(json(page.SOUND_ANSWERS.text("off", "saved")), ["Saved.", "good"]);
  assert.deepEqual(json(page.UPDATES_ANSWERS.text("frequent", "saved")),
    ["Saved. The card uses it from the next position it sends.", "good"]);
});

test("what the clock and the sound settings mean, also with gpxcam that cannot change them", () => {
  assert.match(page.clockNoteText(true), /^Each time it connects, the card sets the camera's clock/);
  assert.match(page.clockNoteText(false), /set the time and the time zone in the camera's menu\. Pairing still/);
  assert.match(page.clockNoteText(true, "1.2.0"), /^gpxcam 1\.2\.0 always sets the camera's clock\./);
  assert.match(page.soundNoteText(), /^Choosing Loud or Quiet plays two short beeps/);
  assert.match(page.soundNoteText("1.2.0"), /^gpxcam 1\.2\.0 always plays its sounds loud\./);
  assert.equal(page.soundText("loud"), "All sounds.");
  assert.equal(page.soundText("quiet"), "All sounds, quieter.");
  assert.match(page.soundText("off"), /^Only quiet beeps when you hold the button/);
  assert.equal(page.soundText(null), "How loud the card is.");
});

test("the clock switch follows the card and turns the time zone choice off with it", async () => {
  const answers = { ...CARD_1_3 };
  const port = simulatedCard(answers);
  const ui = openPage(port);
  assert.equal(ui.clock.disabled, true); // not connected
  assert.equal(ui["clock-note"].textContent, "Connect the card to change this setting.");
  await ui.connect.fire("click");
  assert.equal(ui.clock.checked, true);
  assert.equal(ui.clock.disabled, false);
  assert.equal(ui.zones.disabled, false);
  assert.match(ui["clock-note"].textContent, /^Each time it connects/);

  answers["clock off"] = CLOCK_OFF_SAVED;
  answers.status = STATUS_CLOCK_OFF;
  ui.clock.checked = false;
  await ui.clock.fire("change");
  assert.deepEqual(port.written.slice(-2), ["clock off\n", "status\n"]);
  assert.equal(ui["clock-answer"].textContent, "Saved. It counts from the next connection.");
  assert.equal(ui["clock-answer"].className, "good");
  assert.equal(ui.clock.checked, false);
  assert.equal(ui.zones.disabled, true);
  assert.match(ui["clock-note"].textContent, /camera's menu/);
  assert.equal(ui["state-zone-detail"].textContent, "Clock off: set it on the camera");
  assert.equal(ui["tile-zone"].className, "tile level-idle");

  answers["clock on"] = CLOCK_NOT_SAVED; // the card keeps the clock off
  ui.clock.checked = true;
  await ui.clock.fire("change");
  assert.equal(ui["clock-answer"].className, "bad");
  assert.equal(ui.clock.checked, false); // as the card has it
  assert.equal(ui.zones.disabled, true);

  await ui.disconnect.fire("click");
  assert.equal(ui.clock.checked, false);
  assert.equal(ui.clock.disabled, true);
  assert.equal(ui.zones.disabled, false); // a zone can be chosen before connecting
  assert.equal(ui["clock-answer"].textContent, "");
});

test("with gpxcam 1.2 the clock switch stays on and cannot be changed", async () => {
  const { ui } = await connected(CARD_1_2);
  assert.equal(ui.clock.checked, true);
  assert.equal(ui.clock.disabled, true);
  assert.equal(ui.zones.disabled, false);
  assert.match(ui["clock-note"].textContent, /^gpxcam 1\.2\.0 always sets the camera's clock\. Switching it off needs/);
  await ui.disconnect.fire("click");
});

// The values the firmware gives (src/status_text.cpp, or another file), read from its source.
function firmwareValues(functionName, file = "status_text.cpp") {
  const source = readFileSync(new URL(`../../src/${file}`, import.meta.url), "utf8");
  const body = new RegExp(`const char \\*${functionName}\\([^)]*\\)\\n\\{([\\s\\S]*?)\\n\\}`).exec(source);
  assert.ok(body, functionName);
  return [...body[1].matchAll(/return "([a-z_]+)";/g)].map((match) => match[1]).filter((value) => value !== "unknown");
}

test("every pairing stage, result, link state and charger state of the firmware has its own words", () => {
  const phases = firmwareValues("pairPhaseValue");
  assert.deepEqual(phases, ["idle", "waiting_gps", "searching", "connecting", "waiting_wifi", "pausing"]);
  assert.equal(firmwareValues("pairResultValue").length, 9); // each one's screen: the tests of pairingScreen()
  const links = firmwareValues("linkValue");
  assert.equal(links.length, 6);
  const linkTexts = links.map((link) => page.cameraTile({ ...STATE, link }).join("|"));
  assert.equal(new Set(linkTexts).size, links.length); // none falls to another one's words
  const chargers = firmwareValues("chargerValue");
  const chargerTexts = chargers.map((charger) => page.batteryTile({ ...STATE, charger }).join("|"));
  assert.equal(new Set(chargerTexts).size, chargers.length);
});

test("the page offers every sound setting of the firmware, each with its own choice", () => {
  assert.deepEqual(json(page.SOUND_SETTINGS), firmwareValues("settingName", "sound_rules.cpp"));
  for (const setting of page.SOUND_SETTINGS) assert.match(html, new RegExp(`id="sound-${setting}" type="radio"`));
});

test("the clock is a switch, the sound and the location updates are segmented choices", () => {
  assert.match(html, /<input id="clock" type="checkbox" class="toggle"/);
  assert.match(html, /<fieldset id="sound" class="segmented">/);
  assert.match(html, /<fieldset id="updates" class="segmented">/);
});

test("the sound choice follows the card", async () => {
  const answers = { ...CARD_1_3 };
  const port = simulatedCard(answers);
  const ui = openPage(port);
  const checked = () => [...page.SOUND_SETTINGS].filter((setting) => ui[`sound-${setting}`].checked);
  assert.deepEqual(checked(), []); // not connected
  assert.equal(ui["sound-quiet"].disabled, true);
  assert.equal(ui["sound-note"].textContent, "Connect the card to change this setting.");
  await ui.connect.fire("click");
  assert.deepEqual(checked(), ["loud"]);
  assert.equal(ui["sound-says"].textContent, "All sounds.");
  assert.equal(ui["sound-quiet"].disabled, false);
  assert.match(ui["sound-note"].textContent, /^Choosing Loud or Quiet/);

  answers["sound quiet"] = ["sound: quiet saved\r\n"];
  answers.status = statusWith({ sound: "quiet" });
  ui["sound-quiet"].checked = true; // as the browser does on a click
  await ui["sound-quiet"].fire("change");
  assert.deepEqual(port.written.slice(-2), ["sound quiet\n", "status\n"]);
  assert.equal(ui["sound-answer"].textContent, "Saved.");
  assert.equal(ui["sound-answer"].className, "good");
  assert.deepEqual(checked(), ["quiet"]);
  assert.equal(ui["sound-says"].textContent, "All sounds, quieter.");

  answers["sound off"] = ["sound: could not write the settings file\r\n"]; // the card stays quiet
  ui["sound-off"].checked = true;
  await ui["sound-off"].fire("change");
  assert.equal(ui["sound-answer"].className, "bad");
  ui["sound-quiet"].checked = false; // the browser unchecks the others in the group: the page checks them again
  await ui.refresh.fire("click");
  assert.deepEqual(checked(), ["quiet"]); // as the card has it

  await ui.disconnect.fire("click");
  assert.deepEqual(checked(), []);
  assert.equal(ui["sound-off"].disabled, true);
  assert.equal(ui["sound-answer"].textContent, "");
});

test("the page offers every location updates setting of the firmware, each with its own choice", () => {
  assert.deepEqual(json(page.UPDATES_SETTINGS), firmwareValues("updatesName", "link_policy.cpp"));
  for (const setting of page.UPDATES_SETTINGS) assert.match(html, new RegExp(`id="updates-${setting}" type="radio"`));
});

// The timings in the page's words, the console's help (src/console_commands.cpp) and README.md (the one place in
// the documentation that gives them) are the firmware's (src/link_policy.h).
test("the location updates are explained with the firmware's timings", () => {
  const source = readFileSync(new URL("../../src/link_policy.h", import.meta.url), "utf8");
  const timing = (name) => {
    const match = new RegExp(`${name} = \\{(\\d+), (\\d+), (\\d+)\\}`).exec(source);
    assert.ok(match, name);
    return { seconds: Number(match[1]) / 1000, minGapSeconds: Number(match[2]) / 1000, metres: Number(match[3]) };
  };
  const normal = timing("NORMAL_TIMING");
  const frequent = timing("FREQUENT_TIMING");
  assert.match(page.updatesText("normal"),
    new RegExp(`^Every ${normal.seconds} seconds, sooner when you move ${normal.metres} m\\.`));
  assert.match(page.updatesText("frequent"),
    new RegExp(`^Every ${frequent.seconds} seconds, sooner when you move ${frequent.metres} m\\.`));
  assert.equal(page.updatesText(null), "How often the card sends the camera its position.");
  assert.match(page.updatesNoteText("1.3.0"),
    new RegExp(`^gpxcam 1\\.3\\.0 sends the position every ${normal.seconds} seconds\\. ` +
      "Choosing needs version 1\\.4\\.0\\.$"));
  assert.match(page.updatesNoteText(), /^The camera keeps the last position it got/);
  assert.match(page.updatesNowText({ ...STATE, updates: "frequent", updatesNow: "normal" }),
    new RegExp(`^For now every ${normal.seconds} seconds: the camera answers slowly\\.`));
  const help = readFileSync(new URL("../../src/console_commands.cpp", import.meta.url), "utf8");
  assert.ok(help.includes(`locations every ${normal.seconds} s or every ${frequent.seconds} s`));
  const readme = readFileSync(new URL("../../README.md", import.meta.url), "utf8").replace(/\s+/g, " ");
  assert.ok(readme.includes(`with **Normal** location updates, every ${normal.seconds} s – earlier when it moves ` +
    `more than ${normal.metres} m; with **Frequent** (1.4.0) every ${frequent.seconds} s – earlier when it moves ` +
    `more than ${frequent.metres} m, at most every ${frequent.minGapSeconds} s.`));
});

// updates_now differs from updates only while the guard holds a connection at normal.
test("the state says when the card sends less often than chosen", () => {
  const lines = statusLinesOf(statusWith({ updates: "frequent", updates_now: "normal" }, STATUS_1_4));
  const status = page.parseStatus(lines);
  assert.equal(status.kind, "ok");
  assert.equal(status.state.updates, "frequent");
  assert.equal(status.state.updatesNow, "normal");
  const now = (updates, updatesNow) => page.updatesNowText({ ...STATE, updates, updatesNow });
  assert.match(now("frequent", "normal"), /Frequent comes back when you switch the camera off and on\.$/);
  assert.equal(now("frequent", "frequent"), null);
  assert.equal(now("normal", "normal"), null);
  assert.equal(now(null, null), null); // gpxcam before 1.4.0
});

// storage=damaged: the card did not mount its file system and left it as it is (src/storage.h).
test("the state says when the card cannot read what it saved", () => {
  const lines = (storage) => statusLinesOf(statusWith({ storage }, STATUS_1_4));
  assert.equal(page.parseStatus(lines("damaged")).state.storage, "damaged");
  assert.equal(page.parseStatus(lines("ok")).state.storage, "ok");
  assert.equal(page.parseStatus(statusLinesOf(STATUS_ANSWER)).state.storage, null); // gpxcam 1.3
  assert.match(page.storageNoteText({ ...STATE, storage: "damaged" }), /the pairing comes from its copy/i);
  assert.match(page.storageNoteText({ ...STATE, storage: "damaged", paired: false }),
    /It has no pairing and cannot connect to the camera\./);
  for (const storage of ["ok", null]) assert.equal(page.storageNoteText({ ...STATE, storage }), null);
});

test("a note on the page while the card cannot read what it saved", async () => {
  const answers = { v: VERSION_1_4, status: statusWith({ storage: "damaged" }, STATUS_1_4) };
  const ui = openPage(simulatedCard(answers));
  assert.equal(ui["storage-note"].hidden, true);
  await ui.connect.fire("click");
  assert.equal(ui["storage-note"].hidden, false);
  assert.match(ui["storage-note"].textContent, /^The card cannot read its saved settings/);
  // Pairing and forgetting would be refused: the buttons are off, and the camera note says why.
  for (const id of ["pair-again", "forget", "forget-yes"]) assert.equal(ui[id].disabled, true, id);
  assert.match(ui["camera-note"].textContent, /^The card cannot change the pairing while/);
  answers.status = statusWith({ storage: "ok" }, STATUS_1_4);
  await ui.refresh.fire("click");
  assert.equal(ui["storage-note"].hidden, true);
  for (const id of ["pair-again", "forget", "forget-yes"]) assert.equal(ui[id].disabled, false, id);
  assert.equal(ui["camera-note"].hidden, true);
  await ui.disconnect.fire("click");
});

test("the location updates choice follows the card, with a note while the camera answers slowly", async () => {
  const answers = { v: VERSION_1_4, status: STATUS_1_4 };
  const port = simulatedCard(answers);
  const ui = openPage(port);
  const checked = () => [...page.UPDATES_SETTINGS].filter((setting) => ui[`updates-${setting}`].checked);
  assert.deepEqual(checked(), []); // not connected
  assert.equal(ui["updates-frequent"].disabled, true);
  assert.equal(ui["updates-note"].textContent, "Connect the card to change this setting.");
  assert.equal(ui["updates-now"].hidden, true);
  await ui.connect.fire("click");
  assert.deepEqual(checked(), ["normal"]);
  assert.match(ui["updates-says"].textContent, /^Every 25 seconds/);
  assert.equal(ui["updates-frequent"].disabled, false);
  assert.match(ui["updates-note"].textContent, /^The camera keeps the last position it got/);

  answers["updates frequent"] = ["updates: frequent saved\r\n"];
  answers.status = statusWith({ updates: "frequent", updates_now: "frequent" }, STATUS_1_4);
  ui["updates-frequent"].checked = true; // as the browser does on a click
  await ui["updates-frequent"].fire("change");
  assert.deepEqual(port.written.slice(-2), ["updates frequent\n", "status\n"]);
  assert.equal(ui["updates-answer"].textContent, "Saved. The card uses it from the next position it sends.");
  assert.equal(ui["updates-answer"].className, "good");
  assert.deepEqual(checked(), ["frequent"]);
  assert.match(ui["updates-says"].textContent, /^Every 5 seconds/);
  assert.equal(ui["updates-now"].hidden, true);

  answers.status = statusWith({ updates: "frequent", updates_now: "normal" }, STATUS_1_4); // the guard
  await ui.refresh.fire("click");
  assert.deepEqual(checked(), ["frequent"]); // still the choice
  assert.equal(ui["updates-now"].hidden, false);
  assert.match(ui["updates-now"].textContent, /^For now every 25 seconds: the camera answers slowly\./);

  answers.status = statusWith({ updates: "frequent", updates_now: "frequent" }, STATUS_1_4); // switched off and on
  await ui.refresh.fire("click");
  assert.equal(ui["updates-now"].hidden, true);
  assert.equal(ui["updates-now"].textContent, "");

  answers["updates normal"] = ["updates: could not write the settings file\r\n"];
  ui["updates-normal"].checked = true;
  await ui["updates-normal"].fire("change");
  assert.equal(ui["updates-answer"].className, "bad");
  assert.deepEqual(checked(), ["frequent"]); // as the card has it

  answers.status = statusWith({ updates: "frequent", updates_now: "normal" }, STATUS_1_4);
  await ui.refresh.fire("click");
  await ui.disconnect.fire("click");
  assert.deepEqual(checked(), []);
  assert.equal(ui["updates-normal"].disabled, true);
  assert.equal(ui["updates-answer"].textContent, "");
  assert.equal(ui["updates-now"].hidden, true);
});

test("with gpxcam 1.3 and 1.2 the location updates stay normal and cannot be changed", async () => {
  for (const [answers, version] of [[CARD_1_3, "1.3.0"],
    [CARD_1_2, "1.2.0"]]) {
    const ui = openPage(simulatedCard(answers));
    await ui.connect.fire("click");
    assert.equal(ui["updates-normal"].checked, true, version);
    assert.equal(ui["updates-frequent"].checked, false, version);
    assert.equal(ui["updates-normal"].disabled, true, version);
    assert.equal(ui["updates-frequent"].disabled, true, version);
    assert.match(ui["updates-says"].textContent, /^Every 25 seconds/);
    assert.equal(ui["updates-note"].textContent,
      `gpxcam ${version} sends the position every 25 seconds. Choosing needs version 1.4.0.`);
    assert.equal(ui["updates-now"].hidden, true);
    await ui.disconnect.fire("click");
  }
});

test("with gpxcam 1.2 the sound stays loud and cannot be changed", async () => {
  const { ui } = await connected(CARD_1_2);
  assert.equal(ui["sound-loud"].checked, true);
  assert.equal(ui["sound-says"].textContent, "All sounds."); // 1.2 plays them loud
  assert.equal(ui["sound-loud"].disabled, true);
  assert.equal(ui["sound-off"].disabled, true);
  assert.match(ui["sound-note"].textContent, /^gpxcam 1\.2\.0 always plays its sounds loud\. Choosing needs/);
  await ui.disconnect.fire("click");
});


// ---- The camera section's pairing screens ----

// A line as the card prints it, from the format string in its source that starts with `start`; each
// conversion takes the next value.
function firmwareLine(file, start, ...values) {
  const source = readFileSync(new URL(`../../src/${file}`, import.meta.url), "utf8");
  const escaped = start.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const format = new RegExp(`logger::(?:line|transient)\\("(${escaped}[^"]*)"`).exec(source);
  assert.ok(format, `${file}: ${start}`);
  let next = 0;
  return format[1].replace(/%(02x|lu|u|d|s)/g, () => String(values[next++]));
}

// The text a firmware function returns for a case, or a name in a list of names: asserted to be in the source.
function assertFirmwareText(file, text) {
  assert.ok(readFileSync(new URL(`../../src/${file}`, import.meta.url), "utf8").includes(`"${text}"`), `${file}: ${text}`);
}

// The camera section's pairing screens in the page's HTML.
const pairingHtml = /<div id="pairing" [\s\S]*?<\/details>\n {6}<\/div>/.exec(html)[0];
const screenHtml = (name) => new RegExp(String.raw`<div id="pair-screen-${name}"[\s\S]*?` +
  String.raw`(?=\n {8}<div id="pair-screen-|\n {8}<p id="pair-status")`).exec(pairingHtml)[0];

test("the pairing screens are made of real buttons, labelled checkboxes and one status line, 44 px high", () => {
  for (const name of ["prepare", "camera", "connect", "wifi", "done", "failed"]) assert.ok(screenHtml(name), name);
  const buttons = [...pairingHtml.matchAll(/<button [^>]*>/g)].map((match) => match[0]);
  assert.ok(buttons.length >= 9);
  for (const button of buttons) assert.match(button, /type="button"/, button);
  for (const box of ["check-list", "check-phone"]) {
    assert.match(pairingHtml, new RegExp(`<label><input id="${box}" type="checkbox">`), box);
  }
  assert.match(pairingHtml, /<progress id="pair-countdown-bar" [^>]*aria-label="Time left to find the camera"/);
  assert.equal(pairingHtml.match(/role="status"/g).length, 1);
  assert.match(html, /\.pairing button \{ min-height: 44px; \}/);
  // One element for each step and stage the functions give.
  for (const step of page.pairingSteps("wifi", true)) {
    assert.match(pairingHtml, new RegExp(`id="pair-step-${step.name}"`));
  }
  const stages = page.connectStages(0).length;
  assert.equal(screenHtml("connect").match(/id="connect-stage-\d"/g).length, stages);
  // Pairing as with an older camera is offered on the screen of a failed pairing only.
  assert.equal(pairingHtml.match(/id="pair-older"/g).length, 1);
  assert.match(screenHtml("failed"), /id="pair-older"/);
  // How pairing works is folded, and its words fit the page and the app alike.
  assert.match(pairingHtml, /<details id="pairing-help" class="help">/);
  assert.match(pairingHtml, /When this screen says <em>Start Bluetooth pairing on the camera now<\/em>/);
  assert.doesNotMatch(pairingHtml, /the page says/);
  // Paired: the panel's mark and words in the middle.
  assert.match(html, /\.done-panel \{[^}]*text-align: center;/);
  assert.match(html, /\.done-panel \.row \{ justify-content: center; \}/);
});

test("the camera section's screen follows the card's pairing phase and how the last pairing ended", () => {
  const screen = (state, context) => json(page.pairingScreen({ ...STATE, ...state }, context));
  const prepare = { screen: "prepare", variant: null };
  const done = { screen: "done", variant: null };
  assert.deepEqual(screen({ paired: false }), prepare);
  assert.deepEqual(screen({ paired: true }), done);
  assert.deepEqual(screen({ paired: true }, { pairAgain: true }), prepare);
  assert.deepEqual(screen({ pair: "waiting_gps" }), { screen: "camera", variant: "gps" });
  assert.deepEqual(screen({ pair: "searching" }), { screen: "camera", variant: "search" });
  assert.deepEqual(screen({ pair: "pausing" }), { screen: "camera", variant: "pause" });
  assert.deepEqual(screen({ pair: "connecting" }), { screen: "connect", variant: null });
  assert.deepEqual(screen({ pair: "waiting_wifi" }), { screen: "wifi", variant: null });
  assert.deepEqual(screen({ pair: "waiting_wifi" }, { introducing: true }), { screen: "connect", variant: null });
  assert.deepEqual(screen({ pair: "a_later_phase" }), { screen: "connect", variant: null });
  // A pairing under way is shown whatever ended before.
  assert.deepEqual(screen({ pair: "searching" }, { ended: "no_camera" }), { screen: "camera", variant: "search" });
  // How the last pairing the page followed ended.
  assert.deepEqual(screen({ paired: true }, { ended: "paired" }), done);
  for (const ended of ["no_camera", "not_in_pairing_mode"]) {
    assert.deepEqual(screen({ paired: false }, { ended }), { screen: "failed", variant: "no-camera" }, ended);
  }
  for (const ended of ["failed", "no_gps_fix", "not_saved", "not_possible", "wifi-given-up"]) {
    assert.deepEqual(screen({ paired: true }, { ended }), { screen: "failed", variant: "other" }, ended);
  }
  assert.deepEqual(screen({ paired: false }, { ended: "cancelled" }), prepare);
  assert.deepEqual(screen({ paired: true }, { ended: "cancelled" }), done);
  assert.deepEqual(screen({ paired: false }, { ended: "none" }), prepare);
  // Every phase of the firmware but idle is a pairing under way.
  const phases = firmwareValues("pairPhaseValue");
  for (const pair of phases.slice(1)) {
    assert.ok(["camera", "connect", "wifi"].includes(page.pairingScreen({ ...STATE, pair }).screen), pair);
  }
});

test("the row of steps shows where the pairing is, with the Wi-Fi step only for an older camera", () => {
  const steps = (screen, older) => {
    const row = page.pairingSteps(screen, older);
    return row === null ? null : json(row.map((step) => `${step.name}:${step.state}`));
  };
  assert.deepEqual(steps("prepare", false), ["prepare:current", "camera:later", "connect:later", "done:later"]);
  assert.deepEqual(steps("camera", false), ["prepare:done", "camera:current", "connect:later", "done:later"]);
  assert.deepEqual(steps("connect", true),
    ["prepare:done", "camera:done", "connect:current", "wifi:later", "done:later"]);
  assert.deepEqual(steps("wifi", false), ["prepare:done", "camera:done", "connect:done", "wifi:current", "done:later"]);
  assert.equal(steps("done", true), null);
  assert.equal(steps("failed", false), null);
  assert.deepEqual(json(page.pairingSteps("camera", true).map((step) => step.label)),
    ["Prepare", "Pair on camera", "Connect", "Camera Wi-Fi", "Done"]);
});

test("the badge beside the title says whether the card is paired or pairing", () => {
  assert.equal(page.pairingBadge({ ...STATE, paired: false }), "Not paired yet");
  assert.equal(page.pairingBadge({ ...STATE, paired: true }), "Paired");
  assert.equal(page.pairingBadge({ ...STATE, paired: true, pair: "searching" }), "Pairing…");
});

test("the card's lines start the countdown, tell the stages, the older camera and why the card gave up", () => {
  const at = 1000000;
  const event = (line, searchMs) => json(page.pairingEvent(line, at, searchMs));
  const search = (startsAt, lengthMs = 120000) => ({ search: { startsAt, lengthMs } });
  assert.deepEqual(event(firmwareLine("pairing.cpp", "pair: GPS time ok", 120)), search(at));
  assert.deepEqual(event(firmwareLine("pairing.cpp", "pair: GPS time ok", 60)), search(at, 60000));
  const quickRetry = firmwareLine("pairing.cpp", "pair: quick retry", 2, 5, "no answer in connect");
  assert.deepEqual(event(quickRetry), search(at));
  assert.deepEqual(event(quickRetry, 60000), search(at, 60000));
  assert.deepEqual(event(firmwareLine("pairing.cpp", "pair: next attempt in", 10)), search(at + 10000));
  // After a reconnect: 'p' gives how long the card has looked.
  assertFirmwareText("pairing.cpp", "looking for a camera in pairing mode");
  const looking = firmwareLine("pairing.cpp", "pair: %s for", "looking for a camera in pairing mode", 37, 0, 3);
  assert.deepEqual(event(looking), search(at - 37000));
  assert.deepEqual(event(firmwareLine("pairing.cpp", "pair: camera %s is %s", "********9a", "on", "00")),
    { nearby: true });
  // The stages of the connection, in both dialects.
  assert.deepEqual(event(firmwareLine("pairing.cpp", "pair: attempt %d/%d - camera", 1, 3, "********9a", -50)),
    { stages: 1 });
  assert.deepEqual(event(firmwareLine("newer_session.cpp", "camera: connected")), { stages: 2 });
  assert.deepEqual(event(firmwareLine("older_session.cpp", "older: connected")), { stages: 2 });
  for (const name of ["device name", "time", "identifier"]) {
    assertFirmwareText("newer_session.cpp", name);
    assertFirmwareText("older_session.cpp", name);
  }
  assert.deepEqual(event(firmwareLine("newer_session.cpp", "camera: %s accepted", "device name")), { stages: 3 });
  assert.deepEqual(event(firmwareLine("older_session.cpp", "older: %s written", "device name")), { stages: 3 });
  assert.deepEqual(event(firmwareLine("newer_session.cpp", "camera: %s accepted", "time")), { stages: 4 });
  assert.deepEqual(event(firmwareLine("older_session.cpp", "older: %s written", "time")), { stages: 4 });
  assert.equal(event(firmwareLine("newer_session.cpp", "camera: %s accepted", "identifier")), null);
  // An older camera: Pair switches to it, then it waits for the Wi-Fi step.
  assert.deepEqual(event(firmwareLine("pairing.cpp", "pair: camera %s has no", "********9a")), { older: true });
  assert.deepEqual(event(firmwareLine("older_session.cpp", "older: introduced", 300, " (the camera now waits for " +
    "the Wi-Fi step)")), { wifiStep: true });
  assert.deepEqual(event("older: app register identity 4D454930-0100-1000-8001-A1B2C3D4E5F6 name T1000-E"),
    { wifiStep: true });
  assert.deepEqual(event(firmwareLine("pairing.cpp", "pair: given up - %s", "no camera in pairing mode in time")),
    { gaveUp: "no camera in pairing mode in time" });
  for (const line of ["link: location 3 sent (period), answer 80 ms, frequent", "pair: paired with camera ********9a",
    "camera: key read", "status: pair=searching pair_attempt=0 pair_result=none"]) {
    assert.equal(event(line), null, line);
  }
});

test("the page's search time is the card's", () => {
  const source = readFileSync(new URL("../../src/pairing.cpp", import.meta.url), "utf8");
  assert.equal(page.PAIR_SEARCH_MS, Number(/constexpr uint32_t SEARCH_MS = (\d+);/.exec(source)[1]));
});

test("the countdown runs from 2:00 to 0:00, also for a screen reader", () => {
  const texts = [120000, 119001, 119000, 61000, 60000, 999, 0, -5].map((ms) => page.countdownText(ms));
  assert.deepEqual(texts, ["2:00", "2:00", "1:59", "1:01", "1:00", "0:01", "0:00", "0:00"]);
  assert.equal(page.countdownLabel(120000), "2 minutes left");
  assert.equal(page.countdownLabel(92000), "1 minute 32 seconds left");
  assert.equal(page.countdownLabel(61000), "1 minute 1 second left");
  assert.equal(page.countdownLabel(1000), "1 second left");
  assert.equal(page.countdownLabel(0), "0 seconds left");
  const search = { startsAt: 1000000, lengthMs: 120000 };
  assert.equal(page.searchLeftMs(null, 1000000), null);
  assert.equal(page.searchLeftMs(search, 1020000), 100000);
  assert.equal(page.searchLeftMs(search, 1200000), 0);
  assert.equal(page.searchLeftMs(search, 990000), 120000); // a pause before it: its whole length
});

test("the stages of the connection: done, then the current one, then those to come", () => {
  const stages = (done) => json(page.connectStages(done).map((stage) => stage.state));
  assert.deepEqual(stages(1), ["done", "current", "later", "later"]);
  assert.deepEqual(stages(3), ["done", "done", "done", "current"]);
  assert.deepEqual(stages(4), ["done", "done", "done", "done"]);
  assert.deepEqual(json(page.connectStages(0).map((stage) => stage.label)), ["Camera in pairing mode",
    "Connected over Bluetooth", "Introducing the card to the camera…", "Setting the camera's clock"]);
});

test("under the countdown the page says what the card does", () => {
  const text = (variant, state, context) => json(page.cameraStepText(variant, { ...STATE, ...state }, context));
  assert.deepEqual(text("gps", { gpsSatellites: 3 }),
    ["Waiting for the GPS time – 3 satellites so far. Don't start pairing on the camera yet.", "busy"]);
  assert.deepEqual(text("search", {}), ["Looking for the camera…", "busy"]);
  assert.deepEqual(text("search", { pairAttempt: 1 }), ["Looking for the camera again (attempt 2 of 3)…", "busy"]);
  assert.deepEqual(text("search", {}, { nearby: true }), ["A camera is nearby, but not in pairing mode yet.", "warn"]);
  assert.deepEqual(text("pause", { pairAttempt: 1 }, { pauseLeftMs: 9500 }),
    ["Attempt 1 of 3 didn't work – looking again in 10 s. Keep the camera in pairing mode.", "warn"]);
  assert.match(text("pause", { pairAttempt: 2 })[0], /^Attempt 2 of 3 didn't work – looking again in a moment\./);
});

test("Prepare checks the GPS by itself", () => {
  assert.deepEqual(json(page.gpsCheck({ ...STATE, gpsFix: true, gpsSatellites: 11 })),
    { done: true, detail: "11 satellites – the card knows the time. Checked by itself." });
  assert.deepEqual(json(page.gpsCheck({ ...STATE, gpsFix: false, gpsSatellites: 1 })),
    { done: false, detail: "Waiting for a fix – 1 satellite so far." });
  assert.equal(page.gpsCheck({ ...STATE, gpsOn: false, gpsFix: false }).detail, "The GPS switches on when pairing starts.");
});

test("every way a pairing can end without a camera is explained, the older-camera box only where it helps", () => {
  const results = firmwareValues("pairResultValue").filter((result) => !["none", "paired", "cancelled"].includes(result));
  assert.deepEqual(results, ["not_saved", "not_possible", "no_gps_fix", "no_camera", "not_in_pairing_mode", "failed"]);
  for (const result of [...results, "wifi-given-up"]) {
    assert.doesNotMatch(page.failedText(result).text, /^The card ended the pairing/, result);
  }
  const older = [...results, "wifi-given-up"].filter((result) => page.failedText(result).older);
  assert.deepEqual(older, ["no_camera", "not_in_pairing_mode", "failed"]);
  assert.deepEqual(json(page.failedText("no_camera")), { title: "The camera did not answer",
    text: "The card looked for 2 minutes and didn't find a camera in pairing mode.", older: true });
  assert.match(page.failedText("failed").text, /^The camera did not take the card in 3 attempts\./);
  assert.equal(page.failedText("failed", "no connection to the camera after 5 quick retries (last: link not " +
    "established (0x3e))").text, "The card could not connect to the camera: the connection never came up.");
  assert.equal(page.failedText("failed", "all attempts failed").text, page.failedText("failed").text);
});

test("the answers to 'unpair' are told apart, also the card's line for damaged storage", () => {
  assert.equal(page.classifyUnpairAnswer(["link: off", "pair: camera forgotten (the card id stays)"]), "forgotten");
  assert.equal(page.classifyUnpairAnswer(["pair: no camera paired"]), "not-paired");
  assert.equal(page.classifyUnpairAnswer(["pair: could not write the pairing file"]), "failed");
  assert.equal(page.classifyUnpairAnswer(["pair: not available"]), "failed");
  assert.equal(page.classifyUnpairAnswer(
    ["pair: not possible - the file system is damaged ('storage format yes' first)"]), "storage-damaged");
  // The card's line (src/pairing_store.h NOT_WRITABLE_LINE) must stay the one the page knows.
  const store = readFileSync(new URL("../../src/pairing_store.h", import.meta.url), "utf8");
  assert.ok(store.includes("\"pair: not possible - the file system is damaged ('storage format yes' first)\""));
  assert.match(page.unpairAnswerText("storage-damaged")[0], /^The card cannot change the pairing while/);
  assert.equal(page.unpairAnswerText("storage-damaged")[1], "bad");
  assert.equal(page.classifyUnpairAnswer([]), "none");
  assert.equal(page.unpairAnswerText("failed")[1], "bad");
});

// The camera section as shown: its screen, and the steps of the row.
const shownScreen = (ui) => ["prepare", "camera", "connect", "wifi", "done", "failed"]
  .filter((name) => !ui[`pair-screen-${name}`].hidden);
const shownSteps = (ui) => ["prepare", "camera", "connect", "wifi", "done"]
  .filter((name) => !ui[`pair-step-${name}`].hidden).map((name) => `${name}:${ui[`pair-step-${name}`].className}`);

test("pairing from the page: Prepare, the camera, the connection, done, then forgetting the camera", async () => {
  const answers = { v: VERSION_1_3, status: statusWith(NOT_PAIRED) };
  const port = simulatedCard(answers);
  const ui = openPage(port);
  assert.equal(ui.pairing.hidden, true); // not connected
  assert.equal(ui["camera-note"].textContent, "Connect the card to pair it with your camera.");
  await ui.connect.fire("click");
  assert.equal(ui.pairing.hidden, false);
  assert.equal(ui["camera-note"].hidden, true);
  assert.deepEqual(shownScreen(ui), ["prepare"]);
  assert.equal(ui["pair-badge"].textContent, "Not paired yet");
  assert.deepEqual(shownSteps(ui), ["prepare:current", "camera:later", "connect:later", "done:later"]);
  assert.equal(ui["pair-step-prepare"].attributes["aria-current"], "step");
  assert.equal(ui["check-gps"].className, "check-done"); // a fix: checked by itself
  assert.match(ui["check-gps-detail"].textContent, /^11 satellites – the card knows the time/);
  assert.equal(ui["pair-wait-note"].textContent,
    "Don't start pairing on the camera yet – this page tells you when.");
  assert.equal(ui["pair-keep"].hidden, true); // nothing paired to keep
  assert.equal(ui["check-list"].checked, false); // the reminders do not hold Pair back
  assert.equal(ui["pair-start"].disabled, false);
  assert.equal(ui["pair-stop-row"].hidden, true);

  answers.pair = PAIR_STARTED;
  answers.status = statusWith({ ...NOT_PAIRED, pair: "waiting_gps", gps_fix: 0, gps_sat: 3 });
  await ui["pair-start"].fire("click");
  assert.deepEqual(port.written.slice(-2), ["pair\n", "status\n"]);
  assert.deepEqual(shownScreen(ui), ["camera"]);
  assert.equal(ui["pair-callout"].hidden, true); // not the user's turn yet
  assert.equal(ui["pair-status"].textContent,
    "Waiting for the GPS time – 3 satellites so far. Don't start pairing on the camera yet.");
  assert.equal(ui["pair-status"].className, "busy");
  assert.equal(ui["pair-badge"].textContent, "Pairing…");
  assert.equal(ui["pair-stop-row"].hidden, false);
  assert.equal(ui["pair-start"].disabled, true);
  assert.equal(ui["state-camera"].textContent, "Pairing");

  // The card starts looking: the countdown starts with its line.
  answers.status = statusWith({ ...NOT_PAIRED, pair: "searching" });
  port.say("pair: GPS time ok - looking for a camera in pairing mode (up to 120 s)\r\n");
  await ui.refresh.fire("click");
  assert.equal(ui["pair-callout"].hidden, false);
  assert.equal(ui["pair-countdown"].hidden, false);
  assert.equal(ui["pair-countdown-time"].textContent, "2:00");
  assert.equal(ui["pair-countdown-bar"].max, 120);
  assert.equal(ui["pair-countdown-bar"].attributes["aria-valuetext"], "2 minutes left");
  assert.deepEqual(shownSteps(ui), ["prepare:done", "camera:current", "connect:later", "done:later"]);
  assert.equal(ui["pair-status"].textContent, "Looking for the camera…");
  port.say("pair: camera 123456789a is on (00), not in pairing mode - choose pairing on the camera\r\n");
  await until(() => ui["pair-status"].textContent === "A camera is nearby, but not in pairing mode yet.");
  assert.equal(ui["pair-status"].className, "warn");

  // Found: the stages of the connection.
  answers.status = statusWith({ ...NOT_PAIRED, pair: "connecting", pair_attempt: 1 });
  port.say("pair: attempt 1/3 - camera 123456789a in pairing mode, -50 dBm, connecting\r\n");
  await ui.refresh.fire("click");
  assert.deepEqual(shownScreen(ui), ["connect"]);
  const stages = () => [1, 2, 3, 4].map((stage) => ui[`connect-stage-${stage}`].className);
  assert.deepEqual(stages(), ["done", "current", "later", "later"]);
  port.say("camera: connected\r\ncamera: device name accepted\r\n");
  await until(() => stages()[3] === "current");
  assert.deepEqual(stages(), ["done", "done", "done", "current"]);
  assert.equal(ui["connect-stage-4"].textContent, "Setting the camera's clock");

  answers.status = statusWith({ link: "searching", pair_result: "paired" });
  await ui.refresh.fire("click");
  assert.deepEqual(shownScreen(ui), ["done"]);
  assert.equal(ui["pair-steps"].hidden, true);
  assert.equal(ui["pair-badge"].textContent, "Paired");
  assert.equal(ui["pair-stop-row"].hidden, true);
  await ui.refresh.fire("click");
  assert.deepEqual(shownScreen(ui), ["done"]); // it stays

  assert.equal(ui.forget.className, "danger"); // forgetting cannot be undone: red text, not a red button
  await ui.forget.fire("click");
  assert.equal(ui["forget-confirm"].hidden, false);
  await ui["forget-no"].fire("click");
  assert.equal(ui["forget-confirm"].hidden, true);
  assert.equal(port.written.includes("unpair\n"), false);

  answers.unpair = FORGOTTEN;
  answers.status = statusWith({ ...NOT_PAIRED, pair_result: "paired" }); // the result stays until a new pairing
  await ui.forget.fire("click");
  await ui["forget-yes"].fire("click");
  assert.deepEqual(port.written.slice(-2), ["unpair\n", "status\n"]);
  assert.deepEqual(shownScreen(ui), ["prepare"]);
  assert.equal(ui["pair-status"].textContent, "The camera is forgotten.");
  assert.equal(ui["forget-confirm"].hidden, true);
  await ui.disconnect.fire("click");
  assert.equal(ui.pairing.hidden, true);
  assert.equal(ui["camera-note"].textContent, "Connect the card to pair it with your camera.");
});

test("a cancelled pairing, one refused at once, and one from the card's button that found no camera", async () => {
  const answers = { v: VERSION_1_3, status: statusWith(NOT_PAIRED), pair: PAIR_STARTED };
  const { port, ui } = await connected(answers);

  answers.status = statusWith({ ...NOT_PAIRED, pair: "searching" });
  await ui["pair-start"].fire("click");
  assert.deepEqual(shownScreen(ui), ["camera"]);
  assert.equal(ui["pair-countdown"].hidden, true); // the card's line with its time was not seen
  answers["pair off"] = ["pair: cancelled\r\n"];
  answers.status = statusWith({ ...NOT_PAIRED, pair_result: "cancelled" });
  await ui["pair-stop"].fire("click");
  assert.deepEqual(port.written.slice(-2), ["pair off\n", "status\n"]);
  assert.deepEqual(shownScreen(ui), ["prepare"]);
  assert.equal(ui["pair-status"].textContent, "Pairing cancelled.");

  answers.pair = ["pair: not possible - BLE or the pairing file is not available\r\n"];
  answers.status = statusWith({ ...NOT_PAIRED, pair_result: "not_possible" });
  await ui["pair-start"].fire("click");
  assert.deepEqual(shownScreen(ui), ["failed"]);
  assert.equal(ui["failed-title"].textContent, "The card cannot pair now");
  assert.equal(ui["failed-tips"].hidden, true);
  assert.equal(ui["older-box"].hidden, true);

  answers.status = statusWith({ ...NOT_PAIRED, pair: "searching" }); // click + hold on the card
  await ui.refresh.fire("click");
  assert.deepEqual(shownScreen(ui), ["camera"]);
  answers.status = statusWith({ ...NOT_PAIRED, pair_result: "no_camera" });
  await ui.refresh.fire("click");
  assert.deepEqual(shownScreen(ui), ["failed"]);
  assert.equal(ui["failed-title"].textContent, "The camera did not answer");
  assert.equal(ui["failed-tips"].hidden, false);
  assert.equal(ui["failed-paired"].hidden, true); // no camera was paired before
  assert.equal(ui["older-box"].hidden, true); // gpxcam 1.3.0 cannot pair as with an older camera

  // Try again pairs at once.
  answers.status = statusWith({ ...NOT_PAIRED, pair: "searching" });
  await ui["pair-retry"].fire("click");
  assert.equal(port.written.filter((command) => command === "pair\n").length, 3);
  assert.deepEqual(shownScreen(ui), ["camera"]);
  await ui.disconnect.fire("click");
});

test("How pairing works folds on each new screen; a failed pairing of a paired card keeps its camera", async () => {
  const answers = { v: VERSION_1_3, status: STATUS_ANSWER, pair: PAIR_STARTED };
  const { ui } = await connected(answers);
  assert.deepEqual(shownScreen(ui), ["done"]);
  assert.equal(ui["pairing-help"].open, false);
  ui["pairing-help"].open = true; // the user opens it
  await ui.refresh.fire("click");
  assert.equal(ui["pairing-help"].open, true); // on the same screen it stays open
  await ui["pair-again"].fire("click");
  assert.deepEqual(shownScreen(ui), ["prepare"]);
  assert.equal(ui["pairing-help"].open, false); // a new screen: folded
  ui["pairing-help"].open = true;
  answers.status = statusWith({ pair: "searching" });
  await ui["pair-start"].fire("click");
  assert.deepEqual(shownScreen(ui), ["camera"]);
  assert.equal(ui["pairing-help"].open, false);
  answers.status = statusWith({ pair_result: "no_camera" }); // the card still has its camera
  await ui.refresh.fire("click");
  assert.deepEqual(shownScreen(ui), ["failed"]);
  assert.equal(ui["failed-paired"].hidden, false);
  assert.match(screenHtml("failed"), /<p id="failed-paired" class="hidden">Your camera stays paired as before\.<\/p>/);
  await ui.disconnect.fire("click");
});

test("a failed pairing offers the older camera, with its button in the app only", async () => {
  const searching = { ...NOT_PAIRED, fw: "1.5.0", pair: "searching" };
  const failed = { ...NOT_PAIRED, fw: "1.5.0", pair_result: "no_camera" };
  const browser = { v: VERSION_1_4, status: statusWith(searching) };
  const { ui } = await connected(browser);
  browser.status = statusWith(failed);
  await ui.refresh.fire("click");
  assert.deepEqual(shownScreen(ui), ["failed"]);
  assert.equal(ui["older-box"].hidden, false);
  assert.equal(ui["pair-older"].hidden, true); // a browser cannot do the Wi-Fi step
  assert.equal(ui["older-browser"].hidden, false);
  await ui.disconnect.fire("click");

  const inApp = { v: VERSION_1_4, status: statusWith(searching, STATUS_GPS_HOLD), p: CARD_ID_ANSWER };
  const app = await connectedApp(inApp);
  inApp.status = statusWith(failed, STATUS_GPS_HOLD);
  await app.ui.refresh.fire("click");
  assert.equal(app.ui["older-box"].hidden, false);
  assert.equal(app.ui["pair-older"].hidden, false);
  assert.equal(app.ui["older-browser"].hidden, true);
  await app.ui.disconnect.fire("click");
});

test("with gpxcam 1.2 pairing is left to the card's button", async () => {
  const { ui } = await connected(CARD_1_2);
  assert.equal(ui.pairing.hidden, true);
  assert.equal(ui["camera-note"].hidden, true);
  assert.equal(ui["camera-old"].hidden, false); // pair with the button, with a link to the README
  const readme = '<a href="https://github.com/TomaszBojanowski/gpxcam-t1000e#pairing-once">the README</a>';
  assert.ok(html.includes(readme));
  await ui.disconnect.fire("click");
  assert.equal(ui["camera-note"].hidden, false);
  assert.equal(ui["camera-old"].hidden, true);
});

// ---- Diagnostics ----

// The card's answer to 'log' (src/log.cpp dumpHistory()) and a 'diag scan' run (src/bluetooth.cpp,
// src/scan_dump.cpp), made-up values; a status answer of the page's refresh comes in between.
const LOG_ANSWER = [
  "[     12.3 s] gps: on\r\n[    15.0 s] link: looking for camera 123456789a\r\n",
  "[  12345.6 s] link: position sent\r\n",
];
// The run as 1.5.0 gives it: a Panasonic advert's bytes in a line of their own under its header,
// the reports per channel before "done".
const SCAN_ANSWER = [
  "diag scan: on 30 s, radio 50 ms of every 100 ms, reports stronger than -65 dBm; full bytes for Panasonic\r\n",
  "diag scan: **:**:**:**:**:e2 RPA -48 dBm ch 38 adv connectable 13 B, camera\r\n",
  "diag scan:   bytes: 02 01 06 09 ff 3a 00 00 12 34 56 78 9a\r\n",
  "status: format=1 fw=1.3.0 git=1234567\r\n",
  "diag scan: **:**:**:**:**:33 RPA -40 dBm ch 37 rsp 7 B\r\n",
  "diag scan: by channel 37/38/39: 270/268/274 reports, 110/114/120 from LUMIX cameras\r\n",
  "diag scan: done, 2 device(s) shown, 812 reports received, 0 resume failed, 0 restarts\r\n",
];

// A clipboard that keeps what is written, or one that refuses (focus lost, not allowed).
function simulatedClipboard(refuses = false) {
  const clipboard = { written: [] };
  clipboard.writeText = async (text) => {
    if (refuses) throw new DOMException("Document is not focused.", "NotAllowedError");
    clipboard.written.push(text);
  };
  return clipboard;
}

test("diagnostics: log lines, scan lines and the end of a scan are told apart", () => {
  assert.equal(page.isLogLine("[     12.3 s] gps: on"), true);
  assert.equal(page.isLogLine("[  12345.6 s] link: position sent"), true);
  for (const line of ["gps: on", "status: end", "[12 s] gps: on", "diag scan: done, 0 device(s)"]) {
    assert.equal(page.isLogLine(line), false, line);
  }
  assert.equal(page.belongsToScan("diag scan: **:**:**:**:**:33 RPA -40 dBm rsp 7 B"), true);
  assert.equal(page.belongsToScan("link: off"), true); // the card's log around the run helps too
  assert.equal(page.belongsToScan("status: end"), false);
  for (const line of ["diag scan: done, 2 device(s) shown, 812 reports received", "diag scan: done, 0 device(s)",
    "diag scan: not possible, the SoftDevice did not start", "diag scan: already running",
    "command: unknown 'diag scan'"]) {
    assert.equal(page.scanEnded(line), true, line);
  }
  assert.equal(page.scanEnded("diag scan: on 30 s, radio 50 ms of every 100 ms"), false);
  assert.equal(page.SCAN_RUN_MS, 30000); // src/scan_dump.h DURATION_MS
  assert.match(html, new RegExp(`listens for ${page.SCAN_RUN_MS / 1000} seconds`)); // the Diagnostics text
  assert.equal(page.scanResultText(["diag scan: already running"], "1.5.0")[0],
    "A scan is already running on the card. Try again in 30 s.");
});

test("diagnostics: how a scan ended, and the heading of what is copied", () => {
  const done = ["diag scan: on 30 s", "diag scan: done, 0 device(s) shown, 5 reports received"];
  assert.deepEqual(json(page.scanResultText(done, "1.5.0")), ["Scan done.", "good"]);
  assert.match(page.scanResultText(["command: unknown 'diag scan'"], "1.4.0")[0], /^gpxcam 1\.4\.0 cannot scan/);
  for (const lines of [[], ["diag scan: on 30 s"], ["diag scan: not possible, the SoftDevice did not start"],
    ["diag scan: already running"]]) {
    assert.equal(page.scanResultText(lines, "1.5.0")[1], "bad", JSON.stringify(lines));
  }
  const date = new Date(Date.UTC(2027, 6, 1, 19, 5, 12));
  assert.equal(page.diagnosticsText("Card log", "gpxcam 1.5.0 (git 1234567)", ["a", "b"], date),
    "Card log - gpxcam 1.5.0 (git 1234567) - 2027-07-01T19:05:12.000Z\na\nb\n");
});

test("Copy log reads the card's log, shows it and copies it", async () => {
  const port = simulatedCard({ v: VERSION_1_3, status: STATUS_ANSWER, log: LOG_ANSWER });
  const clipboard = simulatedClipboard();
  const ui = openPage(port, { clipboard });
  assert.equal(ui["copy-log"].disabled, true); // not connected
  assert.equal(ui["diag-answer"].hidden, true);
  await ui.connect.fire("click");
  assert.equal(ui["copy-log"].disabled, false);
  await ui["copy-log"].fire("click");
  assert.equal(port.written.at(-1), "log\n");
  const text = ui["diag-text"].value;
  assert.match(text, /^Card log - gpxcam 1\.3\.0 \(git 1234567\) - \d{4}-\d\d-\d\dT/);
  assert.match(text, /\n\[     12\.3 s\] gps: on\n.*\n\[  12345\.6 s\] link: position sent\n$/);
  assert.match(text, /link: looking for camera \*{8}9a\n/); // the serial number is hidden
  assert.doesNotMatch(text, /123456789a/);
  assert.deepEqual(clipboard.written, [text]);
  assert.equal(ui["diag-status"].textContent, "The card's last 3 events. Copied: paste it into a message.");
  assert.equal(ui["diag-status"].className, "good");
  assert.equal(ui["diag-answer"].hidden, false);
  assert.equal(ui["diag-copy"].disabled, false);
  ui["diag-unmasked"].checked = true; // for the author: the text as the card sent it
  await ui["diag-unmasked"].fire("change");
  assert.match(ui["diag-text"].value, /link: looking for camera 123456789a\n/);
  await ui["diag-copy"].fire("click");
  assert.equal(clipboard.written.at(-1), ui["diag-text"].value);
  assert.equal(ui["diag-status"].textContent, "Copied unmasked: send it only privately.");
  ui["diag-unmasked"].checked = false;
  await ui["diag-unmasked"].fire("change");
  await ui.disconnect.fire("click");
  assert.equal(ui["diag-text"].value, text); // kept for copying after unplugging, masked again
  assert.equal(ui["copy-log"].disabled, true);
});

test("Scan for cameras collects the run without the page's status answers; copy by hand when refused", async () => {
  const port = simulatedCard({ v: VERSION_1_3, status: STATUS_ANSWER, "diag scan": SCAN_ANSWER });
  const clipboard = simulatedClipboard(true);
  const ui = openPage(port, { clipboard });
  await ui.connect.fire("click");
  await ui.scan.fire("click");
  assert.equal(port.written.at(-1), "diag scan\n");
  const lines = ui["diag-text"].value.trim().split("\n");
  assert.match(lines[0], /^Scan for cameras - gpxcam 1\.3\.0 \(git 1234567\) - /);
  assert.deepEqual(lines.slice(1), SCAN_ANSWER.filter((line) => !line.startsWith("status: "))
    .map((line) => line.trim().replace("ff 3a 00 00 12 34 56 78 9a", "ff 3a 00 00 ** ** ** ** 9a")));
  assert.equal(ui["diag-status"].textContent, "Scan done. Click Copy, or select the text below and copy it.");
  assert.equal(ui["diag-progress"].hidden, true);
  assert.equal(ui.scan.disabled, false);
  await ui["diag-copy"].fire("click");
  assert.match(ui["diag-status"].textContent, /^Copying did not work here: select the text below/);
  await ui.disconnect.fire("click");
});

test("Scan for cameras on a card without it says which gpxcam it needs", async () => {
  const port = simulatedCard({
    v: VERSION_1_4, status: STATUS_1_4, "diag scan": ["command: unknown 'diag scan'\r\n", ...OLD_FIRMWARE.slice(1)],
  });
  const ui = openPage(port, { clipboard: simulatedClipboard() });
  await ui.connect.fire("click");
  await ui.scan.fire("click");
  assert.match(ui["diag-status"].textContent, /^gpxcam 1\.4\.0 cannot scan\. Scanning needs version 1\.5\.0\./);
  assert.equal(ui["diag-status"].className, "bad");
  await ui.disconnect.fire("click");
});

// ---- The GPS ready for pairing ('pair prepare', 'status' gps_hold) ----

// STATUS_ANSWER of a card with 'pair prepare': the GPS line has gps_hold.
const STATUS_GPS_HOLD = [STATUS_ANSWER.join("").replace("gps_sat=11", "gps_sat=11 gps_hold=0")];

test("gps_hold tells a card with 'pair prepare'; only an unpaired, idle one gets its GPS ready", async () => {
  const { card } = await openCard({ status: statusWith({ gps_hold: 540 }, STATUS_GPS_HOLD) });
  const { state } = await card.status();
  assert.equal(state.gpsHold, 540);
  const unpaired = { ...STATE, paired: false, link: "off", gpsHold: 0 };
  assert.equal(page.shouldPrepareGps(unpaired), true);
  assert.equal(page.shouldPrepareGps({ ...unpaired, gpsHold: null }), false); // a card without 'pair prepare'
  assert.equal(page.shouldPrepareGps({ ...unpaired, paired: true }), false); // the link has the GPS
  assert.equal(page.shouldPrepareGps({ ...unpaired, pair: "searching" }), false);
});

test("connecting to an unpaired card switches its GPS on for pairing; a paired one is left alone", async () => {
  const unpaired = simulatedCard({ v: VERSION_1_4, status: statusWith(NOT_PAIRED, STATUS_GPS_HOLD),
    "pair prepare": PREPARE_ANSWER });
  const ui = openPage(unpaired);
  await ui.connect.fire("click");
  await tick();
  assert.deepEqual(unpaired.written, ["v\n", "status\n", "pair prepare\n"]);
  await ui.disconnect.fire("click");

  const paired = { v: VERSION_1_4, status: STATUS_GPS_HOLD };
  const withoutPrepare = { v: VERSION_1_4, status: statusWith(NOT_PAIRED) }; // without 'pair prepare'
  for (const answers of [paired, withoutPrepare]) {
    const port = simulatedCard(answers);
    const other = openPage(port);
    await other.connect.fire("click");
    await tick();
    assert.deepEqual(port.written, ["v\n", "status\n"]);
    await other.disconnect.fire("click");
  }
});

test("Prepare reminds to free the camera from an earlier pairing and from the phone, and says when to start", () => {
  const prepare = screenHtml("prepare").replace(/\s+/g, " ");
  assert.match(prepare, /T1000&#8209;E is not on the camera's Bluetooth list/);
  assert.match(prepare, /No phone is connected to the camera/);
  assert.match(prepare, /<button id="pair-start" type="button" class="big">Pair<\/button>/);
  const failed = screenHtml("failed").replace(/\s+/g, " ");
  assert.match(failed, /Start pairing on the camera only when you are told to/);
});

// ---- Masking what Diagnostics copies ----

test("Copy log and Scan hide serial numbers, addresses and the card's identity, and nothing else", () => {
  const mask = page.maskIdentifiers;
  // Made-up values: the serial 12 34 56 78 9a, addresses of the documentation range (RFC 7042).
  assert.equal(mask("link: looking for camera 123456789a"), "link: looking for camera ********9a");
  assert.equal(mask("scan: camera 123456789A seen: on (00), 00:00:5e:00:53:01 RPA, -64 dBm, connectable"),
    "scan: camera ********9A seen: on (00), **:**:**:**:**:01 RPA, -64 dBm, connectable");
  // the same line with the radio channel too
  assert.equal(mask("scan: camera 123456789A seen: on (00), 00:00:5e:00:53:01 RPA, -64 dBm, ch 39, connectable"),
    "scan: camera ********9A seen: on (00), **:**:**:**:**:01 RPA, -64 dBm, ch 39, connectable");
  assert.equal(mask("older: app register identity 4D454930-0100-1000-8001-A1B2C3D4E5F6 name T1000-E"),
    "older: app register identity 4D454930-0100-1000-8001-********E5F6 name T1000-E");
  assert.equal(mask("pair: card id 00112233-4455-4677-8899-a1b2c3d4e5f6, no camera paired"),
    "pair: card id ********-****-****-****-********e5f6, no camera paired");
  // 'diag scan' from 1.5.0: the bytes in their own line under the header, all of them in that line.
  const header = "diag scan: **:**:**:**:**:e2 RPA -48 dBm ch 38 adv connectable 13 B, camera";
  assert.equal(mask(header), header);
  assert.equal(mask("diag scan:   bytes: 02 01 06 09 ff 3a 00 03 12 34 56 78 9a"),
    "diag scan:   bytes: 02 01 06 09 ff 3a 00 03 ** ** ** ** 9a");
  const longest = "diag scan:   bytes: 1e ff 3a 00 06 12 34 56 78 9a" + " ab".repeat(21);
  assert.equal(mask(longest), longest.replace("06 12 34 56 78 9a", "06 ** ** ** ** 9a"));
  assert.equal(mask(header + "\n" + "diag scan:   bytes: 02 01 06 09 ff 3a 00 03 12 34 56 78 9a\n"),
    header + "\n" + "diag scan:   bytes: 02 01 06 09 ff 3a 00 03 ** ** ** ** 9a\n"); // as Copy shows the run
  // the older format: the bytes at the end of the header line
  assert.equal(mask("diag scan: **:**:**:**:**:e2 RPA -48 dBm ch 38 adv connectable 13 B, camera, bytes: " +
    "02 01 06 09 ff 3a 00 03 12 34 56 78 9a"),
  "diag scan: **:**:**:**:**:e2 RPA -48 dBm ch 38 adv connectable 13 B, camera, bytes: " +
    "02 01 06 09 ff 3a 00 03 ** ** ** ** 9a");
  assert.equal(mask("wifi: joined S5M2-0A1B2C"), "wifi: joined S5M2-******");
  for (const line of [ // left as they are
    "Card log - gpxcam 1.5.0-beta.5 (git 9804afb) - 2026-10-26T15:32:55.533Z",
    "[  12345.6 s] gpxcam-t1000e 1.5.0-beta.5 (git 9804afb), uptime 345 s, log dropped 126",
    "[      0.7 s] reset: 0x00010000 (OFF-GPIO)",
    "scan: by channel 37/38/39: 1266/1190/1183 reports, 316/310/316 from LUMIX cameras",
    "diag scan: by channel 37/38/39: 270/268/274 reports, 110/114/120 from LUMIX cameras",
    "diag scan: done, 2 device(s) shown, 812 reports received, 0 resume failed, 0 restarts",
    "memory: heap 185796 B free of 201976 B, loop stack 2416 B never used, main stack 688 B used of 2048 B",
  ]) {
    assert.equal(mask(line), line, line);
  }
});


test("commands sent at once are written one after the other, not refused", async () => {
  // A port whose writes take a while, as over USB: a second writer while one writes would throw.
  const written = [];
  const port = simulatedCard({});
  port.writable = new WritableStream({
    write: (chunk) => new Promise((resolve) => setTimeout(() => {
      written.push(new TextDecoder().decode(chunk));
      resolve();
    }, 10)),
  });
  const card = new page.Card(port);
  await card.open();
  await Promise.all([card.send("status"), card.send("pair older"), card.send("diag scan")]);
  assert.deepEqual(written, ["status\n", "pair older\n", "diag scan\n"]);
  await assert.rejects(card.send("dfu"), /not sent: dfu/); // refused before it waits
  await card.send("v"); // a refused command does not stop the writes after it
  assert.equal(written.at(-1), "v\n");
});

// ---- Older-camera pairing in the app (the wizard) ----

// The card's answer to 'pair older' up to the Wi-Fi step (src/older_session.cpp), made-up identity.
const OLDER_STARTED = [
  "link: off\r\npair: older started - waiting for GPS time (up to 120 s)\r\n",
  "older: introduced - waiting up to 300 s for the camera's 01 (the camera now waits for the Wi-Fi step)\r\n",
  "older: app register identity 4D454930-0100-1000-8001-A1B2C3D4E5F6 name T1000-E\r\n",
];
const OLDER_IDENTITY = "4D454930-0100-1000-8001-A1B2C3D4E5F6 T1000-E";
// The card's state while it waits for the Wi-Fi step.
const WAITING_WIFI = statusWith({ ...NOT_PAIRED, pair: "waiting_wifi" }, STATUS_GPS_HOLD);

// statusAfterStart: the card's state from 'pair older' on (by default it stays idle).
async function olderWizard(registerOutcome, statusAfterStart = null) {
  const answers = { v: VERSION_1_4, status: statusWith(NOT_PAIRED, STATUS_GPS_HOLD), "pair older": OLDER_STARTED,
    "pair off": PAIR_OFF_ANSWER, "pair prepare": PREPARE_ANSWER };
  const app = simulatedApp(["/dev/ttyACM0"], answers);
  app.registerOutcome = registerOutcome;
  const ui = openPage(null, { app });
  await ui.connect.fire("click");
  if (statusAfterStart) answers.status = statusAfterStart;
  await ui["pair-older"].fire("click");
  return { app, ui, answers };
}

test("in the app: Pair as an older camera registers the identity the card prints, over Wi-Fi", async () => {
  const { app, ui } = await olderWizard("accepted", (app) => (app.registered.length ? PAIRED_OLDER : WAITING_WIFI));
  assert.deepEqual(app.registered, [OLDER_IDENTITY]);
  await until(() => !ui["pair-screen-done"].hidden);
  assert.equal(app.calls.includes("send pair off"), false); // a paired card is left as it is
  assert.equal(ui["pair-stop-row"].hidden, true);
  await ui.disconnect.fire("click"); // stops the status refresh
});

test("in the app: a Wi-Fi step that fails says why, keeps the card waiting, and can be tried again", async () => {
  const { app, ui, answers } = await olderWizard("not-on-network", WAITING_WIFI);
  assert.deepEqual(shownScreen(ui), ["wifi"]);
  assert.deepEqual(shownSteps(ui), ["prepare:done", "camera:done", "connect:done", "wifi:current", "done:later"]);
  assert.equal(ui["wifi-app"].hidden, false);
  assert.match(ui["pair-status"].textContent, /^Could not reach the camera\. Join the camera's Wi-Fi network/);
  assert.equal(ui["pair-status"].className, "bad");
  assert.equal(app.calls.includes("send pair off"), false); // the card waits up to 300 s for the step
  assert.equal(ui["wifi-retry"].hidden, false);
  assert.equal(ui["pair-stop-row"].hidden, false);
  // Joined the network now: the second try registers the identity from the card's id.
  app.registerOutcome = "accepted";
  answers.p = CARD_ID_ANSWER;
  answers.status = PAIRED_OLDER;
  await ui["wifi-retry"].fire("click");
  assert.deepEqual(app.registered, [OLDER_IDENTITY, OLDER_IDENTITY]);
  assert.deepEqual(shownScreen(ui), ["done"]);
  assert.equal(ui["pair-stop-row"].hidden, true);
  assert.equal(app.calls.includes("send pair off"), false);
  await ui.disconnect.fire("click");
});

test("in the app: an identity the app refuses is explained, and the card keeps waiting", async () => {
  const answers = { v: VERSION_1_4, status: statusWith(NOT_PAIRED, STATUS_GPS_HOLD), "pair older": OLDER_STARTED };
  const app = simulatedApp(["/dev/ttyACM0"], answers);
  app.registerError = "bad-identity";
  const ui = openPage(null, { app });
  await ui.connect.fire("click");
  answers.status = WAITING_WIFI;
  await ui["pair-older"].fire("click");
  assert.deepEqual(app.registered, []); // refused before anything was sent
  assert.match(ui["pair-status"].textContent, /^The app cannot register the identity this card gave\./);
  assert.equal(ui["pair-status"].className, "bad");
  assert.equal(app.calls.includes("send pair off"), false);
  assert.equal(ui["wifi-retry"].hidden, false);
  await ui.disconnect.fire("click");
});

test("in the app: after a failed Wi-Fi step, Cancel cancels without a red message", async () => {
  const cancelledWhenAsked = (app) => (app.calls.includes("send pair off") ? PAIR_CANCELLED : WAITING_WIFI);
  const { app, ui } = await olderWizard("not-on-network", cancelledWhenAsked);
  assert.equal(ui["wifi-retry"].hidden, false);
  await ui["pair-stop"].fire("click");
  assert.equal(app.calls.includes("send pair off"), true);
  assert.deepEqual(shownScreen(ui), ["prepare"]);
  assert.equal(ui["pair-status"].textContent, "Pairing cancelled.");
  assert.equal(ui["pair-status"].className, "");
  assert.equal(ui["pair-start"].disabled, false);
  await ui.disconnect.fire("click");
});

// Pair again used to leave its last stage ("Connecting to the camera...") in a second section of its own.
test("in the app: Pair again shows Prepare, Keep the current camera goes back, and Pair runs to the end", async () => {
  const answers = { v: VERSION_1_4, status: STATUS_GPS_HOLD, pair: PAIR_STARTED };
  const { ui } = await connectedApp(answers);
  assert.deepEqual(shownScreen(ui), ["done"]);
  await ui["pair-again"].fire("click");
  assert.deepEqual(shownScreen(ui), ["prepare"]);
  assert.equal(ui["pair-keep"].hidden, false);
  assert.equal(ui["pair-wait-note"].textContent, "Don't start pairing on the camera yet – the app tells you when.");
  await ui["pair-keep"].fire("click");
  assert.deepEqual(shownScreen(ui), ["done"]);
  await ui["pair-again"].fire("click");
  answers.status = statusWith({ link: "off", pair: "connecting", pair_attempt: 1 }, STATUS_GPS_HOLD);
  await ui["pair-start"].fire("click"); // the page asks for the card's state at once
  assert.deepEqual(shownScreen(ui), ["connect"]);
  assert.equal(ui["pair-older"].disabled, true); // a pairing is under way
  answers.status = statusWith({ pair_result: "paired" }, STATUS_GPS_HOLD);
  await ui.refresh.fire("click");
  assert.deepEqual(shownScreen(ui), ["done"]);
  assert.equal(ui["pair-older"].disabled, false);
  await ui.disconnect.fire("click");
});

test("in the app: Pair as an older camera follows the card on the same screens, and Cancel stops it", async () => {
  const answers = { v: VERSION_1_4, status: statusWith(NOT_PAIRED, STATUS_GPS_HOLD),
    "pair older": ["link: off\r\npair: older started - waiting for GPS time (up to 120 s)\r\n"],
    "pair off": PAIR_OFF_ANSWER };
  const { app, ui } = await connectedApp(answers);
  const run = ui["pair-older"].fire("click");
  await until(() => app.calls.includes("send pair older"));
  assert.equal(ui["pair-stop-row"].hidden, false);
  assert.equal(ui["pair-start"].disabled, true);
  answers.status = statusWith({ ...NOT_PAIRED, pair: "searching" }, STATUS_GPS_HOLD);
  await ui.refresh.fire("click");
  assert.deepEqual(shownScreen(ui), ["camera"]);
  assert.deepEqual(shownSteps(ui), ["prepare:done", "camera:current", "connect:later", "wifi:later", "done:later"]);
  app.channel.onmessage({ kind: "line",
    text: "pair: camera 123456789a is on (06), not in pairing mode - choose pairing on the camera" });
  await until(() => ui["pair-status"].textContent === "A camera is nearby, but not in pairing mode yet.");
  answers.status = PAIR_CANCELLED;
  await ui["pair-stop"].fire("click");
  await run;
  assert.equal(app.calls.includes("send pair off"), true);
  assert.equal(ui["pair-status"].textContent, "Pairing cancelled.");
  assert.equal(ui["pair-stop-row"].hidden, true);
  await ui.disconnect.fire("click");
});

test("in the app: after a failed Wi-Fi step, a card that stops waiting is shown so", async () => {
  const { ui, answers } = await olderWizard("not-on-network", WAITING_WIFI);
  answers.status = statusWith(NOT_PAIRED, STATUS_GPS_HOLD); // its 300 s ran out
  await tick(page.PAIRING_STATUS_PERIOD_MS + 300); // the next refresh of the state
  assert.deepEqual(shownScreen(ui), ["failed"]);
  assert.equal(ui["failed-title"].textContent, "The Wi-Fi step was not done");
  assert.equal(ui["failed-text"].textContent, "The card no longer waits for the Wi-Fi step.");
  assert.equal(ui["pair-retry"].disabled, false);
  await ui.disconnect.fire("click");
});

test("in the app: a cancelled Wi-Fi step stops the card's pairing", async () => {
  const cancelledWhenAsked = (app) => (app.calls.includes("send pair off") ? PAIR_CANCELLED : WAITING_WIFI);
  const { app, ui } = await olderWizard("cancelled", cancelledWhenAsked);
  assert.equal(app.calls.includes("send pair off"), true);
  assert.deepEqual(shownScreen(ui), ["prepare"]);
  assert.equal(ui["pair-status"].textContent, "Pairing cancelled.");
  assert.equal(ui["pair-older"].disabled, false);
  await ui.disconnect.fire("click");
});

// The app opened again (or the cable came back) while the card still pairs: the page follows it.
// status: the card's state (a function of app, by default the given phase); p: its answer to 'p'.
async function olderReconnect(pairPhase, { status = null, p = CARD_ID_ANSWER, registerOutcome = "accepted" } = {}) {
  const answers = { v: VERSION_1_4, status: status || statusWith({ ...NOT_PAIRED, pair: pairPhase }, STATUS_GPS_HOLD),
    p, "pair off": PAIR_OFF_ANSWER };
  const app = simulatedApp(["/dev/ttyACM0"], answers);
  app.registerOutcome = registerOutcome;
  const ui = openPage(null, { app });
  await ui.connect.fire("click");
  return { app, ui, answers };
}

test("in the app: a card found waiting at the Wi-Fi step goes on by itself, with the identity from its id", async () => {
  const { app, ui } = await olderReconnect("waiting_wifi",
    { status: (app) => (app.registered.length ? PAIRED_OLDER : WAITING_WIFI) });
  await until(() => !ui["pair-screen-done"].hidden);
  assert.equal(app.calls.includes("send p"), true);
  assert.deepEqual(app.registered, [OLDER_IDENTITY]);
  assert.equal(app.calls.includes("send pair prepare"), false); // the card is pairing already
  await ui.disconnect.fire("click");
});

test("in the app: Cancel stops a Wi-Fi step that went on by itself, and the card's pairing", async () => {
  const { app, ui } = await olderReconnect("waiting_wifi", { registerOutcome: "wait",
    status: (app) => (app.calls.includes("send pair off") ? PAIR_CANCELLED : WAITING_WIFI) });
  await until(() => app.registered.length === 1);
  assert.deepEqual(shownScreen(ui), ["wifi"]);
  assert.equal(ui["pair-stop-row"].hidden, false);
  await ui["pair-stop"].fire("click");
  await until(() => app.calls.includes("send pair off"));
  assert.equal(app.calls.includes("cancel_older"), true);
  await until(() => ui["pair-status"].textContent === "Pairing cancelled.");
  assert.equal(ui["pair-stop-row"].hidden, true);
  assert.equal(ui["pair-older"].disabled, false);
  await ui.disconnect.fire("click");
});

test("in the app: a card still looking for the camera shows its countdown from 'p', with Cancel", async () => {
  const { app, ui } = await olderReconnect("searching",
    { p: [...CARD_ID_ANSWER, "pair: looking for a camera in pairing mode for 30 s, attempt 0/3\r\n"] });
  await until(() => ui["pair-countdown-time"].textContent === "1:30");
  assert.deepEqual(shownScreen(ui), ["camera"]);
  assert.equal(ui["pair-stop-row"].hidden, false);
  assert.equal(ui["pair-older"].disabled, true);
  assert.equal(app.calls.includes("send p"), true);
  assert.equal(app.registered.length, 0);
  await ui.disconnect.fire("click");
});

// ---- A plain Pair that finds an older camera: the app does the Wi-Fi step by itself ----

const PAIRED_OLDER = statusWith({ paired: 1, link: "off", pair_result: "paired" }, STATUS_GPS_HOLD);
const PAIR_CANCELLED = statusWith({ ...NOT_PAIRED, pair_result: "cancelled" }, STATUS_GPS_HOLD);

// The card as the firmware pairs it: Pair goes to the Wi-Fi step (an older camera); afterStep(app):
// the state once the Wi-Fi step has run or the pairing was cancelled.
async function pairFindsOlderCamera(registerOutcome, afterStep) {
  const answers = { v: VERSION_1_4, pair: PAIR_STARTED, p: CARD_ID_ANSWER, "pair off": PAIR_OFF_ANSWER,
    "pair prepare": PREPARE_ANSWER,
    status: (app) => afterStep(app) || (app.calls.includes("send pair") ? WAITING_WIFI
      : statusWith(NOT_PAIRED, STATUS_GPS_HOLD)) };
  const app = simulatedApp(["/dev/ttyACM0"], answers);
  app.registerOutcome = registerOutcome;
  const ui = openPage(null, { app });
  await ui.connect.fire("click");
  await ui["pair-start"].fire("click");
  await until(() => app.registered.length === 1);
  return { app, ui };
}

test("in the app: a Pair that finds an older camera goes on with the Wi-Fi step by itself", async () => {
  const { app, ui } = await pairFindsOlderCamera("accepted", (app) => app.registered.length && PAIRED_OLDER);
  assert.deepEqual(app.registered, [OLDER_IDENTITY]); // made from the card's id ('p')
  assert.equal(app.calls.includes("send p"), true);
  assert.equal(app.calls.includes("send pair older"), false);
  await until(() => !ui["pair-screen-done"].hidden); // the camera's answer, then the card's result
  assert.equal(ui["pair-stop-row"].hidden, true);
  assert.equal(app.calls.includes("send pair off"), false);
  await ui.disconnect.fire("click");
});

test("in the app: the Wi-Fi step a Pair started is shown on its screen, and Cancel stops it", async () => {
  const { app, ui } = await pairFindsOlderCamera("wait",
    (app) => app.calls.includes("send pair off") && PAIR_CANCELLED);
  await until(() => /^On the camera's network|^Registering/.test(ui["pair-status"].textContent));
  assert.deepEqual(shownScreen(ui), ["wifi"]);
  assert.equal(ui["wifi-retry"].hidden, true); // the step is running
  assert.equal(ui["pair-stop-row"].hidden, false);
  assert.equal(ui["pair-stop"].disabled, false);
  assert.equal(ui["pair-start"].disabled, true);
  await ui["pair-stop"].fire("click");
  await until(() => app.calls.includes("send pair off"));
  assert.equal(app.calls.includes("cancel_older"), true);
  await until(() => ui["pair-status"].textContent === "Pairing cancelled.");
  assert.equal(ui["pair-stop-row"].hidden, true);
  assert.equal(ui["pair-start"].disabled, false);
  await ui.disconnect.fire("click");
});

test("in the app: a failed Wi-Fi step after Pair can be tried again", async () => {
  const { app, ui } = await pairFindsOlderCamera("not-on-network",
    (app) => app.registered.length > 1 && PAIRED_OLDER);
  await until(() => !ui["wifi-retry"].hidden); // the step has ended
  assert.equal(ui["pair-status"].className, "bad");
  assert.match(ui["pair-status"].textContent, /^Could not reach the camera\. Join the camera's Wi-Fi network/);
  assert.equal(app.calls.includes("send pair off"), false);
  app.registerOutcome = "accepted";
  await ui["wifi-retry"].fire("click");
  assert.deepEqual(app.registered, [OLDER_IDENTITY, OLDER_IDENTITY]);
  assert.deepEqual(shownScreen(ui), ["done"]);
  await ui.disconnect.fire("click");
});

test("in a browser: a Pair that finds an older camera says that the Wi-Fi step needs the app", async () => {
  const answers = { v: VERSION_1_4, status: statusWith(NOT_PAIRED), pair: PAIR_STARTED };
  const { port, ui } = await connected(answers);
  answers.status = statusWith({ ...NOT_PAIRED, pair: "waiting_wifi" });
  await ui["pair-start"].fire("click");
  assert.deepEqual(shownScreen(ui), ["wifi"]);
  assert.equal(ui["wifi-browser"].hidden, false);
  assert.equal(ui["wifi-app"].hidden, true);
  assert.equal(ui["wifi-retry"].hidden, true);
  assert.equal(ui["pair-status"].textContent, "");
  assert.equal(ui["pair-stop-row"].hidden, false);
  assert.equal(port.written.includes("p\n"), false);
  await ui.disconnect.fire("click");
});

