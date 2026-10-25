# gpxcam card – the desktop app

The [card page](../docs/index.html) in its own window, for computers without Chrome or Edge
(Firefox, Safari, Linux without Chromium). It shows the card's state and sets it up the same way:
pairing, the camera's clock and time zone, location updates and sound.

- It talks to the card over USB at 115200 baud, and only to a T1000‑E running gpxcam
  (USB 2886:8057).
- It sends the card only the commands of [test/commands.json](../test/commands.json). The page
  checks them, and the app's Rust side checks them again.
- It sends nothing over the internet and has no Tauri plugins. Pairing an *older* camera is the one
  time it uses the network at all, and only to the camera's own Wi-Fi (see below).

## Install

Take the file for your computer from the app's release on GitHub (tag `app-vX.Y.Z`). `SHA256SUMS`
next to it lets you check the download (`sha256sum -c SHA256SUMS` on Linux).

**Linux** – Fedora: `sudo dnf install ./gpxcam-card-*.x86_64.rpm`; Debian, Ubuntu:
`sudo apt install ./gpxcam-card_*_amd64.deb`. The card's port belongs to the `dialout` group.
Add yourself to it once, then log out and in again:

```
sudo usermod -aG dialout $USER
```

**macOS** (Intel or Apple processor) – open the `.dmg` and drag "gpxcam card"
to Applications. The app is not signed by an Apple developer, so macOS stops it the first time:
open it once, then in System Settings → Privacy & Security click "Open Anyway" next to "gpxcam card"
and confirm. If macOS says the app is damaged, remove the download mark in Terminal and open it
again:

```
xattr -dr com.apple.quarantine "/Applications/gpxcam card.app"
```

**Windows** 10 or 11 (64-bit) – run the `.msi` (or the `-setup.exe`). The installer
is not signed, so Windows SmartScreen may say "Windows protected your PC": click "More info", then
"Run anyway". The app uses Microsoft Edge WebView2, which Windows 10 and 11 usually have; if it is
missing, the installer downloads it.

The `.msi` has been tried with a card on Windows 11. The `-setup.exe` and the macOS version are
built the same way but have not been tried with a card yet.
If something does not work there, please open an issue on GitHub.

## Pairing an older camera

Most newer LUMIX cameras (S5II and the like) pair straight from the card page or this app with
*Pair*. **Older** cameras — the ones that pair with the older Panasonic phone app, not LUMIX Lab —
need one extra step over the camera's own Wi-Fi, which only the app can do (a web page cannot reach
the camera that way). In the app, use *Pair an older camera*:

https://github.com/user-attachments/assets/6de0f7d9-dfe0-4dd2-bccc-11901fb6f236

The video is a schematic run-through; the on-screen steps may differ a little from your camera.

1. Put the card where its GPS gets a fix, outdoors or by a window. The app switches the card's GPS
   on as soon as it connects (gpxcam 1.5.0-beta.2 or newer); the GPS tile shows *Fix* when it is
   ready, which can take a few minutes after a fresh start.
2. Click *Start*. Do not start pairing on the camera yet.
3. **Only when the app says *Now start Bluetooth pairing on the camera*** do it (the camera's
   Bluetooth menu → Pairing). The order matters: the camera stays in pairing mode only about 30
   seconds, so the card must already be looking for it (for up to 2 minutes). The card introduces
   itself over Bluetooth; the camera then shows a Wi-Fi network and a password on its screen.
4. Join that Wi-Fi network on this computer, by hand. The app registers the card over the camera's
   own address (192.168.54.1) — never the internet — and tells you how it went.

Join the camera's Wi-Fi network shown on its screen. On the S5 and similar it has **no password by
default**, so just pick it from the list. If the camera asks for one, the password is on the camera's
screen or in its Wi-Fi setup: **[Setup] → [Wi-Fi] → [Wi-Fi Setup] → [Wi-Fi Password]** (see the
[S5 manual](https://eww.pavc.panasonic.co.jp/dscoi/DC-S5/GA_GW_GC_GH_GN_GT_GD/DC-S5_DVQP2213ZD_full_eng.pdf),
pages 424–426).

If the card does not find the camera, click *Start* again: the card keeps its GPS on for 10 minutes
after a pairing that did not succeed, so it starts looking for the camera at once.

After it succeeds, the card connects to the camera over Bluetooth alone, like any other pairing.
The protocol is in [docs/PROTOCOL.md](../docs/PROTOCOL.md).

## How it is made

- `../docs/index.html` is the page. `tools/app_frontend.py` copies it to `dist/index.html` with two
  changes: its content security policy lets the page talk to the app over Tauri's IPC, and "About
  this app" shows the version from `src-tauri/Cargo.toml`.
- `src-tauri/` is the Rust side. It finds the card, opens its port at 115200 baud with DTR, passes
  the card's lines to the page and sends the allowed commands.
- The page finds the app through `window.__TAURI__`, and Card works over it unchanged.

## Build and run on Linux (Fedora)

```
sudo dnf install webkit2gtk4.1-devel librsvg2-devel libxdo-devel openssl-devel systemd-devel gcc
cargo install tauri-cli --version "^2" --locked
cd app/src-tauri
cargo tauri dev
```

`cargo tauri dev` builds the app and opens its window. The first build takes a few minutes.
`cargo tauri build` makes the installers in `src-tauri/target/release/bundle/`.

On Linux add yourself to the `dialout` group, as under Install.

## Versions

The app has its own version numbers, from 1.0.0, apart from the firmware's. Each release of the app
says which gpxcam it was tested with. It works with older gpxcam as far as the firmware allows:

| gpxcam on the card | What the app can do |
|---|---|
| 1.4.0 | everything |
| 1.3.x | everything but choosing how often the position is sent |
| 1.2.x | only the time zone |
| before 1.2.0 | nothing: install the newest gpxcam first |
