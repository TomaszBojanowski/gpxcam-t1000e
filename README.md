# gpxcam-t1000e

Firmware for the Seeed SenseCAP **T1000-E** tracker card that gives a Panasonic **LUMIX S5II**
the position from the card's own GPS over Bluetooth LE – no phone needed. The camera writes the
position into the EXIF data of every photo. It speaks the same protocol as the gpxcam app for
Android, which this firmware replaces.

Status: all milestones done (M0–M6) – pairing, normal work with the camera and power saving
are tested on the card with an S5II.

## Hardware

- **Seeed SenseCAP T1000-E** (nRF52840, Airoha AG3335 GPS, Semtech LR1110 radio – kept asleep,
  buzzer, button, green LED, 700 mAh battery), with its factory UF2 bootloader and SoftDevice
  S140 7.3.0 (as shipped with Meshtastic).
- **Panasonic LUMIX S5II** (DC-S5M2). Other LUMIX cameras with the same Bluetooth protocol may
  work, but they are not tested.

## Installation

Linux with:
- PlatformIO: `uv tool install platformio`
- your user in the `dialout` group (access to `/dev/ttyACM0`)
- a desktop that mounts USB drives automatically under `/run/media/$USER` (GNOME, KDE)

Build:

```
pio run
```

The result is `.pio/build/t1000e/firmware.uf2`. After every build `tools/uf2check.py` checks it:
UF2 family 0xADA52840, first block at 0x27000, all blocks inside 0x27000–0xEA000. A file that
fails the check is deleted.

Flash (UF2 only):

```
tools/flash.sh          # or: pio run -t upload
```

The script checks the file, says what it will do and asks before doing it. Then it switches the
card into the UF2 bootloader (the `dfu` command), checks the Board-ID and the SoftDevice version
on the T1000-E drive and copies the file. If the card does not run gpxcam yet: hold the button
and plug the cable in twice quickly – the LED lights up and the T1000-E drive appears.

### Flashing safety

- Only UF2 files through the factory bootloader. The bootloader and SoftDevice S140 7.3.0 are
  never touched: no SWD, no mass erase, no nrfjprog.
- `uf2check.py` rejects anything outside the application area, so a wrong file cannot overwrite
  the SoftDevice or the bootloader.
- The data area (0xEA000–0xF4000, with the pairing file) is not written by flashing: the card
  keeps its pairing.
- Do not open the serial port at 1200 baud: that starts the bootloader without its drive.
- Back to Meshtastic: <https://flasher.meshtastic.org> (device: Seeed SenseCAP T1000-E).

## User guide

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
2. On the camera start pairing in its Bluetooth menu (the screen that waits for a new device).
3. On the card: a short click, and within 0.5 s press again and hold for 3 s – two short high
   beeps.
4. The card waits up to 120 s for GPS time and looks for the camera for up to 60 s. On success
   the camera shows "Pairing completed" and lists "T1000-E"; the card plays a rising melody.
   If it gives up: two clicks.

A short press cancels pairing. The card only connects to a camera that is in pairing mode, and
it pairs with one camera at a time: a new pairing replaces the old one.

### Normal use

Switch the card on and keep it near the camera. When the camera is on, the card connects by
itself (two rising beeps) and the camera shows its Bluetooth and GPS icons. The card sends the
position at once and then every 25 s – earlier when it moves more than 20 m. Without a GPS fix
the connection stays, but no position is sent.

When the camera is switched off, the card notices it (two falling beeps) and waits:
- for the first 10 minutes it keeps the GPS ready, so the camera gets a position about 10 s
  after it is switched on again;
- after that, and when the camera is out of sight for 5 minutes, the card saves power: it scans
  less often and the GPS sleeps. The camera then gets a position about 15–30 s after it
  appears again.

The card never wakes a camera that is switched off.

### Sounds and LED

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

| LED (green) | Meaning |
|---|---|
| a short flash every second | on, not working with a camera (no camera paired, or `link off`) |
| two short flashes every 2 s | looking for the camera |
| a short flash every 4 s | connected to the camera |
| fast blinking | `off` countdown |

## Serial console (service)

```
pio device monitor
```

| Command | What it does |
|---|---|
| `v` | version (with the git commit) and the reason of the last reset |
| `p` | state: radio, GPS, Bluetooth, pairing (card id, camera), link, battery |
| `log` | the last 32 events, also from times without USB |
| `off` | power off after a 15 s countdown (refused with USB; a short press cancels) |
| `dfu` | restart into the UF2 bootloader for flashing |
| `pair` / `pair off` | start / cancel pairing (as click + hold) |
| `unpair` | forget the paired camera (the card id stays) |
| `link on` / `link off` | work with the paired camera; on by itself after start when a camera is paired |
| `bat log on` / `bat log off` | battery log in flash: an entry every 10 min on the battery, up to 1024 |
| `bat log` / `bat log clear` | show / delete the battery log |
| `diag bat` | battery voltage at different settling times of the divider (about 4 s) |
| `diag snd N` | all sounds at N % PWM duty (1–50), to choose the volume by ear |

**Privacy:** the log shows the camera's serial number and Bluetooth address and the card's id –
do not publish it. GPS positions are never logged.

Measuring the current with the battery log: `docs/HARDWARE.md`, section "Measuring the current".

## Tests on the PC

```
pio test -e native
```

The modules without hardware dependencies – NMEA parser, LUMIX protocol (advertisement, byte
formats, location), camera list, UTC calendar with the EU summer-time rule, pairing record,
link and battery rules – are tested with made-up data and the vectors from `docs/PROTOCOL.md`.

## Documentation

- [docs/HARDWARE.md](docs/HARDWARE.md) – the card: bootloader, SoftDevice, flash map, DFU, pins,
  measurements and findings of every milestone, differences from Meshtastic.
- [docs/PROTOCOL.md](docs/PROTOCOL.md) – the Bluetooth LE protocol of LUMIX cameras, the
  connection parameters the S5II works with, and test vectors.

## Sources

- Pins, board file and linker script: [Meshtastic](https://github.com/meshtastic/firmware)
  (GPL-3.0), completed from Seeed's T1000-E SDK – details in `docs/HARDWARE.md` and in the code.
- Arduino core: Meshtastic's fork of the Adafruit nRF52 core, pinned in `platformio.ini`.
- Protocol: the gpxcam app for Android.

## License

GNU GPL v3.0 – see [LICENSE](LICENSE).
