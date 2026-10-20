# Development verification — updated 2026-10-20

## Remaining VM application printing matrix (2026-10-19–20)

The unchanged `rc-54d87b83` package (0.1.1, commit `8f336a0`) was tested on
Windows 11 Pro 25H2 x64 26200.6584 using the guest-local Open ZJS `FILE:` queue.
Setup SHA-256 remained
`964e94fabb84aaad4af3173b4ca551f2cfd11a62ecccfe3d724196e5cd515f84`.
Only synthetic documents were used; no physical job was sent.

| Check | Execution path | Captured result |
| --- | --- | --- |
| Two copies of two numbered pages | Edge 140.0.3485.66 x64 actual Print dialog, A4 portrait, Actual size, Copies 2 | Four sheets, `1,2,1,2`; the repeated sets have identical rasters |
| Two copies of the same PDF | SumatraPDF 3.6.1 x86 command-line `2x`, A4, `noscale` | Four sheets, `1,2,1,2`; repeated sets are identical |
| Explicit repeated page ranges | Sumatra command-line `1-2,1-2`, then `1,1,2,2` | Four sheets in each requested order; this tests page sequencing, not a collation toggle |
| Blank middle page | Sumatra actual Print dialog, A4 portrait, all pages, default shrink | Three sheets: source page 1, entirely blank sheet, source page 3; middle raster has zero black pixels |
| A4 portrait → Letter portrait → A4 portrait | Separate Sumatra command-line jobs, explicit paper selection and `noscale` | Correct 4960×7016 / 5100×6600 / 4960×7016 canvases; first and last A4 rasters are identical |
| A4 landscape → A4 portrait | Sumatra actual Print dialog, explicit orientation changes, A4, default shrink | Correct rotation and restored portrait geometry; complete borders, text and shapes |
| Letter portrait → A4 landscape → Letter portrait within one job | Existing x86 `queue_test emf-reset` helper and a synthetic vector EMF | One document, three pages, correct paper dimensions and sequence; entire stream matches reference encoder bytes |

All twelve captured streams passed protocol validation. Eleven are accepted
for the checks above; the remaining CLI landscape attempt is retained as a
settings diagnostic. Accepted Sumatra captures passed the four-pixel border
tolerance, 20 mm square and 100 mm ruler checks. Maximum border deviation was
0.033 mm in portrait and 0.051 mm in the explicit landscape-dialog capture.
Decoded previews confirmed complete content and correct orientation. The dialog's
shrink path and CLI `noscale` path are different settings; their portrait rasters
are not byte-identical, although both pass geometry. The identical A4 restoration
comparison above uses the same CLI settings throughout.

The initial Sumatra CLI `landscape` attempt rotated content on a portrait device
context and clipped the landscape fixture. It is **not** a landscape pass.
The actual dialog follow-up selected Landscape in Windows printing preferences
and passed. Sumatra's [3.6 release source](https://github.com/sumatrapdfreader/sumatrapdf/blob/3.6rel/src/Print.cpp)
distinguishes content rotation from the device orientation; do not use that CLI
flag as evidence that a landscape DEVMODE was selected.

Sumatra's Windows Print dialog hid Copies for this queue; Advanced Options
showed one copy, consistent with the GPD's `MaxCopies: 1`. Nevertheless, its
command-line `2x` request produced four raster pages through Windows printing.
This does not establish device-side copy support. Edge exposed Copies but no
collation toggle; neither tested dialog exposed an uncollated choice. Default
collated copies and explicit uncollated page sequencing passed; user-selectable
uncollated-mode acceptance is not established. Edge retains its known 0.499 mm
border deviation and is not accepted for precise scale.

Final collection found both local queues empty and Normal. The owned `FILE:`
port, saved Letter/simplex/monochrome defaults, unrelated Microsoft Print to PDF,
Setup hash and original source PDF hash were preserved. No driver code or trust
settings changed. Physical Letter, physical copies/page order and USB/firmware
recovery remain pending; Office stays deferred to a future release.

Evidence is local in `artifacts/application-printing-20261019/`: fixture sources
and hashes, `matrix-run.json`, `matrix-validation.json`, `capture-state.json`,
original streams, decoded rasters/previews and the synthetic EMF. The adjacent
`run-print-matrix-vm.ps1` records exact CLI arguments and guards the VM identity,
disconnected network, idle `FILE:` queue and installed package/viewer hashes.
`tests/verify_reset_capture.py` passed the in-job reference comparison.

## Real application printing in the packaged-driver VM (2026-10-15–17)

Inkscape 1.4.4 printed harmless A4 portrait and landscape SVG documents through
its actual Windows Print dialog to `HP LaserJet 1005 (Open ZJS)` on `FILE:`.
The installed 0.1.1 Setup hash matches candidate `rc-54d87b83`, commit `8f336a0`.
Both jobs selected A4 and one copy; landscape orientation was selected explicitly.
The VM remains Windows 11 Pro 25H2 26200.6584 with guest-only lab trust.

Both captured printer streams passed protocol validation and decoded as one
4960 by 7016 raster sheet at 600 dpi. Landscape content has the expected rotation.
Visual inspection found complete borders, text including accented characters,
shapes and gray patches. Measured border positions match the 15 mm inset within
four raster pixels; the 20 mm square and 100 mm ruler match their expected scale
within raster rounding and stroke width. The queue returned to an empty, Normal
state; its saved Letter default and unrelated Microsoft Print to PDF remained
unchanged. No physical job was submitted.

On October 16, Microsoft Edge 140.0.3485.66 printed the harmless three-page PDF
through its actual Print dialog to the same queue: A4 portrait, one copy, all
three pages at Actual size, then a separate page-2 selection. A controlled
three-page retry at explicit 100% produced a byte-identical printer stream to
Actual size. Both Inkscape and Edge executables are x64 (PE machine `0x8664`).

PDF protocol validation, page count/order, visible text/graphics and queue
completion passed. Each decoded page is 4960 by 7016 pixels; the numbered black
markers confirm pages 1, 2 and 3 in order, and only page 2 in the selected-page
capture. Borders, accented text, 8-point text, vector shapes and the raster
gradient remain visible. Final inspection found zero jobs and preserved queue
port/defaults, unrelated PDF printer and installed Setup hash.

**Edge PDF geometry acceptance remains open.** The strict four-pixel border check
used for Inkscape failed for Edge: Actual size/100% has maximum border deviation
0.499 mm
and a ruler extent 0.242 mm beyond its nominal stroke-inclusive bounds. The
page-2 capture has 0.330 mm border deviation and 0.115 mm ruler-extent deviation.
Edge was observed resetting to Fit to printable area on reopening its dialog;
the single-page job did not reselect Actual size, so its raster is not an
equivalent-settings reproducibility comparison. The complete Actual size and
100% streams match SHA-256
`b3d8b5f21684e25eecf02e9b9b394184d2c6f04ee3db8d549dead4634a5490af`.

On October 17, a controlled Edge system-dialog capture bypassed its print
preview, with A4 portrait, all three pages and one copy explicitly checked.
It passed protocol validation and page order, with complete visible content.
Maximum border deviation remained 0.414 mm; the ruler extent was 2375 pixels,
the same as the Actual size/100% captures (0.242 mm beyond nominal bounds).
Bypassing the preview therefore does not eliminate the scale difference.
Its stream SHA-256 is
`424212015b5f6873117ef3b79dbb0a8347aca842042493175df040a7086372fd`.

