# Development-installation migration

This is a maintainer-controlled replacement procedure, not an automatic Setup
feature. Do not point Upgrade at the development installation: it has no owned
package cache, recorded payload hashes or driver-store INF to roll back to.
Setup must continue refusing implicit adoption.

## Inventory and scope

The September 29 host inventory found one development queue on USB003, the
Open ZJS driver and firmware monitor, and an unused **HP LaserJet 1005 GPD
diagnostic** driver. The diagnostic registration shares the renderer and Windows
core files. Neither development driver has a driver-store INF. There were no
pending jobs. EPSON L395 and Microsoft Print to PDF are unrelated resources.

The inventory and copies of the actual live private files are retained locally
under artifacts/migration-20260929/. No host printer, registration, system file,
trust setting or job was changed. The private firmware and Windows binaries
must never enter release archives.

The chosen first-release route is explicit retirement followed by a fresh owned
installation. Retire the unused diagnostic registration as part of this known
development configuration. Any queue using that diagnostic driver, additional
driver sharing private files/monitor, changed file, unrecognized registration
or pending development job blocks the procedure. Do not infer ownership from a
name alone or delete a spool directory recursively.

## Recovery requirements

Keep an independent recovery bundle before any host maintenance:

- Exact live GPD, INI, renderer, monitor, diagnostic GPD and private firmware,
  with hashes checked both when saving and before use.
- Driver and monitor registrations, queue port/metadata/access permissions and
  the global DEVMODE. Save settings before deleting the queue.
- The matching development registration procedure and a compatible Microsoft
  Visual C++ x64 Redistributable. The actual September 13 development DLLs import
  MSVCP140/VCRUNTIME140/VCRUNTIME140_1 and cannot load on the clean VM without it.
- Access to the complete new, trusted candidate and a tested way to remove a
  partial owned installation before restoring the development build.

The current packaged DLLs statically link their runtime. The legacy runtime
requirement belongs to recovery, not to the new package. Microsoft's
[redistributable guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)
and [binary compatibility guidance](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017)
describe the supported v14 runtime relationship.

Recovery restores only the saved global queue configuration. Per-user defaults,
the user's default-printer choice, application-held handles, jobs and printer
sharing require separate handling. Close printing applications, let jobs finish,
and tell users that the new owned queue has a different name. Never delete jobs
to make a migration proceed.

## Replacement sequence

1. Recheck the inventory, no-job condition, dependencies and backup hashes.
   Establish candidate trust and stage its unmodified INF through Windows before
   taking the working development installation out of service. Stage rejection
   must leave it working.
2. Remove the explicit development queue, unregister the two reviewed legacy
   drivers, then unregister the exact legacy firmware monitor. Unregistering
   with DeletePrinterDriverEx flags zero preserves shared driver files.
   Delete only the six enumerated private files after comparing their hashes.
   The imported firmware is read-only on this host; handle that attribute only
   on the verified private file. Access/ownership failures require investigation,
   rather than repeated blind restart attempts.
   Keep all Windows-supplied files and unrelated drivers.
3. If a driver, monitor or file remains in use, stop, restart Windows, and retry
   the remaining cleanup. Keep the backup intact. Do not restart the shared
   spooler or overwrite locked resources. Do not install new files at paths
   awaiting deletion.
4. Run the new Setup as a fresh installation. On physical hardware use normal
   USB discovery; --offline is exclusively for file-output lab checks.
5. Restore the saved global defaults, port, metadata and permissions to the new
   owned queue, converting the DEVMODE through the new driver. Verify live and
   cached hashes, ownership and settings, then validate output.
6. If installation or validation fails, remove any partial owned installation
   with its Setup, completing any restart/retry first. Restore the original
   private files, register the legacy monitor and both legacy drivers, recreate
   the development queue, restore its settings and verify the original hashes
   and printing. Setup's **Roll back** does not perform this legacy recovery.

