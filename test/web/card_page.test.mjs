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
    " startList, parseStatus, cameraTile, gpsTile, batteryTile, zoneTile, classifyClockAnswer, clockAnswerText," +
    " clockNoteText, pairingText, pairResultText, classifyUnpairAnswer, unpairAnswerText, SOUND_SETTINGS," +
    " classifySoundAnswer, soundAnswerText, soundNoteText, soundText })",
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
  "status: pair=idle pair_attempt=0 pair_result=none\r\nstatus: clock=on tz=Europe/Warsaw\r\n",
  "status: sound=loud\r\nsta",
  "tus: end\r\n",
];
const STATE = {
  version: "1.3.0", git: "1234567", batteryMv: 4100, batteryPercent: 89, charger: "charging", gpsOn: true,
  gpsFix: true, gpsSatellites: 11, paired: true, link: "connected", pair: "idle", pairAttempt: 0,
  pairResult: "none", clockOn: true, timeZone: "Europe/Warsaw", sound: "loud",
};
const NO_STATUS = ["command: unknown 'status'\r\n", "commands: v = version and reset reason,\r\n"];
const statusLines = (text) => text.trim().split("\n");
const STATUS_CLOCK_OFF = STATUS_ANSWER.map((piece) => piece.replace("clock=on", "clock=off"));
const CLOCK_OFF_SAVED = [
  "clock: off saved - from the next connection\r\n",
  "clock: off - the card leaves the camera's clock alone; set it in the camera's menu\r\n",
];
const CLOCK_NOT_SAVED = ["clock: could not write the time zone file\r\n"];
// STATUS_ANSWER with other values, e.g. statusWith({ paired: 0, link: "off" }).
const statusWith = (changes) => [Object.entries(changes).reduce(
  (text, [key, value]) => text.replace(new RegExp(` ${key}=\\S+`), ` ${key}=${value}`), STATUS_ANSWER.join(""))];
const NOT_PAIRED = { paired: 0, link: "off" };
const PAIR_STARTED = ["link: off\r\n", "pair: started - waiting for GPS time (up to 120 s)\r\n"];
const FORGOTTEN = ["link: off\r\n", "pair: camera forgotten (the card id stays)\r\n"];
const SOUND_OFF_SAVED = [
  "sound: off saved\r\n",
  "sound: off - the card only answers a held button (switching on and off, pairing), quietly\r\n",
];

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
      toggle: (name, on) => (on ? this.classes.add(name) : this.classes.delete(name)),
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
// browser whose port chooser gives the simulated card (serial: false - a browser without Web Serial).
function openPage(port, { serial = true } = {}) {
  const elements = {};
  for (const [tag] of html.matchAll(/<[a-z]+\b[^>]*>/g)) {
    const id = / id="([^"]+)"/.exec(tag);
    if (id) elements[id[1]] = new FakeElement((/ class="([^"]*)"/.exec(tag) || [])[1]);
  }
  const document = {
    getElementById: (id) => elements[id],
    createElement: () => new FakeElement(),
  };
  const navigator = serial ? { serial: { requestPort: async () => port, addEventListener() {} } } : {};
  vm.runInContext(scripts.join("\n"), vm.createContext({
    TextEncoder, TextDecoder, setTimeout, clearTimeout, console, document, navigator,
    window: { isSecureContext: true },
  }));
  return elements;
}