Source inspection confirmed that all three PDF pages have identical A4 MediaBox
and CropBox values (595.2756 by 841.8898 points), zero rotation, UserUnit 1 and
an identity initial transform. The border, square and ruler coordinates match
the fixture's intended dimensions. Source SHA-256 is
`baef1dbebd76771341bb84df3cba24c1c269e116198384fa24a59113a3b351e4`.

The same PDF was then opened in Inkscape 1.4.4 with Internal import, all pages,
no clipping, Draw missing fonts, Embed Images and Import Pages enabled. Its
actual Windows Print dialog used the same installed driver and FILE: queue,
A4 portrait, all pages and one copy. All three decoded pages passed the existing
geometry tolerances, protocol validation and page-order checks. Visual inspection
found complete borders, text, shapes, gradient and page markers. Each page has
border bounds `[351, 351, 4610, 6665]`, a 473-pixel square and a 2369-pixel ruler
extent. The captured stream SHA-256 is
`65db06ada1f4f192cf5c9bce636b71e2c39a05fe7670f2d016b6e907dcc0ccf8`.

| Same three-page PDF, A4 portrait | Maximum border deviation | Ruler extent deviation | Strict geometry |
| --- | --- | --- | --- |
| Edge Actual size / explicit 100% | 0.499 mm | +0.242 mm | Failed |
| Edge system dialog | 0.414 mm | +0.242 mm | Failed |
| Inkscape Internal import | 0.033 mm | -0.012 mm | Passed |
| SumatraPDF 3.6.1 x86, default shrink | 0.033 mm | +0.030 mm | Passed |

These are measurements of the captured 600 dpi rasters, not physical paper.
Ruler extents include the endpoint ticks' 0.3 mm stroke width.

The Edge reference-path comparison then printed the original PDF through the
guest-local **Microsoft Print to PDF** queue: A4 portrait, Actual size, all three
pages and one copy. All pages retained complete visible content and correct
identity/order. The reference PDF's vector ruler is 100.189774 mm long, versus
100.000012 mm in the source. Its rendered stroke-inclusive ruler extent is
2375 pixels at 600 dpi, matching the enlarged Edge-to-Open-ZJS output. This
reproduces enlargement without using the project driver. The reference PDF
SHA-256 is
`04fee83c29180a62e5602f1f0ee4797b1f32ec841d401a2308a52c3e49f5b5ed`.

A read-only A4 portrait device-context query also recorded the reported metrics:

| Metric | Open ZJS | Microsoft Print to PDF |
| --- | --- | --- |
| DPI X/Y | 600 / 600 | 600 / 600 |
| Physical width/height, pixels | 4960 / 7015 | 4961 / 7016 |
| Printable width/height, pixels | 4770 / 6826 | 4961 / 7016 |
| Physical offset X/Y, pixels | 95 / 95 | 0 / 0 |
| Printable width/height, integer mm | 202 / 289 | 210 / 297 |

The queried Open ZJS physical height differs by one pixel from the 7016-row
encoded canvas; the reference-path reproduction does not depend on that metric.
Private DEVMODE buffers were used, with no default-setting changes or print jobs.
The exact Edge/Windows component responsible remains unproven, but a general
Open ZJS scaling correction is not justified. Keep Edge precise-scale acceptance
open as an application-path limitation; do not advertise exact-size Edge output.

SumatraPDF 3.6.1 portable x86 (PE machine `0x014C`) then printed the original PDF
through its actual Windows Print dialog to the unchanged Open ZJS FILE: queue.
A4 portrait and one copy were checked for both the all-pages job and a separate
custom range of page 2. Its recorded setting was the default `PrintScale = shrink`;
the measured fixture retained nominal scale. Both streams passed protocol,
geometry, page count/order and visual checks. The selected page-2 raster exactly
matches page 2 of the three-page job. All pages have border bounds
`[350, 351, 4610, 6665]`, a 472-pixel square and a 2370-pixel ruler extent. This
provides actual x86 PDF-viewer coverage in this VM, beyond the earlier GDI helper.

