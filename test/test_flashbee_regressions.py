import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FlashbeeRegressionTests(unittest.TestCase):
    def read(self, rel):
        return (ROOT / rel).read_text()

    def test_sensitive_defaults_do_not_ratchet_on_disturbers(self):
        source = self.read("src/fb_as3935.cpp")

        self.assertIn("#define WDTH_DEFAULT       1", source)
        self.assertIn("#define SREJ_DEFAULT       0", source)
        self.assertRegex(
            source,
            r'\{\s*"STORM",\s+1,\s+0,\s+2,\s+0,\s+false,\s+true\s+\}',
        )
        self.assertIn("sensor.watchdogLvl = k.wd", source)
        self.assertIn("sensor.spikeRej    = k.sr", source)

        disturber_branch = re.search(
            r"else if \(reason == INT_D\) \{(?P<body>.*?)\n      \} else if \(reason == INT_L\)",
            source,
            re.S,
        )
        self.assertIsNotNone(disturber_branch)
        self.assertNotIn("tightenFilters()", disturber_branch.group("body"))

    def test_standalone_storm_test_uses_sensitive_defaults(self):
        source = self.read("src/as3935_test.cpp")

        self.assertIn("#define WDTH_DEFAULT       1", source)
        self.assertIn("#define SREJ_DEFAULT       0", source)
        self.assertIn("(NF_DEFAULT << 4) | WDTH_DEFAULT", source)
        self.assertIn("(0 << 4) | SREJ_DEFAULT", source)

    def test_disturber_activity_wakes_display_without_banner(self):
        main_source = self.read("src/fb_main.cpp")
        display_source = self.read("src/fb_display.cpp")

        self.assertIn("lastDisturbers", main_source)
        self.assertIsNotNone(
            re.search(
                r"sensor\.disturberCount\s*!=\s*lastDisturbers.*?powerMarkActivity\(\)",
                main_source,
                re.S,
            )
        )

        self.assertNotIn("STORM ACTIVITY", display_source)
        self.assertNotIn("activityBanner(now)", display_source)


if __name__ == "__main__":
    unittest.main()
