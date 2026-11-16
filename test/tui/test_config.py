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
        config.resize(['Office "shade"', 'Café // room /* east */'])
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

    def test_legacy_settings_are_inherited_independently_for_each_shade(self):
        path = self.project / "include/somfy_config.local.h"
        path.write_text('#define SHADE_TRAVEL_OPEN_MS 22000\n#define SHADE_CLOSE_USES_FAVOURITE false\n')
        config = Installation.load(self.project)
        self.assertEqual([s.open_ms for s in config.shades], [22000, 22000, 22000])
        self.assertTrue(all(not s.favourite for s in config.shades))
        config.shades[0].open_ms = 18000
        config.shades[1].favourite = True
        config.save(self.project)
        loaded = Installation.load(self.project)
        self.assertEqual(config, loaded)
        self.assertEqual([s.open_ms for s in loaded.shades], [18000, 22000, 22000])
        self.assertEqual([s.favourite for s in loaded.shades], [False, True, False])

    def test_sparse_per_shade_lists_keep_other_shared_defaults(self):
        (self.project / "include/somfy_config.local.h").write_text(
            '#define SHADE_TRAVEL_OPEN_MS_PER_SHADE { 18000, 22000, 30000, }\n')
        config = Installation.load(self.project)
        self.assertEqual([s.open_ms for s in config.shades], [18000, 22000, 30000])
        self.assertEqual([s.close_ms for s in config.shades], [17100, 17100, 17100])

    def test_wrong_list_lengths_and_bad_times_are_rejected(self):
        path = self.project / "include/somfy_config.local.h"
        for value in ['{ 20000, 22000 }', '{ 20000, 0, 22000 }']:
            path.write_text(f'#define SHADE_TRAVEL_OPEN_MS_PER_SHADE {value}\n')
            with self.assertRaises(ValueError):
                Installation.load(self.project)

    def test_resize_preserves_existing_settings_and_uses_defaults_for_new_shades(self):
        config = Installation.load(self.project)
        config.shades[0].open_ms = 22000
        config.resize([*config.names, "Office"])
        self.assertEqual(config.shades[0].open_ms, 22000)
        self.assertEqual(config.shades[3].open_ms, 17900)
        config.resize(config.names[:2])
        config.save(self.project)
        self.assertEqual(config, Installation.load(self.project))

    def test_multiline_lists_are_replaced_without_changing_other_macros(self):
        path = self.project / "include/somfy_config.local.h"
        unrelated = '#define CUSTOM_PIN_LIST { \\\n    4, 5, 6 }\n'
        path.write_text(unrelated + '#define SHADE_TRAVEL_OPEN_MS_PER_SHADE { \\\n    18000, 22000, 30000 }\n')
        config = Installation.load(self.project)
        config.shades[1].open_ms = 24000
        config.save(self.project)
        self.assertIn(unrelated, path.read_text())
        self.assertEqual(config, Installation.load(self.project))
