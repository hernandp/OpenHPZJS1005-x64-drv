# Test evidence by deployment path

## Have Disk package

### November 10–11, 2026: installation and manual maintenance

Windows 11 Pro 25H2 x64 build 26200.6584, Secure Boot enabled, passed the
maintenance rehearsal in the isolated `OpenZJS-Win11-25H2` VM. Checkpoint
`Before-manual-maintenance-20261110` was preserved before testing. Local evidence
is in `artifacts/maintenance-20261110`, including `cycles.log`,
`reinstall-0.1.1.json`, `reinstall-0.1.2.json` and `final-verification.json`.

The actual Add Printer Wizard installed the INF on FILE: with sharing disabled.
Initial preferences were Letter, monochrome and simplex; A4 was selected explicitly.
Portrait, landscape, blank, multi-page and Letter file captures passed protocol
validation with the extracted source folder temporarily moved aside. Settings
removed the queue, and Print Server Properties removed the driver and package.
The first package-removal attempt reported the driver in use; the dialog succeeded
after a normal guest restart.

The guide's manual blocks refused cleanup with a dependent queue or registered
driver, and rejected a signed but wrong-version monitor reference. The cleanup
block also refused to run after failed preflight. DeleteMonitor returned error
3008 until another restart. Deleting SYSTEM-owned leftovers required the narrowly
scoped ownership and access commands. Windows PowerShell reported access denial
as `RemoveFileSystemItemArgumentError`; the documented fallback handles this as
well as the unauthorized-access error ID. Complete cleanup and a repeated run
with both files already absent passed.

Scripted Windows INF installation then exercised the same 0.1.1 package and a
private 0.1.2 fixture, using complete removal between versions. Installed monitor
versions were 0.1.1.0 and 0.1.2.0; installed monitor, renderer, GPD, INI and firmware
hashes matched their respective packages. A4, monochrome and simplex were restored,
and each version produced a valid three-page capture with an empty queue afterward.
These reinstall/update cycles used Windows APIs, not a second wizard walkthrough.
Windows changed the default printer during both cycles; explicit restoration of
Microsoft Print to PDF passed. Its driver and PORTPROMPT: port remained correct.

Final verification found no Open ZJS queue, registered driver, driver-store package,
monitor registration or designated monitor/firmware file. The exact tester identity
`DEAD78F5D4F2A4D8A424726555A6C3D536E445FC` was removed separately from the guest's
Root and TrustedPublisher stores, and the catalog was no longer trusted. Secure
Boot remained enabled. The VM was left cleaned rather than reverted to its checkpoint.

Exercised binary ZIP SHA-256 values:

- 0.1.1, `artifacts/mini-b94a4c0a/HPLJ1005x64-0.1.1-mini-self-signed-test.zip`:
  `9e97318ee1169c5de9398f478098140e372cc99e896a1f8c3303b1ed89fbe5f6`.
- Private 0.1.2 fixture, under `artifacts/maintenance-20261110/update-fixture/source/artifacts/mini-583985f2`:
  `48d170f86e0600566323499c8856c5874255bcaa85938231ddecf7f01855152f`.

The first archive contained draft instructions; the corrected guide blocks were
staged separately for the rehearsal. A subsequent ZIP rebuild includes the verified
instructions. These VM results apply to the exact archives above and do not claim
another wizard run against that rebuild. Physical USB, paper handling, the four-sheet
stop and public signing remain separate release prerequisites.

### November 5, 2026: initial wizard rehearsal

On November 5, Windows 11 Pro 25H2 (26200) with Secure Boot enabled verified
the correct firmware and all five driver payload files against the signed catalog.
PnPUtil rejected missing firmware (exit 2, file not found) and an incorrect image
(exit -536870325, catalog hash mismatch). Neither negative test staged a package.
The actual Add Printer Wizard accepted the INF selected using Browse and showed
the Open ZJS model with an Authenticode signature. FILE: was selected for the VM
test. Following explicit approval, the wizard installed the printer successfully;
sharing was disabled and the success page was observed. No Setup executable was
used. Installed firmware and all four project driver files matched the package,
and no Setup ownership key existed. Five captures (portrait, landscape, blank,
multi and Letter) passed protocol validation, geometry/page-count/margin checks
and byte-for-byte reference encoding. The source folder was temporarily renamed
during printing, and the queue cleared. The initial paper setting was Letter;
A4 was explicitly selected for the captures. These are VM FILE: results, not
physical USB/firmware-upload or physical Letter acceptance.

Standard Windows queue and driver/package removal succeeded without error, but
left the language monitor registration, monitor DLL and firmware. DeleteMonitor
then succeeded, but deleting the DLL returned access denied, including after a
guest restart. ACL inspection showed both files owned by SYSTEM, with full access
for SYSTEM and read/execute access for Everyone/application packages; administrator
delete access was absent. The initial loaded-DLL hypothesis was not established.
After checking hashes and absence of dependent Open ZJS drivers, targeted ownership
and permission changes to those two files allowed deletion. This limitation is
documented in the minimal package guide; ordinary removal is not complete cleanup.

Final inspection confirmed the queue, driver, driver-store INF, monitor registration
and installed monitor/firmware files were absent. The exact tester certificate
was removed from both local-machine stores; the catalog was no longer trusted.
Secure Boot remained enabled and the unrelated Microsoft Print to PDF queue was
unchanged. See `results/cleanup-verification.json` and `results/leftover-acls.jsonl`
under the evidence directory. Cleanup used Windows APIs and maintenance commands;
the graphical Print Server Properties removal path was not exercised.

The exercised archive was `artifacts/mini-afa32dca/HPLJ1005x64-0.1.1-mini-self-signed-test.zip`,
SHA-256 `67b3000cf91fd5b9d52902a2160b178bcdc6aca8c8fb51f98dda4d290af0b9b8`.
Its bundled README and build metadata retain the pre-rehearsal pending labels;
this dated record supplies acceptance for that exact archive, not a rebuilt ZIP.

## Earlier packaged driver on USB and FILE:

- On November 2, two consecutive Windows test pages printed correctly on the live USB004 A4 queue. The earlier USB003 blank-page cause remains unresolved. An Inkscape bitmap printed successfully. SumatraPDF printed a bordered A4 page and a three-sheet marked/blank/marked job.
- A four-sheet PDF repeatedly stopped with the second sheet printed but partly inside the printer. A file capture preserved page order. Spooler counts alone cannot confirm ejection. Physical copies/collation and hardware recovery remain open.
- Windows 11 build 26300 has offline capture evidence. The earlier 25H2 VM passed x86 GDI and SumatraPDF, x64 Inkscape and bounded Edge content/order tests. Edge exceeded the strict geometry tolerance and reproduced the ruler difference through Microsoft Print to PDF. Physical Letter and Office remain unverified.

The November 6 packaging change built directly from the canonical INF. Its local signed ZIP passed archive verification and four mutation checks. The subsequent scripts/CI cleanup passed eight Python tests, seven driver tests, support-report tests in two PowerShell versions and clean candidate verification. Keep these build checks distinct from the November 5 wizard rehearsal above.

The full dated record includes original job identifiers, archive digests, failed runs, recovery steps and local evidence paths. [Historical test log](https://github.com/hernandp/OpenHPZJS1005-x64-drv/blob/7f28f61977bc90e0cf3e03fd9fb78535bc45b4ce/docs/TEST_STATUS.md)