The Windows API behavior used here is documented by
[DeletePrinterDriverEx](https://learn.microsoft.com/en-us/windows/win32/printdocs/deleteprinterdriverex)
and [DeleteMonitor](https://learn.microsoft.com/en-us/windows/win32/printdocs/deletemonitor).
Shared Windows core files must not be force-replaced when re-registering the
legacy drivers; the rehearsal uses the development installer's copy-new-files
behavior, followed by exact private-file hash checks.

## Isolated VM rehearsal

The script scripts/test-vm-migration.ps1 is hard-bound to the dedicated isolated
VM and always restores its clean checkpoint. It has no host migration mode.
Its guest fixture is in tests/vm/. Supply a directory containing the six legacy
files and manifest.json entries with file and sha256 fields. The host inventory
supplies those exact live bytes; VM queue settings and account permissions are
representative test values, not a clone of the host account.

Run from elevated PowerShell 7:

~~~powershell
./scripts/test-vm-migration.ps1 -CandidatePath artifacts/rc-CURRENT/HPLJ1005x64 -LegacyFiles artifacts/migration-20260929/legacy-files -QueueTestPath build/Release/queue_test.exe -VcRuntimeInstaller 'C:/path/to/Microsoft/vc_redist.x64.exe'
~~~

The harness verifies the runtime installer's Microsoft signature on the host and
its copied hash in the guest, then installs it only in the guest. It first uses
the candidate with a guest-only lab certificate to obtain Windows core files;
those VM-local files are never exported in the results. Security enforcement and
Secure Boot remain enabled.

Cases cover refusal of implicit adoption, an extra legacy-driver queue, damaged
recovery settings, rejected tampered-package staging, restoration after a failed
fresh install, successful replacement and restoration after removing the new
installation. It checks settings, permissions, actual live file hashes and the
unrelated PDF printer. Three portrait captures are produced before replacement,
after replacement and after recovery. Validate/decode each with lj1005, then
run tests/verify_captures.py with --scenarios portrait and prefixes before-,
migrated- and restored-.

The file last-migration-run.txt identifies the evidence directory. A successful
harness result alone does not assert reference-byte comparison, physical
printing or a successful host migration. See [test status](TEST_STATUS.md) for
dated results. Each run saves and hashes its harness sources and uses the frozen
guest fixture across reconnects. The September 29 clean-checkpoint run and all
three independent reference comparisons passed.

## Before physical acceptance

Choose the target machine and signing/trust route explicitly. On October 21 the
maintainer authorized the dedicated technical-tester certificate and migration
on SIRIUS, superseding the earlier VM-only restriction for this identity and
machine. Rehearsal scripts are not a general migration tool.
Before maintenance, take a fresh host recovery bundle including its actual global
DEVMODE, compare it with the reviewed inventory, and schedule a restart if needed.
Physical USB selection, first print, application printing, power-cycle firmware
reload and paper/error recovery remain separate acceptance checks.

## Fresh private recovery capture

Physical migration was authorized on October 21; backup and recovery checks
must complete before retiring the existing installation.
To prepare a new capture without changing printers, jobs or certificate stores,
run this from an administrator PowerShell 7 shell with a new output directory:

~~~powershell
./scripts/backup-development-printer.ps1 -OutputDirectory ./artifacts/recovery-NEW -VcRuntimeInstaller 'C:/path/to/Microsoft/vc_redist.x64.exe'
~~~

The tool checks the expected legacy queue, both driver registrations, monitor,
no-job condition and runtime signature. It captures the six private files,
registrations, global DEVMODE, queue metadata/permissions, port/configuration,
current user's default-printer name, recovery source references and runtime
installer. It verifies copied hashes and checks that live files, global DEVMODE
and queue metadata did not change during capture. A failure leaves a partial
folder for investigation; only `result.json` plus a verified complete manifest
establish a successful capture.

This directory is **private recovery data**, not a support report. Do not publish
it or import its registry exports blindly. The saved scripts retain their original
layout assumptions; they are references for a reviewed recovery procedure, not a
standalone restore command. Copy the bundle to independent private storage and
rehearse recovery with the fresh files before host maintenance. Other accounts'
per-user defaults, jobs and application settings are not included.

The October 15 capture is under
`artifacts/release-preparation-20261015-164909/private-recovery/`. All 28 manifest
entries were verified. Independent-copy confirmation and recovery rehearsal of
this fresh capture remain pending. The host inventory reported Windows 11 Pro
25H2 build 26200.9457, no development jobs, and Secure Boot disabled; no security
settings were changed. Review the Secure Boot acceptance prerequisite before
physical installation. Earlier successful guest tests kept guest Secure Boot on.
