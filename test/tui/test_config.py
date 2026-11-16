from pathlib import Path
import tempfile
import unittest

from somfy_setup.config import Installation


PROJECT = Path(__file__).resolve().parents[2]


class ConfigTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)
        (self.project / "include").mkdir()
        (self.project / "include/somfy_config.h").write_text((PROJECT / "include/somfy_config.h").read_text())

    def test_roundtrip_preserves_pins_and_include_guard(self):
        path = self.project / "include/somfy_config.local.h"
        path.write_text('#ifndef CUSTOM_H\n#define CUSTOM_H\n#define PIN_SCK 9\n#endif\n')
        config = Installation.load(self.project)
        config.names = ['Office "shade"', 'Café // room /* east */']
        config.address = 0x2A4100
        config.save(self.project)
        loaded = Installation.load(self.project)
        self.assertEqual(config, loaded)
        text = path.read_text()
        self.assertIn("#define PIN_SCK 9", text)
        self.assertLess(text.index("#define SHADE_COUNT"), text.index("#endif"))

    def test_invalid_config_does_not_touch_file(self):
        config = Installation.load(self.project)
        config.address = 0xFFFFFF
        with self.assertRaises(ValueError):
            config.save(self.project)
        self.assertFalse((self.project / "include/somfy_config.local.h").exists())

    def test_unsupported_expression_refuses_to_guess(self):
        (self.project / "include/somfy_config.local.h").write_text('#define SHADE_TRAVEL_OPEN_MS (1000 * 20)\n')
        with self.assertRaisesRegex(ValueError, "not changed"):
            Installation.load(self.project)
