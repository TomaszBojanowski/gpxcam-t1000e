# Hardware: Seeed SenseCAP T1000-E

Findings about the card that the firmware relies on. Every value has a source; "tested on the card"
means a reading from our own unit. The test results name some development commands (`gps on/off`,
`gps test`, `gps raw`, `scan on/off`) that were removed in 1.0.0.
M0–M6 are the milestones of the first version (October 2026), each tested on the card in steps; later
findings name the version that brought them.

## The card

### Components

- MCU: Nordic nRF52840.
- GPS: Airoha AG3335 (UART 115200).
- LoRa radio: Semtech LR1110 – not used, put to sleep at start-up.
- 700 mAh battery, button, two LEDs (green, red), buzzer.
- Temperature (NTC) and light sensors, QMA6100P accelerometer – not used, switched off.

### Factory software (tested on the card 2026-10-19, `INFO_UF2.TXT`)

- Bootloader: `UF2 Bootloader 0.9.1-5-g488711a`, model "Seeed T1000-E for Meshtastic",
  Board-ID `nRF52840-T1000-E-v1`, date Jul 15 2024.
- SoftDevice: **S140 7.3.0**.
- Factory firmware: Meshtastic 2.7.15.567b8ea.

The bootloader is exactly the upstream Adafruit nRF52 Bootloader, commit `488711a` (July 2024, PR #336,
which added the board `src/boards/t1000_e/`) – the version `0.9.1-5-g488711a` in `INFO_UF2.TXT` is 5 commits after the tag 0.9.1.
The bootloader behaviour described below comes from the code of this version (`src/main.c`, `src/boards/boards.c`,
`src/usb/uf2/ghostfat.c`, `Makefile`) and matches what we see on the card.

### Pins

nRF numbering: P0.x = x, P1.x = 32 + x. Base: Meshtastic's `variant.h`. Additions from Seeed's SDK
(`Seeed-Tracker-T1000-E-for-LoRaWAN-dev-board`, `smtc_hal/inc/smtc_hal_config.h`).

| Function | Pin | State after the firmware starts |
|---|---|---|
| Green LED (lit at 1 – tested on the card in M0) | P0.24 | blinks |
| Red LED (Seeed only) | P0.03 | 0 |
| Button (active high, no resistor on the board) | P0.06 | disconnected (M1: pull-down) |
| Buzzer: power / PWM | P1.05 / P0.25 | 0 / 0 |
| GPS: power | P1.11 | 0 (1 while the GPS is on) |
| GPS: RTC backup (VRTC) | P0.08 | 1 permanently (from M2) |
| GPS: reset (active 1) / RESETB / SLEEP_INT / RTC_INT | P1.15 / P1.14 / P1.12 / P0.15 | 0 / input with pull-up / 1 / 0 (as Seeed) |
| GPS: UART 115200 (nRF TX / nRF RX) | P0.13 / P0.14 | disconnected; UARTE while the GPS is on |
| Accelerometer: power / INT | P1.07 / P1.02 | 0 / disconnected |
| I2C SDA / SCL | P0.26 / P0.27 | disconnected |
| Power for the temperature and light sensors and for the dividers of the battery and P0.04 | P1.06 | 0 |
| LR1110: NRESET / NSS / BUSY / IRQ | P1.10 / P0.12 / P0.07 / P1.01 | 1 / 1 / input / disconnected |
| LR1110: SCK / MOSI / MISO | P0.11 / P1.09 / P1.08 | 0 / 0 / disconnected |
| Battery measurement (divider ×2, powered from P1.06) | P0.02 (AIN0) | disconnected |
| External power detection / charging (active 0) / charged | P0.05 (AIN3) / P1.03 / P1.04 | disconnected |
| QSPI flash power (Seeed only) | P1.13 | not touched |
| Voltage measurement of the P1.06 line (divider ≈ 1:2) | P0.04 (AIN2) | not touched |

"Disconnected" = the nRF52 reset state: input with the buffer disconnected, no pull – the lowest
leakage, and a switched-off chip gets no power through its pins.

The LR1110 has no power switch. The firmware resets it and sends the SetSleep command
`01 1B 00 00 00 00 00` (cold start, no wake-up from the RTC) – according to the Semtech lr11xx driver in Seeed's SDK.
In sleep the LR1110 holds BUSY high; the firmware checks this and writes the result to the log.

### Differences from Meshtastic

- **No chips switched on at start-up.** In `initVariant()` Meshtastic switches on the sensors, the accelerometer,
  the buzzer and the GPS backup. Here `initVariant()` is empty, and `board::init()` switches everything off.
- **P0.08 (GPS RTC backup):** from M2 always 1, also in System OFF – the GPS keeps its data between uses
  (hot start). In M0–M1 it was 0. The backup current is not measured yet (it needs a meter, see "Measuring the current").
- **GPS control lines** with the GPS off and the backup on as in Seeed's SDK (`ag3335.c`: `gnss_init()`,
  `gnss_scan_stop()`): RESET 0, SLEEP_INT 1, RTC_INT 0, RESETB input with pull-up. The UART pins stay
  disconnected while the GPS is off (TX high would power the switched-off chip), against table 135 of the nRF52840
  specification, which assumes a powered device on the other side.
- **No `$PAIR513`:** Meshtastic saves the configuration in the GPS flash; we configure the GPS in RAM only.
- **P0.04:** Meshtastic – `T1000X_SENSOR_EN_PIN` (output, set to 1). Seeed's Arduino variant – `A1`
  "VCC voltage detect", Seeed's SDK – a `SENSE_ADC_VCC` definition (AIN2), but unused: the SDK measures "VCC"
  with the ADC's internal VDD channel (`smtc_hal_adc.c`: `VCC_ADC_CHANNEL = NRF_SAADC_INPUT_VDD`).
  The measurement on the card (M1) confirms Seeed's version: P0.04 is a measurement input (≈ VDD/2 with P1.06 on),
  not an output. We do not drive it.
- **P0.03:** the second (red) LED according to Seeed; Meshtastic does not define it.
- **P1.13:** QSPI flash power according to Seeed; nobody drives it, and neither do we.
- **USB:** the firmware identifies as 2886:8057 (Seeed), not 239a:8029 (Adafruit Feather) like Meshtastic.
- **Flashing:** no "1200 bps touch" and no nrfutil – UF2 only (`tools/flash.sh`).
- **Application limit:** up to 0xEA000 instead of 0xED000 (see the flash map).
- **BLE:** central role only (`Bluefruit.begin(0, 1)`), the card does not advertise; Meshtastic is a peripheral.

### Read on the card with `diag`

Checks of the board with the `diag` commands of M1, step 1 (the battery: "Battery").

- **Button P0.06:** active high, the high state lasts the whole time the button is pressed (a 4 s hold
  = 19 consecutive samples every 0.2 s). The released pin "floats" (without a pull the reading depends on the previous
  setting), so there is no resistor on the board – an internal pull-down is needed, as in Meshtastic
  (`INPUT_PULLDOWN_SENSE`). Bootloader `488711a` sets a pull-up and treats the low state as pressed,
  and with a pull-up the pin is always high – **the bootloader never treats the button as a DFU request**.
  Neither a short press nor a 4 s hold resets the processor.
- **P0.04:** 0 mV with P1.06 = 0, about 1502 mV (≈ VDD/2) with P1.06 = 1 – a measurement input, see the differences.
- **nRF supply:** VDD = 3.0 V, regulator in normal mode (supplied through VDD, not VDDH).
- **RESET pin:** P0.18 enabled as reset (`UICR.PSELRESET` = 0x00000012).
- **USB cable:** plugging it in **does not reset** the processor (it kept running on the battery, reset reason unchanged);
  the nRF sees VBUS at once (`USBREGSTATUS.VBUSDETECT`).
- **Charger** (both signals active low, according to Meshtastic): while charging P1.03 = 0, P1.04 = 1;
  when charged (4.18 V) P1.04 = 0 – the firmware then shows `USB, charged`. Without the cable the charger does not work,
  so the firmware reads these lines only while VBUS is present.

