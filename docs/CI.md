# Build verification

GitHub Actions runs on pushes to main, mini-install and codex branches, on pull requests and on manual dispatch. The windows-2022 runner checks C++ layout with clang-format 22.1.3 and enforces the [namespace and constant conventions](CODING_STYLE.md). It also runs Python archive and firmware INF tests and checks support-report privacy under both PowerShell versions.

The unsigned candidate builds from an archived source snapshot. It runs all seven CTest suites, checks x64 imports and version resources for the driver DLLs, validates the INF and GPD and records build provenance. CI verifies both archive manifests and the exact payload against the clean commit. Source and unsigned archives, checksums and validation logs have a 14-day retention period.

## Local reproduction

From the repository root, use Visual Studio C++, the Windows SDK, Python 3 and PowerShell 7:

```powershell
./scripts/format.ps1 -Check
python -m unittest discover -s tests -p 'test_*.py' -v
./tests/test-support-report.ps1
powershell.exe -NoProfile -File ./tests/test-support-report.ps1
$wdk = ./scripts/prepare-ci-wdk.ps1 -Destination artifacts/ci-wdk-new
./scripts/package-candidate.ps1 -WdkRoot $wdk -OutputPathFile artifacts/candidate-path.txt
python ./scripts/verify-candidate.py (Get-Content artifacts/candidate-path.txt) --require-clean
```

Supply a new WDK extraction directory. The preparation script checks the pinned NuGet package hash. The unsigned build requires neither a signing key nor HP firmware. Catalog generation belongs to the separate [tester build](MINI_INSTALL.md#build-locally).
