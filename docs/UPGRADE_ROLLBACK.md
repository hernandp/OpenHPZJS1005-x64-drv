# Owned installation upgrades and rollback

Setup 0.1.1 adds **Upgrade** and **Roll back** for installations created by an
earlier owned Setup with repair hashes. Development installations and previews
without those records still require separate migration. Public signing and
physical USB acceptance remain release gates.

Extract the complete new package, run its Setup, choose **Upgrade**, and select
the original firmware. Setup requires a complete, intact installed package, no
pending jobs and no other queue using the driver or monitor. It rejects older
package versions; **Roll back** is the way to restore the saved previous build.
Different builds with the same version are distinguished by their recorded file
hashes. Firmware is always user-supplied and never bundled.

Before changing the working installation, Setup saves the previous package and
installer, firmware, queue port/comment/location/priority/sharing settings,
global paper defaults and printer permissions. It verifies the new catalog and
asks Windows to stage the package, which checks its catalog membership. Saved
packages and settings are held under a separate protected Program Files
directory, with their hashes and a durable recovery phase in HKLM.

Upgrade removes and recreates only the owned installation, including a changed
monitor DLL. It uses the existing ownership-aware removal and Windows package
installation APIs. It never restarts the shared Print Spooler. Per-user printing
preferences and applications holding a printer handle are outside the saved
global configuration; close printing applications before upgrading.

If Windows retains a file or monitor, restart Windows and rerun the **new
extracted Setup**. Choose **Upgrade** to continue or **Roll back** to restore the
old build. Keep the extracted package until the operation is complete. New files
are not installed at paths that still have pending reboot deletions. Removal is
blocked while an update is unfinished so it cannot discard the recovery data.

An installation or settings-restoration failure triggers rollback. If a restart
is required to complete recovery, Setup preserves the backup and reports that
recovery is unfinished. After process termination during installation, choosing
Upgrade restores the previous build before another upgrade attempt. The
rollback operation itself can also be resumed after interruption.

A successful upgrade retains one previous package for **Roll back**. Starting
another upgrade replaces that saved history with the currently installed build.
Rollback restores the pre-upgrade global queue settings, not settings edited
after the upgrade. Successful rollback discards the update cache; normal removal
also clears a retained backup. Changed backup hashes, unknown ownership and
redirected paths are refused.

For the isolated lab only:

```powershell
Setup.exe --quiet --upgrade --offline --firmware C:\private\sihp1005.img
Setup.exe --quiet --rollback
```

Quiet mode requires elevation. Exit 0 means completion, 3010 requests restart
and rerun, and 1 reports failure (which can include a successfully rolled-back
upgrade). Read the diagnostic and recovery phase rather than assuming that a
failed upgrade left the new build installed. The existing `--install` remains
fresh installation or exact-build repair.

## VM validation

`scripts/test-vm-upgrade.ps1` accepts previous/new candidate directories, firmware,
queue-test executable and an explicitly built `lj1005setup_fault.exe`. It
restores only the dedicated isolated VM checkpoint and signs catalogs with a
certificate trusted only inside that guest. Normal signature enforcement and
Secure Boot remain enabled.

Build the fault fixture with `scripts/build.ps1 -BuildSetupFaultFixture`. Its
compile-time-only hooks pause or fail after durable update-phase writes. The
harness terminates the exact paused fixture process to exercise recovery. The
normal build and candidate package exclude that executable and those hooks.

Cases cover a real version and monitor-binary change, tampered-source rejection,
dependent-queue rejection, retained queue settings/permissions and unrelated PDF
printer, a monitor lock forcing restart, explicit rollback, damaged-backup
rejection, downgrade rejection, automatic rollback after installation failure,
terminated upgrade/rollback processes, print captures, and final removal with a
saved rollback package, including retry after backup cleanup fails. See
`TEST_STATUS.md` for actual dated results; this
procedure alone is not an acceptance claim.
