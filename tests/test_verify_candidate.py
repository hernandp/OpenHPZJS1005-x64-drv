# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile


spec = importlib.util.spec_from_file_location(
    "verify_candidate", Path(__file__).resolve().parents[1] / "scripts/verify-candidate.py"
)
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class CandidateVerification(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = {
            "CMakeLists.txt": b"project(HPLJ1005x64 VERSION 0.1.1 LANGUAGES C CXX)",
            "driver/lj1005.inf": b"DriverVer=09/27/2026,0.1.1.0\n",
            "driver/lj1005.gpd": b"gpd", "driver/lj1005.ini": b"ini",
            "docs/MINI_INSTALL.md": b"engineering preview",
            "CHANGELOG.md": b"# Changelog\n\nDevelopment version history.\n",
            "LICENSE": b"license", "THIRD_PARTY.md": b"third party",
        }
        # These bytes model archive contents, not runnable PE files. PE/import
        # validation belongs to the candidate builder and real-package checks.
        self.binary = dict.fromkeys(verifier.PAYLOAD, b"fixture")
        for name in ("driver/lj1005.inf", "driver/lj1005.gpd", "driver/lj1005.ini",
                     "LICENSE", "THIRD_PARTY.md"):
            self.binary[name] = self.source[name]
        self.binary["README.md"] = self.source["docs/MINI_INSTALL.md"]
        self.info = {
            "version": "0.1.1", "baseCommit": "a" * 40, "workingTreeModified": False,
            "packageState": "unsigned-engineering-candidate", "cleanMachineTested": False,
        }

    def archive(self, name, root, files, corrupt_manifest=False):
        entries = [
            {"path": path, "sha256": verifier.digest(data), "bytes": len(data)}
            for path, data in files.items()
        ]
        if corrupt_manifest:
            entries[0]["sha256"] = "0" * 64
        with zipfile.ZipFile(self.root / name, "w") as archive:
            for path, data in files.items():
                archive.writestr(f"{root}/{path}", data)
            archive.writestr(f"{root}/SHA256-MANIFEST.json", json.dumps(entries))
        return verifier.digest((self.root / name).read_bytes())

    def build(self, corrupt_manifest=False, bad_provenance=False):
        source_name = "HPLJ1005x64-0.1.1-source.zip"
        source_hash = self.archive(source_name, "source", self.source, corrupt_manifest)
        self.info.update(sourceArchive=source_name, sourceArchiveSha256=source_hash)
        if bad_provenance:
            self.info["sourceArchiveSha256"] = "0" * 64
        self.binary["BUILD-INFO.json"] = json.dumps(self.info).encode()
        binary_name = "HPLJ1005x64-0.1.1-unsigned-candidate.zip"
        binary_hash = self.archive(binary_name, "HPLJ1005x64", self.binary)
        (self.root / "SHA256SUMS.txt").write_text(
            f"{source_hash}  {source_name}\n{binary_hash}  {binary_name}\n"
        )

    def test_valid_and_dirty_candidates(self):
        self.build()
        self.assertEqual(verifier.verify(self.root, True)["status"], "passed")
        self.info["workingTreeModified"] = True
        self.build()
        verifier.verify(self.root)
        with self.assertRaisesRegex(ValueError, "Dirty source"):
            verifier.verify(self.root, True)

    def test_only_official_picture_is_allowed_in_source(self):
        self.source["docs/benteveo-and-hp.png"] = b"picture fixture"
        self.build()
        self.assertEqual(verifier.verify(self.root)["status"], "passed")
        self.source["docs/unreviewed.png"] = b"unexpected picture"
        self.build()
        with self.assertRaisesRegex(ValueError, "Unexpected source"):
            verifier.verify(self.root)

    def test_checksum_tampering(self):
        self.build()
        archive = next(self.root.glob("*-source.zip"))
        archive.write_bytes(archive.read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            verifier.verify(self.root)

    def test_manifest_tampering_with_valid_outer_checksum(self):
        self.build(corrupt_manifest=True)
        with self.assertRaisesRegex(ValueError, "Hash mismatch"):
            verifier.verify(self.root)

    def test_firmware_and_test_binary_are_rejected(self):
        for name in ("driver/firmware.img", "Setup.exe", "tools/lj1005.exe", "driver/lj1005.cat"):
            with self.subTest(name=name):
                self.binary[name] = b"unshippable"
                self.build()
                with self.assertRaisesRegex(ValueError, "payload allowlist"):
                    verifier.verify(self.root)
                del self.binary[name]

    def test_packaged_inf_must_match_source(self):
        self.binary["driver/lj1005.inf"] += b"; changed"
        self.build()
        with self.assertRaisesRegex(ValueError, "INF differs"):
            verifier.verify(self.root)

    def test_provenance_must_identify_source_archive(self):
        self.build(bad_provenance=True)
        with self.assertRaisesRegex(ValueError, "provenance mismatch"):
            verifier.verify(self.root)

    def test_source_path_escape(self):
        self.source["../private/secret.md"] = b"private"
        self.build()
        with self.assertRaisesRegex(ValueError, "Unsafe archive path"):
            verifier.verify(self.root)


if __name__ == "__main__":
    unittest.main()