// Chooses a zone in the list as a click does; returns its name element.
async function chooseZone(ui, zone) {
  const item = ui.results.children.find((li) => li.children[0].children[0].value === zone);
  const [radio, text] = item.children[0].children;
  radio.checked = true;
  await radio.fire("change");
  return text.children[0];
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

test("only v, status, pair, pair off, unpair, clock on/off, sound loud/quiet/off, tz and tz NAME can be sent", () => {
  for (const allowed of ["v", "status", "pair", "pair off", "unpair", "clock on", "clock off", "sound loud",
    "sound quiet", "sound off", "tz", "tz Europe/Warsaw", "tz Poland", "tz Etc/GMT+5"]) {
    assert.equal(page.allowedCommand(allowed), true, allowed);
  }
  const refused = ["dfu", "off", "link off", "link on", "log", "bat log clear", "status ", "clock",
    "clock  on", "Clock off", "sound", "sound on", "sound  off", "Sound loud", "diag snd 10", "pair on", "Pair",
    "unpair now", "tz europe/warsaw",
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
  const card = /<section class="panel section">\s*<h2><svg class="icon"[^>]*>[\s\S]*?<\/svg>([^<]+)<\/h2>/g;
  const cards = [...html.matchAll(card)];
  assert.deepEqual(cards.map((card) => card[1]), ["The card", "The camera", "The camera's clock", "Sound"]);
  assert.match(html, /<link rel="icon" href="data:image\/svg\+xml,/);
});

test("hidden parts stay hidden whatever display their own rule gives", () => {
  assert.match(html, /^\.hidden \{ display: none !important; \}/m);
});

test("the page fetches nothing: content security policy", () => {
  const policy = /<meta http-equiv="Content-Security-Policy" content="([^"]*)">/.exec(html);
  assert.ok(policy);
  assert.match(policy[1], /^default-src 'none';/);
  assert.doesNotMatch(policy[1], /connect-src|https?:/);
  assert.match(policy[1], /img-src data:/); // the favicon is a data: image
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

test("the state is shown as tiles, each state in a word, not only by the colour of its dot", () => {
  const state = (changes) => ({ ...STATE, ...changes });
  const tile = (make, changes = {}) => json(make(state(changes)));
  assert.deepEqual(tile(page.cameraTile), ["good", "Connected", "Sends the position"]);
  assert.deepEqual(tile(page.cameraTile, { paired: false, link: "off" }), ["idle", "Not paired", "Pair it below"]);
  assert.deepEqual(tile(page.cameraTile, { link: "searching" }),
    ["idle", "Looking for it", "Camera off or out of range"]);
  assert.deepEqual(tile(page.cameraTile, { link: "searching", gpsFix: false }),
    ["warn", "Waiting for GPS", "Connects once the GPS has a fix"]);
  assert.deepEqual(tile(page.cameraTile, { link: "stopped" }), ["bad", "Refused the card", "Pair them again"]);
  assert.deepEqual(tile(page.cameraTile, { link: "off", pair: "searching" }),
    ["warn", "Pairing", "See The camera below"]);
  assert.deepEqual(tile(page.gpsTile), ["good", "Fix", "11 satellites"]);
  assert.deepEqual(tile(page.gpsTile, { gpsOn: false }), ["idle", "Off", "Saves power"]);
  assert.deepEqual(tile(page.gpsTile, { gpsFix: false, gpsSatellites: 1 }),
    ["warn", "No fix yet", "1 satellite; outdoors 1\u20133 min"]);
  assert.deepEqual(tile(page.batteryTile), ["good", "Charging", "about 89 % \u00b7 4.10 V"]);
  assert.deepEqual(tile(page.batteryTile, { charger: "charged", batteryMv: 4180 }), ["good", "Charged", "4.18 V"]);
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
  assert.equal(ui["card-status"].textContent, "Connected to the card");
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
  const port = simulatedCard({ v: VERSION_1_3, status: STATUS_ANSWER }); // the card has Europe/Warsaw
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
  const port = simulatedCard(answers);
  const ui = openPage(port);
  await ui.connect.fire("click");
  await chooseZone(ui, "America/New_York");
  const statusAfterSave = () => new Promise((resolve) => setTimeout(resolve, 100)); // the page asks for its state
  await ui.save.fire("click");
  assert.ok(port.written.includes("tz America/New_York\n"));
  assert.equal(ui["save-status"].textContent, "Saved: America/New_York.");
  assert.equal(ui["save-note"].hidden, false);
  await statusAfterSave();
  answers["tz America/New_York"] = ["tz: could not write the time zone file\r\n"];
  await ui.save.fire("click");
  assert.equal(ui["save-status"].className, "bad");
  assert.equal(ui["save-note"].hidden, true);
  await statusAfterSave();
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

test("the answers to 'clock on' and 'clock off' are told apart and explained", async () => {
  const { port, card } = await openCard({ "clock off": CLOCK_OFF_SAVED });
  const lines = await card.clock(false);
  assert.deepEqual(port.written, ["clock off\n"]);
  assert.equal(page.classifyClockAnswer(lines), "saved");
  const classify = (lines) => page.classifyClockAnswer(lines);
  assert.equal(classify([]), "none");
  assert.equal(classify(["clock: on saved - from the next connection"]), "saved");
  assert.equal(classify(["clock: already on", "clock: on - the card sets the camera's clock"]), "unchanged");
  assert.equal(classify(["clock: could not write the time zone file"]), "not-saved");
  assert.equal(classify(["command: unknown 'clock off'"]), "no-clock");
  assert.match(page.clockAnswerText(true, "saved")[0], /switch the camera off and on\.$/);
  assert.match(page.clockAnswerText(false, "saved")[0], /leaves the camera's clock alone\.$/);
  assert.equal(page.clockAnswerText(false, "not-saved")[1], "bad");
  assert.equal(page.clockAnswerText(false, "none")[1], "bad");
  assert.match(page.clockNoteText(true), /^Each time it connects, the card sets the camera's clock/);
  assert.match(page.clockNoteText(false), /set the time and the time zone in the camera's menu\. Pairing still/);
  assert.match(page.clockNoteText(true, "1.2.0"), /^gpxcam 1\.2\.0 always sets the camera's clock\./);
  assert.equal(page.zoneTile({ ...STATE, clockOn: false })[2], "Clock off: set it on the camera");
});

test("the clock switch follows the card and turns the time zone choice off with it", async () => {
  const answers = { v: VERSION_1_3, status: STATUS_ANSWER };
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
  assert.equal(ui["clock-answer"].textContent, "Saved. From the next connection the card leaves the camera's clock alone.");
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
  const port = simulatedCard({ v: VERSION_ANSWER, status: NO_STATUS, tz: TZ_STATE });
  const ui = openPage(port);
  await ui.connect.fire("click");
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
  assert.deepEqual(phases, ["idle", "waiting_gps", "searching", "connecting", "pausing"]);
  for (const phase of phases.slice(1)) {
    assert.notEqual(page.pairingText({ ...STATE, pair: phase }), "Pairing...", phase);
  }
  const results = firmwareValues("pairResultValue");
  assert.equal(results.length, 9);
  for (const result of results.slice(1)) assert.doesNotMatch(page.pairResultText(result)[0], /^Pairing ended/, result);
  assert.equal(page.pairResultText("none"), null);
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
  assert.match(html, /<fieldset id="sound" class="segmented">/);
  assert.match(html, /<input id="clock" type="checkbox" class="toggle"/);
});

test("the answers to 'sound loud|quiet|off' are told apart and explained", async () => {
  const { port, card } = await openCard({ "sound off": SOUND_OFF_SAVED });
  const lines = await card.sound("off");
  assert.deepEqual(port.written, ["sound off\n"]);
  assert.equal(page.classifySoundAnswer(lines), "saved");
  const classify = (lines) => page.classifySoundAnswer(lines);
  assert.equal(classify([]), "none");
  assert.equal(classify(["sound: quiet saved"]), "saved");
  assert.equal(classify(["sound: already loud"]), "unchanged");
  assert.equal(classify(["sound: could not write the settings file"]), "not-saved");
  assert.equal(classify(["command: unknown 'sound off'"]), "no-sound");
  assert.deepEqual(json(page.soundAnswerText("saved")), ["Saved.", "good"]);
  assert.equal(page.soundAnswerText("not-saved")[1], "bad");
  assert.equal(page.soundAnswerText("none")[1], "bad");
  assert.equal(page.soundAnswerText("no-sound")[1], "bad");
  assert.match(page.soundNoteText(), /^Choosing Loud or Quiet plays two short beeps/);
  assert.match(page.soundNoteText("1.2.0"), /^gpxcam 1\.2\.0 always plays its sounds loud\./);
  assert.equal(page.soundText("loud"), "All sounds.");
  assert.equal(page.soundText("quiet"), "All sounds, quieter.");
  assert.match(page.soundText("off"), /^Only quiet beeps when you hold the button/);
  assert.equal(page.soundText(null), "How loud the card is.");
});

test("the sound choice follows the card", async () => {
  const answers = { v: VERSION_1_3, status: STATUS_ANSWER };
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

test("with gpxcam 1.2 the sound stays loud and cannot be changed", async () => {
  const port = simulatedCard({ v: VERSION_ANSWER, status: NO_STATUS, tz: TZ_STATE });
  const ui = openPage(port);
  await ui.connect.fire("click");
  assert.equal(ui["sound-loud"].checked, true);
  assert.equal(ui["sound-says"].textContent, "All sounds."); // 1.2 plays them loud
  assert.equal(ui["sound-loud"].disabled, true);
  assert.equal(ui["sound-off"].disabled, true);
  assert.match(ui["sound-note"].textContent, /^gpxcam 1\.2\.0 always plays its sounds loud\. Choosing needs/);
  await ui.disconnect.fire("click");
});

test("the pairing stages and results are explained", () => {
  const pairing = (pair, pairAttempt, gpsSatellites = 3) => page.pairingText({ ...STATE, pair, pairAttempt, gpsSatellites });
  assert.equal(pairing("waiting_gps", 0), "Waiting for GPS time, up to 2 minutes (3 satellites so far)...");
  assert.equal(pairing("searching", 0), "Looking for a camera in pairing mode, up to 1 minute...");
  assert.equal(pairing("searching", 1), "Looking for the camera again (attempt 2 of 3)...");
  assert.equal(pairing("connecting", 2), "Talking to the camera (attempt 2 of 3)...");
  assert.equal(pairing("pausing", 1), "Attempt 1 of 3 did not work; trying again in a moment...");
  assert.deepEqual(json(page.pairResultText("paired")),
    ["Paired. From now on the card connects to the camera by itself.", "good"]);
  assert.match(page.pairResultText("no_gps_fix")[0], /^No GPS time within 2 minutes\. Put the card outdoors/);
  assert.match(page.pairResultText("not_in_pairing_mode")[0], /^The card saw a LUMIX camera, but not in pairing mode/);
  assert.equal(page.pairResultText("failed")[1], "bad");
  assert.match(page.pairResultText("failed")[0], /If the camera lists T1000-E from an earlier pairing, delete it/);
  assert.deepEqual(json(page.pairResultText("cancelled")), ["Pairing cancelled.", ""]);
  assert.equal(page.classifyUnpairAnswer(["link: off", "pair: camera forgotten (the card id stays)"]), "forgotten");
  assert.equal(page.classifyUnpairAnswer(["pair: no camera paired"]), "not-paired");
  assert.equal(page.classifyUnpairAnswer(["pair: could not write the pairing file"]), "failed");
  assert.equal(page.classifyUnpairAnswer(["pair: not available"]), "failed");
  assert.equal(page.classifyUnpairAnswer([]), "none");
  assert.equal(page.unpairAnswerText("failed")[1], "bad");
});

test("pairing from the page: the stages, the result, then forgetting the camera", async () => {
  const answers = { v: VERSION_1_3, status: statusWith(NOT_PAIRED) };
  const port = simulatedCard(answers);
  const ui = openPage(port);
  assert.equal(ui.pair.disabled, true); // not connected
  await ui.connect.fire("click");
  assert.equal(ui["camera-note"].textContent, "The card is not paired with a camera yet.");
  assert.equal(ui.pair.textContent, "Pair");
  assert.equal(ui.pair.disabled, false);
  assert.equal(ui.unpair.hidden, true);
  assert.equal(ui["pair-cancel"].hidden, true);

  answers.pair = PAIR_STARTED;
  answers.status = statusWith({ ...NOT_PAIRED, pair: "waiting_gps", gps_fix: 0, gps_sat: 3 });
  await ui.pair.fire("click");
  assert.deepEqual(port.written.slice(-2), ["pair\n", "status\n"]);
  assert.equal(ui["pair-progress"].textContent, "Waiting for GPS time, up to 2 minutes (3 satellites so far)...");
  assert.equal(ui.pair.disabled, true);
  assert.equal(ui["pair-cancel"].hidden, false);
  assert.equal(ui["state-camera"].textContent, "Pairing");

  answers.status = statusWith({ ...NOT_PAIRED, pair: "connecting", pair_attempt: 1 });
  await ui.refresh.fire("click");
  assert.equal(ui["pair-progress"].textContent, "Talking to the camera (attempt 1 of 3)...");

  answers.status = statusWith({ link: "searching", pair_result: "paired" });
  await ui.refresh.fire("click");
  assert.equal(ui["pair-progress"].textContent, "Paired. From now on the card connects to the camera by itself.");
  assert.equal(ui["pair-progress"].className, "good");
  assert.equal(ui["pair-cancel"].hidden, true);
  assert.equal(ui.pair.textContent, "Pair again");
  assert.equal(ui.pair.className, "secondary");
  assert.equal(ui["camera-note"].textContent, "The card is paired with a camera. Pairing again replaces it " +
    "(with the same camera: delete T1000-E in the camera's Bluetooth menu first).");
  await ui.refresh.fire("click");
  assert.equal(ui["pair-progress"].className, "good"); // the result stays

  assert.equal(ui.unpair.hidden, false);
  assert.equal(ui.unpair.className, "danger"); // forgetting cannot be undone: red text, not a red button
  await ui.unpair.fire("click");
  assert.equal(ui["unpair-confirm"].hidden, false);
  await ui["unpair-no"].fire("click");
  assert.equal(ui["unpair-confirm"].hidden, true);
  assert.equal(port.written.includes("unpair\n"), false);

  answers.unpair = FORGOTTEN;
  answers.status = statusWith({ ...NOT_PAIRED, pair_result: "paired" }); // the result stays until a new pairing
  await ui.unpair.fire("click");
  await ui["unpair-yes"].fire("click");
  assert.deepEqual(port.written.slice(-2), ["unpair\n", "status\n"]);
  assert.equal(ui["pair-progress"].textContent, "The camera is forgotten.");
  assert.equal(ui["unpair-confirm"].hidden, true);
  assert.equal(ui.unpair.hidden, true);
  assert.equal(ui.pair.textContent, "Pair");
  await ui.disconnect.fire("click");
  assert.equal(ui["pair-progress"].textContent, "");
  assert.equal(ui["camera-note"].textContent, "Connect the card to pair it with your camera.");
});

test("a cancelled pairing, one refused at once and one started with the card's button", async () => {
  const answers = { v: VERSION_1_3, status: statusWith(NOT_PAIRED), pair: PAIR_STARTED };
  const port = simulatedCard(answers);
  const ui = openPage(port);
  await ui.connect.fire("click");

  answers.status = statusWith({ ...NOT_PAIRED, pair: "searching" });
  await ui.pair.fire("click");
  answers["pair off"] = ["pair: cancelled\r\n"];
  answers.status = statusWith({ ...NOT_PAIRED, pair_result: "cancelled" });
  await ui["pair-cancel"].fire("click");
  assert.deepEqual(port.written.slice(-2), ["pair off\n", "status\n"]);
  assert.equal(ui["pair-progress"].textContent, "Pairing cancelled.");

  answers.pair = ["pair: not possible - BLE or the pairing file is not available\r\n"];
  answers.status = statusWith({ ...NOT_PAIRED, pair_result: "not_possible" });
  await ui.pair.fire("click");
  assert.match(ui["pair-progress"].textContent, /^The card cannot pair now/);
  assert.equal(ui["pair-progress"].className, "bad");

  answers.status = statusWith({ ...NOT_PAIRED, pair: "searching" }); // click + hold on the card
  await ui.refresh.fire("click");
  assert.match(ui["pair-progress"].textContent, /^Looking for a camera in pairing mode/);
  answers.status = statusWith({ ...NOT_PAIRED, pair_result: "not_in_pairing_mode" });
  await ui.refresh.fire("click");
  assert.match(ui["pair-progress"].textContent, /^The card saw a LUMIX camera, but not in pairing mode/);
  await ui.disconnect.fire("click");
});

test("with gpxcam 1.2 pairing is left to the card's button", async () => {
  const port = simulatedCard({ v: VERSION_ANSWER, status: NO_STATUS, tz: TZ_STATE });
  const ui = openPage(port);
  await ui.connect.fire("click");
  assert.equal(ui.pair.disabled, true);
  assert.equal(ui.unpair.hidden, true);
  for (const id of ["camera-note", "pair-steps", "pair-row"]) assert.equal(ui[id].hidden, true, id);
  assert.equal(ui["camera-old"].hidden, false); // pair with the button, with a link to the README
  const readme = '<a href="https://github.com/TomaszBojanowski/gpxcam-t1000e#pairing-once">the README</a>';
  assert.ok(html.includes(readme));
  await ui.disconnect.fire("click");
  for (const id of ["camera-note", "pair-steps", "pair-row"]) assert.equal(ui[id].hidden, false, id);
  assert.equal(ui["camera-old"].hidden, true);
});