### The bootloader and the LED

The bootloader's board file (`src/boards/t1000_e/board.h` @ 488711a) has `LED_STATE_ON 0`, but the LED on the card lights
on a high level. At start-up the bootloader sets the LED to "off" (`led_pwm_init()`: `1 - LED_STATE_ON`), which
in practice switches it on – at every start, also after a wake-up from System OFF, the LED flashes briefly before the firmware
takes over the pins. Harmless; we do not change the bootloader.

### The buzzer

- **Volume** is the PWM duty on the buzzer (P0.25, PWM3; Meshtastic's `tone()` always uses 50 %, shorter pulses
  are quieter), `src/sound_rules.h`: **loud** 10 % (as up to 1.2, chosen by ear in M1), **quiet** 2 % (chosen by
  ear on 2026-10-23 against 10 and 1 %; 5 % sounded hardly quieter than 10 %), **off**: only the answers to a held
  button at 2 % – the start and power-off melodies, pairing started, and two clicks when switching off or pairing
  is refused – the rest is silent. `diag snd N` ignores the setting.
- **Console:** `sound` shows it, `sound loud|quiet|off` saves it and plays two short rising beeps at the new volume
  (nothing with off). `p` and the start log show it, `status` gives `sound=`. The card reads the setting before the
  start melody, a few ms after `bluetooth::begin()`.

## Flash, UF2 and DFU

### Flash map

| Address | Content | Notes |
|---|---|---|
| 0x00000–0x01000 | MBR | never touched |
| 0x01000–0x27000 | SoftDevice S140 7.3.0 | never touched |
| 0x27000–0xEA000 | application | `variants/t1000-e/nrf52840_s140_v7.ld` |
| 0xEA000–0xEB000 | application data: free | the bootloader does not overwrite 0xEA000–0xF4000 (40 KB) |
| 0xEB000–0xED000 | application data: the copy of the pairing, two pages (1.4.0) | `src/pairing_copy.h`; the first copy goes to 0xEC000 |
| 0xED000–0xF4000 | application data: InternalFS (littlefs) | from M3 – Bluefruit; mounted by `src/storage.h` from 1.4.0 |
| 0xF4000–0x100000 | bootloader, its configuration and settings (0xFF000) | never touched |

Sources:
- Application from 0x27000 and RAM from 0x20006000: Meshtastic's linker script for S140 7.x
  (`src/platform/nrf52/nrf52840_s140_v7.ld`).
- End of the application area 0xEA000: for the nRF52840 the bootloader reserves `DFU_APP_DATA_RESERVED = 10*4096`
  (Makefile), so `USER_FLASH_END = 0xF4000 − 0xA000`. This matches the end of `CURRENT.UF2` on the card.
  Meshtastic's linker script allows code up to 0xED000; we narrow the limit to 0xEA000.
- InternalFS: 0xED000, 7 pages of 4 KB (`libraries/InternalFileSytem/src/InternalFileSystem.cpp` in the core).
- Application data 0xEA000–0xF3FFF is safe from flashing: for a UF2 with the application the bootloader writes only
  blocks below 0xEA000 and rejects the others (`in_app_space()` in `ghostfat.c`); a bootloader update is staged at
  0xE0000–0xE9FFF, below it. Nothing in the core, its libraries or this firmware uses 0xEA000–0xECFFF except the
  pairing copy. Meshtastic 2.8 and later keep their node cache, the `WarmNodeStore` ring, on 0xEA000–0xECFFF
  (`src/mesh/WarmNodeStore.h`, from commit 79a7dcc46); Meshtastic up to 2.7 does not use these pages. On the test card
  0xEB000 and 0xEC000 were erased (all 0xFF) before the first copy.
- Bootloader from 0xF4000, settings 0xFF000: the bootloader's `linker/nrf52840.ld`.

### UF2

- We flash the firmware with family **0xADA52840**. The bootloader also accepts the board family 0x28860057.
- **The bootloader writes every block in the range 0x1000–0xEA000, also in the SoftDevice area.**
  It skips blocks below 0x1000 (MBR) and rejects blocks above 0xEA000 (`write_block()` in `ghostfat.c`).
  That is why `tools/uf2check.py` lets through only blocks in 0x27000–0xEA000 and requires the first one at 0x27000.
- `CURRENT.UF2` from the bootloader drive has family 0x28860057 and covers 0x1000–0xEA000, so it **contains the SoftDevice**.
  A copy of Meshtastic in this format – for emergencies only. The normal way back: https://flasher.meshtastic.org.

### USB

