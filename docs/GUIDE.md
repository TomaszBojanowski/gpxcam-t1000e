# gpxcam-t1000e: user guide

The details behind the [README](../README.md): the card page, normal use, the time zone and the clock, sounds
and LED, a damaged file system, known limitations, Meshtastic, and the serial console.

## The card page in detail

[The card page](https://tomaszbojanowski.github.io/gpxcam-t1000e/), and the desktop app with it:
- it shows the card's state: firmware, battery, GPS, the camera link and the time zone, refreshed
  every few seconds;
- it pairs the card with the camera, step by step, and says why if pairing fails; it can also
  forget the paired camera;
- it sets whether the card sets the camera's clock, and the time zone;
- it sets how loud the card is (see [Sounds and LED](#sounds-and-led));
- it sets how often the card sends the position, Normal or Frequent (see [Normal use](#normal-use));
- when something does not work, it copies the card's log, or a 30-second scan of the Bluetooth
  devices next to the card, ready to paste into a message (with the camera's serial number, the
  Bluetooth addresses and the card's identity hidden).

The page runs only in the browser and sends nothing over the internet. It sends the card only `v`,
`status`, `p`, `pair`, `pair off`, `pair prepare`, `unpair`, `clock on`, `clock off`, `sound loud`,
`sound quiet`, `sound off`, `updates normal`, `updates frequent`, `log`, `diag scan`, `tz` and `tz`
with a zone from its list; the app also `pair older` for older cameras. From `p` it only reads how
long the card has been looking for the camera, and in the app the card's id for an older camera.
With gpxcam 1.2 it only sets the time zone; the rest needs 1.3.0 or
newer, the location updates 1.4.0 and the diagnostics 1.5.0. On connecting to a card that is not
paired yet, the page sends `pair prepare` (gpxcam 1.5.0): the card switches its GPS on for 10
minutes, so that pairing finds the time ready.

On Linux, if the page says "Reading from the card failed" after a serial monitor (for example
`pio device monitor`) used the card: unplug the card and plug it in again. Such programs leave the
port with a setting that Chrome does not handle, and plugging in again resets it.

## Normal use

Charge the card with the magnetic cable on the bare card, not through a case, until the magnet
clicks into place. A contact that keeps making and breaking can hang the card (see
[docs/HARDWARE.md](HARDWARE.md#the-hang-of-2026-10-24)).

When the camera is switched off, the card notices it (two falling beeps) and waits:
- for the first 20 minutes it keeps the GPS ready and looks for the camera every second, so the
  camera gets a position about 10 s after it is switched on again (most of that time the camera
  itself is starting up);
- after that, and when the camera is out of sight for 5 minutes, the card saves power: it looks
  for the camera less often and the GPS sleeps. The camera then gets a position about 25–35 s
  after it is switched on or comes back; after many hours the GPS needs longer, up to about
  1–2 minutes.

The card never wakes a camera that is switched off.

## The time zone in detail

The time zone changes only the camera's local time; the position and the GPS time in EXIF do not
depend on it. The card keeps the setting through restarts and firmware updates and switches summer
time by itself, with the rules of the tz database (IANA).

With a [serial terminal](../README.md#with-a-serial-terminal):

| Command | What it does |
|---|---|
| `tz` | shows the time zone, its rule and, with a GPS fix, what the camera gets now |
| `tz Europe/Warsaw` | sets a time zone by its name in the tz database (any case; old names such as `Poland` work too) |
| `tz <+0545>-5:45` | sets a POSIX TZ rule directly (summer-time dates in the `Mm.w.d` form, offsets within ±15 h) |

## Leave the camera's clock alone

If you prefer to set the camera's clock yourself, switch off **The card sets the camera's clock and
time zone** on the card page (or send `clock off`). From the next connection the card leaves the
camera's time and time zone as they are and only sends the position; the GPS time in EXIF still
comes from the card. Pairing still sets the clock once, so set the camera's time zone in its menu
after pairing. `clock on` brings the card's clock setting back; the camera takes it at the next
connection (switch the camera off and on).

Without writing the time, the card cannot see at once that the camera has forgotten it (removed
from the camera's list of devices): the camera refuses the time only from a card it does not know.
So with the clock off the card stops working with the camera when the camera refuses any step of
the introduction, or ends three connections in a row before it takes a position. The card page
then says that the camera refused the card: pair them again.

## Sounds and LED

| Sound | Meaning |
|---|---|
| three rising notes | switched on |
| three falling notes | switching off |
| a short tick | short button press |
| two clicks | refused or given up (power off with the cable, pairing, camera, low battery) |
| two short high beeps | pairing started |
| rising melody | paired |
| two short rising beeps | connected to the camera |
| two short falling beeps | connection ended |

**Volume (1.3.0):** **Loud**, **Quiet** or **Off** – choose under **Sound** on
[the card page](../README.md#the-card-page), or send `sound loud`, `sound quiet` or `sound off`. Off keeps only
the answers to a held button, quietly: switching on and off, pairing started, and two clicks when
the card refuses to switch off or to pair. Without them you could not tell when to let go. The card
keeps the setting through restarts and firmware updates.

| LED (green) | Meaning |
|---|---|
| a short flash every second | on, not working with a camera (no camera paired, or `link off`) |
| two short flashes every 2 s | looking for the camera |
| a short flash every 4 s | connected to the camera |
| fast blinking | `off` countdown |

## If the card forgot its settings

From 1.4.0 the card never erases its stored settings by itself. If it cannot read them – its
file system is damaged, as after the hang described in
[docs/HARDWARE.md](HARDWARE.md#the-hang-of-2026-10-24) – it:
- works with the default settings: sound Loud, the clock on, Europe/Warsaw, location updates
  Normal;
- stays paired with the camera, from the copy of its pairing, and connects as usual;
- says so: the card page shows a note, `p` shows `storage: DAMAGED`, `status` has
  `storage=damaged`;
- saves no changes: pairing, forgetting the camera and the settings are refused until it is
  repaired.

To repair it, send `storage format yes` in a [serial terminal](../README.md#with-a-serial-terminal); the card
page never sends it. The card erases its file system, restarts and takes the pairing back from its
copy. The time zone, the sound, the clock and location update settings and the battery log are
lost: set them again on the card page.

## Known limitations

- The battery percent is estimated from the voltage. With the USB cable plugged in the voltage
  reads higher, so the percent shown on the card page is only about right.
- Where summer time is not one hour ahead (Australia/Lord_Howe, Antarctica/Troll), the camera shows
  the right local time, but no summer-time mark.
- The time zone rules are built into the firmware (tz database 2026e since 1.2.0). When a country
  changes its rules later, the card needs a newer firmware for them.
- After 1.4.0, an older firmware (gpxcam 1.3 or older, Meshtastic up to 2.7.15) restarts once by
  itself about 30 s after its first start: the watchdog of 1.4.0 keeps running until then. This
  does no harm.

## Back to Meshtastic and back

How to install Meshtastic again: [README](../README.md#back-to-meshtastic).
Whether Meshtastic keeps its old settings has not been checked – be ready to set it up again.
After gpxcam 1.4.0, Meshtastic up to 2.7.15 (the factory version) restarts once by itself about
30 s after its first start, as any older firmware does (see [Known limitations](#known-limitations)).
Meshtastic 2.7.16 and later feed the same watchdog and should not restart; this has not been
tested.

Coming back to gpxcam (1.4.0 or newer) later:
- The copy of the pairing lives on two flash pages of its own (0xEB000 and 0xEC000), which
  flashing never writes. Meshtastic up to 2.7 does not use them, so the copy is still there.
  Meshtastic 2.8 and later keep their own node cache on these pages and overwrite the copy; gpxcam
  then takes the pages back and writes the copy again from the pairing file at its next start.
- Whether Meshtastic leaves gpxcam's files (the pairing file, the settings) in the file system
  they share has not been checked. Without the pairing file and its copy, pair the card again.

## Serial console (service)

```
pio device monitor
```

| Command | What it does |
|---|---|
| `v` | version (with the git commit) and the reason of the last reset |
| `p` | state: file system, radio, GPS, Bluetooth (transmit power), pairing (card id, camera, the copy's two pages), time zone, sound, location updates, link, watchdog, USB, battery |
| `status` | the same in short `key=value` lines without the card id and the camera, for the card page |
| `log` | the last 64 lines of the log, also from times without USB |
| `off` | power off after a 15 s countdown (refused with USB; a short press cancels) |
| `dfu` | restart into the UF2 bootloader for flashing |
| `pair` / `pair off` | start / cancel pairing (as click + hold); the card finds out whether the camera is a newer or an older one |
| `pair older` | pairing for an older camera that `pair` does not recognise; it waits up to 5 minutes for the Wi-Fi step of [the desktop app](../README.md#the-desktop-app) |
| `pair prepare` | switch the GPS on for 10 minutes, so that pairing finds the time ready (the card page sends it on connecting to a card that is not paired) |
| `unpair` | forget the paired camera (the card id stays: to pair with that camera again, delete "T1000-E" in its Bluetooth menu too) |
| `tz` / `tz NAME` / `tz RULE` | show / set the time zone, see [Set the time zone](../README.md#set-the-time-zone) |
| `clock` / `clock on` / `clock off` | show / set whether the card sets the camera's clock, see [Leave the camera's clock alone](#leave-the-cameras-clock-alone) |
| `sound` / `sound loud` / `sound quiet` / `sound off` | show / set how loud the card is, see [Sounds and LED](#sounds-and-led); a new Loud or Quiet plays two short beeps |
| `updates` / `updates normal` / `updates frequent` | show / set how often the card sends the position, see [Normal use](#normal-use) |
| `link on` / `link off` | work with the paired camera; on by itself after start when a camera is paired |
| `bat log on` / `bat log off` | battery log in flash: an entry every 10 min on the battery, written six at a time, up to 1024 |
| `bat log` / `bat log clear` | show / delete the battery log |
| `diag bat` | battery voltage at different settling times of the divider (about 4 s) |
| `diag snd N` | all sounds at N % PWM duty (1–50), whatever the setting, to choose the volume by ear |
| `diag hang` | test of the watchdog: holds the main loop; the card restarts after 30 s |
| `diag scan` | 30 s of the Bluetooth devices near the card (stronger than −65 dBm), with the full advertisement of Panasonic devices (*Scan for cameras* on the card page) |
| `diag clk` / `diag clk S` | how much of 1 s the 64 MHz crystal runs, now or in S seconds (kept for `log`, so it can be measured on the battery) |
| `storage format yes` | erase the file system and restart, for a damaged one, see [If the card forgot its settings](#if-the-card-forgot-its-settings); the pairing comes back from its copy, the other settings and the battery log are lost |

**Privacy:** the log shows the camera's serial number and Bluetooth address and the card's id –
do not publish it. GPS positions are never logged.

Measuring the current with the battery log: `docs/HARDWARE.md`, section "Measuring the current".
