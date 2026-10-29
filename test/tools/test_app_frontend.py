"""Tests of tools/app_frontend.py, on the PC: python3 -I test/tools/test_app_frontend.py"""
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
spec = importlib.util.spec_from_file_location("app_frontend", ROOT / "tools" / "app_frontend.py")
app_frontend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app_frontend)


CARGO = """[package]
name = "gpxcam-card"
version = "{}"
"""


class AppPage(unittest.TestCase):
    def test_only_the_policy_and_the_version_change(self):
        page = (ROOT / "docs" / "index.html").read_text(encoding="utf-8")
        app = app_frontend.app_page(page, "1.2.3")
        changed = [(a, b) for a, b in zip(page.splitlines(), app.splitlines()) if a != b]
        self.assertEqual(len(page.splitlines()), len(app.splitlines()))
        self.assertEqual(len(changed), 2)
        (policy_before, policy_after), (version_before, version_after) = changed
        self.assertIn('http-equiv="Content-Security-Policy"', policy_before)
        self.assertEqual(policy_after, policy_before.replace(
            "default-src 'none'; ", "default-src 'none'; connect-src ipc: http://ipc.localhost; "))
        self.assertEqual(version_before.strip(), app_frontend.VERSION_SLOT)
        self.assertEqual(version_after.strip(), '<p id="about-version">gpxcam card 1.2.3</p>')

    def test_an_unexpected_policy_is_refused(self):
        meta = '<meta http-equiv="Content-Security-Policy" content="{}">'
        for page in ["<html></html>", meta.format("default-src 'none'; img-src data:") * 2,
                     meta.format("default-src 'self'; img-src data:"),
                     meta.format("default-src 'none'; connect-src https:")]:
            with self.assertRaises(ValueError, msg=page):
                app_frontend.app_page(page + app_frontend.VERSION_SLOT, "1.2.3")

    def test_the_scripts_of_the_page_are_its_files(self):
        page = (ROOT / "docs" / "index.html").read_text(encoding="utf-8")
        names = app_frontend.page_scripts(page)
        self.assertEqual(names, ["zones.js"])
        for name in names:
            self.assertTrue((ROOT / "docs" / name).is_file(), name)
        for script in ['<script src="https://example.com/x.js"></script>', '<script src="../zones.js"></script>',
                       '<script type="module" src="zones.js"></script>']:
            with self.assertRaises(ValueError, msg=script):
                app_frontend.page_scripts(script)

    def test_a_page_without_one_place_for_the_version_is_refused(self):
        meta = '<meta http-equiv="Content-Security-Policy" content="default-src \'none\'; img-src data:">'
        for slots in [0, 2]:
            with self.assertRaises(ValueError, msg=slots):
                app_frontend.app_page(meta + app_frontend.VERSION_SLOT * slots, "1.2.3")


class AppVersion(unittest.TestCase):
    def test_the_version_comes_from_the_package(self):
        self.assertEqual(app_frontend.app_version(CARGO.format("1.0.0")), "1.0.0")

    def test_the_app_has_a_version(self):
        cargo = (ROOT / "app" / "src-tauri" / "Cargo.toml").read_text(encoding="utf-8")
        self.assertRegex(app_frontend.app_version(cargo), r"^\d+\.\d+\.\d+$")

    def test_only_x_y_z_reaches_the_page(self):
        for version in ["1.0", "1.0.0-beta", "1.0.0<script>", ""]:
            with self.assertRaises(ValueError, msg=version):
                app_frontend.app_version(CARGO.format(version))


if __name__ == "__main__":
    unittest.main()
