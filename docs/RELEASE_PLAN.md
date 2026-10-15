# First public release plan

Goal: download Setup, connect the original LaserJet 1005, import firmware from
the user's old HP driver package, print a test page, then print from applications.
Users should not need a compiler, WDK, PowerShell 7 or manual registry changes.

The first beta should support one original 1005 over USB, on explicitly tested
Windows 11 x64 builds, with A4 monochrome simplex at 600 dpi. Letter requires a
physical-paper test. Parallel, ARM64 and printer sharing remain outside the
initial supported scope, even where development code has partial support.

## 1. Package and installation foundation — lab installation verified

- Completed checkpoint: `c882ffd`, working development implementation and style.
- Declare package-aware Unidrv core dependencies in the INF.
- Statically link the runtime and check shipped binary imports.
- Build a candidate from its exact archived source; include only production
  binaries in the user archive and preserve corresponding GPL source.
- Validate with GpdCheck, InfVerif and Inf2Cat; keep logs and create an unsigned
  catalog. Do not describe these checks as certification.
- Passed on 2026-09-17: unsigned rejection, VM-only lab signing, driver-store
  staging, package-aware registration and five GDI file-capture scenarios on
  clean Windows 11 Pro 25H2 x64. Secure Boot remained enabled. All capture bytes
  matched the Linux reference encoder. See [VM testing](VM_TESTING.md).
- Pending: public certificate trust and full end-user installer acceptance.
  The native preview has since passed lab installation, extended repair and
  removal checks; see [deployment](DEPLOYMENT.md).

Build a candidate with Visual Studio C++, PowerShell 7 and WDK 10.0.26100.0:

```powershell
./scripts/package-candidate.ps1
# For an extracted WDK, use its root containing bin/ and tools/:
./scripts/package-candidate.ps1 -WdkRoot C:/path/to/wdk
```

The script builds in a new `artifacts/rc-*` directory, runs the seven automated
test suites, validates the package and produces source/binary ZIPs and hashes.
It never installs the driver, changes trust stores or sends a print job. Dirty
working copies are allowed for engineering candidates and recorded explicitly;
public releases must come from a reviewed tag. ZIPs are not promised to be
bit-for-bit reproducible across builds: timestamps and toolchains can differ.

Default catalog validation is `10_GE_X64` (Windows 11 24H2 x64). The current
WDK does not recognize the later `10_25H2_X64` target. Select a newer WDK and
`-CatalogOS` when validating that target; local hardware tests on build 26200
do not substitute for the appropriate package validation or a clean install.

## 2. Signed installer, firmware import and removal

Resolve publisher identity, certificate/submission route, timestamping and trust
on a clean PC. Build a GUI installer with elevation only for installation work.
Detect architecture, supported OS, Protected Print Mode, spooler readiness and
the exact USB model. Explain failures without requiring developer terminology.

Import and hash-check firmware selected in a file dialog (`sihp1005.img`); do not bundle HP
firmware without confirmed distribution rights. Install through the driver store,
select the live USB port, enable bidirectional communication and set A4 defaults.
Provide repair, upgrade, rollback and uninstall with ownership tracking. Never
remove unrelated print jobs or overwrite another printer's resources. Resolve
the existing development monitor name and migration strategy before release.

The native Setup preview now implements the firmware file picker, preflight,
protected package cache, driver-store installation, ownership journal,
same-build repair, different-build upgrade, rollback and removal. The maintainer selected VM-only lab signing while
the public certificate route is unresolved. See [deployment](DEPLOYMENT.md) for
the preview's bounds and [test status](TEST_STATUS.md) for exercised paths.
Development migration has a tested maintainer procedure; applying it to the
physical-printer machine and physical USB acceptance remain outstanding.
This milestone is not complete.

Interactive VM checks passed on September 23–24, including firmware selection,
cancellation, errors, elevation and confirmed removal with unrelated-printer
preservation. A foreground-dialog defect found during testing was corrected and
retested. The VM has no USB printer, so successful interactive installation and
physical printing remain separate acceptance checks.

