"""Makes the desktop app's page, app/dist/index.html, from the card page docs/index.html with one
change: the content security policy lets the page talk to the app's Rust side over Tauri's IPC
(connect-src ipc: http://ipc.localhost, as Tauri's documentation of security.csp gives it).
Everything else stays as on GitHub Pages. Run by tauri.conf.json before 'cargo tauri dev' and
'cargo tauri build'. Usage: python3 tools/app_frontend.py"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAGE = ROOT / "docs" / "index.html"
OUT = ROOT / "app" / "dist" / "index.html"

META = '<meta http-equiv="Content-Security-Policy" content="'
PAGE_POLICY_START = "default-src 'none'; "
APP_POLICY_START = "default-src 'none'; connect-src ipc: http://ipc.localhost; "


def app_page(html):
    """The page with the app's policy; ValueError if the page's policy is not the expected one."""
    if html.count(META) != 1:
        raise ValueError("the page needs exactly one Content-Security-Policy meta element")
    start = html.index(META) + len(META)
    if not html.startswith(PAGE_POLICY_START, start) or "connect-src" in html[start:html.index('"', start)]:
        raise ValueError("the page's policy does not start with default-src 'none' alone")
    return html[:start] + APP_POLICY_START + html[start + len(PAGE_POLICY_START):]


def main():
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(app_page(PAGE.read_text(encoding="utf-8")), encoding="utf-8")
    print(f"app_frontend: wrote {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
