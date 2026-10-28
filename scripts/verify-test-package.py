"""Verify rehearsal archive integrity/provenance; Windows signature trust is a separate VM gate."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location("candidate", Path(__file__).with_name("verify-candidate.py"))
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)
require = candidate.require
digest = candidate.digest

DOCS = {"TESTER_RELEASE.md", "VM_TESTER_REHEARSAL.md", "USER_GUIDE.md",
        "RELEASE_ACCEPTANCE.md", "DEVELOPMENT_MIGRATION.md", "UPGRADE_ROLLBACK.md",
        "TEST_STATUS.md"}
SIGNED_PE = {"Setup.exe", "tools/lj1005.exe", "driver/lj1005render.dll", "driver/lj1005monitor.dll"}


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


def verify(directory, original_directory):
    directory, original_directory = Path(directory), Path(original_directory)
    original_report = candidate.verify(original_directory)
    unsigned_zip = next(original_directory.glob("*-unsigned-candidate.zip"))
    original = candidate.read_archive(unsigned_zip, "HPLJ1005x64")
    archive = list(directory.glob("*-self-signed-test.zip"))
    require(len(archive) == 1, "Expected one self-signed test archive")
    files = candidate.read_archive(archive[0], "HPLJ1005x64")
    expected = candidate.PAYLOAD | {"TEST-CERTIFICATE.cer", "SIGNING-INFO.json"} | {
        "docs/" + name for name in DOCS}
    require(set(files) == expected, "Tester payload allowlist differs")
    checksums = {}
    for line in (directory / "SHA256SUMS.txt").read_text().splitlines():
        checksum, name = line.split("  ")
        require(Path(name).name == name and "/" not in name and "\\" not in name and ":" not in name,
                "Unsafe checksum filename")
        require(name not in checksums, "Duplicate checksum filename")
        require(digest((directory / name).read_bytes()) == checksum, "Checksum mismatch: " + name)
        checksums[name] = checksum
    info = json.loads(files["BUILD-INFO.json"].decode("utf-8-sig"))
    old_info = json.loads(original["BUILD-INFO.json"].decode("utf-8-sig"))
    signing = json.loads(files["SIGNING-INFO.json"].decode("utf-8-sig"))
    require(set(checksums) == {archive[0].name, info["sourceArchive"], "TEST-CERTIFICATE.cer"},
            "Unexpected checksum set")
    require(digest((directory / info["sourceArchive"]).read_bytes()) == info["sourceArchiveSha256"],
            "Source provenance mismatch")
    require(info["packageState"] == "self-signed-test-rehearsal"
            and info["cleanMachineTested"] is False and info["signing"] == signing,
            "Invalid rehearsal state")
    for key, value in old_info.items():
        if key != "packageState":
            require(info[key] == value, "Changed source provenance: " + key)
    require(signing["vmRehearsal"] == "pending" and signing["publication"] == "not-published"
            and signing["hostTrustAdded"] is False and signing["timestamped"] is False
            and signing["windowsTrustAndCatalogMembershipRehearsed"] is False,
            "Unexpected acceptance claim")
    cert = files["TEST-CERTIFICATE.cer"]
    require(cert == (directory / "TEST-CERTIFICATE.cer").read_bytes(), "Certificate copies differ")
    require(digest(cert) == signing["certificateFileSha256"], "Certificate digest mismatch")
    require(hashlib.sha1(cert).hexdigest().upper() == signing["thumbprintSha1"].upper(),
            "Certificate thumbprint mismatch")
    source = candidate.read_archive(directory / info["sourceArchive"], "source")
    for name in DOCS:
        require(files["docs/" + name] == source["docs/" + name], "Changed packaged guide: " + name)
    for name in candidate.PAYLOAD - SIGNED_PE - {"driver/lj1005.cat", "BUILD-INFO.json", "README.md"}:
        require(files[name] == original[name], "Unexpected payload change: " + name)
    for name in SIGNED_PE:
        compare_pe(original[name], files[name])
    require(b"SELF-SIGNED TEST BUILD" in files["README.md"]
            and signing["thumbprintSha1"].encode() in files["README.md"], "Missing test identity/label")
    return {"status": "passed", "signatureTrustAcceptance": "pending VM",
            "peCodeAndDataUnchangedBySigning": True, "payloadFiles": len(files),
            "version": original_report["version"], "archivesAndCertificate": checksums}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("original_candidate", type=Path)
    args = parser.parse_args()
    report = verify(args.directory, args.original_candidate)
    text = json.dumps(report, indent=2) + "\n"
    (args.directory / "validation" / "tester-archive-verification.json").write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
