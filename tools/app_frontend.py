"""Makes the desktop app's page, app/dist/index.html, from the card page docs/index.html with two
changes: the content security policy lets the page talk to the app's Rust side over Tauri's IPC
(connect-src ipc: http://ipc.localhost, as Tauri's documentation of security.csp gives it), and
"About this app" shows the app's version from app/src-tauri/Cargo.toml. The scripts the page loads
from docs/ (<script src="zones.js">) are copied as they are. Everything else stays as on GitHub
Pages. Run by tauri.conf.json before 'cargo tauri dev' and 'cargo tauri build'.
Usage: python3 tools/app_frontend.py (Python 3.11 or newer, for tomllib)"""
import pathlib
import re
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAGE = ROOT / "docs" / "index.html"
CARGO = ROOT / "app" / "src-tauri" / "Cargo.toml"
OUT = ROOT / "app" / "dist" / "index.html"

META = '<meta http-equiv="Content-Security-Policy" content="'
PAGE_POLICY_START = "default-src 'none'; "
APP_POLICY_START = "default-src 'none'; connect-src ipc: http://ipc.localhost; "

VERSION_SLOT = ('<p id="about-version" class="hidden">'
                "<!-- the desktop app's version, written by tools/app_frontend.py --></p>")
VERSION = re.compile(r"\d+\.\d+\.\d+")  # only digits and dots reach the page
SCRIPT = re.compile(r'<script src="([a-z_]+\.js)"></script>')  # a file of docs/, next to the page


def app_version(cargo_toml):
    """The version of Cargo.toml's [package], such as "1.0.0"; ValueError if it is not x.y.z."""
    version = tomllib.loads(cargo_toml)["package"]["version"]
    if not VERSION.fullmatch(version):
        raise ValueError(f"the app's version is not x.y.z: {version!r}")
    return version


def app_page(html, version):
    """The page with the app's policy and version; ValueError if the page is not as expected."""
    if html.count(META) != 1:
        raise ValueError("the page needs exactly one Content-Security-Policy meta element")
    start = html.index(META) + len(META)
    if not html.startswith(PAGE_POLICY_START, start) or "connect-src" in html[start:html.index('"', start)]:
        raise ValueError("the page's policy does not start with default-src 'none' alone")
    if html.count(VERSION_SLOT) != 1:
        raise ValueError("the page needs exactly one place for the app's version")
    html = html[:start] + APP_POLICY_START + html[start + len(PAGE_POLICY_START):]
    return html.replace(VERSION_SLOT, f'<p id="about-version">gpxcam card {version}</p>')


def page_scripts(html):
    """The files of the scripts the page loads; ValueError for a script from anywhere else."""
    sources = re.findall(r"<script\b[^>]*\bsrc=[^>]*>", html)
    names = SCRIPT.findall(html)
    if len(names) != len(sources):
        raise ValueError("the page loads a script that is not a plain file next to it")
    return names


def main():
    version = app_version(CARGO.read_text(encoding="utf-8"))
    html = PAGE.read_text(encoding="utf-8")
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(app_page(html, version), encoding="utf-8")
    scripts = page_scripts(html)
    for name in scripts:
        (OUT.parent / name).write_bytes((PAGE.parent / name).read_bytes())
    print(f"app_frontend: wrote {OUT.relative_to(ROOT)} (gpxcam card {version}) and {', '.join(scripts)}")


if __name__ == "__main__":
    main()
