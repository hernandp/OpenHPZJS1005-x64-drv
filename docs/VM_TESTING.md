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

References: [Windows image deployment](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/capture-and-apply-windows-system-and-recovery-partitions),
[customizing Windows PE](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/winpe-mount-and-customize),
[PowerShell Direct](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/powershell-direct),
and [Add-PrinterDriver](https://learn.microsoft.com/en-us/powershell/module/printmanagement/add-printerdriver).
