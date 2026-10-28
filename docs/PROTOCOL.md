# Bluetooth LE protocol of LUMIX cameras

The protocol the card uses to give a Panasonic LUMIX S5II (DC-S5M2) its position and the time.
It was worked out and tested on the S5II with gpxcam, the author's Android app (repository
`TomaszBojanowski/gpxcam`, `android/app/src/main/java/me/bojanowski/gpxcam/LumixProtocol.kt` and
`PROTOKOL.md`) – that code is the reference. The gpxcam app and its repository are private, so this
document is the public description of the protocol.

## The camera's advertisement

- Manufacturer specific data: `3a 00 <state> <5-byte serial number>`.
- `<state>`: `00` camera on, `03` pairing mode ("Add New Device"), `06` switched off with the switch
  (standby). **Never connect in state `06` – a connection wakes the camera.**
- The older Panasonic app knows more states: `01` asleep, `02` waking up, `04` off, `05` on but asleep, `07` on
  but asleep with a quick wake, `08`–`0A` the same with automatic transfer. It registers a new camera only in
  `03`; a camera it already knows it also connects to in `05` and `07`. Seen so far: `00`, `03`, `06` on the
  S5II (both dialects); `07` on an S5 (1st generation) that showed its pairing screen (2026-10-25).
- Advertised service: `054AC620-3214-11E6-AC0D-0002A5D5C51B`.
- On: a resolvable private address (RPA). Standby: the public address. Legacy ADV_IND, connectable.
- Connect only when the state and the serial number are known and the number matches the paired one.
- The card accepts exactly this layout: the manufacturer data (AD type `0xFF`) is 8 bytes long including
  `3a 00`. Panasonic data in another layout (other devices of the company) is not a camera. The data may come
  in the advertisement or in the scan response – the card reads both (`src/lumix.cpp`).
- Seen on the S5II (M3, active scan 50 ms of every 100 ms): the manufacturer data is only in the
  advertisement, never in the scan response; about 3 reports per second. Every change of state changes the
  address: `00` → `03` and `03` → `00` – a new RPA, `00` → `06` – the public address, `06` → `00` – a new RPA
  again. In standby (`06`) the camera keeps advertising, but only for a while – about 20 minutes after
  it was switched off, seen twice on 2026-10-21/22 – and then falls silent until it is switched on.
- Pairing the card with the S5II (M4): the camera grants an MTU of 57 (the card asks for 185, like gpxcam) and
  does not ask for encryption; from the connection to the accepted time took about 1.2 s (at 20 ms; the
  times since 1.0.2 are under "Connection parameters"). No pairing at the Bluetooth level, like gpxcam.
- Normal work with the S5II (M5): switching the camera off ends the connection (`HCI 0x13`). The EXIF data of a
  photo holds the position and the GPS time of the last location (the time of sending), status `A`, no
  altitude – the same with the Lumix Lab app. The notification characteristic has no CCCD, yet the camera
  sends a notification: `02` just before it is switched off (seen 2026-10-21).
- The first connection right after the camera is switched on is often not established (`HCI 0x3e`); a second
  attempt a second later works (1.0.1).

## Connection parameters

The device that connects (the card or the phone) sets the connection parameters. The S5II never asks for
others – neither from the card nor from the phone – it takes what it gets.

