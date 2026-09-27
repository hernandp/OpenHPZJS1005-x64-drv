# Development verification — updated 2026-09-27

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
production source matches `rc-8bd03d60`; subsequent edits are harness retry
handling and acceptance documentation. Formatting passes.

All installation/trust changes were limited to the isolated guest, with Secure
Boot enabled. No host installation or physical USB print was performed. The new
Upgrade/Roll back confirmations, cancellation and recovery messages still need
interactive acceptance. Public signing and development-installation migration
remain separate release gates.

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