| Card state | VID:PID | Name |
|---|---|---|
| gpxcam firmware | 2886:8057 | T1000-E gpxcam (PID as in Seeed's `boards.txt` for the T1000-E) |
| UF2 bootloader | 2886:0057 | T1000-E |
| Meshtastic 2.7.15 | 239a:8029 | T1000-E-BOOT |

- **The console writes only while the computer holds DTR** (`src/log.cpp`: without it `Serial` is false and lines
  are counted as dropped). Checked from Python on 2026-10-22: with DTR off the card received `v` and sent nothing.
- **Linux and Chromium (the time zone page, 1.2.0):** programs that use pyserial (`pio device monitor`, the scripts
  of this project) leave the port's terminal settings with `VMIN=0, VTIME=0`, and the kernel keeps them for the next
  program (`cdc_acm` does not reset them). Chromium's Web Serial does not set them and takes the first empty read
  for a lost device: "The device has been lost". Unplugging the card and plugging it in again resets them
  (`stty -F /dev/ttyACM0 -a`: `min = 0` before, `min = 1` after; 2026-10-22).

### Entering flashing mode (DFU)

- **By hand:** hold the button and plug the cable in twice quickly. The LED lights green, the T1000-E drive appears.
  This always works, whatever firmware is on the card. Tested on the card (M0).
- **The `dfu` command** (gpxcam firmware, `tools/dfu.sh`): `enterUf2Dfu()` from the core disables the SoftDevice, sets
  GPREGRET = 0x57 and resets. The bootloader starts with its drive. Tested on the card (M0);
  `INFO_UF2.TXT` after this operation is identical to the one before the first flashing.
- **"1200 bps touch"** (opening and closing the port at 1200 baud): the core calls `enterSerialDfu()`
  (GPREGRET = 0x4E). The bootloader starts **without its drive**, with a serial port only.
  The way out: manual DFU and flashing a UF2 file. That is why we never start the port monitor at 1200 baud.
- After an entry through GPREGRET (the command or 1200 bps) the bootloader waits for a file as long as USB is connected
  (`main.c`: the 3 s limit applies only when USB does not connect).
- **Hold the button and plug the cable in once:** the processor restarts (`RESETREAS` = `RESETPIN`) and the firmware
  starts normally. This restarted the hung card of 2026-10-24 (see "Reliability").

## Power and battery

### Waking up from System OFF (nRF52840)

According to the descriptions of the `RESETREAS` bits in the Nordic MDK header (`nrf52840_bitfields.h`), the processor
wakes up from System OFF on: GPIO DETECT, e.g. the button (bit 16 `OFF`), **VBUS – plugging in the USB cable** (bit 20 `VBUS`),
LPCOMP (bit 17), an NFC field (bit 19) and a pin reset (bit 0). A wake-up always ends in a reset, so the bootloader
(including manual DFU) runs after every wake-up. The wake-up on VBUS cannot be disabled (Nordic DevZone). If the GPIO DETECT
condition is met at the moment of entering System OFF, the processor wakes up at once – before switching off, the firmware
has to wait for the button to be released. VBUS wakes the processor only when the voltage **rises** during System OFF
(PS 5.3.2), so the firmware does not enter System OFF with USB connected.

Tested on the card (M1, step 4): switching off on the battery works (after the cable is unplugged, the nRF no longer sees VBUS);
plugging in the cable wakes the processor (`RESETREAS` = 0x00100000, `VBUS`); the button wakes the processor
(`RESETREAS` = 0x00010000, `OFF`); a short press sends the card back to sleep without a sound;
a 3 s hold switches the card off, and with USB connected switching off is refused. The pin configuration and GPREGRET survive System OFF (PS 6.9, 5.3.6.8, 5.3.7.16),
but before starting the firmware the bootloader puts all pins back into their reset state (`board_teardown()`
in `src/boards/boards.c`) – after every start the pins must be set again and the LR1110 put to sleep.

### Start-up time and the hold to switch on

Part of the time from the button press is not counted by `millis()`. The bootloader exits at once
(`DFU_MAGIC_SKIP` in GPREGRET skips the 500 ms delay, the bootloader's `src/main.c`), but the Arduino core only
starts the 32 kHz crystal (`cores/nRF5/wiring.c`, without waiting), and the system counter starts only when the crystal
is running (usually a few hundred ms). Measurement (release time stored in GPREGRET2, M2; holds timed by feel):
"about 1 s" ended after 500 ms of firmware time, "about 3 s" after 1553 ms. Switch-on threshold: 400 ms of firmware time.
Tested with the 400 ms threshold: a quick tap – released after 170 ms, the card goes back to sleep; a hold of
about 1 s – released after 836 ms, the card starts.

### Battery

- **Battery:** the ×2 divider on AIN0 is powered from P1.06. P1.06 = 0: 0 mV; P1.06 = 1: 2088 mV on the pin,
  i.e. 4176 mV of battery (with the cable connected, while charging); 0 again: 1 mV. We switch P1.06 on only
  for the measurement, like Seeed's SDK (`sensor_bat_sample()`: AIN0, gain 1/5, internal reference,
  12 bit, 40 µs, result ×2). Meshtastic keeps P1.06 on all the time. The divider settles practically
  at once (M1, step 3, `diag bat` with the cable: 4179 mV without waiting, 4181 mV after 10 ms, 4185 mV after 50 ms);
  the firmware waits 10 ms to be safe.
- **Battery without the cable** (M1, log buffer): full battery, 10 minutes on the battery – 4174–4184 mV, spread
  of single measurements about ±5 mV; no drop visible in this time. For the current see "Measuring the current".
- **Battery percent** (`battery_rules::percentFromMillivolts`): Meshtastic's estimate for the T1000-E – its
  discharge curve `OCV_ARRAY` (4190, 4042, 3957, 3885, 3820, 3776, 3746, 3725, 3696, 3644, 3100 mV for 100 … 0 %,
  `variants/nrf52840/tracker-t1000-e/variant.h` at 6d41e279f1f5) with straight lines between the points – scaled
  so that 3.40 V, where the card powers off (5 % on that curve), is 0 %. Rough: under load and while charging the
  voltage is not the resting voltage of the curve.
- **DC/DC:** we do not enable it – no confirmation that the board has the inductor for REG1; Meshtastic does not enable it either.
- **Low voltage:** below 3.40 V on the battery the card writes an entry to the battery log, plays two clicks and enters
  System OFF – at start-up at once, during work after two consecutive measurements 60 s apart (a short drop under load
  does not cause it). Never with USB connected (`battery_rules::powerOffForLowBattery`, test on the PC): a discharged
  card starts normally when the cable is plugged in, and charges.

### Energy

- **Search modes** (`link_policy::searchMode`), only while the card is not connected:
  - normal – scan 50 ms every 100 ms, GPS on;
  - rare – the camera not seen for 5 min: scan 100 ms every 2 s, GPS in Backup;
  - standby – the camera's last report is `06`, the card only waits for `00`: for the first 20 min (a break between
    photos) scan 100 ms every 1 s and the GPS stays on with a fix; after that scan 100 ms every 10 s, GPS in Backup.
    Up to 1.0.2: 10 min and every 5 s.

  A report in state `00` restores normal mode at once (the GPS is switched on only in this mode). A connection attempt only at
  −85 dBm or more.
- **Tested on the card (M6, step 2):** the first version (GPS in Backup after a minute of standby, scan every 10 s) – from switching on
  the camera to the connection about 25 s (scan + GPS hot start 5 s + connection); hence the two-stage standby: a break of
  2 min – about 8 s from switching on the camera, location 2 s after detection; out of range – disconnection `HCI 0x08`, after 5 min rare mode;
  after coming back, connection about 11 s after detection (GPS hot start 9 s). In standby the camera is sometimes invisible for 30 s
  (sparse advertising in standby and sparse scanning) – the mode stays, because the card remembers the last state.
- **Faster reconnection (1.1.0):** the first standby stage lasts 20 min with a scan every 1 s (above), and the card
  keeps the camera's handles in RAM (`src/handle_cache.cpp`, `docs/PROTOCOL.md`): from the connection to the first
  location about 1.1 s instead of 3.7 s. Measured on 2026-10-22, 3 cycles each, short breaks (under 20 min),
  from switching the camera on to the first location: 13.7 s (1.0.2) → 12.8 s (scan every 1 s) → 10.4 s (and kept
  handles). About 8–9 s of it is the camera itself: it starts advertising only then; from the card seeing it to the
  connection takes about 0.3 s. After a longer break the GPS has to wake up from Backup first: about 18 s after
  6.5 min in Backup, 39 s after 5.5 h, 87 s after 6.5 h. The longer first stage costs, as an estimate (not
  measured), about +6–8 mA (GPS on, scan every 1 s) for up to 10 more minutes per break: about 1–1.3 mAh.
- **The 64 MHz crystal after unplugging** (`diag clk 120`, then unplugging): running 0 % of the time with the radio idle,
  100 % on USB – nothing keeps it running on the battery, as in 1.3.0.

### Measuring the current

**Without a meter – a discharge test with the battery log** (average current during work, in the mA range):
1. Charge the card fully (`p` shows `charged`).
2. `bat log clear`, then `bat log on`.
3. Set up one scenario, e.g. the camera on and connected; the camera in standby overnight; the camera far away (rare mode).
4. Unplug the cable. The first entry is made at once, the next ones every 10 min (uptime, voltage, `link` state), up to 1024 entries.
   From 1.4.0 they wait in RAM and reach the flash six at a time (see "The battery log").
5. Leave the card until it switches off at 3.40 V (last entry: `low battery, powered off`), or stop the test by plugging in
   the cable – logging pauses on USB.
6. Plug in the cable, wait 10 s (1.4.0 writes the entries still in RAM then), `bat log`. Average current ≈ 700 mAh × used fraction / number of hours. Down to 3.40 V almost
   the whole capacity is used – this is the most accurate variant; from the voltage drop alone in a shorter test the result is rough
   (±20–30 %), because the voltage of a Li-ion cell is not linear in its charge.

**Result during work (1.0.1, 2026-10-20):** the camera on and connected all the time (GPS on, a location every 25 s,
the old 20 ms interval): 4179 → 4070 mV in 6.7 h. With the T1000-E discharge curve from Meshtastic (`OCV_ARRAY` in its
`variant.h`) that is about 7 % of the capacity, so about 7–8 mA on average – about 3.5–4 days of continuous work from a
full battery to 3.40 V. Rough: near full charge the voltage says little about the charge (with a generic Li-ion curve
the same drop would mean up to 14 mA).

The idle current (System OFF, in the µA range) cannot be measured this way: the voltage drop is a few mV per week.

**With equipment:**
- Nordic Power Profiler Kit II (PPK2) – powers the card instead of the battery (3.7–4.2 V) and measures 200 nA – 1 A with a graph:
  idle, scan, GPS and connection can be seen separately. It has to be connected in place of the battery (opening the case).
- A multimeter with a µA/mA range in series with the battery – good for idle; during work the reading jumps (the radio works in pulses).
- A USB tester will not do – it measures charging, not the draw from the battery.

## GPS

Tested on the card (M2, step 1, `gps on` near a window, cold start, nothing sent to the GPS):
- The first byte 220 ms after power-on; UART 115200 8N1 correct (apart from a few bytes at start-up).
- At start-up the chip prints by itself: ROM text (`F1: …`, `Leaving the BROM`), `$PAIR001,002,1`, `$PAIR001,021,0`,
  the version `$PAIR021,AG3335M_V2.6.0.AG3335_20221230,…` (longer than 100 characters), `$PAIR011`, `$PAIR010`.
