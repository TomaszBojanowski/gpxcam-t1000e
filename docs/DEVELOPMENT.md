# gpxcam-t1000e: development

Building the firmware, flashing it safely and the tests on the PC. Installing a release needs none of this:
see [Install the release](../README.md#install-the-release-windows-macos-linux).

## Build from source (Linux)

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

The time zone table (`src/tz_table_data.inc`), its test vectors and the zone list of the card
page (`docs/zones.js`) are generated from the tz database of IANA in a pinned version (archives checked by their
SHA-256). After a new tz database release, update the version in the script (its first lines say
how) and run it; it needs Python 3.9+, `cc`, `make` and the network on the first run:

```
python3 tools/gen_tz_table.py            # --check: only tells whether the files are up to date
```

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
- Back to Meshtastic: see [the README](../README.md#back-to-meshtastic).

## Tests on the PC

```
pio test -e native
pio test -e native_sessions
node --test test/web/card_page.test.mjs
```

The modules without hardware dependencies – NMEA parser, LUMIX protocol (advertisement, byte
formats, location), camera list, UTC calendar, time zone table, POSIX time zone rules (against the
full tz database at every change of 2026–2031), time zone file, pairing record and its copy,
settings file, the camera handles kept between connections, link, battery and sound rules, the
battery log's batches and the stable-USB rule, the lines of `status`, the console's commands and
help (against the table in
[docs/GUIDE.md](GUIDE.md#serial-console-service) and the commands of the card page) – are tested with made-up data
and the vectors from `docs/PROTOCOL.md`. The second command runs the state machines – the
introduction in both dialects, pairing and the link – against fakes of the hardware and a test
camera that can disconnect, stay silent or refuse at any step. The third (Node.js, tested with
version 22) tests the card page with a simulated card.
