# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
"""Check candidate archives without extracting or executing their contents."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile


PAYLOAD = {
    "driver/lj1005.inf",
    "driver/lj1005.gpd", "driver/lj1005.ini",
    "driver/lj1005render.dll", "driver/lj1005monitor.dll",
    "LICENSE", "THIRD_PARTY.md", "README.md", "BUILD-INFO.json",
}
SOURCE_ROOT_FILES = {
    "CMakeLists.txt", "LICENSE", "README.md", "CHANGELOG.md", "THIRD_PARTY.md",
    ".gitignore", ".gitattributes", ".clang-format",
}
SOURCE_DIRS = {"driver", "include", "src", "scripts", "tests", "vendor", "docs", ".github"}
SOURCE_EXTENSIONS = {
    ".cpp", ".hpp", ".c", ".h", ".ps1", ".py", ".cmake",
    ".md", ".gpd", ".inf", ".ini", ".def", ".yml",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(name):
    path = PurePosixPath(name)
    require(
        name and "\\" not in name and ":" not in name
        and not path.is_absolute() and ".." not in path.parts
        and str(path) == name,
        f"Unsafe archive path: {name!r}",
    )
    return path


def read_archive(path, root):
    with zipfile.ZipFile(path) as archive:
        files = {}
        seen = set()
        for entry in archive.infolist():
            name = entry.filename.rstrip("/") if entry.is_dir() else entry.filename
            parsed = safe_path(name)
            require(parsed.parts[0] == root, f"Unexpected archive root: {name}")
            require(name.casefold() not in seen, f"Duplicate archive entry: {name}")
            seen.add(name.casefold())
            require((entry.external_attr >> 16) & 0o170000 != 0o120000,
                    f"Archive symlink: {name}")
            if not entry.is_dir():
                require(len(parsed.parts) > 1, "File at archive root")
                files["/".join(parsed.parts[1:])] = archive.read(entry)
    require("SHA256-MANIFEST.json" in files, "Missing archive manifest")
    entries = json.loads(files.pop("SHA256-MANIFEST.json").decode("utf-8-sig"))
    require(isinstance(entries, list) and entries, "Empty or invalid manifest")
    declared = set()
    for entry in entries:
        name = entry["path"]
        safe_path(name)
        require(name.casefold() not in declared, f"Duplicate manifest path: {name}")
        declared.add(name.casefold())
        require(name in files, f"Missing declared file: {name}")
        require(len(files[name]) == entry["bytes"], f"Size mismatch: {name}")
        require(digest(files[name]) == entry["sha256"], f"Hash mismatch: {name}")
    require(declared == {name.casefold() for name in files}, "Unlisted archive file")
    return files


def verify(candidate, require_clean=False):
    candidate = Path(candidate)
    checksums = {}
    for line in (candidate / "SHA256SUMS.txt").read_text(encoding="utf-8-sig").splitlines():
        match = re.fullmatch(r"([a-f0-9]{64})  ([^/\\:]+\.zip)", line)
        require(match is not None, "Malformed SHA256SUMS entry")
        checksum, name = match.groups()
        require(name not in checksums, "Duplicate SHA256SUMS entry")
        checksums[name] = checksum
    require(len(checksums) == 2, "Expected exactly two release archives")
    require({path.name for path in candidate.glob("*.zip")} == set(checksums),
            "Archive/checksum file set differs")
    for name, checksum in checksums.items():
        require(digest((candidate / name).read_bytes()) == checksum,
                f"Archive checksum mismatch: {name}")
    sources = [name for name in checksums if name.endswith("-source.zip")]
    binaries = [name for name in checksums if name.endswith("-unsigned-candidate.zip")]
    require(len(sources) == len(binaries) == 1, "Unexpected archive names")
    source = read_archive(candidate / sources[0], "source")
    binary = read_archive(candidate / binaries[0], "HPLJ1005x64")
    require(set(binary) == PAYLOAD, "Production payload allowlist differs")
    for name in source:
        path = PurePosixPath(name)
        require(name in SOURCE_ROOT_FILES or name in {"driver/version.rc.in", "docs/benteveo-and-hp.png"} or (
            len(path.parts) > 1 and path.parts[0] in SOURCE_DIRS
            and path.suffix in SOURCE_EXTENSIONS
        ), f"Unexpected source file: {name}")
    cmake = source["CMakeLists.txt"].decode("utf-8-sig")
    version_match = re.search(r"project\(HPLJ1005x64 VERSION (\d+\.\d+\.\d+)\b", cmake)
    require(version_match is not None, "Missing CMake version")
    version = version_match.group(1)
    inf = source["driver/lj1005.inf"]
    require(re.search(r"(?m)^DriverVer=[^,\r\n]+," + re.escape(version) + r"\.0\s*$",
                      inf.decode("utf-8-sig")) is not None, "CMake/INF version mismatch")
    require(binary["driver/lj1005.inf"] == inf, "Packaged INF differs from source")
    for name in ("lj1005.gpd", "lj1005.ini"):
        require(binary["driver/" + name] == source["driver/" + name],
                f"Packaged {name} differs from source")
    for name in ("LICENSE", "THIRD_PARTY.md"):
        require(binary[name] == source[name], f"Packaged {name} differs from source")
    require(binary["README.md"] == source["docs/MINI_INSTALL.md"], "Packaged instructions differ")
    info = json.loads(binary["BUILD-INFO.json"].decode("utf-8-sig"))
    require(info["version"] == version, "Build provenance version mismatch")
    require(info["sourceArchive"] == sources[0]
            and info["sourceArchiveSha256"] == checksums[sources[0]],
            "Source archive provenance mismatch")
    require(sources[0] == f"HPLJ1005x64-{version}-source.zip"
            and binaries[0] == f"HPLJ1005x64-{version}-unsigned-candidate.zip",
            "Archive/version mismatch")
    require(info["packageState"] == "unsigned-engineering-candidate"
            and info["cleanMachineTested"] is False, "Unexpected acceptance claim")
    require(re.fullmatch(r"[a-f0-9]{40}", info["baseCommit"]) is not None,
            "Invalid source revision")
    require(isinstance(info["workingTreeModified"], bool), "Invalid dirty-tree flag")
    require(not require_clean or not info["workingTreeModified"], "Dirty source candidate")
    return {
        "status": "passed", "version": version, "baseCommit": info["baseCommit"],
        "workingTreeModified": info["workingTreeModified"],
        "sourceFiles": len(source), "binaryFiles": len(binary),
        "archives": checksums,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--require-clean", action="store_true")
    args = parser.parse_args()
    try:
        report = verify(args.candidate, args.require_clean)
    except (ValueError, KeyError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Candidate verification failed: {error}\n")
    text = json.dumps(report, indent=2) + "\n"
    output = args.candidate / "validation" / "archive-verification.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")
    print(text, end="")


if __name__ == "__main__":
    main()