Extended same-build repair passed VM acceptance on 2026-09-24 with candidate
`rc-a6cc1e56`: recovery of the owned queue, cache, uninstaller, firmware and live
driver files; failure/interruption retries; foreign-resource protection; five
reference-matched captures; and removal with restart/retry when Windows retained
the monitor. Unrelated printer preservation and final cleanup passed. The
earlier driver-directory comparison error was corrected. See test status for
the exact build, harness revision scope and evidence.

Different-build upgrade and rollback passed quiet-mode VM acceptance on
2026-09-27, using version 0.1.0 as the installed baseline and 0.1.1 as the target.
The checks cover changed binaries, retained global printer settings/permissions,
restart recovery, explicit rollback, automatic failure rollback, terminated
upgrade/rollback processes and refusal of unsafe sources or dependencies.
See [upgrade and rollback](UPGRADE_ROLLBACK.md) and test status for the exact
candidate and evidence.

Interactive upgrade/restart recovery and rollback also passed on September 27
with `rc-8bd03d60`. Rollback cancellation and the confirmation/success paths were
exercised by the user; installed builds, file hashes and preserved settings were
verified after both operations. A misleading restart error now directs update
users to Upgrade/Roll back; that text-only change postdates the tested package.

Development migration passed an isolated VM rehearsal on September 29 with
rc-90c47676, using the actual host development binaries. Explicit replacement,
settings/permissions preservation, refusal cases, failed-install recovery and
restoration after successful migration passed, with three reference-matched
captures. See [the migration procedure](DEVELOPMENT_MIGRATION.md) and test status.
Setup still refuses automatic adoption of an unowned development installation.

On September 30 the maintainer chose to wait for public signing before physical
installation and continue independent release work. Physical migration, a fresh
complete host recovery bundle and USB acceptance are deferred until that signing
route is ready. Lab certificate trust remains VM-only.

The preparation-retry picker checks passed on October 2 and 15 using clean CI
candidate `rc-54d87b83` from commit `8f336a0`: picker cancellation, invalid firmware,
elevation denial, upgrade-confirmation cancellation and the expected missing-USB
error. The offline retry then exercised the corrected error 3008 restart dialog
and successful recovery through the main menu. Installed candidate hashes,
rollback availability and preserved settings were verified after recovery.
See test status for evidence and scope. This closes that specific VM acceptance
gap; physical USB/print acceptance and any other unexercised branches remain
separate checks.

## 3. Reliability and diagnostics

Validate complete ZJS framing, geometry, page count and resource limits before
sending potentially harmful data; checking only the `JZJZ` prefix is insufficient.
Completed on 2026-09-21 for file-based `usb-print`: strict supported-profile
validation runs before device access, including bounded JBIG decoding, exact
padding, page/copy counts and paper geometry. The offline `validate` command
exposes the same checks. Seven automated suites pass; verified captures remain
accepted and the historical duplicate-header stream is rejected. See
[print validation](PRINT_VALIDATION.md). This gate covers direct file submission;
the spooler continues using the native encoder and its existing monitor checks.

Make paper-out, door-open, disconnected, cancelled and timed-out jobs actionable.
Avoid blind retry of partially transmitted jobs. Bound memory and parser work.
Provide a support report without document contents or firmware binaries.

## 4. Release validation and automation

Test fresh install, upgrade and removal across supported Windows builds; actual
Inkscape Print dialog, PDF and Office output; 32/64-bit applications; paper changes
and ResetDC; copies; empty pages; cancellations; unplug/replug; sleep/wake; no-paper
recovery; repeated power cycles. Keep dated evidence and known limitations.

Build/style/test and unsigned-package CI passed its first complete hosted run
on October 1 (f771508); see [CI](CI.md) for its tools and archive checks.
Windows file/product version resources and Setup's fallback version now derive
from CMake, with configuration/package checks enforcing agreement with the INF.
Complete protected signing and tagged releases. Publish checksums and the exact
corresponding source alongside binaries. Static runtime security fixes require
rebuilding and redistributing the binaries. Choose issue templates and a short
user guide before advertising general availability.
