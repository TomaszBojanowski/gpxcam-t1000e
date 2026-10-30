// Screenshots of the desktop app gpxcam card for README.md and app/README.md: the card page
// (docs/index.html) as the app shows it, with the app's side simulated (test/web/simulated_app.mjs)
// and a card that answers with made-up values (no real card, camera or position involved).
//
//   npm install               (once: playwright-core, see package.json)
//   npm run screenshots       (needs Chromium and oxipng; CHROMIUM=/path/to/chrome to choose one)
//
// Writes docs/images/app-*.png: 1000 px wide, the dark theme, optimized with oxipng.
import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, statSync } from "node:fs";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";

import { chromium } from "playwright-core";

import { simulatedApp } from "../test/web/simulated_app.mjs";

const ROOT = new URL("../", import.meta.url);
const PAGE = new URL("docs/index.html", ROOT);
const OUT = new URL("docs/images/", ROOT);
const WIDTH = 1000;
const HEIGHT = 900;
const MARGIN = 12; // px of the page's background around a clipped part
const MAX_BYTES = 150 * 1024;
// A made-up moment in summer, so the zone tile shows summer time and every run gives the same text.
const FIXED_TIME = new Date("2027-07-01T17:05:12Z");
const CHROMIUM_PATHS = ["/usr/bin/chromium-browser", "/usr/bin/chromium", "/usr/bin/google-chrome"];

// The card's state ('status' keys, src/status_text.h) for every shot; a shot changes some of them.
const CARD_STATE = {
  format: 1, fw: "1.5.0", git: "1234567", battery_mv: 4180, battery_pct: 98, charger: "charged",
  gps: "on", gps_fix: 1, gps_sat: 11, gps_hold: 0, paired: 1, link: "connected", pair: "idle", pair_attempt: 0,
  pair_result: "none", clock: "on", tz: "Europe/Warsaw", sound: "quiet", updates: "frequent",
  updates_now: "frequent", watchdog_restart: 0, storage: "ok",
};
const NOT_PAIRED = { paired: 0, link: "off" };

// Runs in the page before its own scripts: window.__TAURI__ from simulatedApp, with a card that
// answers as gpxcam 1.5.0 does (src/main.cpp, src/pairing.cpp, src/bluetooth.cpp) and keeps its state
// as its commands change it; window.__card lets a shot change that state and send a line of the card's
// log. Made-up values only: the card id of the test vectors, the camera serial number 123456789a,
// Bluetooth addresses of the documentation range.
function installCard(simulatedApp, state) {
  const STATUS_LINES = [
    ["format", "fw", "git"], ["battery_mv", "battery_pct", "charger"], ["gps", "gps_fix", "gps_sat", "gps_hold"],
    ["paired", "link"], ["pair", "pair_attempt", "pair_result"], ["clock", "tz"], ["sound"],
    ["updates", "updates_now"], ["watchdog_restart"], ["storage"],
  ];
  const statusLine = (keys) => `status: ${keys.map((key) => `${key}=${state[key]}`).join(" ")}\r\n`;
  const status = () => [...STATUS_LINES.map(statusLine), "status: end\r\n"];
  const SCAN = [
    "diag scan: on 30 s, radio 50 ms of every 100 ms, reports stronger than -65 dBm; full bytes for Panasonic",
    "diag scan: **:**:**:**:**:01 public -52 dBm ch 38 adv connectable 31 B, camera",
    "diag scan:   bytes: 02 01 06 09 ff 3a 00 03 12 34 56 78 9a 11 07 1b c5 d5 a5 02 00 0d ac e6 11 14 32 20 c6 4a 05",
    "diag scan: **:**:**:**:**:01 public -52 dBm ch 39 rsp 15 B",
    "diag scan: **:**:**:**:**:02 random static -61 dBm ch 37 adv connectable 31 B",
    "diag scan: by channel 37/38/39: 212/236/219 reports, 104/110/113 from LUMIX cameras",
    "diag scan: done, 2 device(s) shown, 667 reports received, 0 resume failed, 0 restarts",
  ].map((line) => line + "\r\n");
  const answers = {
    v: ["gpxcam-t1000e 1.5.0 (git 1234567), uptime 4321 s, log dropped 0\r\n", "reset: 0x00000004 (SREQ)\r\n"],
    status,
    "pair prepare": () => {
      state.gps_hold = 600;
      return ["pair: GPS on for 600 s, ready for pairing\r\n"];
    },
    pair: () => {
      Object.assign(state, { paired: 0, link: "off", pair: "searching", pair_attempt: 0, pair_result: "none" });
      return ["link: off\r\n", "pair: GPS time ok - looking for a camera in pairing mode (up to 120 s)\r\n"];
    },
    "pair off": () => {
      Object.assign(state, { pair: "idle", pair_result: "cancelled" });
      return ["pair: cancelled\r\n"];
    },
    // The card has introduced itself to an older camera and waits for the Wi-Fi step (src/older_session.cpp);
    // the identity of the made-up seed of test/device_identity_vectors.txt.
    "pair older": () => {
      Object.assign(state, { paired: 0, link: "off", pair: "waiting_wifi", pair_attempt: 1, pair_result: "none" });
      return ["link: off\r\n", "older: introduced - waiting up to 300 s for the camera's 01\r\n",
        "older: app register identity 4D454930-0100-1000-8001-A1B2C3D4E5F6 name T1000-E\r\n"];
    },
    // The pairing lines of 'p'; a search has run for 18 s.
    p: () => ["pair: card id 00112233-4455-4677-8899-a1b2c3d4e5f6, no camera paired\r\n",
      state.pair === "searching" ? "pair: looking for a camera in pairing mode for 18 s, attempt 0/3\r\n"
        : "pair: idle\r\n"],
    "diag scan": SCAN,
  };
  const app = simulatedApp(["/dev/ttyACM0"], answers);
  app.registerOutcome = "wait"; // the Wi-Fi step waits, as while the user joins the camera's network
  app.registerStages = [];
  window.__TAURI__ = app.window.__TAURI__;
  window.__card = { state, say: (text) => app.channel.onmessage({ kind: "line", text }) };
}

