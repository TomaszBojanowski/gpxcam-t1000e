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

**Windows** 10 or 11 (64-bit) – run the `.msi` (or the `-setup.exe`). The installers are signed by
the SignPath Foundation (see [Code signing policy](#code-signing-policy)); a new signature may still
make Windows SmartScreen say "Windows protected your PC" until it gains reputation: click "More
info", then "Run anyway". The app uses Microsoft Edge WebView2, which Windows 10 and 11 usually
have; if it is missing, the installer downloads it.

The `.msi` has been tried with a card on Windows 11. The `-setup.exe` and the macOS version are
built the same way but have not been tried with a card yet.
If something does not work there, please open an issue on GitHub.

## Code signing policy

The Windows installers of gpxcam card (`.msi` and `-setup.exe`) are signed by the
[SignPath Foundation](https://signpath.org). This program uses free code signing provided by
[SignPath.io](https://signpath.io) and a free code signing certificate by the SignPath Foundation.
Signing applies from the first signed release onwards; the macOS and Linux downloads are not signed
this way (see Install above).

**Team roles**

- **Author / maintainer** – Tomasz Bojanowski, the repository owner: writes and maintains the source.
- **Reviewer** – Tomasz Bojanowski: reviews every change from anyone who is not a committer before
  it is merged.
- **Approver** – Tomasz Bojanowski: approves each signing request.

gpxcam card is a one-person project: the development, maintenance and signing team is the same
person, who owns the repository. Every signing request is approved by hand.

**Privacy policy**

gpxcam card sends nothing over the internet: no telemetry, no analytics, no automatic updates. It
talks to the SenseCAP T1000-E card over USB. Its only network connection is the one-time Wi-Fi step
of pairing an older LUMIX camera, and only when the user pairs such a camera: the app then talks
only to the camera, at 192.168.54.1 on the camera's own Wi-Fi network, which the user joins by hand,
and sends it the card's identity and device name. Before registering, it also sends one SSDP search
(a standard local discovery packet with no user or card data) to the local network, to wake the
camera's service. No information is transferred to any other networked system.

**Verifying the signature (Windows)**

Right-click the installer, then Properties → Digital Signatures: the signer is "SignPath
Foundation". Or in PowerShell: `Get-AuthenticodeSignature <file>`. The `SHA256SUMS` file on the
release page has the checksum of every download.

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