- After that only `$GNGGA` and `$GNRMC` every 1 s – a configuration saved in the module earlier by Meshtastic (`$PAIR513`).
- Without a fix the GPS already gives the UTC time, but the date is still the default one (`070180`) – the time must not
  be used without status `A` in RMC.
- After `gps off` no lines; RESETB high (reset finished).
- First fix near a window: 70 s after `gps on`, 8 satellites (practically a cold start); after 6 min 19 satellites,
  0 bad checksums in 769 lines.
- Configuration in RAM (step 2, Quectel LC26G/LC76G/LC86G V1.4 specification): after `$PAIR011,001` (about 0.7 s after
  power-on) `$PAIR063,-1` returns `$PAIR001,063,0` and ten `$PAIR063,<type>,<rate>`;
  `$PAIR067` returns `$PAIR001,067,0` and `$PAIR067,1,1,1,1,0,0` (standard `*hh` format). The module already had
  GGA+RMC and GPS+GLONASS+Galileo+BeiDou, so the firmware changed nothing. `$PAIR066` restarts the module,
  `$PAIR513` saves to flash – the firmware sends them only when something differs / never, respectively.
- To check in step 4: at every start (also after a wake-up from System OFF) the bootloader puts the pins back
  into their reset state, so P0.08 (GPS backup) is not driven for a few ms – a comparison of the time to fix will show
  whether the backup survives a wake-up. After two flashings of the firmware (in DFU mode the pins are
  in their reset state for seconds) the GPS clock went back to the default 1980-01-06 – the backup was lost; so flashing
  is not suitable for judging the hot start.
- Step 3: the RMC/GGA parser works on the card; every 10 s a status line without coordinates, 0 bad checksums.
- Step 4 (Backup mode, `gps test 2` near a window without USB): cold start – fix after 61 s (10 satellites); after 5 min
  in Backup mode a hot start – fix after 2 s (8 satellites). By default the module puts its processor to sleep between
  measurements and then loses commands: a single `$PAIR650,0` was sometimes ignored. Seeed's sequence works:
  `$PAIR382,1` (sleep lock) after every start, when switching off `$PAIR382,0`, and then `$PAIR650,0`
  repeated every 40 ms until the acknowledgement `$PAIR001,650,0` (near the window only the 23rd–24th copy, at the desk the 11th–14th)
  and 1.5 s of silence. After a wake-up from Backup mode the first byte comes after 100–120 ms instead of 190 ms.
- The backup survives switching the card off: GPS in Backup mode, card 5 min in System OFF, wake-up with the button
  (the bootloader does not drive P0.08 for a few ms), then `gps test 1` – fix after 3 s (12 satellites). The backup
  is lost only by flashing (DFU mode lasts seconds).

## Bluetooth

### SoftDevice and Bluefruit

- **Start:** `Bluefruit.begin(0, 1)` in `setup()` after the wake-up handling – a short tap at wake-up
  goes back to System OFF before the SoftDevice starts. Scanning runs only for the link or pairing.
- **POWER registers:** while the SoftDevice runs, reading is allowed, writing ends in a HardFault
  (S140 specification, "Peripheral runtime protection"). So System OFF goes through `sd_power_gpregret_clr/set`
  and `sd_power_system_off()`. Reading VBUS (`USBREGSTATUS`) stays direct.
- **DFU:** `enterUf2Dfu()` in the core (`wiring.c`, `reset_mcu()`) disables the SoftDevice before writing GPREGRET.
- **Interrupts:** the SoftDevice reserves priorities 0, 1 and 4 and the interrupts POWER_CLOCK, RADIO, RTC0, TIMER0, RNG,
  ECB, CCM_AAR, TEMP, NVMC and SWI5 (`nrf_nvic.h` of S140 7.3.0). The GPS UART is UARTE0 with priority 3 (`Uart.cpp`),
  USB – 2, the buzzer's PWM3 runs without interrupts – no conflict. The core receives from the UART 1 byte at a time (`RXD.MAXCNT = 1`),
  and the hardware buffers 4 bytes: at 115200 that is about 0.35 ms of tolerance for a delayed interrupt. Tested (M3, step 3):
  GPS and scanning together for 6 min – 754 NMEA lines, 0 bad checksums; 3 cut lines and 11 non-ASCII bytes are the GPS start-up
  (the same in 40 s without scanning).
- **File system:** `Bluefruit.begin()` always calls `bond_init()` – it mounts InternalFS (0xED000–0xF4000),
  and when that fails, it formats it; it creates the directories `/adafruit/bond_prph` and `/adafruit/bond_cntr`.
  From 1.4.0 `storage::begin()` mounts it first, so Bluefruit never formats it (see "The file system").
  When writing flash the library lights `LED_BUILTIN` – we use `-DLED_BUILTIN=-1`, as in Meshtastic.
- **LEDs:** `Bluefruit.autoConnLed(false)` – in the variant `LED_BLUE` is -1.
- **Tested on the card (M3, step 1):** log `ble: SoftDevice on`; `off` with the cable – refused after 15 s, the card keeps running;
  `dfu` (through `tools/flash.sh`) – bootloader drive, the firmware comes back; GPS – fix after 58 s (cold start after flashing),
  switching off into Backup mode confirmed; without the cable: switching off with the button → System OFF (start with `OFF-GPIO`),
  tap (released after 180 ms) → back to sleep, hold (1061 ms) → start with the SoftDevice.

### Scan

- **Scan (M3, step 3):** the reports from the SoftDevice are received by our own function in the BLE task (`Bluefruit.setEventCallback`),
  checked by `lumix::parseAdvert()`, the cameras go into a static queue (8 slots), and the scan is resumed
  by hand – `BLEScanner` does that by itself only with its own filters or with a callback that allocates
  memory for every report (`ada_callback`). Stopping also works while the scan is paused after a report (`ble_gap.h`).
- **Tested on the card (M3, step 3):** at the desk about 37 reports/s from all devices, only the camera in the log;
  the queue lost nothing (0 of about 15,600 reports); sounds during the scan; switching off with the button during the scan →
  System OFF and start with `OFF-GPIO`.
