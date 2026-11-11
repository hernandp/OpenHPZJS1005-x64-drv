# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
import configparser
from pathlib import Path
import unittest


class HaveDiskFirmware(unittest.TestCase):
    def test_wizard_copies_user_image_to_monitor_location(self):
        # Model entries can repeat, and CopyFiles sections contain bare filenames.
        inf = configparser.ConfigParser(interpolation=None, strict=False, allow_no_value=True)
        path = Path(__file__).resolve().parents[1] / "driver/lj1005.inf"
        with path.open(encoding="ascii") as stream:
            inf.read_file(stream)

        self.assertIn("FirmwareFiles", [name.strip() for name in inf["Install"]["CopyFiles"].split(",")])
        self.assertIn("lj1005-firmware.img,sihp1005.img", inf["FirmwareFiles"])
        self.assertEqual(inf["DestinationDirs"]["FirmwareFiles"], "11")
        disk = inf["SourceDisksFiles"]["sihp1005.img"]
        self.assertIn(disk, inf["SourceDisksNames"])
        self.assertEqual(inf["Version"]["CatalogFile"], "lj1005.cat")