The viewer came from the [official portable download](https://www.sumatrapdfreader.org/download-free-pdf-viewer).
Its Authenticode signature verified as valid for Krzysztof Kowalczyk on the host;
host and guest executable hashes matched
`3793fa285bc890a5e4c263f6f9854cf425bee63c72e52f5362dbc41d60956bcf`.
The three-page capture SHA-256 is
`e8f091912b75df2adfa88e2e57b46061084280da6457049288c4188399f14196`;
the selected-page capture is
`1c5d367869f4b79f3ca4cb6952ba7afd851dbea7923c8d691ee2085135820a73`.
After collection both local queues were empty and Normal. The Open ZJS FILE:
port, saved Letter default, installed Setup hash and source PDF hash were
preserved. The temporary viewer launch task was removed. Enhanced-session
redirected host queues were not selected.

Microsoft Office remains **not tested** and is explicitly deferred to a future
release by maintainer choice; it is not an initial-beta acceptance requirement.
Copies/collation were not tested in these October 15–17 runs; see the later
matrix above for the exercised paths and limits. These results do not
establish physical printing, USB/firmware recovery, production signing, or broad
compatibility with other PDF documents and application versions.

Local evidence: `artifacts/application-printing-20261015/`, including fixture
sources, the original `.prn` captures, decoded PBMs/previews,
`application-validation.json`, `pdf-source-geometry.json`,
`pdf-reference-comparison.json`, `pdf-reference-preparation.json`, and
`capture-state.json`.
Inkscape's MSI matched
the SHA-256 linked from its official download page before guest installation:
`2fa033e95113ae83d6430b2479e7e63e4266d0632a5b03f99ee3db9360e3b8b8`.
The installer has a self-signed Inkscape certificate; no trust store was changed.
The fixture generator and capture-verification script are retained with this
local evidence. The verifier records the Edge PDF geometry failure rather than
relaxing the Inkscape tolerance. No production driver source or binary changed.

## Release preparation and x86 capture coverage (2026-10-15)

The maintainer reaffirmed that physical installation stays deferred until a
signing route is chosen. A fresh private recovery capture saved six legacy
files, registrations, queue metadata/permissions, global DEVMODE, port/configuration,
recovery-source references and the Microsoft-signed legacy runtime installer.
All 28 saved files passed manifest verification. Live files, queue metadata,
global DEVMODE and no-job state remained unchanged during capture. The six legacy
file hashes match the September 29 rehearsal inputs. Independent private storage
and recovery rehearsal of this fresh settings capture remain pending.

The new read-only `export-support-report.ps1` passed privacy projection tests in
PowerShell 7 and Windows PowerShell 5.1, including sensitive-property canaries,
invalid values and missing data. Actual collection passed on the development
host and the owned 0.1.1 VM installation. Existing report overwrite was refused.
The host is Windows 11 Pro 25H2 26200.9457 with Secure Boot off; the VM is Windows
11 Pro 25H2 26200.6584 with Secure Boot on. No security settings were changed.
Host and VM reports were checked for machine/user identifiers and excluded
private fields. Secure Boot off on the host must be addressed in acceptance
planning; the read-only inventory is not physical production-trust acceptance.

The existing `src/queue_test.cpp` was separately compiled for I386 with the static
MSVC runtime, and its PE machine type checked. Five captures against the VM's
idle `FILE:` queue passed: portrait, landscape, blank, multi and Letter. Each
passed protocol validation and decoding; geometry, page count, margins and
reference bytes matched the independent encoder. Installed Setup matched
rc-54d87b83 (commit 8f336a0). The queue metadata/permissions and unrelated PDF
printer were unchanged, with no remaining jobs. This validates x86 GDI file
output, not actual x86 application dialogs or physical printing.

Evidence is under `artifacts/release-preparation-20261015-164909/`, including
the **private** `private-recovery/` bundle, host/VM support reports,
`vm-report-validation.json`, and `x86-capture/validation-result.json` plus the
build log, source/executable hashes, captures and reference comparison log.
Eight archive verifier tests passed. Their first sandbox run failed on temporary
directory permissions; rerunning with a temporary directory inside the workspace
passed. Windows PowerShell script authorization required execution outside the
sandbox; no execution policy was changed. Hosted CI has not yet run the added
support-report tests. No production driver source or binary changed.

[Remaining acceptance](RELEASE_ACCEPTANCE.md) defines Windows targets and the
physical/application/recovery matrix. The [user guide](USER_GUIDE.md), diagnostics
instructions and GitHub issue templates are prepared; hardware results, final
signed-release instructions and Windows 24H2 acceptance are still pending.

## Interactive preparation retry and restart recovery — passed (2026-10-15)

The user exercised production Setup from clean CI candidate `rc-54d87b83`,
version 0.1.1, commit `8f336a074ca8410adae831bea571fc5ecd649b26`, downloaded
from [run 37013977734](https://github.com/hernandp/OpenHPZJS1005-x64-drv/actions/runs/37013977734).
Independent archive verification passed with `--require-clean`. Setup SHA-256:
`964E94FABB84AAAD4AF3173B4CA551F2CFD11A62ECCCFE3D724196E5CD515F84`.

Preparation on October 2 restored the isolated VM's clean checkpoint, installed
the older `rc-a6cc1e56` baseline, and signed package catalogs inside the VM only.
A deliberately altered guest-only copy of the new package failed Windows catalog
membership validation, leaving update phase `Preparing` and the old installation
intact. All user-operated checks used the unmodified production Setup and package
payload, with the guest lab-signed catalog.

The preparation-retry firmware picker opened correctly. Picker cancellation and
invalid firmware rejection passed without elevation on October 2. On October 15,
the user confirmed valid-firmware elevation denial (error 1223), cancellation at
the upgrade confirmation, and the expected missing-USB-printer error after
confirmation. Foreground behavior passed. Inspection after these checks verified
the old installed payload and preserved queue settings, permissions, Letter paper,
firmware and unrelated Microsoft Print to PDF queue; update phase remained
`Preparing`.

The explicit offline upgrade shortcut then reached monitor-removal error 3008.
The actual dialog displayed the corrected instruction to restart Windows and
choose Upgrade or Roll back. Inspection confirmed `Removing` / `UpgradeRemoving`.
After restarting the guest and resuming Upgrade through the main menu, the user
provided the foreground completion dialog: the operation was complete and saved
printer settings were restored, with no further restart requested. Fresh
inspection verified the new Setup identity, matching cached/live driver payload,
`Complete` installation status, `Available` rollback phase, and preserved queue
settings/permissions, Letter paper, firmware and unrelated PDF printer.

Evidence and screenshots are in
`artifacts/OpenZJS-Win11-25H2/interactive-preparation-20261002-104831`, including
`picker-results.json`, `valid-firmware-results-20261015.json`,
`upgrade-restart-3008-result.json`, `upgrade-recovery-result.json` and
`after-upgrade-recovery-20261015.json`. The observer was stopped after verification.
Final success-dialog dismissal/process exit has not been separately confirmed.
This closes the preparation-retry picker and corrected restart-dialog checks;
it does not repeat every installer branch or establish physical USB/print
acceptance. Public signing and physical installation remain deferred.

## Windows version metadata — local candidate passed (2026-10-02)

Candidate rc-e5a6049a adds Windows version resources to Setup.exe, lj1005.exe,
lj1005render.dll and lj1005monitor.dll. Numeric and text file/product versions
are 0.1.1.0, generated from the CMake project version and checked against the INF.
Setup's missing-state display-version fallback uses the same generated version.
The resources identify each module and mark this build as a prerelease.
They do not establish publisher trust or change the installed driver.

The archived-source build passed all seven CTest suites, GpdCheck, Inf2Cat,
InfVerif (existing warnings 1296/2083), x64/static-runtime import checks, and
the new binary version/identity checks. Independent archive verification passed
for 83 source files and 12 production files, plus the two manifests. The source
ZIP includes both version templates and the version verifier. All eight archive
tests and C++ formatting passed.

Negative checks rejected a wrong expected version, the earlier package without
resources, a same-version monitor substituted for the renderer, and an INF
version different from CMake. The separately excluded Setup fault fixture still
compiled with the generated version header and remained absent from the package.
Evidence is in artifacts/rc-e5a6049a/validation/, especially
binary-versions.json, version-gate-tests.json and archive-verification.json.

This engineering candidate records modified-source provenance against f771508.
The acceptance notes were added after its source snapshot. At that stage the
resource changes and fallback text had not been installed or exercised
interactively. The later clean CI build's VM acceptance is recorded above;
the missing-state display-version fallback was not specifically exercised.
Earlier VM results apply to the binaries identified in their respective sections.
Public signing and physical installation remain deferred as requested.

## Hosted candidate CI — passed (2026-10-01)

The [complete hosted run](https://github.com/hernandp/OpenHPZJS1005-x64-drv/actions/runs/36863787006)
passed on f771508, on Windows Server 2022 / Visual Studio 2022. Formatting,
archive-verifier tests, WDK download/extraction, production build/tests, package
validation, independent archive verification and both artifact uploads succeeded.
The initial run on 90a21c9 failed because Get-Command returned both installed
clang-format executables; the corrected lookup selects the first application.
That failure was reproduced locally with two formatters before verifying the fix.
This hosted run predates the version-resource work above.

## Candidate CI — local validation passed (2026-10-01)

The new GitHub Actions workflow covers C++ formatting, archive-verifier tests,
an archived-source production build, all seven CTest suites, WDK validation,
runtime-import checks and independent verification of both ZIPs.
See [CI](CI.md) for the tool pins and local commands.

Local candidate rc-30e0624a passed this build/packaging path with the pinned WDK
package freshly extracted through prepare-ci-wdk.ps1. All seven CTest suites,
GpdCheck, Inf2Cat and binary-import checks passed. InfVerif retained only the
known 1296/2083 warnings. The verifier accepted 80 source files and 12 production
files, plus one manifest in each ZIP. Source provenance links the binary archive
to its exact source archive, including the new workflow and scripts.

All eight archive-verifier tests passed, covering corrupted archive checksums,
inner-manifest tampering, forbidden production files, embedded Setup test hooks,
source/package INF disagreement, wrong source provenance, path escape and
clean-versus-modified provenance handling. It also accepted the previously
VM-tested rc-90c47676 candidate with --require-clean. C++ formatting passed with
clang-format 22.1.3, PowerShell scripts parsed, and actionlint 1.7.7 accepted the
workflow. Local evidence is in artifacts/rc-30e0624a/validation/.

The new candidate accurately records modified-source provenance against base
commit 1d6c537; CI requires a clean checkout. These results used local Visual
Studio 2026. Hosted execution was pending at this checkpoint and subsequently
passed as recorded above. The acceptance documentation was updated after the snapshot.
No production driver or Setup code changed in this work.

On September 30 the maintainer chose to wait for public signing before physical
installation. No host recovery/migration operation, printer installation,
certificate-trust change or physical printing was performed for this CI work.
Protected signing, public releases and physical acceptance remain pending.

## Development migration — VM rehearsal passed (2026-09-29)

The explicit replacement and recovery procedure in
[development migration](DEVELOPMENT_MIGRATION.md) passed from the isolated VM's
clean checkpoint. The fixture used copies of the six actual host development
files, including the original renderer, monitor and firmware, and recreated the
unused diagnostic driver registration. Representative VM queue metadata,
permissions and Letter global defaults were saved and restored; these are not
a clone of the host's account or a complete host recovery bundle.

Candidate rc-90c47676 (0.1.1) matches production source at 1b980ce, including
the corrected upgrade/restart wording. Its archived-source build passed all
seven CTest suites, GpdCheck, Inf2Cat, InfVerif (existing warnings 1296/2083),
binary-import checks and archive verification (72 source, 13 binary entries).
Source ZIP SHA-256:
e5b7d15eb555959a7bd09c1c8b06b967fa52fcdf32616a01b79e9960f9e43ce6;
binary ZIP SHA-256:
36de7e7313e4472727f329449c2ac4fecc6e1129a11998d43980a7c24e1dcc14.
The migration harness and this documentation postdate that source snapshot.

The final frozen-harness run passed:

- Refusal of implicit Setup adoption while preserving the development build.
- Preflight rejection of an additional dependent queue and a damaged settings
  backup; rejection of a tampered candidate before legacy retirement.
- Staging the valid target before removal, then restoring the original
  development build after a fresh-install failure caused by invalid firmware.
- Explicit replacement with the owned package, preserving the tested settings
  and permissions and verifying exact live payload/firmware hashes.
- Removing the owned package and restoring the original development build,
  diagnostic registration and private files. The diagnostic registration was
  unused; successful printing through that diagnostic driver is not claimed.
- Preservation of Microsoft Print to PDF throughout, with Secure Boot enabled
  and certificate trust changes confined to the disconnected guest.

All three portrait captures (before, migrated and restored) passed strict ZJS
validation, decoding, geometry/margin checks and byte-for-byte comparison with
the Linux reference encoder. The final run required no guest restart.

Evidence: artifacts/OpenZJS-Win11-25H2/migration-20260929-185336, including
result.json, results/steps.jsonl, results/assertions.jsonl, three reference
verification logs, runtime identity and frozen harness sources/hashes.
The wrapper's capture checks and shutdown are recorded under
artifacts/migration-wrapper-20260929-155336.
The host's previously inventoried printer properties and all six original
payload hashes still matched afterward (artifacts/migration-20260929/host-after.json).

Earlier attempts exposed harness issues retained in local logs: guest script
execution policy, missing dynamic VC++ runtime dependencies in the old DLLs,
shared Windows-file copy flags, an unusable diagnostic queue fixture, and
read-only imported firmware. The final harness submits commands through
PowerShell Direct without changing execution policy, installs a verified
Microsoft redistributable only in the guest, preserves shared core files and
handles the read-only attribute only for hash-verified private files. An earlier
run resumed after a restart with a fixture correction; acceptance above is based
on the subsequent complete run with frozen sources.

This verifies a maintainer-controlled offline migration rehearsal, not an
automatic Setup migration feature or physical USB acceptance. Failure after
partial owned installation remains subject to Setup's separately tested recovery
paths; the migration failure case here stops at firmware validation. Host
migration, full host settings backup, public signing, physical printing and
unexercised interactive dialog branches remain pending.

## Different-build upgrade and rollback — VM acceptance passed (2026-09-27)

Setup 0.1.1 adds owned upgrades and one saved rollback package. It snapshots the
previous package/Setup, firmware, global printer defaults and queue permissions,
validates and stages the target before removal, and records recovery phases.
The changed monitor is replaced through owned removal/reinstallation. See
[upgrade and rollback](UPGRADE_ROLLBACK.md) for recovery and preservation limits.

Candidate `rc-2c0e7be2` (0.1.1) passed the quiet-mode upgrade matrix against the
installed `rc-a6cc1e56` (0.1.0) baseline. The monitor binaries differ. Evidence is
`artifacts/OpenZJS-Win11-25H2/upgrade-20260927-194408`: `result.json`,
`steps.jsonl`, `results/assertions.jsonl` and both reference-verification logs.
The checks passed for:

- Version/build replacement with exact cached/live payload and firmware hashes,
  retained Letter defaults, queue metadata and access permissions.
- Tampered upgrade source, dependent queue, older-version package and damaged
  rollback backup rejection while preserving the working installation.
- A locked monitor forcing exit 3010, then successful upgrade after one guest
  restart. New files were not installed before pending deletion completed.
- Explicit rollback, recovery after terminated upgrade and rollback processes,
  and automatic restoration after an injected failure following installation.
- Removal with a retained backup, including an injected unexpected backup file:
  removal refused that file, retained ownership, and succeeded on retry after
  the harness removed its injected file. Both used driver-store packages were
  absent after final removal, and Microsoft Print to PDF was preserved.

Normal operations and recovery use the packaged production Setup. Deterministic
termination/failure injection uses a separately compiled fixture from the same
source; production Setup and the user archive exclude that fixture and its
hooks. Both post-upgrade and post-rollback portrait captures passed strict ZJS
validation, geometry/margin checks and exact Linux-reference encoder comparison.

The same candidate passed the existing installation/extended-repair cases and
five further reference-matched captures in `installer-20260927-194907`. Removal
returned monitor-in-use error 3008, then 3010 after restart. The harness queried
CIM during the next shutdown and lost its connection; this was a harness timing
failure, not a successful unattended run. Reconnecting after reboot completed
removal with exit 0 and verified all cleanup assertions. The original failure
is retained. Continuation evidence and normal VM shutdown are recorded in
`artifacts/finish-repair-20260927`. Both harnesses now retry transient boot-time
queries; that retry change has syntax validation but has not had a full rerun.

The latest engineering candidate, `rc-8bd03d60`, includes one subsequent GUI
condition fix: a failed preparation reopens the firmware picker when Upgrade is
selected. Its transaction/quiet-mode implementation is unchanged from the
VM-tested candidate; this newer executable has not been run through the VM
matrix. Both candidates pass all seven CTest suites, GpdCheck, Inf2Cat and
InfVerif (the existing 1296/2083 warnings remain), import checks and archive
manifest/hash verification (72 source entries, 13 binary entries). Current
production source at commit `e0921c5` matches `rc-8bd03d60`; that commit's
subsequent edits were harness retry handling and acceptance documentation.
Formatting passes.

### Interactive upgrade and rollback (2026-09-27)

The user exercised `rc-8bd03d60` through interactive lab shortcuts with offline
file-output mode, starting from `rc-a6cc1e56`. The first upgrade stopped with
Windows error 3008 while removing the monitor. Inspection confirmed
`UpgradeRemoving` / `Removing`, and both saved packages, Setup executables,
firmware and printer-settings backups passed their recorded hash checks before
and after a guest restart. The user retried Upgrade, accepted elevation and the
foreground confirmation, received the completion dialog, and dismissed it;
Setup exited successfully.

Inspection then verified version 0.1.1.0, exact cached/live payload hashes,
retained Letter defaults, queue metadata/permissions and the unrelated PDF
printer. The rollback record was available. The user confirmed that rollback
cancellation appeared in front and closed Setup, then exercised OK, elevation
and the success dialog. Inspection verified restoration of version 0.1.0.0 and
the original driver-store INF, Setup/payload/firmware hashes and the same queue
settings/permissions; the update phase was cleared. The observer was stopped
after recording the result. The VM remains running with the restored old build.

Evidence is in `artifacts/OpenZJS-Win11-25H2/interactive-upgrade-20260927`,
including `upgrade-verified.json`, `rollback-verified.json`, their state
snapshots and `restart-20260928-001727`. The folder uses the local test date;
some evidence timestamps are September 28 UTC. This is interactive acceptance
of upgrade/restart recovery and rollback in offline mode, not physical USB or
an exhaustive repeat of every dialog/error branch. The preparation-retry picker
was not exercised in this run; the October 15 acceptance above closes that gap.

The 3008 dialog exposed misleading wording: it said to retry removal during an
upgrade. The current source now tells users to restart and choose Upgrade or
Roll back when an update cannot remove its monitor or stored package. Normal
uninstall wording is unchanged. This text-only follow-up is newer than the
tested candidate; it does not change transaction behavior. The Release build,
all seven CTest suites and formatting pass after this wording change. Its new
text was subsequently exercised through error 3008 in the October 15 acceptance
above.

All installation/trust changes were limited to the isolated guest, with Secure
Boot enabled. No host installation or physical USB print was performed. Public
signing and development-installation migration remain separate release gates.

## Interactive installer dialogs — VM checks passed (2026-09-23–24)

Candidate `rc-2316786f` was exercised through its actual dialogs in the isolated
Windows 11 VM, with the user operating the elevated VM console. Main-dialog and
firmware-picker cancellation closed Setup without elevation. Invalid firmware
displayed the original-1005 mismatch error before elevation. Denying elevation
displayed "Administrator permission was not granted" with Windows error 1223
(user-reported); accepting elevation with valid firmware displayed the expected
missing-USB-printer error. The user also canceled the removal confirmation.
After cancellation, printer names/drivers/ports, the installation ownership,
status, INF path and repair schema matched the baseline, and the firmware hash
was unchanged. This was not a complete payload integrity check.
The initial, picker, invalid-firmware and missing-USB dialogs were visually
inspected; results are recorded in
`artifacts/OpenZJS-Win11-25H2/interactive-20260923/dialog-test-matrix.csv`.

The user reported that some dialogs appeared behind other windows. Code review
found that the main dialog was destroyed before opening the unowned picker or
launching elevation, and result message boxes did not request foreground focus.
Setup now keeps the main dialog alive as owner during picker/elevation handling
and requests foreground activation for initial and result dialogs. The Release
build and all seven existing CTest suites pass; these tests do not exercise
window focus. On September 24 the user confirmed that the picker/invalid-firmware
error, denied-elevation error 1223, accepted-elevation missing-printer error,
removal confirmation and removal success result appeared in front. Dismissing
the success result closed Setup. Removal did not request a restart. A subsequent
guest inspection verified removal of the owned queue, driver, driver-store INF,
monitor registration/DLL, firmware, package cache, ownership journal and uninstall
entry. Microsoft Print to PDF retained its driver and port, and Secure Boot
remained enabled. Evidence is in `removal-verified.json` and its referenced
snapshot under the same interactive evidence directory. These checks do not
verify a successful interactive installation with a physical USB printer.
The packaged candidate above predates this fix. A separate `Setup-foreground.exe`
engineering build was copied to the guest test folder and its SHA-256 verified;
`foreground-ready.json` records its identity. The original candidate executable
was preserved for comparison.

## Extended same-build repair — VM acceptance passed (2026-09-24)

Setup now records the hashes of its cached payload and installed Setup after a
successful fresh installation. Repair checks the supplied package against that
record before changing files, checks existing queue/driver/monitor ownership,
and restores the cache, uninstaller, firmware and a deleted owned queue. Live
driver files are reinstalled through `InstallPrinterDriverFromPackageW` with
`IPDFP_COPY_ALL_FILES`, then verified against the recorded hashes. A damaged
driver-store package is rejected before that reinstall. Failed repairs retain
`NeedsRepair`; both that state and an interrupted `Repairing` state are retryable.
Known temporary siblings from interrupted writes are included in owned removal.
Older previews without the hash record require removal and fresh installation.

The extended VM harness injects cache/firmware corruption, a deleted queue,
missing installed Setup and live renderer, a corrupt live GPD, a sharing
violation and an interrupted repair journal. It checks rejection of tampered
repair sources and a foreign same-name queue before mutation, verifies restored
file hashes and A4 settings, compares the unrelated PDF queue, and then captures
five scenarios and removes the installation. The guest spooler is stopped only
by fault injection while damaging live files; Setup does not restart it.

The first VM run (`installer-20260922-170039`, candidate `rc-05df2dd8`) passed
initial installation but failed the new driver-ownership check during repair.
Inspection showed that `GetPrinterDriverDirectoryW` returns the `x64` environment
root; the code now explicitly uses its `3` subdirectory for this Type 3 driver.
That failed attempt made no repair changes and left the dedicated guest running
with its initial installation for inspection. The host printer was not changed.

The September 22 candidate `artifacts/rc-2316786f` includes the directory fix, bounded queue
ownership-marker checks and interrupted-write cleanup. Its archived-source build
passes all seven CTest suites, GpdCheck and Inf2Cat; InfVerif accepts the existing
1296/2083 warnings. Formatting passes. Both ZIP manifests and archive hashes
were checked (69 source entries, 13 binary entries), and the prior nine captures
and four encoder byte comparisons still pass. Evidence is in the candidate's
`validation/` directory, including `verification.json`.

The September 22 rerun was canceled at elevation. On September 24, fresh
candidate `rc-a6cc1e56`, which also contains the foreground fix, passed the seven
CTest suites, formatting, WDK validation with the same known INF warnings, both
archive manifests/hashes, nine existing captures and four encoder byte
comparisons. Production source matches the archive; subsequent changes are
acceptance documentation and VM-harness removal-retry handling.

The first September 24 run passed all repair cases and five reference-verified
captures, then hit Windows error 3008 while removing an in-use monitor. Setup
preserved its partial-removal record and instructed the user to restart and
retry. The harness was extended to exercise that exact condition; other removal
errors still fail. The complete rerun passed at
`artifacts/OpenZJS-Win11-25H2/installer-20260924-201121`: initial install,
same-build repair, deleted queue/cache/firmware restoration, live-file repair,
sharing-failure retry and interrupted-journal recovery all passed. Tampered
sources and a foreign queue were rejected without the prohibited changes.

After the monitor-in-use error, the first guest restart allowed removal to
return 3010; a second restart and cleanup returned 0. All owned-resource removal
assertions passed and the unrelated PDF printer was preserved. Portrait,
landscape, blank, three-page and Letter captures passed geometry, page-count,
margin and Linux-reference byte checks. Evidence includes `results/steps.jsonl`,
`result.json`, `reference-verification.txt` and `final-vm.json`; the wrapper
transcript/result is in `artifacts/repair-r2-20260924`. The VM was shut down
normally. This is lab acceptance with guest-only certificate trust and Secure
Boot enabled; no physical USB print or host installation was performed.

## Direct USB print validation (2026-09-21)

`usb-print` now validates its complete in-memory input before device enumeration
or access. The new offline `validate` command exercises the same strict original
1005 profile: document/page/image order, required settings, geometry, consecutive
page numbers, copy limits, bounded JBIG decoding and exact trailing padding.
See [print validation](PRINT_VALIDATION.md) for the accepted profile and limits.

All seven CTest suites pass. Protocol tests reject every truncated prefix of a
small valid job, missing/duplicate boundaries, invalid items and settings,
geometry/header mismatches, corrupt image data and padding, and resource-limit
violations. The new CLI test checks that a signature-only input produces the
validation error in `usb-print` before device lookup. Format checks pass.

The five September 21 installer captures, repaired three-page mixed-media ResetDC
capture, repaired landscape image, bold three-page job and Linux-reference job
all pass strict validation. The historical `artifacts/venteveos-reset.zjs` with
duplicate document headers is rejected. Encoder wire output is unchanged.
No physical print or host installation was performed for this change. This
completes direct-file validation; status reporting, support diagnostics and the
other release gates remain open.

Fresh candidate `artifacts/rc-62568a37` built from its archived source and passed
all seven suites, GpdCheck and Inf2Cat. InfVerif accepted the same previously
documented warnings 1296 and 2083. Both ZIP manifests were independently checked
(69 source entries and 13 binary entries), including archive hashes and source
provenance. Its CLI accepted the nine captures above and rejected the historical
duplicate-header job. Four A4/Letter single/multi-page encoding comparisons
remained byte-identical to the earlier baseline. Results are in the candidate's
`validation/verification.json` and tool logs. This candidate is unsigned and has
not been installed or physically tested; earlier installer VM evidence applies
to `rc-93e8c09e`. The source archive predates only this packaging-result paragraph.

## Uninterrupted installer rerun (2026-09-21)

After relocating the workspace to `C:\src\HPLJ1005x64`, the revised native
installer harness completed from `Clean-Windows-before-driver` without manual
recovery. The existing `artifacts/rc-93e8c09e` candidate passed its file manifest
check; its 27 production source/build-definition files matched the current
checkout. The relocation build had also passed all six CTest suites.

Unsigned catalog, invalid firmware, altered package and foreign same-name queue
checks each returned the expected failure. Installation and same-build repair
returned 0. The installed uninstaller returned 3010, and cleanup after the guest
restart returned 0. All removal assertions passed, including driver-store,
queue, monitor, firmware, cache and ownership-record cleanup, with Microsoft
Print to PDF preserved. Transient PowerShell Direct connection and credential
errors recovered through the harness's existing retries.

All five captures (portrait, landscape, blank, three-page and Letter) decoded
successfully. Independent verification checked seven raster pages, geometry,
margins, the blank middle page and distinct first/third-page markings, and found
exact Linux-reference byte equality for every capture. Secure Boot remained
enabled; signing trust was added only inside the isolated guest. The dedicated
VM was shut down normally after verification. No host driver was installed or
physical print job submitted.

Local evidence: `artifacts/OpenZJS-Win11-25H2/installer-20260921-144401`, including
`result.json`, `results/steps.jsonl`, `reference-verification.txt` and
`final-vm.json`. The orchestration transcript and successful exit code are in
`artifacts/resume-20260921`. This closes the uninterrupted-harness rerun gap;
interactive Setup/elevation, physical USB acceptance, production signing,
upgrades and development-install migration remain pending.

## Native installer preview (2026-09-18)

The new native x64 `Setup.exe` provides an Install/repair/Remove dialog and a
standard file picker for the user's original `sihp1005.img`. Size and SHA-256
validation run before elevation and again during installation. No HP firmware,
Windows components, signing certificate or private key is bundled.

The candidate in `artifacts/rc-93e8c09e` built from its archived source with all
six CTest suites passing. All four production executables/DLLs are x64 with no
separate MSVC runtime dependency. GpdCheck and Inf2Cat passed; InfVerif accepted
the same two previously documented warnings. Both ZIP manifests were verified:
66 source entries and 13 binary-package entries, including Setup. The CLI still
matches previously verified A4/Letter single/multi-page bytes and round trips.

On the isolated Windows 11 Pro 25H2 VM, native Setup rejected an unsigned
catalog, incorrect firmware, a modified signed-catalog member and a same-name
queue owned by Microsoft Print to PDF. The tampered-package rollback left no
owned queue, cache or installation record. With a certificate generated and
trusted only in the guest, installation and same-build repair returned success,
imported the exact firmware and configured A4. Five GDI file captures covered
portrait, landscape, blank, three-page and Letter output. Secure Boot and normal
signature enforcement remained enabled; the working host printer was untouched.

Initial removal tests found the Windows-installed monitor's SYSTEM-only write
ACL. Cleanup now grants administrators deletion permission only after checking
the owned monitor's hash and other driver/queue dependencies. The existing ACL's
read permissions are preserved. Loaded files and the running installed Setup
can be scheduled for deletion at restart; an ownership record blocks reuse
until that cleanup completes. The installed uninstaller returned 3010; after
restart, its executable was gone and a second invocation from the extracted
package completed cleanup with exit 0. Assertions verified removal of the driver
store package, queue, monitor registration/DLL, firmware, cache, uninstall entry
and ownership record. Microsoft Print to PDF remained intact.

The automated run timed out reconnecting during restart. A later connection
completed the remaining assertions and exported the guest evidence to
`artifacts/OpenZJS-Win11-25H2/installer-recovery-20260918-152558`. Its `result.json`,
`results/steps.jsonl` and `reference-verification.txt` record cleanup and exact
Linux-reference equality for all five captures (seven raster pages). The guest
was then shut down normally. The harness now checks boot time rather than a
fixed delay and allows more connection time. Its uninterrupted rerun passed on
September 21, as recorded above. Earlier failures remain recorded.

Remaining acceptance: manual dialog/elevation testing, USB port detection and
physical printing through this installer, additional OS builds, production
signature trust, binary upgrades, migration of development installs and broader
failure injection. Repair currently restores missing firmware and queue port/A4
defaults, not deleted queues or damaged binaries. This is an engineering preview,
not the completed end-user release milestone. See [deployment](DEPLOYMENT.md).

## Clean Windows 11 VM installation (2026-09-17)

**Package installation and offline output passed on a fresh Windows 11 Pro 25H2
x64 VM, build 26200.6584.** The VM was created from the maintainer's ISO, with Secure
Boot and TPM enabled, no connected network adapter, no Visual Studio, and no
registered x64 Visual C++ redistributable. Normal signature enforcement remained
enabled. The fresh Windows image already included Unidrv in its spool directory;
no Windows components were copied there manually.

The unchanged engineering candidate from `artifacts/rc-6f7a09d0` was checked
against its manifest before transfer. Unsigned PnP staging failed as expected
with `0x800B0100` (no signature). A new code-signing certificate was generated
and trusted **only inside the disposable guest**, then used to sign its copy of
the catalog. PnP staging succeeded, followed by `Add-PrinterDriver` using the
staged INF. Windows reported `IsPackageAware=True`, the registered Unidrv core
dependency GUID, the expected language monitor and matching installed DLL hashes.
This validates package mechanics under lab trust, not a publicly trusted release.

A `FILE:` queue captured portrait, landscape, blank, three-page and Letter jobs.
All five scenarios cleared the queue and passed ZJS inspection. Independent host
decoding verified seven raster pages, their dimensions and margins, the blank
middle page, distinct first/third-page markings and byte-for-byte equality with
the OpenPrinting reference encoder. No physical printer or firmware was used.

Local evidence is under
`artifacts/OpenZJS-Win11-25H2/runs/20260917-135639-43deeaa8`: `results/` contains
baseline, signing, driver-store, registration and capture reports; `captures/`
contains the ZJS and decoded PBMs; `reference-verification.txt` records the
independent comparison. A pre-driver/pre-certificate checkpoint is preserved as
`Clean-Windows-before-driver`. The ISO SHA-256 was
`d141f6030fed50f75e2b03e1eb2e53646c4b21e5386047cb860af5223f102a32`.
After verification the empty test queue and OS revision were recorded, and the
VM was shut down normally with its installed state and clean checkpoint retained.

Provisioning initially hit a host BCD-store collision; boot files were instead
created from isolated Windows PE inside the guest. The test harness was corrected
to reconnect after checkpoints, use the native certificate importer under
PowerShell Direct, query the driver's `Path` property, and preserve per-run
evidence. Earlier failed attempts remain in local logs. See [VM testing](VM_TESTING.md)
for the scripts and their scope. The final test ran after restoring the clean
checkpoint. End-to-end recreation with the consolidated provisioning scripts has
not yet been repeated; their stages were exercised during this recovery.

Still pending: production certificate trust, Setup/firmware import, upgrade and
uninstall, additional Windows builds, actual application dialogs and physical USB
testing of this package. The working host printer was not changed.

## Release packaging foundation (2026-09-16)

Checkpoint commit `c882ffd` preserves the previously verified development source.
The subsequent package-aware INF declares the Windows Unidrv core dependency.
New builds statically link the MSVC runtime. A fresh candidate build from an
archived source snapshot passed all six CTest suites. Dumpbin found only Windows
system imports in the shipped renderer, monitor and diagnostic utility; no
separate MSVC runtime imports remained. The static-runtime utility produced the
same previously verified A4/Letter single/multi-page ZJS bytes and decoded PBMs.

WDK 10.0.26100.0 GpdCheck passed. InfVerif accepted the INF with warnings 1296
(no associated service in this printer install section) and 2083 (the generic
validator reports the printer-specific package section as unreferenced). The
section follows Microsoft's documented `PrinterPackageInstallation.amd64`
pattern; actual handling by the printer installer remains a clean-VM test.
Inf2Cat for `10_GE_X64` generated an unsigned catalog with no errors or warnings.
These results do not establish WHQL eligibility or public signature trust.

The source ZIP's 60 file entries and binary ZIP's 12 file entries were checked
against their complete manifests, including SHA-256 and size. The source archive
matched its working-copy snapshot, and the binary provenance matched the source
archive hash. The production archive contains no firmware, Windows binaries or
test executables. Local evidence: `artifacts/rc-6f7a09d0/validation/`, the two ZIPs
in that directory, and `artifacts/verify-candidate.py`.

No driver was installed and no physical print was sent for this milestone.
Signing, clean-machine installation and runtime/hardware acceptance of these
specific binaries are still pending. Hyper-V is available locally, but the
existing VM is a development environment; clean installation media or another
clean VM must be selected before deployment testing.

## Coding style and edit verification (2026-09-16)

The maintainer's formatting examples established the conventions in `CODING_STYLE.md` and `.clang-format`. Project-owned C++ functions and data names were updated consistently with their declarations and callers, and 22 source/header files were formatted. Windows/COM/standard-library/JBIG names and vendored source bytes were preserved.

The initial edited `firmware.cpp` failed the build: assignments following a throw were unreachable, and the device-ID parsing function could exit without returning a value. Misplaced braces also moved model validation and the return inside the field loop. These were corrected while keeping the requested style. File-local helper namespaces were restored in the firmware and USB implementations. Additional firmware tests cover field ordering, ignored fields, a missing final separator, empty firmware versions, and duplicate manufacturer/model fields.

The repaired and restyled build passes all six CTest suites. Formatting checks pass with clang-format 22.1.3, and a token comparison confirmed that formatting itself did not alter C++ tokens. Offline CLI comparisons against the earlier clean-build executable produced identical single/multi-page A4/Letter ZJS bytes, inspection output, synthetic firmware envelopes, and malformed-PBM errors; decoding preserved the original PBM bytes. No physical printing or driver installation was performed for this change. The installed driver's hardware evidence below refers to the earlier builds, not the newly restyled binaries.

## Paper-change regression and repair (2026-09-13)

The user reported repeated blank sheets after changing Letter to A4 in Inkscape; subsequent Windows test pages also fed blanks. The printer was switched off, the development queue paused, and the failed Windows test entries removed after preserving a spool-file sample. The original failure was not captured at the USB boundary, so the following is a reproduced driver defect, not proof of every step in that incident.

Offline reproduction using an EMF export of the user's SVG and `queue_test ... emf-reset input.emf` switches Letter portrait → A4 landscape → Letter portrait within one job. The old installed driver produced a 742448-byte stream with `JZJZ` headers at offsets 0, 245508 and 496924. The decoder rejected the second header as an invalid ZJS chunk. Each paper change reset the document header and page number instead of continuing the job.

Unidrv repeats DOC_SETUP following ResetDC, as described in Microsoft's [command execution order documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/command-execution-order). CmdStartDoc now belongs to JOB_SETUP and CmdEndDoc to JOB_FINISH. The renderer also preserves an already-active document if the start callback repeats. ResetPDEV continues carrying document state while retaining the new paper geometry.

The renderer regression test failed before the repair and passes afterward. It exercises Letter/A4-landscape/Letter PDEV transitions, repeated setup, page numbering, and a subsequent independent job. All six CTest suites pass. The patched renderer and GPD are installed and their hashes match the build. Repeating the same Windows file capture now decodes as one document with three correctly sized pages; each page matches the Linux reference encoder byte for byte, including framing and sequence after adjustment for its position in the job. `tests/verify_reset_capture.py` reproduces this check. Separate image-heavy A4 and Windows test-page captures also matched the reference before the repair; those tests alone did not exercise ResetDC.

**Physical A4 landscape retesting passed.** The queue was emptied before resuming for single-sheet tests. Job 27 loaded firmware and received its setup echo and START; no paper was available, and a USB disconnect ended completion monitoring. The printer subsequently identified without firmware, confirming a new startup state. At the user's request, job 28 retried the single A4 landscape bird page. Its trace shows firmware upload, setup echo and START. Initially the user reported yellow blinking with green off; after paper/door checks were requested, the user confirmed that the birds printed successfully. A subsequent read-only Windows query found the queue Normal on USB003 with no remaining jobs, without manual cancellation of job 28. The trace was stopped before recovery, so this run did not capture its final END event. Local evidence: `artifacts/monitor-reset-fix-20260913.xml`, `artifacts/monitor-reset-retry-20260913.xml`, and `artifacts/reset-fix-final-queue.json`.

This physical test used an EMF export of the user's SVG through `queue_test emf-landscape`, not Inkscape's own Print dialog. Paper changes within a job were verified offline; a direct Inkscape print after restarting the application is still unconfirmed. The five earlier portrait, landscape, blank, multi-page and Letter captures were repeated with the installed repair and remained byte-identical to their verified reference captures. WDK GPDCheck accepted the updated command ordering.

The September 10 hardware results below describe the earlier version and remain limited to the scenarios actually tested. Restart applications that had loaded the old driver before testing the installed repair.

Target: original HP LaserJet 1005 Series, USB VID 03F0 / PID 1317, on Windows x64 build 26200. Implementation: C++20 Unidrv renderer and user-mode language monitor, with C JBIG-KIT. Firmware: the user's original image, version 20021030. Installed queue: HP LaserJet 1005 Open ZJS (development), USB003.

## Verified hardware behavior

| Test | Physical result | Windows/device evidence |
| --- | --- | --- |
| Native A4 diagnostic | User confirmed border, bars and checkerboard | Direct USB ZJS transfer |
| Bold A4 portrait over USB | User confirmed text, all 16 solid bars and complete border | 2072-byte file, SHA-256 `1f67bdb45a3adc6286e42715b2244eb05d0063197f0173892cc6dc438dccc0bd` |
| Windows portrait after power cycle | User confirmed correct bold page | Firmware absent before job 3; loaded automatically. Matching setup echo and END with PAGES=1; job removed automatically |
| Windows landscape | User confirmed correct orientation, text, bars and border | Job 6: matching END with PAGES=1; removed automatically |
| Windows three-page job | User confirmed Page 1, blank middle sheet, Page 3; no missing, clipped, duplicated or corrupted content | Job 7: matching END with PAGES=3; removed automatically |

The final tests used the same installed monitor DLL, SHA-256 `b501225d13bee88dc5448ce8982515eb2af91744521021a74d1bbae3a877677d`. Its hash was checked against the built binary. The new toner cartridge was installed for the final tests, and the user confirmed its sealing strip had been removed.

The cold-start trace is `artifacts/monitor-cold-echo-20260910.xml` (original ETL alongside it). Token `lj1005-3-47273140-2` received its setup echo, START, and END/PAGES=1, followed by LowerEndDoc success and CompletionAcknowledged. Firmware loaded at 23:43:30; the completion report arrived at 23:43:48 local time.

The layout trace is `artifacts/monitor-layouts-20260910.xml`. Tokens `lj1005-6-47488640-3` and `lj1005-7-47498531-4` received matching END/PAGES=1 and END/PAGES=3 respectively. Both have CompletionAcknowledged events. A separate Windows queue query confirmed no remaining entries. Diagnostic sessions were stopped and decoded.

## Software and packaging checks

- Six CTest suites pass: protocol, firmware state, PJL parsing, simulated monitor callbacks, production monitor load/initialization, and renderer COM initialization.
- Parser tests cover fragmented and stale setup echoes, incomplete/malformed/mismatched END reports, and cancellation. Setup readiness cannot acknowledge physical completion.
- Simulated monitor tests reject raster writes before the setup echo. Cancellation or read failure before synchronization sends no raster. Failure after page submission does not request automatic replay or falsely acknowledge success.
- A4 portrait, landscape, blank, three-page and Letter file captures pass geometry, margin and reference-byte checks. Bold landscape and bold three-page captures also match the OpenPrinting reference encoder and preserve the blank middle page.
- The optional reference encoder is OpenPrinting foo2zjs commit `80499ed5bf6caa2963ad337e37cfda78a80aab1e`. Protocol and wrapped-firmware bytes were compared against it.
- WDK GPDCheck accepted the GPD. InfVerif accepted the INF with legacy service/data-section warnings. Inf2Cat `/os:10_GE_X64 /nocat` reported no signability errors or warnings. These are metadata checks, not a signed catalog or hardware certification.
- The development ZIP allowlists source, native binaries, tests, scripts and licenses. SHA-256 manifest verification checks each archive entry. HP firmware, Windows binaries, downloaded WDK tools, reverse-analysis libraries and local experimental artifacts are excluded.

## Corrections and failed tests retained for context

An earlier cropped-raster GDI job caused repeated blank sheets. Padding the printable image onto the full physical sheet, without ZJS offsets, produced a successful direct USB page on 2026-09-08. The earlier 2080-byte portrait file has SHA-256 `b7067bde7bbcf1debf15a4e5de73103c40857d6ff277baeaf5f4633fec6de72b`. This confirms the workaround, without isolating dimensions from offsets as the cause.

An initial queue test was blocked by WORK_OFFLINE; clearing that attribute allowed it to print. The installer now clears it when connecting USB and matches the live USB instance rather than stale port registrations.

Earlier cold-start tests loaded firmware but failed to establish successful page output or queue cleanup. One produced a blank page. Later direct output showed text but almost invisible one-dot rectangle outlines. The bold test scenarios use solid bars and a 12-dot border to make hardware inspection clear.

The first completion monitor sent setup and raster without synchronization. Warm printing succeeded, but no status bytes arrived and the queue remained COMPLETE after its timeout. A unique setup ECHO before raster transmission enabled a verified synchronization point. The final clean-start and layout traces prove job completion and removal with this version.

Experimental empty PJL/ZJS jobs left pending job-name state: a later real page reported the earlier probe name `100501`. The parser correctly rejected this mismatch. The test entry was cancelled, the printer power-cycled, and final tests run without probes. The empty-job CLI command was removed. No monitor firmware modifications were made; analysis used the original firmware's symbols and code only to understand status behavior.

Guarded monitor updates removed only explicitly identified integration-test entries, retained backup DLLs, and verified that no unrelated queued jobs or running dependent services would be interrupted. Test entries were never replayed for maintenance.

## Release limits

This is a working USB development release, not a signed production package. Parallel transport is implemented through the inbox port monitor and simulated callbacks, but no parallel hardware test was available. Letter output was checked offline; physical tests used A4. The feature set is 600 dpi, monochrome, simplex, A4/Letter. Paper/toner status presentation, hardware certification, and signed deployment are not provided. A compatible x64 Visual C++ runtime and the user's original firmware are required.
