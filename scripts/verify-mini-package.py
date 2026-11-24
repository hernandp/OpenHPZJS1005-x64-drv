# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
"""Verify the firmware-free mini ZIP against its unsigned build candidate."""
import argparse
import importlib.util
import hashlib
import struct
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location("candidate", Path(__file__).with_name("verify-candidate.py"))
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)
require = candidate.require
digest = candidate.digest
FIRMWARE_HASH = "638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1"
PAYLOAD = {"lj1005.inf", "lj1005.gpd", "lj1005.ini", "lj1005render.dll",
           "lj1005monitor.dll", "lj1005.cat", "LICENSE", "THIRD_PARTY.md",
           "README.md", "BUILD-INFO.json", "TEST-CERTIFICATE.cer", "Preinstall.ps1"}
OPTIONAL_HELPERS = {"Uninstall.ps1"}


def compare_pe(original, signed):
    """Allow only Authenticode's checksum, security directory, padding and certificate addition."""
    pe = struct.unpack_from("<I", original, 0x3c)[0]
    require(original[pe:pe + 4] == b"PE\0\0", "Invalid PE")
    optional = pe + 24
    require(struct.unpack_from("<H", original, optional)[0] == 0x20b, "Expected PE32+")
    security = optional + 112 + 8 * 4
    require(original[security:security + 8] == b"\0" * 8, "Input PE already signed")
    offset, size = struct.unpack_from("<II", signed, security)
    require(size > 8 and offset >= len(original) and offset % 8 == 0
            and offset + size == len(signed), "Invalid added certificate table")
    require(not any(signed[len(original):offset]), "Unexpected data before certificate")
    length, revision, kind = struct.unpack_from("<IHH", signed, offset)
    require(8 < length <= size and revision == 0x200 and kind == 2, "Invalid WIN_CERTIFICATE")
    a, b = bytearray(original), bytearray(signed[:len(original)])
    for start, count in [(optional + 64, 4), (security, 8)]:
        a[start:start + count] = b"\0" * count
        b[start:start + count] = b"\0" * count
    require(a == b, "PE code/data changed beyond signing fields")


def verify(directory, original):
    directory, original = Path(directory), Path(original)
    candidate.verify(original)
    archives = list(directory.glob("*-mini-self-signed-test.zip"))
    require(len(archives) == 1, "Expected one mini ZIP")
    files = candidate.read_archive(archives[0], "HPLJ1005-mini")
    baseline = candidate.read_archive(next(original.glob("*-unsigned-candidate.zip")), "HPLJ1005x64")
    plugins = ("lj1005render.dll", "lj1005monitor.dll")
    if "driver/lj1005ui.dll" in baseline:
        plugins += ("lj1005ui.dll",)
    require(PAYLOAD | set(plugins) <= set(files) <= PAYLOAD | set(plugins) | OPTIONAL_HELPERS,
            "Mini payload differs: firmware and unexpected files are forbidden")
    require(all(digest(data) != FIRMWARE_HASH for data in files.values()), "Firmware leaked into ZIP")
    info = json.loads(files["BUILD-INFO.json"].decode("utf-8-sig"))
    prior = json.loads(baseline["BUILD-INFO.json"].decode("utf-8-sig"))
    require(info["packageState"] == "mini-self-signed-test-rehearsal", "Wrong package state")
    require(info["requiredFirmware"] == {
        "file": "sihp1005.img", "bytes": 116892, "sha256": FIRMWARE_HASH,
        "included": False, "installedFile": r"%SystemRoot%\System32\lj1005-firmware.img"
    }, "Wrong firmware contract")
    require(set(info) == set(prior) | {"requiredFirmware", "signing"}, "Unexpected metadata")
    for key in prior:
        if key != "packageState":
            require(info[key] == prior[key], "Changed provenance: " + key)
    require(any(archives[0].name == f"{prefix}-{info['version']}-mini-self-signed-test.zip"
                for prefix in candidate.ARCHIVE_PREFIXES), "Archive/version mismatch")
    for name in ("lj1005.inf", "lj1005.gpd", "lj1005.ini"):
        require(files[name] == baseline["driver/" + name], "Changed driver payload: " + name)
    for name in plugins:
        compare_pe(baseline["driver/" + name], files[name])
    for name in ("LICENSE", "THIRD_PARTY.md", "README.md"):
        require(files[name] == baseline[name], "Changed packaged file: " + name)
    require(hashlib.sha1(files["TEST-CERTIFICATE.cer"]).hexdigest().upper() ==
            info["signing"]["thumbprintSha1"], "Certificate thumbprint mismatch")
    require(digest(files["TEST-CERTIFICATE.cer"]) == info["signing"]["certificateFileSha256"],
            "Certificate SHA-256 mismatch")
    checksums = {}
    for line in (directory / "SHA256SUMS.txt").read_text(encoding="utf-8-sig").splitlines():
        checksum, name = line.split("  ")
        require(name == Path(name).name and not any(c in name for c in "/\\:"), "Unsafe checksum name")
        require(name not in checksums, "Duplicate checksum")
        require(digest((directory / name).read_bytes()) == checksum, "Archive checksum mismatch")
        checksums[name] = checksum
    require(set(checksums) == {archives[0].name, info["sourceArchive"]}, "Unexpected archive set")
    require({p.name for p in directory.glob("*.zip")} == set(checksums), "Unlisted archive")
    require(digest((directory / info["sourceArchive"]).read_bytes()) == info["sourceArchiveSha256"], "Source mismatch")
    source = candidate.read_archive(directory / info["sourceArchive"], "source")
    require(files["README.md"] == source["docs/MINI_INSTALL.md"], "Guide differs from source")
    for helper in {"Preinstall.ps1"} | OPTIONAL_HELPERS:
        expected = source.get(f"scripts/{helper}")
        require(files.get(helper) == expected, f"{helper} differs from source")
        if expected is not None:
            require(info["signing"]["thumbprintSha1"].encode() in expected
                    and info["signing"]["certificateFileSha256"].upper().encode() in expected,
                    f"{helper} does not pin the bundled certificate")
    return {"status": "passed", "firmwareIncluded": False, "files": len(files),
            "wizardAcceptance": "pending VM", "catalogMembership": "requires Windows verification"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("original", type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.directory, args.original), indent=2))
