# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
"""Mutation tests against a real generated mini package; no signing key or firmware needed."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import zipfile

spec = importlib.util.spec_from_file_location(
    "mini", Path(__file__).resolve().parents[1] / "scripts/verify-mini-package.py")
mini = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mini)


def test_packages(directory, candidate):
    mini.verify(directory, candidate)
    results = []
    cases = {"firmware-leak": "Mini payload differs", "modified-dll": "Invalid added certificate table",
             "altered-inf": "Changed driver payload", "wrong-firmware-hash": "Wrong firmware contract"}
    for case, expected in cases.items():
        with tempfile.TemporaryDirectory(prefix="negative-test-", dir=directory) as temporary:
            target = Path(temporary)
            for path in directory.glob("*.zip"):
                shutil.copyfile(path, target / path.name)
            archive = next(target.glob("*-mini-self-signed-test.zip"))
            with zipfile.ZipFile(archive) as z:
                files = {i.filename: z.read(i) for i in z.infolist() if not i.is_dir()}
            prefix = "HPLJ1005-mini/"
            if case == "firmware-leak":
                files[prefix + "sihp1005.img"] = b"forbidden extra image"
            elif case == "modified-dll":
                files[prefix + "lj1005render.dll"] += b"tampered"
            elif case == "altered-inf":
                files[prefix + "lj1005.inf"] += b"\n; changed\n"
            else:
                info = json.loads(files[prefix + "BUILD-INFO.json"].decode("utf-8-sig"))
                info["requiredFirmware"]["sha256"] = "0" * 64
                files[prefix + "BUILD-INFO.json"] = json.dumps(info).encode()
            # Recompute all ordinary hashes, proving checks go beyond self-consistency.
            files[prefix + "SHA256-MANIFEST.json"] = json.dumps([
                {"path": k[len(prefix):], "bytes": len(v), "sha256": hashlib.sha256(v).hexdigest()}
                for k, v in files.items() if not k.endswith("SHA256-MANIFEST.json")]).encode()
            with zipfile.ZipFile(archive, "w") as z:
                for name, data in files.items():
                    z.writestr(name, data)
            (target / "SHA256SUMS.txt").write_text("".join(
                hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name + "\n"
                for p in target.glob("*.zip")))
            try:
                mini.verify(target, candidate)
            except ValueError as error:
                if expected not in str(error):
                    raise AssertionError(f"{case}: wrong rejection: {error}") from error
                results.append({"case": case, "result": "rejected", "reason": str(error)})
            else:
                raise AssertionError(case + " unexpectedly passed")
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    print(json.dumps(test_packages(args.directory, args.candidate), indent=2))
