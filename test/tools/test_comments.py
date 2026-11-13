"""Comments say what and why, not when: python3 -I test/tools/test_comments.py"""
import io
import pathlib
import re
import tokenize
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent

# The code and its tests, by the patterns of their files.
SOURCES = {
    "src": ("*.cpp", "*.h"),
    "app/src-tauri/src": ("*.rs",),
    "test": ("*.cpp", "*.h", "*.mjs", "*.py"),
    "docs": ("index.html",),
}

# Words of a change rather than of the code as it is.
HISTORY_WORDS = re.compile(r"\b(?:used to|previously|formerly|no longer|anymore)\b", re.IGNORECASE)
DATE = re.compile(r"\b20\d\d-\d\d-\d\d\b")
TIME_OF_DAY = re.compile(r"\b\d\d:\d\d\b")  # a date with a time is a timestamp of the data, not a day of a change

C_COMMENT = re.compile(r"(?<![:\"'\\])//[^\n]*|/\*.*?\*/", re.DOTALL)
HTML_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)


def comments(path):
    """(line number, text) of each comment, and of each docstring in Python."""
    text = path.read_text(encoding="utf-8")
    if path.suffix == ".py":
        tokens = tokenize.generate_tokens(io.StringIO(text).readline)
        return [(token.start[0], token.string) for token in tokens
                if token.type == tokenize.COMMENT
                or (token.type == tokenize.STRING and token.string.startswith('"""'))]
    found = list(C_COMMENT.finditer(text))
    if path.suffix == ".html":
        found += HTML_COMMENT.finditer(text)
    return [(text.count("\n", 0, match.start()) + 1, match.group()) for match in found]


def tells_when(line):
    """Whether a line of a comment tells when, not what."""
    return bool(HISTORY_WORDS.search(line) or (DATE.search(line) and not TIME_OF_DAY.search(line)))


class Comments(unittest.TestCase):
    maxDiff = None

    def test_no_comment_tells_the_history_of_the_code(self):
        files = [path for folder, patterns in SOURCES.items() for pattern in patterns
                 for path in sorted((ROOT / folder).rglob(pattern))]
        self.assertGreater(len(files), 100)
        found = [f"{path.relative_to(ROOT)}:{number + offset}: {line.strip()}"
                 for path in files for number, comment in comments(path)
                 for offset, line in enumerate(comment.splitlines()) if tells_when(line)]
        self.assertEqual(found, [])

    def test_the_words_and_dates_of_a_change_are_found(self):
        self.assertTrue(tells_when("// Pair again used to leave its last stage"))
        self.assertTrue(tells_when("/* checked on 2026-10-22 */"))
        self.assertFalse(tells_when("// 2026-10-13 22:30:29 UTC, Europe/Warsaw"))


if __name__ == "__main__":
    unittest.main()
