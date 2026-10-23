// Tests of the card page, docs/index.html, on the PC:
// node --test test/web/card_page.test.mjs
// The page's scripts run in a separate context without a browser; the card is simulated with
// answers in the form of the real card's console (src/main.cpp, src/tz_store.cpp), made-up values.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import vm from "node:vm";

const PAGE = new URL("../../docs/index.html", import.meta.url);
const html = readFileSync(PAGE, "utf8");
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((match) => match[1]);
const appCode = scripts[1];
const page = vm.runInContext(
  scripts.join("\n") +
    "\n;({ TZ_DATA, BAUD_RATE, CARD_USB, MAX_RESULTS, findZone, searchZones, utcOffsetText, allowedCommand," +
    " tzCommand, parseVersion, classifyTzAnswer, LineSplitter, Card, preferredZone, noAnswerText, tzProblemText," +
    " startList, parseStatus, batteryText, gpsText, cameraText, zoneText })",
  vm.createContext({ TextEncoder, TextDecoder, setTimeout, clearTimeout, console }),
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
  "status: pair=idle pair_attempt=0 pair_result=none\r\nstatus: clock=on tz=Europe/Warsaw\r\nsta",
  "tus: end\r\n",
];
const STATE = {
  version: "1.3.0", git: "1234567", batteryMv: 4100, batteryPercent: 89, charger: "charging", gpsOn: true,
  gpsFix: true, gpsSatellites: 11, paired: true, link: "connected", pair: "idle", pairAttempt: 0,
  pairResult: "none", clockOn: true, timeZone: "Europe/Warsaw",
};
const NO_STATUS = ["command: unknown 'status'\r\n", "commands: v = version and reset reason,\r\n"];
const statusLines = (text) => text.trim().split("\n");

// A serial port that answers commands with chunks of text; it records what the page does.
// readFails: reading fails at once, as in Chromium on Linux after a pyserial program.
function simulatedCard(answers, readFails = false) {
  const encoder = new TextEncoder();
  let output;
  const port = {
    openedWith: null,
    signals: null,
    closed: false,
    written: [],
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
          setTimeout(() => output.enqueue(encoder.encode(piece)), 5);
        }
      },
    }),
    async open(options) { port.openedWith = options; },
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
    };
    this.textContent = "";
    this.disabled = false;
    this.value = "";
    this.checked = false;
    this.children = [];
    this.listeners = {};
  }
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
// browser whose port chooser gives the simulated card.
function openPage(port) {
  const elements = {};
  for (const [tag] of html.matchAll(/<[a-z]+\b[^>]*>/g)) {
    const id = / id="([^"]+)"/.exec(tag);
    if (id) elements[id[1]] = new FakeElement((/ class="([^"]*)"/.exec(tag) || [])[1]);
  }
  const document = {
    getElementById: (id) => elements[id],
    createElement: () => new FakeElement(),
  };
  const navigator = { serial: { requestPort: async () => port, addEventListener() {} } };
  vm.runInContext(scripts.join("\n"), vm.createContext({
    TextEncoder, TextDecoder, setTimeout, clearTimeout, console, document, navigator,
    window: { isSecureContext: true },
  }));
  return elements;
}

async function openCard(answers, readFails = false) {
  const port = simulatedCard(answers, readFails);
  const card = new page.Card(port);
  await card.open();
  return { port, card };
}

