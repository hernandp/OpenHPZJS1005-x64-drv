# Candidate build automation

The GitHub Actions workflow in .github/workflows/ci.yml runs on pushes to main
and codex branches, pull requests, and manual dispatch. It uses a Windows Server
2022 runner with Visual Studio 2022 and Python 3.13. This build environment is
not a supported-printer-OS claim.

The workflow checks project-owned C++ formatting with clang-format 22.1.3,
runs the archive verifier's tests, then builds an unsigned engineering candidate
using scripts/package-candidate.ps1. That builder compiles its exact source
snapshot, runs all seven CTest suites, checks x64 PE/runtime imports and CMake/INF
version agreement, and runs GpdCheck, InfVerif and Inf2Cat. Setup obtains its
package version from the INF. Consistent executable version resources remain
separate release work.

scripts/prepare-ci-wdk.ps1 downloads Microsoft's WDK x64 NuGet package
10.0.26100.6584 and verifies a pinned SHA-256 before extracting it to a new
temporary directory. Only its validation tools are used; there is no machine
installation. The tools retain the 10.0.26100.0 layout and the existing
10_GE_X64 catalog target. Updating the package hash/version requires reviewing
new tool output and the supported catalog target.

The final scripts/verify-candidate.py check opens both ZIPs without extracting
or running their contents. It checks complete manifests, sizes and hashes,
source/binary provenance, matching package metadata and the exact production
file allowlist. Unexpected firmware or test binaries, Setup fault-injection
hooks, unsafe paths and version mismatches fail the check. CI also rejects a
candidate marked as built from a modified checkout. This is consistency
verification, not publisher-signature verification.

Successful runs retain the source ZIP, unsigned binary ZIP and SHA256SUMS for
14 days. Validation logs are retained even when candidate building fails.
Only those explicit paths are uploaded; the WDK extraction, private local
firmware and existing developer artifacts are not CI inputs or outputs.
GitHub Actions artifacts are temporary engineering results, not public releases.

All actions are pinned by commit. The workflow has read-only repository
permissions, does not persist checkout credentials, and receives no signing
keys. It runs no printer installation, physical print, certificate import,
VM maintenance or public-release publishing step.

## Local reproduction

With Visual Studio C++, Python 3 and PowerShell 7 available:

~~~powershell
./scripts/format.ps1 -Check
python -m unittest discover -s tests -p test_verify_candidate.py -v
$wdk = ./scripts/prepare-ci-wdk.ps1 -Destination artifacts/ci-wdk-new
./scripts/package-candidate.ps1 -WdkRoot $wdk -OutputPathFile artifacts/candidate-path.txt
python ./scripts/verify-candidate.py (Get-Content artifacts/candidate-path.txt)
~~~

Use a new extraction destination each time. To reuse an already downloaded
package, pass -PackagePath to prepare-ci-wdk.ps1; its hash is still checked.
Add --require-clean to verification for a clean checkout, as CI does. Local
engineering work can retain a modified-source provenance flag.

The first hosted GitHub run must still be checked after this workflow is pushed.
Local execution cannot validate hosted-runner provisioning or artifact upload.
Protected signing, reviewed release tags, public certificate trust and physical
printer acceptance remain separate release gates.

References: Microsoft's [WDK NuGet documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/install-the-wdk-using-nuget),
the pinned [WDK package](https://www.nuget.org/packages/Microsoft.Windows.WDK.x64/10.0.26100.6584),
and GitHub's [Windows runner image](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md).
