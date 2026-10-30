# gpxcam-t1000e

<img align="right" width="280" src="docs/images/t1000e-s5ii.jpg" alt="SenseCAP T1000-E next to a LUMIX S5II">

**GPS for a Panasonic LUMIX camera from a SenseCAP T1000-E card, without a phone.** The card connects to
the camera over Bluetooth by itself, sets its clock and gives it the position for every photo.

[![Firmware](https://img.shields.io/github/v/release/TomaszBojanowski/gpxcam-t1000e?label=firmware&cacheSeconds=3600)](https://github.com/TomaszBojanowski/gpxcam-t1000e/releases/latest)
[![Desktop app](https://img.shields.io/github/v/release/TomaszBojanowski/gpxcam-t1000e?label=desktop%20app&filter=app-v*&cacheSeconds=3600)](app/README.md#install)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

**You need**
- a Seeed SenseCAP Card Tracker T1000-E (the one sold for Meshtastic);
- a LUMIX S5II – the only camera tested so far; the others are under [Cameras](#cameras);
- for pairing and settings, a computer with Chrome or Edge, or the desktop app.

**Start here:** [install the firmware](#install-the-release-windows-macos-linux) ·
[the card page](https://tomaszbojanowski.github.io/gpxcam-t1000e/) · [the desktop app](app/README.md#install) ·
[pairing](#pairing-once)

<br clear="right">

*LUMIX and Panasonic are trademarks of Panasonic and SenseCAP is a trademark of Seeed; this project is not
affiliated with either.*

## What it does

- When the camera is switched on, the card finds it, connects and sends the position from its GPS: every
  25 s, sooner after 20 m (Normal), or every 5 s, sooner after 5 m (Frequent). The camera writes it into the
  EXIF data of every photo.
- Each time it connects, the card sets the camera's clock and time zone from the GPS; this can be switched off.
- While the camera is off, the card saves power: after 20 minutes its GPS sleeps and it looks for the camera
  less often.
- The card page and the desktop app pair the card, show its state and change its settings over USB. Neither
  sends anything over the internet.
- Meshtastic can go back on the card at any time.

Pairing, everyday use with the camera and power saving all work and are tested with a LUMIX S5II. What
changed in each version is on the [Releases](https://github.com/TomaszBojanowski/gpxcam-t1000e/releases) page.

## Cameras

Which LUMIX cameras work, and how each one pairs:

| Camera | How it pairs | Status |
|---|---|---|
| LUMIX S5II | *Pair* on the card page or in the app | tested |
| Other current bodies: S5IIX, S9, GH7, G9II, S1II, S1RII, S1IIE, L10 | *Pair* on the card page or in the app | same protocol as the S5II, expected to work, not tested yet |
| LUMIX S5II in its older protocol | *Pair as an older camera* in the app (offered when *Pair* does not work), with the Wi-Fi step | tested by the author: pairing, locations with Normal and Frequent |
| LUMIX Sync generation: S5, S1, S1R, S1H, GH5, GH5 II, GH5S, GH6, G9, G97, G100, BS1H, BGH1 | *Pair* in the desktop app, with a one-time step over the camera's Wi-Fi | not confirmed; the one field test so far, a LUMIX S5, has not connected |

> [!WARNING]
> **Older cameras (LUMIX Sync generation): experimental.** The card speaks their older Bluetooth protocol, but
> it is not yet confirmed on any camera of that generation. If you own one and can spend an hour testing, please
> say so in [issue #2](https://github.com/TomaszBojanowski/gpxcam-t1000e/issues/2).
> How to pair them: [app/README.md](app/README.md#pairing-an-older-camera).

## Install the release (Windows, macOS, Linux)

No tools needed: the card takes the firmware as a file copied to a USB drive.

1. Before you start: gpxcam replaces Meshtastic. If you may want to go back later, note your
   Meshtastic settings (channels and keys) first.
2. Download `gpxcam-t1000e-<version>.uf2` from the
   [Releases](https://github.com/TomaszBojanowski/gpxcam-t1000e/releases) page.
3. Put the card into flashing mode: hold its button and plug the USB cable in twice quickly. The
   LED lights green and a USB drive named **T1000-E** appears.
4. Open `INFO_UF2.TXT` on that drive. It must show `Board-ID: nRF52840-T1000-E-v1` and
   `SoftDevice: S140 7.3.0`. If it shows something else, stop: this firmware is not for that card.
5. Copy the `.uf2` file onto the drive. The card restarts with gpxcam and the drive disappears.
   There is no need to eject the drive. Your computer may warn that the drive was not ejected
   properly, or even report a copy error at the end – this is normal, the card restarts as soon as
   it has the whole file.
6. Pair the card with the camera, see [Pairing](#pairing-once).

The file holds only the application: the bootloader and the SoftDevice stay as they are.

## The card page

[The card page](https://tomaszbojanowski.github.io/gpxcam-t1000e/) talks to the card over USB from
Chrome or Edge on Windows, macOS or Linux (Firefox, Safari and phones cannot talk to the card; on a
computer without Chrome or Edge use [the desktop app](#the-desktop-app)). It shows the card's state, pairs
it with the camera step by step and changes its settings; all it does and sends:
[docs/GUIDE.md](docs/GUIDE.md#the-card-page-in-detail).

1. Plug the card into the computer with a USB cable. Close serial monitors that use it.
2. Open [the card page](https://tomaszbojanowski.github.io/gpxcam-t1000e/).
3. Click **Connect** and choose **T1000-E gpxcam**.

## The desktop app

gpxcam card is the card page as an app for Linux, macOS and Windows, for computers without Chrome or
Edge. It does the same as the page and, like it, sends nothing over the internet.
[app/README.md](app/README.md#install) says how to install it, how to open it the first time and
which gpxcam versions it works with. The app can also pair *older* LUMIX cameras (the LUMIX Sync
generation), which need one extra step over the camera's own Wi-Fi; see
[app/README.md](app/README.md#pairing-an-older-camera).

![The app's card section: connected to the card, with tiles for the camera (Connected), the GPS (Fix, 11 satellites), the battery (Charged, 100 %) and the time zone (Europe/Warsaw, clock on)](docs/images/app-state.png)

![Pairing, the user's turn: "Start Bluetooth pairing on the camera now", the camera menu path, a countdown at 1:42 with its bar, "left to find the camera", and below it "A camera is nearby, but not in pairing mode yet." with Cancel](docs/images/app-pair-camera.png)

*The app's state after Connect, and the moment to start pairing on the camera. More in
[app/README.md](app/README.md#what-it-looks-like); made with `tools/screenshots.mjs` and a simulated card.*

## User guide

The short way: switch the card on, pair it once, keep it near the camera. The details – sounds and LED, the
clock setting, a damaged file system, the serial console – are in [docs/GUIDE.md](docs/GUIDE.md).

### Switching on and off

- **On:** hold the button for about 1 s (start melody), or plug in the USB cable. A shorter press
  sends the card back to sleep (the LED may blink once – that is the bootloader).
- **Off:** hold the button for 3 s (power-off melody). With the USB cable plugged in the card
  stays on: two clicks.
- Below 3.40 V the card switches itself off to protect the battery (two clicks). With the cable
  it always starts and charges.

### Pairing (once)

1. Put the card where the GPS can get a fix (outdoors or near a window): the camera needs the
   time from the GPS.
2. If the camera lists "T1000-E" from an earlier pairing, delete it there first: the camera does
   not pair a card it already knows.
3. Start pairing on the card: click **Pair** on [the card page](#the-card-page), which shows each
   stage and, if pairing fails, why. Or on the card: a short click, and within 0.5 s press again
   and hold for 3 s – two short high beeps. Do not start pairing on the camera yet.
4. The card waits up to 120 s for GPS time, then looks for the camera for up to 120 s. **Only
   now** start pairing in the camera's Bluetooth menu (the screen that waits for a new device):
   some cameras stay in pairing mode only about 30 seconds, so the card must already be looking.
   The card page says when, *Start Bluetooth pairing on the camera now*, and counts the 2 minutes
   down. On success the camera shows "Pairing completed" and lists "T1000-E"; the card plays a
   rising melody. If it gives up: two clicks. The card then keeps its GPS on for 10 minutes, so that
   another try starts looking for the camera at once.

A short press cancels pairing. The card only connects to a camera that is in pairing mode, and
it pairs with one camera at a time: a new pairing replaces the old one.

An older camera (of the LUMIX Sync generation) needs one more step over its
Wi-Fi at the first pairing: pair it from [the desktop app](#the-desktop-app), which does that step,
see [app/README.md](app/README.md#pairing-an-older-camera).

### Normal use

Switch the card on and keep it near the camera. The card connects to the camera only when its GPS
has a position fix: the camera needs the exact time from it. After you switch the card on, the
first fix takes about 1–3 minutes outdoors or near a window; deep indoors it may not come at all.

When the camera is on, the card connects by itself (two rising beeps), sets the camera's clock
(see [Set the time zone](#set-the-time-zone); this can be switched off) and the camera shows its
Bluetooth and GPS icons.
The card sends the position at once and then, with **Normal** location updates, every 25 s –
earlier when it moves more than 20 m; with **Frequent** every 5 s – earlier when it moves
more than 5 m, at most every 2 s. If the GPS loses its fix later, the connection stays, but no
position is sent until the fix is back.

When the camera is switched off, the card notices it (two falling beeps) and waits; after a while it saves
power. It never wakes a camera that is switched off. How long it waits, and how to charge the card:
[docs/GUIDE.md](docs/GUIDE.md#normal-use).

### Set the time zone

The card sets the camera's clock every time it connects: the time from the GPS and the time zone.
Without a setting the time zone is Europe/Warsaw (Central European Time).

A new time zone reaches the camera at the next connection: **switch the camera off and on** after
changing it.

#### With the card page

1. Connect the card on [the card page](#the-card-page).
2. Under **The camera's clock**, find the time zone by country, city or zone name (the computer's
   own is selected already) and click **Save**.
3. Switch the camera off and on.

#### With a serial terminal

Any terminal at **115200 baud** (never 1200: that rate restarts the card into its bootloader):
- Windows: the Serial Monitor of the Arduino IDE (any line ending except "No line ending"), or
  PuTTY (connection type Serial, the COM port of the card from the Device Manager, speed 115200);
- macOS: `screen /dev/cu.usbmodem* 115200` (leave it with Ctrl+A, then K and Y);
- Linux: `pio device monitor` in this repository, or `screen /dev/ttyACM0 115200`.

The card does not show what you type: type the command and press Enter.
The time zone commands: [docs/GUIDE.md](docs/GUIDE.md#the-time-zone-in-detail).

<a id="leave-the-cameras-clock-alone-130"></a>
#### Leave the camera's clock alone

The card can leave the camera's time and time zone alone and send only the position: switch off **The card
sets the camera's clock and time zone** on the card page, or send `clock off`. What changes then:
[docs/GUIDE.md](docs/GUIDE.md#leave-the-cameras-clock-alone).

### Sounds and LED

The card answers with sounds and its green LED; what each one means is in
[docs/GUIDE.md](docs/GUIDE.md#sounds-and-led). Choose **Loud**, **Quiet** or **Off** under **Sound** on the card
page.

### If the card forgot its settings

If the card cannot read its stored settings, it works with the defaults, stays paired from the copy of its
pairing and says so on the card page. How to repair it:
[docs/GUIDE.md](docs/GUIDE.md#if-the-card-forgot-its-settings).

## Known limitations

- The camera gets a new time zone only when the card connects to it: after changing the time zone,
  switch the camera off and on (see [Set the time zone](#set-the-time-zone)).
- The battery percent, summer time in a few zones, later changes of time zone rules and going back to an
  older firmware: [docs/GUIDE.md](docs/GUIDE.md#known-limitations).

## Back to Meshtastic

1. Put the card into flashing mode: hold its button and plug the USB cable in twice quickly.
2. Install Meshtastic with the [Meshtastic Web Flasher](https://flasher.meshtastic.org) (device:
   Seeed SenseCAP T1000-E), or copy a Meshtastic `.uf2` file for the T1000-E onto the T1000-E drive.

gpxcam never changes the bootloader or the SoftDevice, so Meshtastic installs as on a new card.
Its settings, and coming back to gpxcam later: [docs/GUIDE.md](docs/GUIDE.md#back-to-meshtastic-and-back).

## Serial console (service)

`pio device monitor`, or any terminal at 115200 baud (see [With a serial terminal](#with-a-serial-terminal)).
The commands: [docs/GUIDE.md](docs/GUIDE.md#serial-console-service). The log shows the camera's serial number
and Bluetooth address and the card's id – do not publish it.

## Support

- Questions and problems: [Issues](https://github.com/TomaszBojanowski/gpxcam-t1000e/issues). Say which camera,
  which gpxcam version (`v`, or the card page) and what you did.
- An older camera (LUMIX Sync generation): [issue #2](https://github.com/TomaszBojanowski/gpxcam-t1000e/issues/2).
- Logs: *Copy log* and *Scan for cameras* under **Diagnostics** on the card page hide the camera's serial
  number, the Bluetooth addresses and the card's identity. Do not paste `p` or the log from a serial
  terminal without hiding them.

## Documentation

- [docs/GUIDE.md](docs/GUIDE.md) – the rest of the user guide: the card page in detail, normal use, the time
  zone, the clock setting, sounds and LED, a damaged file system, Meshtastic, the serial console.
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) – building from source, flashing safety and the tests on the PC.
- [docs/HARDWARE.md](docs/HARDWARE.md) – the card and how the firmware uses it, by topic: pins, flash
  and DFU, power and battery, GPS, Bluetooth, storage, reliability, pairing and the link, with the
  measurements behind them and the differences from Meshtastic.
- [docs/PROTOCOL.md](docs/PROTOCOL.md) – the Bluetooth LE protocol of LUMIX cameras, the
  connection parameters the S5II works with, how the camera takes the time zone, and test vectors.
- [The card page](https://tomaszbojanowski.github.io/gpxcam-t1000e/) – the card's state, pairing,
  the camera's clock and time zone, sound, location updates and Diagnostics, from Chrome or Edge
  (source: `docs/index.html`).
- [app/README.md](app/README.md) – the desktop app: install, pairing an older camera, versions.

## Sources

- Pins, board file and linker script: [Meshtastic](https://github.com/meshtastic/firmware)
  (GPL-3.0), completed from Seeed's T1000-E SDK – details in `docs/HARDWARE.md` and in the code.
- Arduino core: Meshtastic's fork of the Adafruit nRF52 core, pinned in `platformio.ini`.
- Protocol: worked out with gpxcam, the author's own Android app that sends the phone's position to the
  camera (private, not published); this firmware does its job without the phone.

## License

GNU GPL v3.0 – see [LICENSE](LICENSE).
