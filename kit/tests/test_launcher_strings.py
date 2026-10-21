"""tools/launcher_strings.py: every language has every launcher text, placeholders agree, the app's copy is current."""

import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("launcher_strings", ROOT / "tools/launcher_strings.py")
launcher_strings = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher_strings)


class LauncherStringsTests(unittest.TestCase):
    def test_catalog_is_complete_and_built_in(self):
        text = launcher_strings.JSON.read_text()
        self.assertEqual(launcher_strings.check(json.loads(text)), [])
        self.assertEqual(launcher_strings.SWIFT.read_text(), launcher_strings.catalog_swift(text))

    def test_a_missing_language_and_a_stray_placeholder_are_found(self):
        data = json.loads(launcher_strings.JSON.read_text())
        del data["strings"]["play"]["de"]
        data["strings"]["installing"]["fr"] = "Installation de {nom}…"
        errors = launcher_strings.check(data)
        self.assertIn("play: no de", errors)
        self.assertTrue(any(e.startswith("installing [fr]: placeholders") for e in errors))


if __name__ == "__main__":
    unittest.main()
