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
    " classifySoundAnswer, soundAnswerText, soundNoteText, soundText, UPDATES_SETTINGS, classifyUpdatesAnswer," +
    " updatesAnswerText, updatesNoteText, updatesText, updatesNowText, storageNoteText, chooseTransport," +
    " openProblemText, parseOlderRegister, olderOutcomeText, olderStartProgress, FIXED_COMMANDS, isLogLine," +
    " belongsToScan, scanEnded, scanResultText, diagnosticsText, SCAN_RUN_MS })",
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
  storage: null,
};
const NO_STATUS = ["command: unknown 'status'\r\n", "commands: v = version and reset reason,\r\n"];
const statusLines = (text) => text.trim().split("\n");
const STATUS_CLOCK_OFF = STATUS_ANSWER.map((piece) => piece.replace("clock=on", "clock=off"));
const CLOCK_OFF_SAVED = [
  "clock: off saved - from the next connection\r\n",
  "clock: off - the card leaves the camera's clock alone; set it in the camera's menu\r\n",
];
const CLOCK_NOT_SAVED = ["clock: could not write the time zone file\r\n"];
// A card with gpxcam 1.4.0: the same state and the location updates.
const STATUS_1_4 = [STATUS_ANSWER.join("").replace("fw=1.3.0", "fw=1.4.0")
  .replace("status: end", "status: updates=normal updates_now=normal\r\nstatus: end")];