- **Scan watchdog (1.5.0-beta.3):** the result of `Scanner.resume()` after each report is checked, and a failure
  while the scan is on is counted (`resume failed` in the 10 s scan line and in the summary of `diag scan`). At the
  normal timing (50 ms of every 100 ms: pairing, `diag scan`, the link's normal search) a scan with no report for 5 s
  is stopped and started again (`restarts`, one log line per silence). At the slower timings of the search modes
  (100 ms every 1, 2 or 10 s) a silence of 5 s is normal, and with 10 s between windows the watchdog restarted the
  scan before each of them (1.5.0-beta.3 to beta.5); since 1.5.0 only a failed resume restarts it there (one
  log line per scan, then counted), as it would leave the scan paused and the card blind until the camera is seen.
  A failed resume while the loop restarts the scan (new timing, watchdog) is not counted. The test build's `diag scan
  stall` leaves out the next resume, as if it had failed.

### Connection

- **GATT without blocking Bluefruit functions:** `BLEClientCharacteristic::read()/write_resp()` wait only 100 ms
  (`BLE_GENERIC_TIMEOUT`, cannot be changed), create a semaphore on the heap at every call, and a late response
  would land in a stale buffer (`AdaMsg`). Connection, MTU, discovery of the service and characteristics, read and write
  with response go directly through the SoftDevice API (`src/camera_link.cpp`), with our own timeouts
  (`src/newer_session.cpp`). We do not register Bluefruit client objects, so its procedures ignore our responses.
  Bluefruit answers the camera's requests for us: PHY, packet length, connection parameters, the MTU from the camera.
- **MTU:** the SoftDevice is configured for 185 (`configCentralConn` before start; there is enough RAM). The S5II grants 57 –
  writes of up to 54 bytes; 32 are needed.
- **Connection parameters (1.0.2):** the card connects with `sd_ble_gap_connect()` itself, because Bluefruit's `BLECentral`
  sets only the interval, not the supervision timeout. It uses what Android uses with the S5II: 48.75 ms, latency 0, 5 s,
  and asks for 61-byte packets right after connecting. With Bluefruit's defaults (20 ms, 2 s, 27 bytes) the camera's
  Bluetooth dropped the link or hung after hours; with the new parameters: no failure in a 7-hour test. Android's 7.5 ms
  interval for the introduction made the camera stop answering the characteristic discovery when the card tried it, so the
  card does not use it: from the connection to the first location takes about 3.7 s instead of 1.5 s. Details:
  `docs/PROTOCOL.md`.

### Transmit power

- +8 dBm, the most of the nRF52840, instead of the SoftDevice's default 0 dBm: on 2026-10-24 the connection was lost
  when another person carried the card a few metres from the camera.
- Set once with `sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_SCAN_INIT, …)`: the initiator has the scanner's power
  and a connection inherits the initiator's (`ble_gap.h`). `Bluefruit.setTxPower()` sets only the advertiser's power
  (`BLEAdvertising.cpp`). `p` shows the power.
- Cost, an estimate: without DC/DC the radio draws 32.7 mA instead of 10.6 mA while it transmits (PS v1.1 6.20.15.2),
  about 0.1–0.2 ms of every 48.75 ms connection event: about 0.05–0.1 mA more, about 1 % of a connection.
- EU limits for 2400–2483.5 MHz wideband data transmission: 100 mW (20 dBm) e.i.r.p., and 10 mW/MHz for modulations
  other than frequency hopping (ERC/REC 70-03 annex 3, ETSI EN 300 328). +8 dBm from the chip stays within both while
  the antenna gain is at most 2 dBi; Seeed does not state the gain.
- The RSSI the card shows is the camera's signal, which the card's power does not change (at the desk −47 dBm with
  1.3.0, −51 dBm with +8 dBm). How much further the connection reaches has not been measured.

## Storage

### The file system

- **File system** (`src/storage.h`): mounted before Bluefruit with the base class's `Adafruit_LittleFS::begin()`, which
  never formats (`lfs_mount()` of littlefs 1.7 only reads). An area of all 0xFF (a new card) is formatted; anything else
  that does not mount is left as it is, Bluefruit gets a 2 KB littlefs in RAM for its bond directories, the card uses the
  default settings, and `storage format yes` erases the 7 pages and restarts. `status: storage=ok|damaged`.
- **A file system that mounts but is damaged (1.5.0):** littlefs writes 128-byte blocks, but the core's flash
  cache erases and programs the whole 4 KB page (`flash_cache.c`, `flash_cache_flush()`), so a reset in that moment
  (power, watchdog) can erase both halves of a directory, or blocks a file points to. `lfs_mount()` reads only the
  superblock, so such a file system mounts, and littlefs then stops the card in an assert (`LFS_ASSERT(block <
  block_count)`) at the first write that meets an erased pointer: a restart by the watchdog at every write, and at
  every start if `pairing.bin` was hit. After mounting, the card now reads every directory and every file's block
  list with `lfs_traverse()`, which passes each block number to a check before reading it; a block outside the area
  or a directory with a bad CRC makes the file system damaged, as one that does not mount (`p`: `has a damaged
  block`). `storage format yes` now works whatever the state (up to 1.5.0-beta.5 it was refused while the file system
  mounted), so a card damaged in a way no check finds can still be repaired; the pairing comes back from its copy.
  So "temporary file, rename" (`record_file.h`) protects a record from a cut-off write of that record's own blocks,
  not from the loss of the page around them. The test build's `diag storage break middle yes` erases the page of the
  file system, other than the first, with the most blocks in use, and restarts.
- **Tested on the card** with the test build (`pio run -e t1000e_test`, which adds `diag storage break yes`: it zeroes
  the start of both copies of littlefs's superblock, blocks 0 and 1): damaged → BLE on, the default settings, the pairing
  from the copy, connected to the camera, `pair` and `unpair` refused with nothing changed; `storage format yes` → the
  empty area formatted, `pairing.bin` restored from the copy, connected again. The copies alternated: 1 and 3 on
  0xEC000, 2 on 0xEB000. Erasing only the first page of the file system had not been enough: a young file system fits in
  it, and the area was then empty.

### The pairing file

`/gpxcam/pairing.bin` in the InternalFS file system (0xED000–0xF4000), 32 bytes (`src/pairing_record.h`):

| Bytes | Content |
|---|---|
| 0–3 | magic `GXPR` |
| 4 | format version = 1 |
| 5 | 1 = camera paired, 0 = none |
| 6–21 | card UUID (16 B, in the order of the text form), version 4 (random) |
| 22–26 | camera serial number (zeros when byte 5 = 0) |
| 27 | the camera's dialect: 0 = newer, 1 = older (1.5.0; up to 1.4.0 always 0, read as newer) |
| 28–31 | CRC-32 (IEEE 802.3, as zlib) of bytes 0–27, little-endian |

- The UUID is created once, at the first start without the file, from the hardware generator through the SoftDevice
  (`sd_rand_application_vector_get`). A damaged file is taken back from the pairing's copy (1.4.0, "Pairing copy"
  below); without the copy it means a new UUID and a log entry – the camera has to be paired again.
- Writing (`src/record_file.cpp`, as the time zone and the settings, since 1.5.0): a temporary file
  `/gpxcam/pairing.tmp`, read back (`File::close()` does not report errors), then `rename`
  (atomic in littlefs 1.7 for the record's own blocks, not for the loss of the 4 KB page around them: "A file system
  that mounts but is damaged" above) and a check read. Opening a file allocates memory on the heap (library) –
  only at start-up and when the pairing changes.
- Tested on the card: the first start creates the file (about 0.9 s), the UUID has the version 4 markers; the same UUID after
  flashing again through `dfu` and after switching off and on with the button.
- **Pairing copy** (`src/pairing_copy.h`, `src/pairing_copy_record.h`): 48 bytes at the start of a page – magic
  `GXPC`, version 1, a sequence number, the 32-byte pairing record as in `pairing.bin`, CRC-32 – on 0xEC000 and 0xEB000
  in turn, always on the older one. A page with anything else than 0xFF, a copy or Meshtastic's node cache (pages
  starting with `WRNG`, `WRN2` or `WRN3`) is never erased or written. With a damaged file system the card stays paired
  from the copy and refuses `pair` and `unpair`; a missing or damaged `pairing.bin` comes back from the copy.

### The time zone file

`/gpxcam/timezone.bin` in the InternalFS file system, 124 bytes (`src/tz_record.h`), so it fits in one 128-byte
block of the file system (`LFS_BLOCK_SIZE` in the core's `InternalFileSystem.cpp`):

| Bytes | Content |
|---|---|
| 0–3 | magic `GXTZ` |
| 4 | format version = 1 |
| 5 | clock (1.3.0): 0 = the card sets the camera's clock (also in the files of 1.2), 1 = it does not (`clock off`) |
| 6–7 | 0 (reserved) |
| 8–55 | zone name from the table, NUL-terminated, zero-padded (empty for a rule given directly) |
| 56–119 | POSIX TZ rule, NUL-terminated, zero-padded |
| 120–123 | CRC-32 (IEEE 802.3, as zlib) of bytes 0–119, little-endian |

- **Without the file** the zone is Europe/Warsaw, with the same time packet as up to 1.1, and nothing is written.
  A damaged file, or one whose rule cannot be used, means Europe/Warsaw and a log entry. Setting the zone that is
  already saved writes nothing (the first `tz`, also with Europe/Warsaw, writes the file).
- **A name from the table takes the table's current rule** when the card starts; the saved rule is only the
  fallback. So a newer firmware with a newer tz database updates a saved zone by itself.
- **Writing** as the pairing file: a temporary file, `rename`, a check read (`src/record_file.cpp` since 1.3.0,
  before in `src/tz_store.cpp`). The file system is used only when Bluefruit mounted it: the core's
  `InternalFS.begin()` erases the whole data area, the pairing included, when it cannot mount it.
- **Table:** `src/tz_table_data.inc`, generated by `tools/gen_tz_table.py` from the IANA release archives
  tzdata2026e and tzcode2026e (SHA-256 pinned in the script, PGP signatures checked when pinning): the script
  builds `zic` and takes the rule of every zone from the footer of its TZif file (RFC 8536). 344 zones and 253
  aliases, 93 distinct rules, about 11 KB in flash. Every rule agrees with the full database from the release of
  2026e (2026-09-29) to 2031; earlier in 2026 Morocco (permanent +00 from 2026-09-20) and parts of Canada
  (permanent summer time from 2026-03-08) changed their law, which the current rules rightly do not cover.
- **Console:** `tz`, `tz NAME`, `tz RULE`; commands up to 66 characters (`tz`, a space and a 63-character rule). The
  argument of `tz` keeps its case (`M3.5.0`); all other commands are matched in lower case.
- **Size:** flash +15.0 KB over 1.1.0, about 157.6 KB in all (the table about 11 KB), RAM +192 bytes.
- **Tested on the card (2026-10-22):** zones by name in any case (`asia/kolkata` saved as `Asia/Kolkata`), an
  alias, a POSIX rule, Dublin, Lord Howe; the zone and the pairing kept after switching off and on with the button
  and after flashing a new build; a wrong name, a rule without summer-time dates and a 67-character command
  refused; the camera's photos with the expected offset (`docs/PROTOCOL.md`, "The time zone in the camera"). The
  time zone page (`docs/timezone/index.html`, Chromium on Linux): connect, read, save, disconnect, the cable pulled
  out while connected, connecting after `pio device monitor` (the message above, then fine after plugging in
  again); the card never restarted and got only `v` and `tz`.

### The settings file

- **File** `/gpxcam/settings.bin`, 16 bytes (`src/settings_record.h`):

  | Bytes | Content |
  |---|---|
  | 0–3 | magic `GXST` |
  | 4 | format version = 1 |
  | 5 | sound: 0 = loud, 1 = quiet, 2 = off |
  | 6 | location updates: 0 = normal, 1 = frequent ("Location updates") |
  | 7–11 | 0 (reserved, not checked when read) |
  | 12–15 | CRC-32 (IEEE 802.3, as zlib) of bytes 0–11, little-endian |

  Without the file the sound is loud and nothing is written; a damaged file (also an unknown sound value) means
  loud and a log entry. A later setting takes a reserved byte with 0 as its default, so an older firmware still
  reads a newer file (and writes 0 there when it saves). Writing as the time zone file (`src/record_file.cpp`).

### The battery log

- The entries wait in RAM and go to `/log/battery.bin` six at a time (1 h): littlefs 1.7 in the core uses 128-byte
  blocks, and every write to a file copies its last block and rewrites a block of its directory, while the core erases
  and programs each 4 KB flash page touched (`flash_cache.c`).
- The entries in RAM are also written when the cable has been in for 10 s without a break, on `bat log off`, before
  System OFF and before `dfu`. A restart in between loses them (up to 5).
- Its own directory: its writes do not rewrite `/gpxcam`, which holds the pairing, the time zone and the settings. The
  file of 1.3 (`/gpxcam/battery.bin`) is moved once; with both files (back to 1.3 and up again) both stay, and
  `bat log clear` removes both.
- **Battery log** written only after 10 s of USB without a break, and before `dfu` (below). Tested: a short touch of
  the cable wrote nothing, a 9 s plug-in neither, a firm plug-in wrote after about 10 s.

## Reliability

### The hang of 2026-10-24

- A 1.4.0 test build (the battery log written at once when USB came, no watchdog) ran about 3 h on the battery with
  Frequent location updates and the battery log on. The positions in the photos stayed fresh to the end.
- The magnetic cable was then put on through a case, and its contact kept breaking: the PC saw one failed enumeration
  (`new low-speed USB device`, no descriptor) and then nothing, also with a good contact.
- The card did not respond: no LED, no click from the button, no USB. Holding the button while plugging the cable in
  once restarted it (`RESETPIN`, battery 4.18 V).
- At that start the file system did not mount, and `InternalFS.begin()` inside `Bluefruit.begin()` erased and formatted
  it: the pairing, the time zone, the settings and the battery log were gone, and the card made a new card id.
- **Probable cause, not reproduced:** `tusb_hal_nrf_power_event()` (Adafruit_TinyUSB_Arduino 3.1.3, `dcd_nrf5x.c`)
  waits without a limit, on the READY event, for the USBD peripheral and then for the high-frequency clock. When the
  cable bounces, READY can be handled after REMOVED has disabled the USBD and released the clock; neither then comes,
  and the Bluefruit SOC task (priority 3), which calls the handler, spins for good. The loop task (priority 1) never
  runs again, and the flash operations, whose completion events the same task delivers, never end. That build wrote the
  battery log right at plug-in, so the likely damage is a flash page erased but not programmed again. The same waits are
  in TinyUSB master and Adafruit_TinyUSB_Arduino 3.7.7 (2026-10); meshtastic/firmware#8865 saw a hang at the same line.
- **Reproduction attempt** with 1.3.0 (the same TinyUSB, no write at plug-in): two rounds of a bouncing cable through
  a case, the second with 33 kernel events (7 enumerations, 6 errors `-71`, one `low-speed` detection like the one
  above) – no hang. Not tried further.

### Watchdog and USB guard

- **Watchdog** (`src/watchdog.h`): 30 s, started first in `setup()`, fed only from `loop()`; it runs while the CPU
  sleeps and pauses only under a debugger. It survives a soft reset (`dfu`, the start after flashing); a watchdog, pin,
  brownout or power-on reset stops it (PS v1.1 5.3.6.8). The UF2 bootloader feeds a running watchdog in its DFU loop
  (`bootloader.c`, `wait_for_events()`). Tested: `diag hang` → restart after about 31 s, `reset: DOG`,
  `status: watchdog_restart=1`; flashing with the watchdog running → not interrupted, then `kept running through the
  reset` and no restart; back to 1.3.0 → one restart after about 30 s (`DOG`), then stable.
- **USB guard** (`src/usb_power.cpp`, linker option `--wrap=tusb_hal_nrf_power_event`): READY goes on to TinyUSB's
  handler only when the USBD is still enabled, VBUS is there, and both the USBD and the clock are ready within 100 ms;
  then the handler's waits end at once. The clock is asked for only when it does not run within 10 ms (DETECTED did
  not ask for it) and given back when READY is skipped. Skipped READY events are counted in `p`. All four callers of
  the handler go through the guard (checked with `objdump`). Tested: three clean plug-ins and unplugs → 0 skipped; one
  short bouncing contact on a test build (kernel error `-71`) → no hang. **The new version was not tested with the
  rounds of a bouncing cable.**

### Robustness

From a review of the code before 1.5.0; besides the older dialect ("Normal work"), the scan watchdog ("Scan") and
the file system ("The file system").
Host tests: `pio test -e native_sessions` (the state machines against fakes and a test camera).

- **A connection nobody wants:** one that came up just as the card gave it up (the connect time limit, `link off`,
  `pair off`) stayed open without an owner, the camera showing a connection without locations. `camera_link` marks a
  connection as wanted; a Connected event while none is is closed at once in the BLE task (`camera: a connection that
  came up after it was given up was closed`).
- **A lost Disconnected event** (`camera_link`'s queue full, counted in `p`): the link and the older dialect's
  lasting connection waited for it, and without a GPS fix nothing ended them. They now also read
  `camera_link::connected()`.
- **The CCCD search** timed its step from a time read before the events, so a second descriptor search could end at
  once with "no answer in time" (not on the S5II, which has no CCCD).
- **Pairing and the link:** a scan that only a `diag scan` run kept on, and the link's GPS, are no longer put back
  after a pairing; `link off` and `unpair` during a pairing keep the link off after it; the GPS and the scan go back
  when the link cannot start then. Only "attribute not found", or an answer without it, means a camera without the
  newer dialect. `link on` starts the rate guard of the frequent timing afresh.
- **Camera reports** keep the time the BLE task got them, not the time the loop read them.
- **The log:** after one 50 ms wait for a host that does not read, the next lines do not wait until there is room
  (`bat log` could hold the loop past the watchdog); no line is longer than 125 characters (long ones are split); a
  scan that cannot start is logged once; the link's scan lines between connection attempts are only sent.
- **Records:** the temporary file is read back before it replaces a record (`File::close()` reports no errors); the
  battery log checks the file's size after a batch; the pairing copy's page is erased once per write, not twice.

## Pairing and the link

### Pairing with the camera

- **Gesture:** a click and, within 0.5 s, a 3 s hold (`CLICK_HOLD_GAP_MS`); a 3 s hold alone still switches the card off.
- **Tested on the card (M4, step 3):** no fix for 120 s – gives up with a clear message, GPS back in Backup
  mode; camera in `00` and in `06` – message "not in pairing mode", no connecting, gives up after 60 s; camera in the pairing
  menu – paired at the first attempt in about 1.8 s, the camera showed "Pairing completed", and "T1000-E" in its device
  list; the gesture starts the session, a short press interrupts it; the camera's number is kept after switching off and on.
- **Not tested on the card:** the failure path with 10 s and 30 s pauses – no attempt ended in failure.
- **Timing (1.5.0-beta.2):** an S5 stays in pairing mode only about 30 s (a tester's nRF Connect: 29 advertisements,
  then silence), so the card must already be looking when pairing starts on the camera. The search lasts 120 s
  (60 s before). A session that ends without pairing keeps a GPS it switched on running for 10 min, and `pair
  prepare` (sent by the card page on connecting to an unpaired card) switches it on for 10 min; a session started
  meanwhile skips the wait for GPS time. When the hold runs out the GPS goes back to Backup mode, unless the link
  uses it; `status` shows the seconds left as `gps_hold`.
- **Connecting (1.5.0-beta.4):** a tester's S5 in pairing mode was heard only 3 times in a search; two attempts got
  no answer in 10 s and the third ended with HCI 0x3e (the camera sent nothing in the first connection events). The
  card now listens all the time while it connects (window = interval, 50 ms) instead of the scanner's 50 of 100 ms,
  for at most 5 s. In pairing, a connection that never came up (no answer, or 0x3e) is tried again at once at the
  camera's next fresh pairing advertisement, up to 5 times (`pair: quick retry N/5`), without the 10 s and 30 s
  pauses, which are kept for what fails after the link was made (at most 3 attempts). The logs give the kind of
  each camera advertisement (connectable, scan-only, non-connectable). Up to 1.5.0-beta.5 they also gave the
  camera's reports in the last 10 s at each connect; in pairing that was almost always 1 (the card connects at the
  first fresh pairing advertisement), so the summary of the scan before the attempt is the measure instead.
- **The time for an older camera (1.5.0):** an older camera takes the time only after its `01`, up to 300 s
  after the card introduced itself, while the user joins its Wi-Fi network on a computer, often indoors, where the GPS
  loses its fix. Without a fresh GPS time the card then uses the time of the last fix plus the time since it came
  (`gps::utcSinceFix()`, `millis()` on the 32 kHz crystal: a few ms in 10 min), for up to 10 min, with one line
  (`older: no fresh GPS time - the time of the fix N s ago`). Up to 1.5.0-beta.5 the pairing failed at that last step
  (`time: no GPS time any more`). The test build's `diag gps stale` / `diag gps fresh` make the time and position count
  as not fresh while the last fix stays, to test this on the card.
- **Channels and a startup scan (1.5.0-beta.5, diagnostics only):** the same tester's S5, with the phone's and the
  computer's Bluetooth off, was still heard once in 10-30 s at -32 to -39 dBm. Each report carries its advertising
  channel (37, 38, 39 - `ch_index` of the report). Since 1.5.0 it is only in `diag scan`: in its device lines,
  and the reports per channel, of all devices and of LUMIX cameras, in the line before its summary (1.5.0-beta.5 also
  had it in the camera lines, the 10 s scan line and a second summary line of every scan). In 1.5.0-beta.5 only, a
  card that was not paired scanned for 30 s, 10 s after it started, and kept what it heard in the log, for a test in a
  wall charger away from a computer; 1.5.0 leaves it out (it took the radio and the log of every new card at
  start), and *Scan for cameras* in the app or on the card page does the same with a computer. The SoftDevice does not
  report CRC errors while scanning; its channel survey (`sd_ble_gap_qos_channel_survey_start`, energy per channel)
  needs a role flag that Bluefruit's `begin()` does not set.

### Normal work

- **Order:** scan → a report of the paired camera in state `00` (not older than 1 s) and UTC time from the GPS → connection
  and introduction as in pairing, but the connection stays → `01` for GPS mode → CCCD → locations.
  Without the time from the GPS the card does not connect at all: the first version connected earlier and every attempt ended at the
  time step (the camera rejected nothing, but the connection was useless).
- **Tested on the card (M5, step 2):** connection about 1.5 s after the report, white Bluetooth and GPS icons on the camera;
  location at once and every 25 s; photo: position in EXIF 8.4 m from the card's position (indoors), GPS time in EXIF
  19 s before the photo (the last sending), status `A`; `gps off` meanwhile – the connection stays, locations wait,
  `gps on` – fix after 8 s and the sending resumes; camera switched off – the camera closes the connection (`HCI 0x13`), the card sees
  it in standby `06` and does not connect for 5 min; switched on – connection in 1.6 s.
- **Notifications without CCCD:** in the S5II the notification characteristic (handle `0x008e`, "notify" only) has no CCCD
  descriptor – right after its value comes the declaration of the next characteristic (`0x2803`). The gpxcam app does not write anything either
  (`getDescriptor` returns null). The card listens for notifications without a CCCD; the camera sent one about 2 s before switching off.
- **Altitude:** the S5II does not write it to EXIF, neither with the card nor with the Lumix Lab app (the same set of GPS tags,
  `GPSMeasureMode` 0); the card sends the altitude as the protocol says.
- **Signals (M5, step 3):** LED – searching: two 50 ms flashes every 2 s, connected: one every 4 s, without `link`: every 1 s;
  sounds – connection: two rising tones (CS4, FS4), end of connection: two falling ones.
- **Range (M5, step 3):** out of range after 134 s – `HCI 0x08` (supervision timeout), pause 5 s; an attempt at
  −90 dBm failed (`HCI 0x3e`), pause 15 s; after coming back, connection in 1.5 s.
- **Quick retry (1.0.1):** the S5II often rejects the first connection right after it is switched on (`HCI 0x3e`, the connection
  was not established; once also with a camera that had been on for a long time). The card then tries again after 1 s, without
  lengthening the pauses; the next such retry is allowed only after a successful connection. In 1.0.0, after such a failure,
  the connection came back after about 17 s.
- **Connection ended by the camera (1.0.1):** switching the camera off ends the connection with code `HCI 0x13` – this is a normal
  end, not a failure: always a 5 s pause, also after a short connection. In 1.0.0 short connections (camera switched off
  quickly) lengthened the pauses 5 → 15 → 60 s, and after the third cycle the card connected only after a minute. Loss of range
  (`HCI 0x08`) and errors still lengthen the pauses.
- **Older dialect (1.5.0):** the link's pauses follow the same rules as in the newer dialect: a camera that
  closes the connection after it took the card (`HCI 0x13`, switched off) gives the 5 s pause, a connection that was
  not established (`HCI 0x3e`) is tried again after 1 s once, other ends make the pauses longer (5, 15, 60 s). Up to
  1.5.0-beta.5 every end of an older-dialect connection made them longer. A connection in which the camera did not
  take the card (a refused write of the introduction or the time, or no `01` within 15 s) counts, and the third in a
  row stops the link as a refusal does (`link: 3 connections in a row ended without camera … taking the card`); a
  location the camera takes starts the count again. Up to 1.5.0-beta.5 the first one stopped it. The link also plays
  the connected sound when an older camera takes the card (up to 1.5.0-beta.5 only the sound of the end). Since
  1.5.1 the link sends the locations of both dialects: `older_session` ends once the camera took the card, and
  Normal or Frequent, the rate guard and the line without a fix (once) apply to older cameras too (up to 1.5.0 a
  location every 5 s whatever the setting, and a line every 5 s without a fix). Host tests:
  `pio test -e native_sessions`.

### Clock off

- **Clock off** (`clock off`, byte 5 of `timezone.bin`): a normal connection skips both time writes; the
  introduction is the key, the identifier, the app name and the device name, then the GPS mode and the
  locations. Pairing writes the time as before. Without the time write the card does not learn at once that the
  camera has forgotten it, so with the clock off it stops (as after a refused time, until `link on`, a new
  pairing or a restart) when the camera refuses any write of the introduction, the GPS mode or the first
  location, or when 3 connections in a row end before the camera takes a location
  (`link_policy::refusalMeansUnknownCard`, `earlyEndMeansUnknownCard`, tested on the PC).

### Location updates

- **Normal** (the timing up to 1.3) and **Frequent:** `NORMAL_TIMING` and `FREQUENT_TIMING` in `link_policy.h`,
  described for users in README.md, "Normal use"; a test of the card page checks that both, the page and the
  console's help give the same numbers. Stored in byte 6 of `settings.bin`, which 1.3 kept reserved: 1.3 reads the
  file and writes the byte back as 0 (Normal) when it saves.
- **Guard:** the camera answers a location write in 1–2 connection events (about 50–100 ms); an answer later than 1 s
  counts as late, and after two in one connection the card uses Normal to its end. A connection that did not end by the
  camera's own close (`HCI 0x13`) makes the next one Normal too. `status: updates_now` shows the timing in effect.
- **Night test** (test build, 2026-10-23/24): one connection for 8 h 11 min, about 5,950 locations, answers 58–244 ms
  (average 86, 99 % within 156 ms), no late answer, refusal or disconnection.
- **Walk** (Frequent, 2026-10-24): 32 photos, the age of the position in EXIF at most 7 s (median 3 s), its distance to
  a watch's GPS track at the moment of the photo median 2.7 m, 90 % within 6.5 m.

## The card page and `status`

- **`status`** (`src/status_text.h`, tested on the PC): the state for the card page (`docs/index.html`) as
  `status: key=value ...` lines, `status: end` last, without the camera's serial number, the card id or a position:

  ```
  status: format=1 fw=1.3.0 git=1234567
  status: battery_mv=4100 battery_pct=89 charger=charging
  status: gps=on gps_fix=1 gps_sat=11
  status: paired=1 link=connected
  status: pair=idle pair_attempt=0 pair_result=none
  status: clock=on tz=Europe/Warsaw
  status: sound=loud
  status: end
  ```

  Values: `charger` no_usb, charging, charged, not_charging; `link` off, searching, connecting, connected, pausing,
  stopped (the camera refused the card); `pair` idle, waiting_gps, searching, connecting, pausing; `pair_result`
  (of the last pairing since the start) none, paired, not_saved, not_possible, no_gps_fix, no_camera,
  not_in_pairing_mode (a LUMIX camera seen, but not in state `03`), failed, cancelled; `tz` the zone name, or the
  POSIX rule when it was given directly; `sound` loud, quiet, off. A later version may add keys; a changed meaning
  raises `format`. Every line fits one log line (at most 125 characters); the lines are not kept in the log
  history, as the page asks every 5 s (every 2 s while pairing).
- **The card page** sends only the commands of `test/commands.json`, one command at a time (answers are told
  apart only by their first word). The old address `/timezone/` leads to it.
- **Size:** flash +3.0 KB over 1.2.0, about 160.6 KB in all; RAM +8 bytes.
- **Size:** flash +1.6 KB over the clock setting; 1.3.0 in all +4.6 KB over 1.2.0, about 162.2 KB; RAM +12 bytes
  over 1.2.0.
- **Tested on the card with the S5II (2026-10-23):** the card page (Chromium on Linux, from localhost) shows the
  same state as `p`; clock off with New York set in the camera: photos at −04:00 over two connections; clock on
  again: the camera took Europe/Warsaw back at the next connection, photo at +02:00; pairing with the clock off
  set the camera's zone once; forgetting the camera after the question, pairing from the page, a camera not in
  pairing mode (the message after 60 s); a card with 1.2.0 on the page (the time zone only, the rest locked with
  a message); Loud, Quiet and Off from the page and the console, with Off the answers to a held button quiet and
  the connection silent; the pairing, time zone and settings files kept over flashing 1.2.0 and back. Found and
  fixed in the test: Quiet at 5 % hardly differed from Loud (now 2 %); two clicks followed by `gps::off()`
  sounded as one note of about 2 s (now `sound::playToEnd()` first); a camera that still lists the card does not
  pair it again (`docs/PROTOCOL.md`).

## Test builds and `diag`

- **`diag`** (every build; README, "Serial console (service)"): `diag bat`, `diag snd N`, `diag scan` and
  `diag clk [S]` measure, play or log, and `diag hang` tests the watchdog; none of them changes stored data.
  Their findings are in the sections of the card, the battery, the buzzer, the watchdog, the scan and the
  energy above.
- **The test build** (`pio run -e t1000e_test`, never released) adds the commands of `GPXCAM_TEST_COMMANDS`
  (`src/console_commands.cpp`), which damage stored data or the state on purpose to test the repair:
  `diag storage break yes` and `diag storage break middle yes` ("The file system"), `diag scan stall` ("Scan"),
  `diag gps stale` and `diag gps fresh` ("Pairing with the camera").

## Open questions

- **Wake-up and LATCH (M2, step 4):** once, after a wake-up with the button, the P0.06 bit in `LATCH` could not be cleared
  (`LATCH P0 0x00000040`), and the firmware, following its rule, started instead of going back to sleep. In the next attempt
  the problem did not occur; the firmware retries the clearing for 100 ms and logs the details if it comes back.
- **P0.05:** analog reading 2.73 V, but digital 0, also with a pull-up – contradictory, unexplained.
  To detect USB power we use the VBUS register in the nRF; P0.05 is not needed.
- **Standby after the camera falls silent:** the camera advertises its standby only for about 20 minutes
  (`docs/PROTOCOL.md`). Then the card no longer sees it and after 5 min switches from long standby (scan every 10 s)
  to rare mode (every 2 s) – more scanning for the rest of the night. To be decided after the standby current is
  measured.
- **Manual DFU (button + cable twice):** works, but neither plugging in the cable alone nor pressing the button alone
  resets the processor, so the mechanism is different. The bootloader enters DFU after a double reset on the
  RESET pin (P0.18), and the pin is enabled – probably something on the board causes this reset. We do not investigate as long as it works.

## Still to do

- The range with +8 dBm against 0 dBm: another person carries the card away until the connection ends (1.3.0 and 1.4.0,
  same place, same pocket).
- The current with +8 dBm (the battery log in the same scenario as 1.0.1).
- Pairing: say on the card page when the GPS starts from nothing (after flashing; since 1.5.0 the card keeps it on
  for 10 minutes around a pairing).
- A shorter pause before connecting again after a loss of range (`HCI 0x08`).
- Whether Meshtastic leaves gpxcam's files in the file system after a trip there and back.
- Older cameras: a test with a camera of the S5 generation (issue #2). The one so far, a tester's LUMIX S5, was heard
  only a few times in a search and never connected.
- A night with one connection on 1.5.0 (the newer and the older dialect).
- Measure the current in standby and with 1.1.0 (the discharge test above; during work with 1.0.1 about 7–8 mA,
  rough), and at rest in System OFF (it needs a meter). The LR1110 in sleep should draw less than 1 µA.
