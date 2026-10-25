<!-- Description for a gpxcam card GitHub Release (tag app-vX.Y.Z). Releases are made by hand from
     the files of the CI run. Fill in the version, the gpxcam firmware it was tested with and the
     platforms; keep the "Code signing policy" line - the SignPath Foundation requires that term on
     the download page. Attach the installers for Linux, macOS and Windows and the SHA256SUMS file
     from the run. -->

# gpxcam card X.Y.Z

The desktop app - the card page in its own window, for computers without Chrome or Edge. Tested
with gpxcam FIRMWARE_VERSION on PLATFORMS.

## Install

See [app/README.md](https://github.com/TomaszBojanowski/gpxcam-t1000e/blob/main/app/README.md#install)
for each system. Check your download against `SHA256SUMS` (`sha256sum -c SHA256SUMS` on Linux).

## Code signing policy

The Windows installers (`.msi`, `-setup.exe`) are signed by the SignPath Foundation. See the
[Code signing policy](https://github.com/TomaszBojanowski/gpxcam-t1000e/blob/main/app/README.md#code-signing-policy).
The macOS and Linux downloads are not signed this way.

## Files

- Linux: `.rpm` (Fedora), `.deb` (Debian, Ubuntu)
- macOS: `.dmg` (Intel and Apple processors)
- Windows: `.msi` and `-setup.exe`
- `SHA256SUMS`