const VERSION_1_4 = ["gpxcam-t1000e 1.4.0 (git 1234567), uptime 12 s, log dropped 0\r\n"];
// STATUS_ANSWER (or another answer) with other values, e.g. statusWith({ paired: 0, link: "off" }).
const statusWith = (changes, answer = STATUS_ANSWER) => [Object.entries(changes).reduce(
  (text, [key, value]) => text.replace(new RegExp(` ${key}=\\S+`), ` ${key}=${value}`), answer.join(""))];
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
// serialEvents collects the listeners the page adds to navigator.serial. app: the desktop app
// (simulatedApp()) instead of a browser.
// clipboard: navigator.clipboard (none by default, as in a web view without it).
function openPage(port, { serial = true, serialEvents = {}, app = null, clipboard = null } = {}) {
  const elements = {};
  for (const [tag] of html.matchAll(/<[a-z]+\b[^>]*>/g)) {
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

// The desktop app's Rust side as app/src-tauri gives it to the page: list_cards, open_card with a
// channel for the card's lines, send (the answers come back over the channel, line by line) and
// close_card. openError: the word open_card fails with.
function simulatedApp(paths, answers, { openError = null } = {}) {
  const app = { calls: [], channel: null };
  const core = {
    Channel: class {
      constructor() { this.onmessage = null; }
    },
    invoke: async (command, args = {}) => {
      app.calls.push([command, args.path ?? args.command].filter((part) => part !== undefined).join(" "));
      if (command === "list_cards") return paths.map((path) => ({ path }));
      if (command === "open_card") {
        if (openError) throw openError; // Tauri rejects with the command's error value
        app.channel = args.onEvent;
        return null;
      }
      if (command === "send") {
        const lines = (answers[args.command] || []).join("").split("\n").map((line) => line.replace(/\r$/, ""));
        for (const text of lines.filter(Boolean)) setTimeout(() => app.channel.onmessage({ kind: "line", text }), 5);
        return null;
      }
      if (command === "close_card") {
        setTimeout(() => app.channel.onmessage({ kind: "closed", error: null }), 1);
        return null;
      }
      throw new Error(`no such command: ${command}`);
    },
  };
  app.window = { isSecureContext: true, __TAURI__: { core } };
  app.unplug = () => app.channel.onmessage({ kind: "closed", error: "the card was unplugged" });
  return app;
}

const tick = (ms = 20) => new Promise((resolve) => setTimeout(resolve, ms));

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

test("parseOlderRegister reads the card's line, and olderOutcomeText maps the result", () => {
  assert.deepEqual(
    json(page.parseOlderRegister("older: app register identity 4D454930-0100-1000-8001-A1B2C3D4E5F6 name T1000-E")),
    { identity: "4D454930-0100-1000-8001-A1B2C3D4E5F6", name: "T1000-E" },
  );
  assert.equal(page.parseOlderRegister("older: introduced - waiting up to 300 s for the camera's 01"), null);
  assert.equal(page.olderOutcomeText("accepted")[1], "good");
  assert.equal(page.olderOutcomeText("cancelled")[1], "");
  for (const bad of ["not-confirmed", "not-on-network", "failed", "anything"]) {
    assert.equal(page.olderOutcomeText(bad)[1], "bad");
  }
  for (const phase of ["waiting_gps", "searching", "connecting", "waiting_wifi"]) {
    assert.notEqual(page.olderStartProgress(phase), null, phase);
  }
  for (const phase of ["idle", "pausing"]) assert.equal(page.olderStartProgress(phase), null, phase);
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

test("what a returning reader does not need is folded: the steps once paired, and About", () => {
  assert.match(html, /<details id="pair-help" class="help" open>\s*<summary>How to pair<\/summary>/);
  assert.match(html, /<details class="about">\s*<summary id="about-title">About this page<\/summary>/);
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
  const lines = statusLines(STATUS_ANSWER.join("").replace(/gps: .*\r\n/, "").replaceAll("\r", ""));
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
    ["warn", "Pairing", "See The camera below"]);
  assert.deepEqual(tile(page.gpsTile), ["good", "Fix", "11 satellites"]);
  assert.deepEqual(tile(page.gpsTile, { gpsOn: false }), ["idle", "Off", "Saves power"]);
  assert.deepEqual(tile(page.gpsTile, { gpsFix: false, gpsSatellites: 1 }),
    ["warn", "No fix yet", "1 satellite; outdoors 1\u20133 min"]);
  assert.deepEqual(tile(page.batteryTile), ["good", "Charging", "about 89 % \u00b7 4.10 V"]);
  assert.deepEqual(tile(page.batteryTile, { charger: "charged", batteryMv: 4180, batteryPercent: 100 }),
    ["good", "Charged", "about 100 % \u00b7 4.18 V"]); // every battery tile has its percent
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
  assert.equal(ui["card-status"].textContent, "The connection to the card ended (the card was unplugged).");
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
  const port = simulatedCard({ v: VERSION_1_3, status: STATUS_ANSWER });
  const serialEvents = {};
  const ui = openPage(port, { serialEvents });
  await ui.connect.fire("click");
  assert.equal(ui["card-status"].textContent, "Connected to the card");
  serialEvents.disconnect({ target: {} }); // another port: nothing happens
  assert.equal(port.closed, false);
  serialEvents.disconnect({ target: port });
  await new Promise((resolve) => setTimeout(resolve, 20));
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
  assert.equal(ui["save-lines"].textContent, ""); // "Saved: ..." says what the card's lines say
  await statusAfterSave();
  answers["tz America/New_York"] = ["tz: could not write the time zone file\r\n"];
  await ui.save.fire("click");
  assert.equal(ui["save-status"].className, "bad");
  assert.equal(ui["save-note"].hidden, true);
  assert.equal(ui["save-lines"].textContent, "tz: could not write the time zone file");
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
  assert.equal(page.clockAnswerText(false, "saved")[0], "Saved. It counts from the next connection.");
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
  assert.deepEqual(phases, ["idle", "waiting_gps", "searching", "connecting", "waiting_wifi", "pausing"]);
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

test("the page offers every location updates setting of the firmware, each with its own choice", () => {
  assert.deepEqual(json(page.UPDATES_SETTINGS), firmwareValues("updatesName", "link_policy.cpp"));
  for (const setting of page.UPDATES_SETTINGS) assert.match(html, new RegExp(`id="updates-${setting}" type="radio"`));
  assert.match(html, /<fieldset id="updates" class="segmented">/);
});

// The timings in the page's words are the firmware's (src/link_policy.h).
test("the location updates are explained with the firmware's timings", () => {
  const source = readFileSync(new URL("../../src/link_policy.h", import.meta.url), "utf8");
  const timing = (name) => {
    const match = new RegExp(`${name} = \\{(\\d+), (\\d+), (\\d+)\\}`).exec(source);
    assert.ok(match, name);
    return { seconds: Number(match[1]) / 1000, metres: Number(match[3]) };
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
});

test("the answers to 'updates normal|frequent' are told apart and explained", async () => {
  const saved = [
    "updates: frequent saved\r\n",
    "updates: frequent - a location every 5 s, sooner after 5 m or when the camera asks (at most every 2 s)\r\n",
  ];
  const { port, card } = await openCard({ "updates frequent": saved });
  const lines = await card.updates("frequent");
  assert.deepEqual(port.written, ["updates frequent\n"]);
  assert.equal(page.classifyUpdatesAnswer(lines), "saved");
  const classify = (lines) => page.classifyUpdatesAnswer(lines);
  assert.equal(classify([]), "none");
  assert.equal(classify(["updates: normal saved"]), "saved");
  assert.equal(classify(["updates: already frequent"]), "unchanged");
  assert.equal(classify(["updates: could not write the settings file"]), "not-saved");
  assert.equal(classify(["command: unknown 'updates normal'"]), "no-updates");
  assert.deepEqual(json(page.updatesAnswerText("saved")), ["Saved. The card uses it from the next position it sends.",
    "good"]);
  assert.equal(page.updatesAnswerText("unchanged")[1], "good");
  for (const answer of ["not-saved", "none", "no-updates"]) assert.equal(page.updatesAnswerText(answer)[1], "bad");
});

// updates_now differs from updates only while the guard holds a connection at normal.
test("the state says when the card sends less often than chosen", () => {
  const lines = statusLines(statusWith({ updates: "frequent", updates_now: "normal" }, STATUS_1_4).join("")
    .replace(/gps: .*\r\n/, "").replaceAll("\r", ""));
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
  const lines = (storage) => statusLines(STATUS_1_4.join("").replace("status: end", `status: storage=${storage}\r\nstatus: end`)
    .replace(/gps: .*\r\n/, "").replaceAll("\r", ""));
  assert.equal(page.parseStatus(lines("damaged")).state.storage, "damaged");
  assert.equal(page.parseStatus(lines("ok")).state.storage, "ok");
  assert.equal(page.parseStatus(statusLines(STATUS_ANSWER.join("").replace(/gps: .*\r\n/, "").replaceAll("\r", "")))
    .state.storage, null); // gpxcam 1.3
  assert.match(page.storageNoteText({ ...STATE, storage: "damaged" }), /the pairing comes from its copy/i);
  assert.match(page.storageNoteText({ ...STATE, storage: "damaged", paired: false }),
    /It has no pairing and cannot connect to the camera\./);
  for (const storage of ["ok", null]) assert.equal(page.storageNoteText({ ...STATE, storage }), null);
});

test("a note on the page while the card cannot read what it saved", async () => {
  const answers = { v: VERSION_1_4, status: [STATUS_1_4.join("").replace("status: end", "status: storage=damaged\r\nstatus: end")] };
  const ui = openPage(simulatedCard(answers));
  assert.equal(ui["storage-note"].hidden, true);
  await ui.connect.fire("click");
  assert.equal(ui["storage-note"].hidden, false);
  assert.match(ui["storage-note"].textContent, /^The card cannot read its saved settings/);
  // Pairing and forgetting would be refused: the buttons are off, and the camera note says why.
  assert.equal(ui.pair.disabled, true);
  assert.equal(ui.unpair.disabled, true);
  assert.equal(ui["unpair-yes"].disabled, true);
  assert.match(ui["camera-note"].textContent, /^The card cannot change the pairing while/);
  answers.status = [STATUS_1_4.join("").replace("status: end", "status: storage=ok\r\nstatus: end")];
  await ui.refresh.fire("click");
  assert.equal(ui["storage-note"].hidden, true);
  assert.equal(ui.pair.disabled, false);
  assert.equal(ui.unpair.disabled, false);
  assert.equal(ui["camera-note"].textContent, "The card is paired with a camera. Pairing again replaces it.");
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
  for (const [answers, version] of [[{ v: VERSION_1_3, status: STATUS_ANSWER }, "1.3.0"],
    [{ v: VERSION_ANSWER, status: NO_STATUS, tz: TZ_STATE }, "1.2.0"]]) {
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
  assert.match(page.pairResultText("failed")[0], /If the camera lists T1000\u2011E from an earlier pairing, delete it/);
  assert.deepEqual(json(page.pairResultText("cancelled")), ["Pairing cancelled.", ""]);
  assert.equal(page.classifyUnpairAnswer(["link: off", "pair: camera forgotten (the card id stays)"]), "forgotten");
  assert.equal(page.classifyUnpairAnswer(["pair: no camera paired"]), "not-paired");
  assert.equal(page.classifyUnpairAnswer(["pair: could not write the pairing file"]), "failed");
  assert.equal(page.classifyUnpairAnswer(["pair: not available"]), "failed");
  assert.equal(page.classifyUnpairAnswer(
    ["pair: not possible - the file system is damaged ('storage format yes' first)"]), "storage-damaged");
  assert.match(page.unpairAnswerText("storage-damaged")[0], /^The card cannot change the pairing while/);
  assert.equal(page.unpairAnswerText("storage-damaged")[1], "bad");
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
  assert.equal(ui["pair-help"].open, true); // the steps are open while no camera is paired
  assert.equal(ui.pair.textContent, "Pair");
  assert.equal(ui.pair.disabled, false);
  assert.equal(ui.unpair.hidden, true);
  assert.equal(ui["pair-cancel"].hidden, true);

  answers.pair = PAIR_STARTED;
  answers.status = statusWith({ ...NOT_PAIRED, pair: "waiting_gps", gps_fix: 0, gps_sat: 3 });
  await ui.pair.fire("click");
  assert.deepEqual(port.written.slice(-2), ["pair\n", "status\n"]);
  assert.equal(ui["pair-progress"].textContent, "Waiting for GPS time, up to 2 minutes (3 satellites so far)...");
  assert.equal(ui["pair-progress"].className, "busy"); // under way: a pulsing dot
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
  assert.equal(ui["camera-note"].textContent, "The card is paired with a camera. Pairing again replaces it.");
  assert.equal(ui["pair-help"].open, false); // the steps fold away once paired
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
  for (const id of ["camera-note", "pair-help", "pair-row"]) assert.equal(ui[id].hidden, true, id);
  assert.equal(ui["camera-old"].hidden, false); // pair with the button, with a link to the README
  const readme = '<a href="https://github.com/TomaszBojanowski/gpxcam-t1000e#pairing-once">the README</a>';
  assert.ok(html.includes(readme));
  await ui.disconnect.fire("click");
  for (const id of ["camera-note", "pair-help", "pair-row"]) assert.equal(ui[id].hidden, false, id);
  assert.equal(ui["camera-old"].hidden, true);
});

// ---- Diagnostics ----

// The card's answer to 'log' (src/log.cpp dumpHistory()) and a 'diag scan' run (src/bluetooth.cpp,
// src/scan_dump.cpp), made-up values; a status answer of the page's refresh comes in between.
const LOG_ANSWER = [
  "[     12.3 s] gps: on\r\n[    15.0 s] link: looking for camera 123456789a\r\n",
  "[  12345.6 s] link: position sent\r\n",
];
const SCAN_ANSWER = [
  "diag scan: on 30 s, radio 50 ms of every 100 ms, reports stronger than -65 dBm; full bytes for Panasonic\r\n",
  "diag scan: **:**:**:**:**:e2 RPA -48 dBm adv 13 B, camera, bytes: 02 01 06 09 ff 3a 00 00 12 34 56 78 9a\r\n",
  "status: format=1 fw=1.3.0 git=1234567\r\n",
  "diag scan: **:**:**:**:**:33 RPA -40 dBm rsp 7 B\r\n",
  "diag scan: done, 2 device(s) shown, 812 reports received\r\n",
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
  assert.deepEqual(clipboard.written, [text]);
  assert.equal(ui["diag-status"].textContent, "The card's last 3 events. Copied: paste it into a private message.");
  assert.equal(ui["diag-status"].className, "good");
  assert.equal(ui["diag-answer"].hidden, false);
  assert.equal(ui["diag-copy"].disabled, false);
  await ui.disconnect.fire("click");
  assert.equal(ui["diag-text"].value, text); // kept for copying after unplugging
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
    .map((line) => line.trim()));
  assert.equal(ui["diag-status"].textContent, "Scan done. Click Copy, or select the text below and copy it.");
  assert.equal(ui["diag-progress"].hidden, true);
  assert.equal(ui.scan.disabled, false);
  await ui["diag-copy"].fire("click");
  assert.match(ui["diag-status"].textContent, /^Copying did not work here: select the text below/);
  await ui.disconnect.fire("click");
});

test("Scan for cameras on a card without it says which gpxcam brought it", async () => {
  const port = simulatedCard({
    v: VERSION_1_4, status: STATUS_1_4, "diag scan": ["command: unknown 'diag scan'\r\n", ...OLD_FIRMWARE.slice(1)],
  });
  const ui = openPage(port, { clipboard: simulatedClipboard() });
  await ui.connect.fire("click");
  await ui.scan.fire("click");
  assert.match(ui["diag-status"].textContent, /^gpxcam 1\.4\.0 cannot scan; the scan came with 1\.5\.0-beta\.1\./);
  assert.equal(ui["diag-status"].className, "bad");
  await ui.disconnect.fire("click");
});
