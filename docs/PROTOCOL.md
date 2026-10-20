# Bluetooth LE protocol of LUMIX cameras

The protocol the card uses to give a Panasonic LUMIX S5II (DC-S5M2) its position and the time.
It was worked out and tested on the S5II with the gpxcam app for Android (repository
`TomaszBojanowski/gpxcam`, `android/app/src/main/java/me/bojanowski/gpxcam/LumixProtocol.kt` and
`PROTOKOL.md`) – that code is the reference.

## The camera's advertisement

- Manufacturer specific data: `3a 00 <state> <5-byte serial number>`.
- `<state>`: `00` camera on, `03` pairing mode ("Add New Device"), `06` switched off with the switch
  (standby). **Never connect in state `06` – a connection wakes the camera.**
- Advertised service: `054AC620-3214-11E6-AC0D-0002A5D5C51B`.
- On: a resolvable private address (RPA). Standby: the public address. Legacy ADV_IND, connectable.
- Connect only when the state and the serial number are known and the number matches the paired one.
- The card accepts exactly this layout: the manufacturer data (AD type `0xFF`) is 8 bytes long including
  `3a 00`. Panasonic data in another layout (other devices of the company) is not a camera. The data may come
  in the advertisement or in the scan response – the card reads both (`src/lumix.cpp`).
- Seen on the S5II (M3, active scan 50 ms of every 100 ms): the manufacturer data is only in the
  advertisement, never in the scan response; about 3 reports per second. Every change of state changes the
  address: `00` → `03` and `03` → `00` – a new RPA, `00` → `06` – the public address, `06` → `00` – a new RPA
  again. In standby (`06`) the camera keeps advertising.
- Pairing the card with the S5II (M4): the camera grants an MTU of 57 (the card asks for 185, like gpxcam) and
  does not ask for encryption; from the connection to the accepted time takes about 1.2 s. No pairing at the
  Bluetooth level, like gpxcam.
- Normal work with the S5II (M5): switching the camera off ends the connection (`HCI 0x13`). The EXIF data of a
  photo holds the position and the GPS time of the last location (the time of sending), status `A`, no
  altitude – the same with the Lumix Lab app. The notification characteristic has no CCCD, yet the camera
  sends a notification (e.g. just before it is switched off).
- The first connection right after the camera is switched on is often not established (`HCI 0x3e`); a second
  attempt a second later works (1.0.1).

## Service and characteristics

Service: `1FE71B85-8344-4E6A-BF1A-AC7ACDB3F8AF`

| Characteristic | What |
|---|---|
| `239C7203-922F-43E9-BDF1-6D1FEDF6F43F` | key (read, 4 bytes, different in every connection) |
| `BFF12E7F-5944-4EB6-A90F-48D8786157EA` | device identifier (write, encrypted) |
| `ED8B1352-5AA7-4155-B7C9-521292DD1932` | app name (write, plain text, 32 bytes) |
| `05A797B6-9C2F-4D18-99A7-FA61A3880BFE` | device name (write, encrypted) |
| `39A02C41-C6E3-4B73-A3EA-537F23F0085A` | date and time (write, 10 bytes) |
| `C289B6A4-B9EE-49AB-AB06-7099BB5EDAA8` | GPS mode of the camera: `01` on, `02` off |
| `437E6512-321A-4A94-9281-FAB1BFF49A91` | notifications (enabled through the CCCD `0x2902` where there is one; the S5II has none) |
| `565B4253-8BAC-46AE-8C5D-EC6F94FC8714` | location (write, 16 bytes) |

All writes are writes with response.

## Order after every connection

Strictly in this order, each step only after the answer to the previous one:
1. read the key,
2. write the identifier,
3. write the app name,
4. write the device name,
5. write the time – **success means the camera knows the card** (with an unknown identifier it returns an
   ATT error and disconnects),
6. write `01` to the GPS mode, enable notifications,
7. the location at once, then about every 25 s.

Pairing is the same sequence, run while the camera is in state `03`; the camera then shows "Pairing
completed". Without pairing an unknown identifier is refused at the time write.

## Byte formats

- **wordSwap(d):** pad with zeros to a multiple of 4, then reverse the byte order in every group of 4.
- **XOR:** with the 4-byte key repeated every 4 bytes.
- **Identifier:** `xor(wordSwap(uuid16) + ff ff ff cd, key)`. The UUID is 16 bytes in big-endian order (as in
  its text form), created once and kept in flash (the pairing file, `docs/HARDWARE.md`).
- **Device name:** `xor(wordSwap([length][UTF-8]) + ff ff ff 00, key)`, the text at most 23 bytes, cut
  between characters. The card's name is `T1000-E` (the camera shows it in its list of paired devices).
- **App name:** plain UTF-8 text padded with zeros to 32 bytes; the card sends `gpxcam T1000-E <version>`.
- **Time (10 B, little-endian):** year u16, month, day, hour, minute, second – **UTC**; then the standard
  time-zone offset in minutes (i16) and the summer-time flag (u8). For Poland: `60` and `1` in summer / `0` in
  winter (EU rule: the change is on the last Sunday of March and of October at 01:00 UTC).
- **Location (16 B, little-endian):** GPS time u32 = `unix_seconds − 315964800` (**without** leap seconds),
  latitude i32 × 1e7, longitude i32 × 1e7, altitude i16 in metres, `0x41` ("A" – valid position), `0x00`.
  **The time is the moment of sending, not the time of the fix** – otherwise the camera greys out its GPS icon
  after a while. Without a valid fix no location is sent.

## Test vectors

From the gpxcam tests. The location vector has a made-up position (at sea); it was computed from the format
above with code that reproduces the other gpxcam vectors.

Moment: 2026-10-13 22:30:29 UTC, time zone Europe/Warsaw (summer time).
- time → `ea070a0d161e1d3c0001`
- location (54.6000000, 19.0000000, 0 m) → `8573f957804c8b20802b530b00004100`
- `wordSwap(01 02 03 04 05)` → `0403020100000005`
- name "iPhone" before XOR → `6850690600656e6fffffff00`; "iPhone 17 Pro Max" → 24 bytes
- identifier for the UUID `00010203-0405-0607-0809-0a0b0c0d0e0f` before XOR →
  `03020100070605040b0a09080f0e0d0cffffffcd`
- `xor(00 00 00 00 0f, 01 02 03 04)` → `010203040e`

All vectors, the location included, are checked in `test/test_lumix`; the calendar and the EU summer-time rule
in `test/test_utc`. The name "Zażółć gęślą jaźń aparatu" (34 bytes of UTF-8) is cut to 22 bytes, as in gpxcam.

Advertisement (made-up serial number `12 34 56 78 9a`, tests in `test/test_lumix`):
- `02 01 06 09 ff 3a 00 00 12 34 56 78 9a` → camera, state `00` (on), serial number `123456789a`
- `09 ff 3a 00 06 12 34 56 78 9a` (a scan response alone) → camera, state `06` (standby)
- `05 ff 3a 00 01 02` → Panasonic data in another layout, not a camera

## Camera behaviour worth knowing

- After a few refused connections the camera stops advertising – switching it off and on helps.
- The camera disconnects by itself when the sequence breaks; the card must not then reconnect in a quick
  loop (pauses).
- Garmin watches and other devices do not matter here – the card is the BLE central, the camera the
  peripheral.