- **Android 15 with the gpxcam app** (read from the phone's HCI log, 2026-10-20): it connects asking for an
  interval of 30–50 ms, latency 0 and a supervision timeout of 5 s; the phone's controller takes 48.75 ms.
  Right after connecting it switches to 7.5 ms for about 2.5 s (service discovery), then back to 48.75 ms,
  and asks for 61-byte packets (data length; the camera's MTU 57 plus the 4-byte L2CAP header). The PHY
  stays 1M.
- **The card since 1.0.2** connects with the same 48.75 ms, latency 0 and 5 s and asks for 61-byte packets
  right after connecting (`src/camera_link.cpp`). It does not switch to 7.5 ms for the introduction: tried
  with the card (also with the packets asked for only after it), the camera stopped answering the
  characteristic discovery at 7.5 ms every time. So the introduction is slower (table below).
- **Kept handles (1.1.0):** the handles of one camera do not change between connections, so after an
  introduction the camera accepted the card keeps them in RAM and skips the service and characteristic
  discovery the next time; the camera accepts the introduction without it. The card looks the handles up
  again after a restart, for another camera, and after any error once connected (not after `HCI 0x3e`,
  when no connection was made) – `src/handle_cache.cpp`.
- **The card up to 1.0.1** used the Bluefruit library's defaults: 20 ms, latency 0, 2 s and 27-byte packets.
  The camera's Bluetooth then failed after hours in two ways:
  - short drops: the camera fell silent for more than 2 s (`HCI 0x08`) and advertised again within
    seconds – 4 in a night;
  - hangs, after about 2 and 4 hours: the camera suddenly stopped answering a location write (no ATT
    response, while the link itself stayed up), and after the card ended the connection it did not
    advertise at all for hours, also in standby and in pairing mode. Until the last second the signal
    (about −45 dBm), the answer times and the parameters were normal.

  With the phone's parameters: no failure in a 7-hour test. The cause is a fault in the camera; the
  phone's parameters avoid it. Which of the three differences matters was not tested (most likely the
  interval).

From the connection to the first location (1.5 s measured on 2026-10-20/21, the rest on 2026-10-22;
at 48.75 ms the service and characteristic discovery takes about 2.6 s of it):

| Card | Interval | Time |
|---|---|---|
| up to 1.0.1 | 20 ms | about 1.5 s |
| 1.0.2 | 48.75 ms | about 3.7 s |
| 1.1.0, first connection after the card starts | 48.75 ms | about 3.7 s |
| 1.1.0, kept handles | 48.75 ms | about 1.1 s |

Since 1.2.0 the card writes the time twice in every connection that stays open (below, "The time zone in the
camera"): one more write, about 0.1 s.

While connected, the camera sends a notification `01` on handle 0x0046 every 30 s (also to the gpxcam app
and to Lumix Lab), and `01` on handle 0x0094 once right after connecting. The card does not need them.

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
   ATT error and disconnects); the card (since 1.2.0, not when pairing) then writes it once more, see "The time
   zone in the camera"; with the clock setting off (since 1.3.0) the card skips this step, see below,
6. write `01` to the GPS mode, enable notifications,
7. the location at once, then about every 25 s.

Pairing is the same sequence, run while the camera is in state `03`; the camera then shows "Pairing
completed". Without pairing an unknown identifier is refused at the time write.

**A card the camera still lists is not paired again** (S5II, 2026-10-23): after the card forgot the camera
(`unpair`) while the camera kept "T1000-E" in its list, the camera in state `03` took the key, the identifier, the
app name and the device name, then closed the connection at the time write (`HCI 0x13`) in each of 3 attempts.
After "T1000-E" was deleted in the camera's Bluetooth menu, the next pairing worked at the first attempt (the card
id was the same).

