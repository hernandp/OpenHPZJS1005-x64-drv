# Engineering package: not yet an end-user release

This unsigned candidate targets the **original HP LaserJet 1005 Series** over USB
on Windows x64. It is not for the P1005 or M1005. Do not use this candidate to
replace a working installation: publicly trusted signing and the end-user
installation flow are still release gates. The package includes an experimental
native Setup with firmware selection, same-build repair, owned upgrades/rollback
and ownership-aware removal.
Package installation and offline GDI captures passed on a fresh
Windows 11 Pro 25H2 x64 VM using a certificate trusted only inside that guest;
this does not establish public trust or physical compatibility of this package.

## Contents and requirements

- `Setup.exe`: native x64 installer preview for Windows 11 24H2 or later.
- `driver/`: package-aware Type 3 INF, GPD, INI, renderer, firmware monitor and
  **unsigned** catalog. The catalog is an integrity inventory, not a signature.
- `tools/lj1005.exe`: optional command-line diagnostic utility.
- `BUILD-INFO.json`: build provenance and matching source archive hash.
- `SHA256-MANIFEST.json`: file hashes, excluding the manifest itself.
- `LICENSE` and `THIRD_PARTY.md`: GPL terms and codec attribution.

The DLLs and utility statically link the MSVC runtime. A separately installed
Visual C++ redistributable was not needed for the clean-VM installation and GDI
capture test. Windows supplies Unidrv and the USB printer port driver.
Neither Windows binaries nor HP firmware are included.

Automatic firmware upload requires the original `sihp1005.img`, obtained by the
user from their HP driver package. The accepted 116892-byte image has SHA-256:

```
638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1
```

## Setup preview

Extract the complete candidate ZIP. On a clean test machine with a trusted,
signed copy of the driver catalog, connect and turn on one original LaserJet
1005, run `Setup.exe`, and choose **Install or repair**. The next dialog asks for
the **firmware image file**, `sihp1005.img`, from your extracted original HP driver
package. Setup checks its size and SHA-256 before requesting administrator
permission, then validates it again in the elevated process. Cancelling the file
dialog makes no installation changes. Firmware remains user-supplied and is
never included in the downloadable package.

Setup installs through the Windows driver store, selects the matching USB port,
enables bidirectional communication and creates **HP LaserJet 1005 (Open ZJS)**
with A4 defaults. It requires a running Print Spooler and reports incompatible
architecture, OS, Protected Print Mode, missing hardware and trust failures.
It never imports a certificate or changes Windows security settings.

Run Setup again from the **same complete extracted package** to repair an owned
installation. Setup verifies the source files against hashes recorded during
installation, restores missing or damaged cache files, its installed uninstaller
and firmware, recreates a deleted owned queue, and resets its port and A4 defaults.
It asks Windows to reinstall the live driver files and verifies their hashes
before reporting success. Repair does not restart the shared Print Spooler.
If files are in use, close printing applications or restart Windows and run
repair again. A failed or interrupted repair can be retried with the same package.

A conflicting queue, changed driver/monitor registration, redirected destination,
dependent queue or pending job prevents repair. Setup preserves unrelated printers.
It requires an intact Windows driver-store package; driver-store corruption is
reported before reinstalling those files. Keep the original signed package for
repair, including its catalog. Development-install migration and repair of older
previews without recorded file hashes are refused. Keep using the development
tooling for an existing development queue.

Setup 0.1.1 adds **Upgrade** for a different complete package and **Roll back**
for the saved previous build. Upgrade requires an intact owned installation and
saves its package, firmware, global queue preferences and permissions before
replacement. Older package versions are refused through Upgrade. If Windows
retains a monitor/file, restart and rerun the new extracted Setup to continue.
An interrupted installation is recovered by rollback. Keep the extracted new
package until recovery finishes; do not use an older Setup to remove an
unfinished update. One previous build is retained; a subsequent upgrade replaces
that history. See the source archive's `docs/UPGRADE_ROLLBACK.md` for details and
`docs/TEST_STATUS.md` for the actual acceptance scope.

Extended repair passed its failure-injection VM run on September 24, including
restoration of damaged files and a deleted queue, failed/interrupted repair
retries, and removal after a monitor-in-use restart/retry. Consult the source repository's `docs/TEST_STATUS.md`
before treating this engineering preview as a verified recovery tool.

