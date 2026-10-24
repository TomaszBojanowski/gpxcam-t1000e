"""Tests of tools/app_frontend.py, on the PC: python3 -I test/tools/test_app_frontend.py"""
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
spec = importlib.util.spec_from_file_location("app_frontend", ROOT / "tools" / "app_frontend.py")
app_frontend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app_frontend)


class AppPage(unittest.TestCase):
    def test_only_the_policy_changes(self):
        page = (ROOT / "docs" / "index.html").read_text(encoding="utf-8")
        app = app_frontend.app_page(page)
        changed = [(a, b) for a, b in zip(page.splitlines(), app.splitlines()) if a != b]
        self.assertEqual(len(page.splitlines()), len(app.splitlines()))
        self.assertEqual(len(changed), 1)
        before, after = changed[0]
        self.assertIn('http-equiv="Content-Security-Policy"', before)
        self.assertEqual(after, before.replace("default-src 'none'; ",
                                               "default-src 'none'; connect-src ipc: http://ipc.localhost; "))

    def test_an_unexpected_policy_is_refused(self):
        meta = '<meta http-equiv="Content-Security-Policy" content="{}">'
        for page in ["<html></html>", meta.format("default-src 'none'; img-src data:") * 2,
                     meta.format("default-src 'self'; img-src data:"),
                     meta.format("default-src 'none'; connect-src https:")]:
            with self.assertRaises(ValueError, msg=page):
                app_frontend.app_page(page)


if __name__ == "__main__":
    unittest.main()