**Without the time write** (1.3.0, `clock off`; tested on the S5II on 2026-10-23): the camera takes the connection
just the same. It accepted the GPS mode and the locations, showed its GPS icon, and the photo had the time zone set
by hand in the camera's menu (−04:00) with the right GPS time and position. Since the time write is where the
camera refuses an unknown card, the card then treats a refusal of any other step, or three connections in a row
that end before a location, as "the camera does not know the card" (`docs/HARDWARE.md`, "Card page and clock
setting").

## Byte formats

- **wordSwap(d):** pad with zeros to a multiple of 4, then reverse the byte order in every group of 4.
- **XOR:** with the 4-byte key repeated every 4 bytes.
- **Identifier:** `xor(wordSwap(uuid16) + ff ff ff cd, key)`. The UUID is 16 bytes in big-endian order (as in
  its text form), created once and kept in flash (the pairing file, `docs/HARDWARE.md`).
- **Device name:** `xor(wordSwap([length][UTF-8]) + ff ff ff 00, key)`, the text at most 23 bytes, cut
  between characters. The card's name is `T1000-E` (the camera shows it in its list of paired devices).
- **App name:** plain UTF-8 text padded with zeros to 32 bytes; the card sends `gpxcam T1000-E <version>`.
- **Time (10 B, little-endian):** year u16, month, day, hour, minute, second – **UTC**; then the standard
  time-zone offset in minutes (i16) and the summer-time flag (u8), which adds one hour. For Poland: `60` and `1` in
  summer / `0` in winter. The card takes both from the time zone set with `tz` (`src/posix_tz.cpp`). Summer time one
  hour *behind* standard time (Europe/Dublin, whose standard time is the summer one) goes the other way round:
  `0` and `1` in summer. Summer time that is not one hour ahead (Australia/Lord_Howe +30 min, Antarctica/Troll +2 h)
  goes as the offset in effect without the flag: the local time is right, the camera shows no summer-time mark.
- **Location (16 B, little-endian):** GPS time u32 = `unix_seconds − 315964800` (**without** leap seconds),
  latitude i32 × 1e7, longitude i32 × 1e7, altitude i16 in metres, `0x41` ("A" – valid position), `0x00`.
  **The time is the moment of sending, not the time of the fix** – otherwise the camera greys out its GPS icon
  after a while. Without a valid fix no location is sent.

## The time zone in the camera

Tested on the S5II on 2026-10-22 with the card writing different time zones (photos checked with exiftool):
- **The camera takes the standard offset from the time packet *before* the last one, and only the summer-time
  flag from the last one.** Example: after Warsaw (`60`, summer) the card wrote New York (`-300`, summer): the photo
  had +02:00 (60 + 1 h); after that, Kolkata (`330`, no flag): −05:00 (−300 + 0). All 16 photos of the first test
  follow this rule, and it predicted the last of them before it was taken.
- Neither the end of the connection nor 5.6 minutes of waiting brings the new offset in; only the next time write
  does. The camera's UTC clock is right in every case: its own UTC stamp (Panasonic `TimeStamp` in EXIF) and the
  GPS time agree.
- So since 1.2.0 the card writes the time **twice** in every connection that stays open: the second packet makes
  the zone of the first count. With it, New York, Kolkata and Warsaw were right in the photos after one connection
  each. Pairing writes the time once (as up to 1.1); the connection right after it writes it twice.
- A second write that cannot be sent (no GPS time any more, refused by the SoftDevice) or that the camera
  refuses is logged and ends nothing: the zone then counts from the next connection. No answer to it within 5 s
  ends the connection like any other step: the write stays pending, so the card could not write anything else.
- The phone apps (gpxcam, Lumix Lab) write the time once per connection, so after a change of the phone's time
  zone the camera most likely shows the old one until the next connection (not checked).

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

All vectors, the location included, are checked in `test/test_lumix`; the calendar in `test/test_utc`, the time
zone rules in `test/test_posix_tz` (Europe/Warsaw gives the camera the same bytes as the EU rule of versions up to
1.1). The name "Zażółć gęślą jaźń aparatu" (34 bytes of UTF-8) is cut to 22 bytes, as in gpxcam.

Advertisement (made-up serial number `12 34 56 78 9a`, tests in `test/test_lumix`):
- `02 01 06 09 ff 3a 00 00 12 34 56 78 9a` → camera, state `00` (on), serial number `123456789a`
- `09 ff 3a 00 06 12 34 56 78 9a` (a scan response alone) → camera, state `06` (standby)
- `05 ff 3a 00 01 02` → Panasonic data in another layout, not a camera

## Older dialect: the card's identity

Cameras that the older Panasonic app pairs with (the S5II among them) also speak an older dialect, with other
characteristics. At the first pairing the camera takes a device only when the identity it presents over Bluetooth
is the one registered over the camera's Wi-Fi; after that the device connects over Bluetooth alone.

- **Identity (16 B):** the fixed prefix `4d 45 49 30 01 00 10 00 80 01` (ASCII "MEI0" and six bytes), then a 6-byte
  seed. The card's seed is the last 6 bytes of its UUID (the pairing file), so the identity stays as long as the
  card keeps its pairing.
- **Over Wi-Fi** it is registered as text, upper case: `4D454930-0100-1000-8001-` and the seed in hex.
- **Over Bluetooth**, in the byte order and with the XOR of the newer dialect (key read from the camera):
  - identity: `xor(wordSwap(10 + identity + ff ff ff), key)`, 20 bytes;
  - device name: `xor(wordSwap([length] + name + 00… + ff ff ff), key)`, 20 bytes; printable ASCII, 1 to 16
    characters, the same name as registered over Wi-Fi.

Made-up vectors in `test/device_identity_vectors.txt`, checked in `test/test_device_identity`: seed
`a1b2c3d4e5f6`, key `11 22 33 44` → identity field `58677e5401223274b023b344f4f6f0f6eeddccb2`; name `T1000-E` →
`21136743540f03741122334411223344eeddcc44`.

## Older dialect: the one-time Wi-Fi step

At the first pairing the camera opens its own Wi-Fi access point and shows its network name and password on its
screen. A computer joins that network by hand and registers the same identity and device name over the camera's
local HTTP service; the camera then accepts the card over Bluetooth, and later connections need no Wi-Fi. The
gpxcam card desktop app does this step (`app/src-tauri/src/older_wifi.rs`); the card only waits for the camera's
acceptance (up to 300 s).

- The camera is at `192.168.54.1` on its access point (HTTP, no TLS; a UPnP description on port 60606, the
  registration on port 80).
- Before the registration the app, like the older phone app, only nudges the camera awake: it reads the UPnP
  description (TCP to port 60606, `GET /Lumix/Server0/ddd`) twice, 2 s apart, with one SSDP search (a UDP multicast
  `M-SEARCH` for `urn:schemas-upnp-org:device:MediaServer:1` to 239.255.255.250:1900) in between, and ignores the
  answers and any error.
- On one keep-alive connection: `GET /cam.cgi?mode=accctrl&type=req_acc_g` returns `ok,<token>` with a 4-byte
  session token (hex); then `GET /cam.cgi?mode=accctrl&type=req_acc_e&value=<identity>&value2=<name>`.
- **Wi-Fi encoding** of a text: pad its ASCII bytes with `0xFF` to a multiple of four, then for each 4-byte word
  emit `byteSwap(word XOR token)` as eight lower-case hex digits — the same XOR-and-byte-swap as the Bluetooth
  identity above, over the text form, keyed by the session token.
- The camera answers `ok_under_research…` or `err_user_refused…` until it accepts (the user allows the device on
  the camera, and the identity and name must match); then `ok,…`, and it sends the card `01` over Bluetooth.

Made-up example (token `1a2b3c4d`, the seed and name above): `value2` for `T1000-E` is `7d0c1a4eb279062a`; the app's
test (`app/src-tauri/src/older_wifi.rs`) checks the full encoding against `test/device_identity_vectors.txt`.

## Older dialect: the connection

The card's order (`src/older_session.cpp`), each step after the answer to the previous one, at most 5 s each:
1. read the key, write the identity, write the device name (both above), read the camera's name and text;
2. wait for the camera's `01` (it knows the card) on one of the two notification characteristics, next to the
   location or next to the time: up to 300 s when pairing, while a computer does the Wi-Fi step; 15 s in a lasting
   connection, where a camera that knows the card sends it right after the device name;
3. write the time, in the newer dialect's format; in a lasting connection twice, as in the newer dialect (the
   camera takes the zone from the packet before), and not at all with the clock setting off;
