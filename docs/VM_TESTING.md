# Clean Windows VM package test

These are maintainer tools, not the end-user installer. They require an elevated
PowerShell 7 session on a Windows x64 host with Hyper-V enabled and a Windows 11
x64 ISO containing the Pro edition. Supply your own appropriately licensed media.

```powershell
./scripts/new-test-vm.ps1 -IsoPath C:/path/to/Windows11.iso
./scripts/test-vm-package.ps1 `
  -CandidatePath artifacts/rc-XXXXXXXX/HPLJ1005x64 `
  -QueueTestPath artifacts/rc-XXXXXXXX/source/build/Release/queue_test.exe
```

The provisioning script creates `OpenZJS-Win11-25H2` under
`C:\ProgramData\OpenZJS-Lab`, with a 128 GB dynamically expanding disk, 6 GB RAM,
four virtual CPUs, Secure Boot and TPM. It refuses existing names/directories.
The label alone does not validate the ISO version; the media edition list and
hash are recorded, and the test records the actual guest OS build.

Windows is applied to a newly created VHDX. A temporary Windows PE disk from the
same ISO creates the guest boot files inside the VM, then is detached. The host's
boot store is not modified. The guest has no connected network adapter and is
managed through PowerShell Direct. Setup creates a local administrator with a
random password; the host copy is DPAPI-encrypted in the ignored evidence folder.
The test removes the guest's temporary answer file and autologon password before
taking the baseline checkpoint. Never publish the VHDX or credential file.

The test creates `Clean-Windows-before-driver` before copying in the driver or
changing guest trust. It verifies Secure Boot, records the clean baseline, checks
that unsigned staging is rejected, and generates a lab signing certificate **only
inside the VM**. It trusts that certificate in the guest, signs the copied catalog,
stages the package, registers the printer driver and creates a `FILE:` queue.
Five GDI scenarios write capture files; no printer or firmware is needed.

This deliberately separates package mechanics from production certificate trust.
Lab trust is not a public signing solution, and physical USB/firmware behavior is
not tested by file captures. No host certificate store or installed printer is
changed. The guest's signed catalog differs from the original candidate manifest;
the original candidate remains unchanged and its hashes are checked before copy.

Results, logs and captures are copied to a new directory under
`artifacts/OpenZJS-Win11-25H2/runs` for each run. `run-status.json` records the
latest status and evidence directory. Check the timestamped `test.log` and the
run's `test-error.txt` after failures; a failed attempt may leave partial results. Decode and
compare the captures independently on the host before declaring output correct.

For example, with the optional reference encoder built and Python/Pillow available:

```powershell
$run = Get-Content artifacts/OpenZJS-Win11-25H2/run-status.json -Raw | ConvertFrom-Json
if ($run.status -ne 'passed') { throw 'The VM test did not pass.' }
$captures = Join-Path $run.evidenceDirectory 'captures'
foreach ($scenario in @('portrait', 'landscape', 'blank', 'multi', 'letter')) {
    ./build/Release/lj1005.exe decode "$captures/$scenario.zjs" "$captures/$scenario.pbm"
    if ($LASTEXITCODE) { throw "Decode failed: $scenario" }
}
python ./tests/verify_captures.py --capture-directory $captures --prefix ''
```

To rerun from the project's clean checkpoint, add `-RestoreCleanCheckpoint` to
`test-vm-package.ps1`. This explicitly discards subsequent state **in that dedicated
VM**. The script checks the VM ID against its creation record. Do not use that
flag for a VM containing work you want to keep.

Provisioning failures preserve the disks and evidence for diagnosis; scripts do
not delete or replace existing VMs. A failed PE stage may leave the temporary disk
attached. Inspect the stage logs before deciding how to recover. The virtual disks
are kept outside the OneDrive source tree and excluded from release archives.

## Native installer tests

Once the clean checkpoint exists, run the following from an elevated PowerShell
7 session. **This always restores that checkpoint**, discarding subsequent work
in the dedicated project VM. The VM identity and network isolation are checked.

```powershell
./scripts/test-vm-installer.ps1 `
  -CandidatePath artifacts/rc-XXXXXXXX/HPLJ1005x64 `
  -FirmwarePath C:/private/original-driver/sihp1005.img `
  -QueueTestPath artifacts/rc-XXXXXXXX/source/build/Release/queue_test.exe
```

The firmware file is copied privately into the disposable guest for import
testing. It is not added to the candidate or copied back in the evidence. The
script invokes native Setup in quiet mode, checks rejection of unsigned and
altered packages, invalid firmware and an unrelated queue with the same name,
then checks installation, A4 defaults, same-build repair and five file captures.
The repair checks deliberately delete the owned queue, cached INI, installed
Setup and live renderer, corrupt firmware and cached/live GPD files, hold a
cached file open to force a sharing failure, and set an interrupted repair
journal. They verify exact restored hashes, retry behavior and unchanged PDF
queue settings. Tampered sources and a foreign same-name queue must fail before
mutation. Known temporary repair files are injected before the removal check.
Only the disposable guest's spooler is stopped for live-file fault injection;
the installer itself must not restart it. These extended cases were added on
September 22. On September 24 all repair cases and five reference-verified
captures passed; final removal encountered a monitor-in-use error (3008).
The harness now checks that exact error and the partial-removal journal before
restarting the guest and retrying removal once. Other removal errors still fail
the run. The full rerun `installer-20260924-201121` passed: removal retry returned
3010, the subsequent restart/cleanup returned 0, all resource assertions passed,
and all five captures matched the reference. See test status for evidence.
It runs the installed uninstaller and, when needed, restarts only the guest and
finishes cleanup. It checks that the driver package, queue, monitor, firmware,
cache and ownership record are gone and Microsoft Print to PDF remains.

Signing certificates are created and trusted only inside the guest. Secure Boot
and normal signature enforcement remain enabled. The source candidate stays
unsigned. The test does not exercise the interactive picker, elevation dialog,
USB port selection or physical printing.

`last-installer-run.txt` points to the new `installer-YYYYMMDD-HHMMSS` evidence
directory. Inspect `result.json`, `failure.txt` when present, and
`results/steps.jsonl`. Decode and independently compare `captures/` using the
commands above with this run directory before accepting the raster output.
The harness leaves the dedicated VM running for inspection; shut it down
normally when finished.
`progress.log` records host-side phases, and the transcript reports each Setup
test label, so a connection delay can be distinguished from an installer failure.

The revised harness completed without manual recovery on 2026-09-21, including
the guest restart and removal assertions. Five captures then passed independent
geometry and Linux-reference byte checks, and the VM was shut down normally.
See `docs/TEST_STATUS.md` for the evidence location and remaining acceptance gaps.

References: [Windows image deployment](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/capture-and-apply-windows-system-and-recovery-partitions),
[customizing Windows PE](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/winpe-mount-and-customize),
[PowerShell Direct](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/powershell-direct),
and [Add-PrinterDriver](https://learn.microsoft.com/en-us/powershell/module/printmanagement/add-printerdriver).
