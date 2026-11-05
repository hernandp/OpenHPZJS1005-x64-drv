"""Verify the firmware-free mini ZIP against its validated signed input package."""
import argparse
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location("tester", Path(__file__).with_name("verify-test-package.py"))
tester = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tester)
candidate = tester.candidate
require = candidate.require
digest = candidate.digest
FIRMWARE_HASH = "638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1"
PAYLOAD = {"lj1005.inf", "lj1005.gpd", "lj1005.ini", "lj1005render.dll",
           "lj1005monitor.dll", "lj1005.cat", "LICENSE", "THIRD_PARTY.md",
           "README.md", "BUILD-INFO.json", "TEST-CERTIFICATE.cer"}


def verify(directory, signed_input, original):
    directory, signed_input = Path(directory), Path(signed_input)
    tester.verify(signed_input, original)
    archives = list(directory.glob("*-mini-self-signed-test.zip"))
    require(len(archives) == 1, "Expected one mini ZIP")
    files = candidate.read_archive(archives[0], "HPLJ1005-mini")
    require(set(files) == PAYLOAD, "Mini payload differs: firmware/tools/unexpected files are forbidden")
    require(all(digest(data) != FIRMWARE_HASH for data in files.values()), "Firmware leaked into ZIP")
    baseline = candidate.read_archive(next(signed_input.glob("*-self-signed-test.zip")), "HPLJ1005x64")
    info = json.loads(files["BUILD-INFO.json"].decode("utf-8-sig"))
    prior = json.loads(baseline["BUILD-INFO.json"].decode("utf-8-sig"))
    require(info["packageState"] == "mini-self-signed-test-rehearsal", "Wrong package state")
    require(info["requiredFirmware"] == {
        "file": "sihp1005.img", "bytes": 116892, "sha256": FIRMWARE_HASH,
        "included": False, "installedFile": r"%SystemRoot%\System32\lj1005-firmware.img"
    }, "Wrong firmware contract")
    require(set(info) == set(prior) | {"requiredFirmware"}, "Unexpected metadata")
    for key in prior:
        if key != "packageState":
            require(info[key] == prior[key], "Changed provenance: " + key)
    for name in ("lj1005.gpd", "lj1005.ini", "lj1005render.dll", "lj1005monitor.dll"):
        require(files[name] == baseline["driver/" + name], "Changed driver payload: " + name)
    for name in ("LICENSE", "THIRD_PARTY.md", "TEST-CERTIFICATE.cer"):
        require(files[name] == baseline[name], "Changed attribution/identity: " + name)
    inf = baseline["driver/lj1005.inf"].decode("ascii").replace("\r\n", "\n")
    inf = inf.replace("CopyFiles=DriverFiles,MonitorFiles", "CopyFiles=DriverFiles,MonitorFiles,FirmwareFiles")
    inf = inf.replace("[DestinationDirs]", "[FirmwareFiles]\nlj1005-firmware.img,sihp1005.img\n\n[DestinationDirs]\nFirmwareFiles=11")
    inf = inf.replace("[SourceDisksFiles]", "[SourceDisksFiles]\nsihp1005.img=1")
    require(files["lj1005.inf"].decode("ascii").replace("\r\n", "\n") == inf, "Unexpected INF changes")
    require(files["lj1005.cat"] != baseline["driver/lj1005.cat"], "Catalog was not regenerated")
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
    return {"status": "passed", "firmwareIncluded": False, "files": len(files),
            "wizardAcceptance": "pending VM", "catalogMembership": "requires Windows verification"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("signed_input", type=Path)
    parser.add_argument("original", type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.directory, args.signed_input, args.original), indent=2))