test("the data holds the card's zones, aliases and countries", () => {
  assert.equal(page.TZ_DATA.version, "2026e");
  assert.equal(page.TZ_DATA.zones.length, 344);
  assert.equal(page.TZ_DATA.aliases["Europe/Copenhagen"], "Europe/Berlin");
  assert.deepEqual(json(page.TZ_DATA.countries.DK), ["Denmark", [["Europe/Copenhagen", ""]]]);
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
  assert.equal(warsaw[0].detail, "Poland - your computer");
  assert.equal(warsaw.length, page.TZ_DATA.zones.length); // Warsaw is not listed twice
  assert.equal(new Set(warsaw.map((result) => result.name)).size, warsaw.length);
  const copenhagen = page.startList(page.preferredZone("Europe/Copenhagen"));
  assert.equal(copenhagen[0].detail, "Denmark (same as Europe/Berlin) - your computer");
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

test("only v, status, tz and tz with a name from the table can be sent", () => {
  for (const allowed of ["v", "status", "tz", "tz Europe/Warsaw", "tz Poland", "tz Etc/GMT+5"]) {
    assert.equal(page.allowedCommand(allowed), true, allowed);
  }
  const refused = ["dfu", "off", "pair", "unpair", "link off", "log", "bat log clear", "status ", "tz europe/warsaw",
    "tz Mars/Olympus_Mons", "tz <+0545>-5:45", "tz Europe/Warsaw\ndfu", "tz Europe/Warsaw ", "V", ""];
  for (const command of refused) assert.equal(page.allowedCommand(command), false, command);
  for (const [name] of page.TZ_DATA.zones) assert.equal(page.allowedCommand(`tz ${name}`), true, name);
  for (const alias of Object.keys(page.TZ_DATA.aliases)) assert.equal(page.allowedCommand(`tz ${alias}`), true, alias);
  assert.equal(page.tzCommand("america/new_york"), "tz America/New_York");
  assert.equal(page.tzCommand("Mars/Olympus_Mons"), null);
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

test("the card's other answers are told apart", () => {
  const classify = (lines) => page.classifyTzAnswer(lines);
  assert.equal(classify([]), "none");
  assert.equal(classify(["tz: already set", "tz: Europe/Warsaw, rule CET-1CEST,M3.5.0,M10.5.0/3 (tz database 2026e)"]), "unchanged");
  assert.equal(classify(["tz: unknown time zone or rule 'Mars/Olympus_Mons'"]), "unknown");
  assert.equal(classify(["tz: could not write the time zone file"]), "not-saved");
  assert.equal(classify(["command: unknown 'tz'"]), "no-tz");
});

test("firmware without tz is recognised", async () => {
  const { card } = await openCard({ tz: OLD_FIRMWARE });
  assert.equal(page.classifyTzAnswer(await card.timeZone()), "no-tz");
});

test("a card without tz and a card that does not answer tz get different messages", () => {
  assert.match(page.tzProblemText("1.1.0", "no-tz"), /^gpxcam 1\.1\.0 cannot be set up from this page/);
  assert.match(page.tzProblemText("1.2.0", "none"), /^No answer from the card to 'tz'/);
  assert.equal(page.tzProblemText("1.2.0", "state"), null);
});

test("the page fetches nothing: content security policy", () => {
  const policy = /<meta http-equiv="Content-Security-Policy" content="([^"]*)">/.exec(html);
  assert.ok(policy);
  assert.match(policy[1], /^default-src 'none';/);
  assert.doesNotMatch(policy[1], /connect-src|https?:/);
  assert.doesNotMatch(html, /<script[^>]+src=|<link[^>]+href="(?!data:)/);
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
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.equal(card.readError.message, "The device has been lost.");
  assert.equal(card.reader, null); // reading has ended
  assert.match(page.noAnswerText(card.readError), /^Reading from the card failed \(The device has been lost\.\)/);
  assert.match(page.noAnswerText(card.readError), /unplug the card, plug it in again/);
  assert.match(page.noAnswerText(null), /^No answer from the card/);
});

test("closing ends reading and closes the port", async () => {
  const { port, card } = await openCard({});
  let closed = false;
  card.onClosed = () => { closed = true; };
  await card.close();
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.equal(port.closed, true);
  assert.equal(closed, true);
});

test("the old address of the time zone page leads here and fetches nothing", () => {
  const old = readFileSync(new URL("../../docs/timezone/index.html", import.meta.url), "utf8");
  assert.match(old, /<meta http-equiv="refresh" content="0; url=\.\.\/">/);
  assert.match(old, /<meta http-equiv="Content-Security-Policy" content="default-src 'none'">/);
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
  const lines = statusLines(STATUS_ANSWER.join("").replace(/gps: .*\r\n/, "").replaceAll("\r", ""));
  assert.equal(page.parseStatus(lines).kind, "ok");
  assert.equal(page.parseStatus(lines.filter((line) => !line.includes("battery"))).kind, "none");
  assert.equal(page.parseStatus(lines.slice(0, -1)).kind, "none"); // no "status: end"
  assert.equal(page.parseStatus([]).kind, "none");
  assert.equal(page.parseStatus(["status: format=2 fw=2.0.0", "status: end"]).kind, "newer");
  // A key added later is skipped.
  const more = [...lines.slice(0, -1), "status: storage=ok", "status: end"];
  assert.deepEqual(json(page.parseStatus(more).state), STATE);
});

test("the state is shown in words", () => {
  const state = (changes) => ({ ...STATE, ...changes });
  assert.equal(page.batteryText(state({ charger: "no_usb" })), "89 % (4.10 V)");
  assert.equal(page.batteryText(STATE), "charging, about 89 % (4.10 V)");
  assert.equal(page.batteryText(state({ charger: "charged", batteryMv: 4180 })), "charged (4.18 V)");
  assert.equal(page.batteryText(state({ charger: "not_charging" })), "on USB, not charging, about 89 % (4.10 V)");
  assert.equal(page.gpsText(STATE), "fix, 11 satellites");
  assert.equal(page.gpsText(state({ gpsOn: false })), "off, to save power");
  assert.match(page.gpsText(state({ gpsFix: false, gpsSatellites: 1 })), /^no fix yet \(1 satellite\); outdoors/);
  assert.equal(page.cameraText(STATE), "paired, connected");
  assert.equal(page.cameraText(state({ paired: false, link: "off" })), "not paired");
  assert.equal(page.cameraText(state({ link: "searching", gpsFix: false })),
    "paired, looking for the camera; it connects once the GPS has a fix");
  assert.equal(page.cameraText(state({ link: "searching" })), "paired, looking for the camera");
  assert.match(page.cameraText(state({ link: "stopped" })), /refused the card: pair them again$/);
  assert.equal(page.cameraText(state({ link: "off", pair: "searching" })), "pairing");
  assert.match(page.zoneText(state({ timeZone: "Asia/Kolkata" })), /^Asia\/Kolkata, now UTC\+05:30$/);
  assert.equal(page.zoneText(state({ timeZone: "<+0545>-5:45" })), "<+0545>-5:45"); // a rule: no offset
});

test("commands go one at a time: the next after the answer to the one before", async () => {
  const { port, card } = await openCard({ v: VERSION_ANSWER, status: STATUS_ANSWER });
  const version = card.version();
  const status = card.status();
  await new Promise((resolve) => setTimeout(resolve, 1));
  assert.deepEqual(port.written, ["v\n"]);
  assert.equal(await version, "1.2.0");
  assert.equal((await status).kind, "ok");
  assert.deepEqual(port.written, ["v\n", "status\n"]);
  await assert.rejects(card.ask("dfu", () => true, 50));
  assert.equal((await card.status()).kind, "ok"); // a refused command does not stop the queue
});

test("the page shows the state of a card with 'status'", async () => {
  const port = simulatedCard({ v: VERSION_1_3, status: STATUS_ANSWER });
  const ui = openPage(port);
  assert.equal(ui.connect.disabled, false);
  await ui.connect.fire("click");
  assert.deepEqual(port.written, ["v\n", "status\n"]);
  assert.equal(ui["card-status"].textContent, "Connected.");
  assert.equal(ui["card-status"].className, "good");
  assert.equal(ui.state.hidden, false);
  assert.equal(ui["state-firmware"].textContent, "gpxcam 1.3.0 (git 1234567)");
  assert.equal(ui["state-camera"].textContent, "paired, connected");
  assert.match(ui["state-zone"].textContent, /^Europe\/Warsaw, now UTC\+0[12]:00$/);
  assert.equal(ui["refresh-row"].hidden, false);
  assert.equal(ui["card-note"].hidden, true);
  await ui.refresh.fire("click");
  assert.deepEqual(port.written, ["v\n", "status\n", "status\n"]);
  await ui.disconnect.fire("click");
  assert.equal(port.closed, true);
  assert.equal(ui.state.hidden, true);
  assert.equal(ui["card-status"].textContent, "Not connected.");
});

test("the page sets the time zone of a card with gpxcam 1.2 and says what 1.3.0 adds", async () => {
  const port = simulatedCard({ v: VERSION_ANSWER, status: NO_STATUS, tz: TZ_STATE });
  const ui = openPage(port);
  await ui.connect.fire("click");
  assert.deepEqual(port.written, ["v\n", "status\n", "tz\n"]);
  assert.equal(ui["card-status"].textContent, "Connected: gpxcam 1.2.0. Its time zone now:");
  assert.match(ui["card-lines"].textContent, /^tz: Europe\/Warsaw/);
  assert.equal(ui.state.hidden, true);
  assert.equal(ui["refresh-row"].hidden, true);
  assert.match(ui["card-note"].textContent, /^gpxcam 1\.2\.0 can only set the time zone from this page/);
  assert.equal(ui.save.disabled, false); // the computer's zone, or nothing, is selected
  await ui.disconnect.fire("click");
});

test("a card that does not answer 'status' is closed with a message", async () => {
  const port = simulatedCard({ v: VERSION_1_3 });
  const ui = openPage(port);
  await ui.connect.fire("click");
  assert.equal(port.closed, true);
  assert.match(ui["card-status"].textContent, /^No answer from the card to 'status'/);
  assert.equal(ui["card-status"].className, "bad");
  assert.equal(ui.connect.disabled, false);
});