// Serves docs/index.html at http://127.0.0.1:<port>/ (a secure context, as the app's web view).
function serve() {
  const html = readFileSync(PAGE);
  const server = createServer((request, response) => {
    if (request.url === "/") {
      response.writeHead(200, { "Content-Type": "text/html; charset=utf-8" }).end(html);
    } else if (/^\/[a-z_]+\.js$/.test(request.url) && existsSync(new URL(`.${request.url}`, PAGE))) {
      // the scripts the page loads next to it (docs/zones.js)
      response.writeHead(200, { "Content-Type": "text/javascript; charset=utf-8" })
        .end(readFileSync(new URL(`.${request.url}`, PAGE)));
    } else {
      response.writeHead(404).end();
    }
  });
  return new Promise((resolve) => server.listen(0, "127.0.0.1", () => resolve(server)));
}

function chromiumPath() {
  const path = process.env.CHROMIUM || CHROMIUM_PATHS.find((candidate) => existsSync(candidate));
  if (!path) throw new Error(`no Chromium found in ${CHROMIUM_PATHS.join(", ")}: set CHROMIUM=/path/to/chrome`);
  return path;
}

// A page connected to the simulated card in the given state.
async function connectedPage(browser, url, changes) {
  const context = await browser.newContext({
    viewport: { width: WIDTH, height: HEIGHT },
    deviceScaleFactor: 1,
    colorScheme: "dark",
    locale: "en-GB",
    timezoneId: "Europe/Warsaw",
  });
  await context.grantPermissions(["clipboard-read", "clipboard-write"], { origin: url });
  const page = await context.newPage();
  await page.clock.setFixedTime(FIXED_TIME);
  const state = { ...CARD_STATE, ...changes };
  await page.addInitScript({ content: `(${installCard})(${simulatedApp}, ${JSON.stringify(state)});` });
  await page.goto(url);
  await page.click("#connect");
  await page.waitForSelector("#state:not(.hidden)");
  return page;
}

// Saves the parts (CSS selectors) of the page as one PNG, with a margin of its background.
async function shoot(page, name, selectors) {
  await page.mouse.move(0, 0);
  await page.evaluate(() => document.activeElement && document.activeElement.blur());
  await page.evaluate(() => window.scrollTo(0, 0)); // boundingBox() is relative to the scrolled view
  const boxes = [];
  for (const selector of selectors) boxes.push(await page.locator(selector).boundingBox());
  const left = Math.max(0, Math.min(...boxes.map((box) => box.x)) - MARGIN);
  const top = Math.max(0, Math.min(...boxes.map((box) => box.y)) - MARGIN);
  const right = Math.min(WIDTH, Math.max(...boxes.map((box) => box.x + box.width)) + MARGIN);
  const bottom = Math.max(...boxes.map((box) => box.y + box.height)) + MARGIN;
  const path = fileURLToPath(new URL(`app-${name}.png`, OUT));
  await page.screenshot({
    path, fullPage: true, animations: "disabled", caret: "hide",
    clip: { x: left, y: top, width: right - left, height: bottom - top },
  });
  return path;
}