4. pairing ends here; in a lasting connection the card then sends the locations as to a newer camera (Normal or
   Frequent, `link_policy.h`).

- **The time when pairing:** the camera sends its `01`, and the card writes the time, up to 300 s after the card
  introduced itself, while the user joins the camera's Wi-Fi network on a computer, often indoors, where the GPS
  loses its fix. Without a fresh GPS time the
  card then uses the time of the last fix plus the time since it came, for up to 10 min.
- **After the connection ends** the card waits as in the newer dialect: 5 s when the camera closed a connection in
  which it had taken the card (`HCI 0x13`, switched off); one more try after 1 s when the connection was not
  established (`HCI 0x3e`); otherwise 5 s, 15 s, then 60 s. A connection in which the camera did not take the card
  (it refused a write, or sent no `01` in time) counts, and the third in a row stops the card as a refusal does; a
  location the camera takes starts the count again.

## Camera behaviour worth knowing

- After a few refused connections the camera stops advertising – switching it off and on helps.
- The camera disconnects by itself when the sequence breaks; the card must not then reconnect in a quick
  loop (pauses).
- Garmin watches and other devices do not matter here – the card is the BLE central, the camera the
  peripheral.
- With no paired device left the S5II switches Bluetooth off: no icon, no advertising (2026-10-23). When "T1000-E"
  was deleted in its menu while the card was connected with the clock off, the camera ended that connection; the
  card's next connection went through the introduction and ended after 1 s, before a location; then the camera
  fell silent. The card, still paired on its side, just keeps looking; it never gets the three early ends that
  stop it with the clock off ("Without the time write" above), which need a camera that keeps advertising, for
  example with a phone still paired.
