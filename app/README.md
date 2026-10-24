# gpxcam card – the desktop app

The [card page](../docs/index.html) in its own window, for computers without Chrome or Edge
(Firefox, Safari, Linux without Chromium). It shows the card's state and sets it up the same way:
pairing, the camera's clock and time zone, location updates and sound.

- It talks to the card over USB at 115200 baud, and only to a T1000‑E running gpxcam
  (USB 2886:8057).
- It sends the card only the commands of [test/commands.json](../test/commands.json). The page
  checks them, and the app's Rust side checks them again.
- It sends nothing over the internet, and it has no Tauri plugins.

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

**Windows** 10 or 11 (64-bit) – run the `-setup.exe` (or the `.msi`). The installer
is not signed, so Windows SmartScreen may say "Windows protected your PC": click "More info", then
"Run anyway". The app uses Microsoft Edge WebView2, which Windows 10 and 11 usually have; if it is
missing, the installer downloads it.

The macOS and Windows versions are built automatically and have not been tried with a card yet.
If something does not work there, please open an issue on GitHub.

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