const CARD = ["main > header.top", "section:has(#state)"];
const CAMERA = ["section:has(#pairing)"];
const SETTINGS = ["section:has(#clock)", "section:has(#updates-normal)", "section:has(#sound-loud)"];
const DIAGNOSTICS = ["section:has(#scan)"];

// Each shot, by the name of its file (app-<name>.png): the page made ready for it, and its parts.
const SHOTS = {
  // The card's state after Connect: paired and connected, a GPS fix, the battery charged.
  state: async (browser, url) => ({ page: await connectedPage(browser, url, {}), parts: CARD }),
  // The camera section's pairing screens. Prepare: a card not paired yet, with a GPS fix.
  "pair-prepare": async (browser, url) => {
    const page = await connectedPage(browser, url, NOT_PAIRED);
    await page.waitForSelector("#pair-screen-prepare:not(.hidden)");
    return { page, parts: CAMERA };
  },
  // The card has looked for the camera for 18 s ('p' after Connect): start pairing on the camera now; a
  // camera nearby is not in pairing mode yet.
  "pair-camera": async (browser, url) => {
    const page = await connectedPage(browser, url, { ...NOT_PAIRED, pair: "searching" });
    await page.waitForFunction(() => document.getElementById("pair-countdown-time").textContent === "1:42");
    await page.evaluate(() => window.__card.say("pair: camera 123456789a is on (00), not in pairing mode - " +
      "choose pairing on the camera"));
    await page.waitForSelector("#pair-status.warn");
    return { page, parts: CAMERA };
  },
  // Camera found: connected over Bluetooth, the card introduces itself.
  "pair-connect": async (browser, url) => {
    const page = await connectedPage(browser, url, { ...NOT_PAIRED, pair: "connecting", pair_attempt: 1 });
    await page.evaluate(() => {
      window.__card.say("pair: attempt 1/3 - camera 123456789a in pairing mode, -48 dBm, connecting");
      window.__card.say("camera: connected");
    });
    await page.waitForSelector("#connect-stage-3.current");
    return { page, parts: CAMERA };
  },
  // An older camera at its Wi-Fi step (found waiting there after Connect, the app goes on by itself): the
  // app waits for this computer to join the camera's network.
  "pair-wifi": async (browser, url) => {
    const page = await connectedPage(browser, url, { ...NOT_PAIRED, pair: "waiting_wifi", pair_attempt: 1 });
    await page.waitForSelector("#pair-screen-wifi:not(.hidden)");
    await page.waitForSelector("#pair-status.warn");
    return { page, parts: CAMERA };
  },
  // Paired.
  "pair-done": async (browser, url) => {
    const page = await connectedPage(browser, url, {});
    await page.waitForSelector("#pair-screen-done:not(.hidden)");
    return { page, parts: CAMERA };
  },
  // A pairing that found no camera: why, the tips, Try again, and pairing as with an older camera.
  "pair-failed": async (browser, url) => {
    const page = await connectedPage(browser, url, { ...NOT_PAIRED, pair: "searching" });
    await page.evaluate(() => Object.assign(window.__card.state, { pair: "idle", pair_result: "no_camera" }));
    await page.waitForSelector("#pair-screen-failed:not(.hidden)");
    return { page, parts: CAMERA };
  },
  // Diagnostics: Scan for cameras, the camera's serial number hidden.
  diagnostics: async (browser, url) => {
    const page = await connectedPage(browser, url, {});
    await page.click("#scan");
    await page.waitForFunction(() => document.getElementById("diag-status").textContent.startsWith("Scan done"));
    return { page, parts: DIAGNOSTICS };
  },
  // The settings: the camera's clock and time zone, the location updates and the sound.
  settings: async (browser, url) => ({ page: await connectedPage(browser, url, {}), parts: SETTINGS }),
};

const server = await serve();
const url = `http://127.0.0.1:${server.address().port}/`;
const browser = await chromium.launch({ executablePath: chromiumPath() });
const files = [];
try {
  for (const [name, prepare] of Object.entries(SHOTS)) {
    const { page, parts } = await prepare(browser, url);
    try {
      files.push(await shoot(page, name, parts));
    } finally {
      await page.context().close();
    }
  }
} finally {
  await browser.close();
  server.close();
}
execFileSync("oxipng", ["--opt", "4", "--strip", "safe", "--quiet", ...files]);
let tooBig = false;
for (const file of files) {
  const bytes = statSync(file).size;
  tooBig ||= bytes > MAX_BYTES;
  console.log(`${file}: ${Math.round(bytes / 1024)} KB${bytes > MAX_BYTES ? ` - over ${MAX_BYTES / 1024} KB` : ""}`);
}
process.exitCode = tooBig ? 1 : 0;