Choose **Remove** in Setup or uninstall **Open ZJS - HP LaserJet 1005** from
Windows installed apps. Removal checks ownership and dependent queues, refuses
pending jobs, and removes the owned driver, firmware and cache. Windows may keep
the monitor or running Setup executable open; Setup then requests a restart to
finish deleting those files. It does not restart the shared Print Spooler.
A small protected cleanup record can remain after restart; a subsequent Setup
installation or removal clears it. Reinstallation is blocked while file removal
is still pending. Unexpected or changed resources are preserved with an error.

For automated isolated lab tests only, `Setup.exe --quiet --install --offline
--firmware "C:\path\sihp1005.img"` uses `FILE:` instead of USB. `--quiet --remove`
removes the owned installation. Quiet mode requires an already elevated process;
exit codes are 0 for success, 3010 for success requiring restart, 1 for an
operation failure and 2 for an unknown/incomplete option. No driver-signature
bypass is provided. The unsigned candidate is expected to fail trust checks.

Interactive VM checks passed on September 23–24: firmware selection, both cancel
paths, invalid-firmware errors, denied/accepted elevation and confirmed removal.
The foreground-dialog fix was retested, and removal preserved the unrelated PDF
printer. Successful interactive installation still requires physical USB
acceptance. Interactive Upgrade/restart recovery and Roll back also passed on
September 27, including rollback cancellation and verified preservation of
global settings and permissions. A follow-up clarifies the upgrade restart
error; that wording and the preparation-retry picker need checking in the next
candidate. Public signing and development migration also remain release gates.
See `docs/TEST_STATUS.md` for build identities and scope.
Development scripts in the matching source archive are not the clean-machine
deployment path.

## Security settings

Windows Protected Print Mode must be off because it excludes third-party
drivers. Setup must report this condition and respect administrator policy;
it must not silently turn the setting off. Normal driver-signature enforcement
and Secure Boot must remain enabled for release acceptance.

## Clean-machine acceptance procedure

The original package-registration and file-output checks passed in the
25H2 VM on 2026-09-17. Native installer results are recorded separately;
production trust, physical USB and additional OS builds remain pending. See the source repository's
`docs/TEST_STATUS.md` and `docs/VM_TESTING.md` for scope and evidence.

Use a disposable VM snapshot or spare PC. Record the exact Windows edition,
build, architecture and security settings. No Visual Studio, WDK, previous Open
ZJS installation or manually copied Unidrv files should be present. A VM is
sufficient for package installation and file-output checks; physical printing
also needs usable USB passthrough or a real PC.

1. Verify both archive hashes and every file in the binary manifest. Preserve
   the matching source archive with the results.
2. Establish the signing route in the disposable environment. Any lab certificate
   is only for lab testing; importing one does not prove public trust. For final
   acceptance use the intended production signature and an otherwise unchanged
   Windows trust store. Never disable signature enforcement to declare a pass.
3. Stage the signed INF package through Windows driver-package installation and
   register the printer driver through the print subsystem. Confirm Windows
   resolves the declared Unidrv core dependency from its own driver store. Do
   not run `install-dev.ps1` or pre-copy files into the spool directory.
4. Inspect installed driver files, package-aware status, monitor registration and
   renderer loading. Capture a page to a file before connecting hardware.
5. Test firmware import, exact device/port matching, bidirectional configuration,
   A4 defaults and creation of one queue. Power-cycle, then print one controlled
   page and confirm job completion. No-paper recovery must not duplicate it.
6. Exercise repair, upgrade, uninstall and rollback. Confirm unrelated queues,
   monitors, drivers, jobs and files survive unchanged. Repeat on a fresh snapshot.

Record each result as passed, failed or not tested. WDK validation alone is not
installation, WHQL certification, signature trust or physical compatibility.

## Signing order

Choose the publisher identity and validate the available signing route before
promising a public install experience. If signing executable payloads, do so
before regenerating the catalog; then sign and timestamp the catalog. Recompute
the manifest and archive hashes after signing. The current candidate builder
does not perform signing or publish assets. A future release pipeline must
verify trust and catalog membership before producing a distributable asset.

## Sources

- [Microsoft: Unidrv core-driver packaging](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/package-aware-print-drivers-that-share-files)
- [Microsoft: core-driver dependencies](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/using-core-drivers)
- [Microsoft: Inf2Cat](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/inf2cat)
- [Microsoft: Windows Protected Print Mode](https://learn.microsoft.com/en-us/windows/modern-print/windows-protected-print-mode/windows-protected-print-mode)
