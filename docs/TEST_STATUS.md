# Test evidence by deployment path

## First GitHub release — November 18, 2026

The maintainer reported testing the signed 0.1.2 mini ZIP and publishing it
alongside the matching source ZIP and checksums. GitHub reports the mini ZIP
SHA-256 as `1df6797a25b9edc1274fa08146c4a2108b3a4935e87f95ba2aab2b6f04573cf0`,
which matches the locally verified build. The build snapshot used base commit
`a9ccfb2c863ed00d6b43601b780aa06dc2728114` with the archive naming changes
still in the working tree. The attached source ZIP contains that snapshot.
The guest Windows build and individual test results were not supplied for
this report. [GitHub release](https://github.com/hernandp/OpenHPZJS1005-x64-drv/releases/tag/0.1.2)

## VMware Workstation Have Disk and A4 test page — November 17, 2026

The maintainer reported that Have Disk installed the 0.1.2 tester mini ZIP in
a Windows 11 VMware Workstation guest. The printer was connected from the
VMware menu, the guest queue used USB001, and the guest detected
`USB\VID_03F0&PID_1317` with status OK. The exact guest Windows build and ZIP
digest were not independently verified for this run.

The wizard's first physical test produced multiple blank sheets. A separate
FILE: queue using the same driver captured a Windows test page that validated
as one Letter sheet, one copy, at 600 dpi. The decoded raster contained the
expected text and printer graphic. Capture SHA-256:
`b4e5ed15e1637b3a61bfd7af9257ac956bac2d92c7b010c073a652b14a28d4ef`.

After changing the USB001 queue from Letter to A4, the maintainer reported
that the physical Windows test page printed correctly. No monitor trace was
supplied. This records a successful A4 retry on this VM and printer; the cause
of the initial blank sheets and physical Letter behavior remain unverified.

## USB completion retry fix — November 14–16, 2026 (timed recovery validated)

The 0.1.2 signed retry candidate was built from base commit
`93fee0ddb4bb7d6d5740a3bd43f764e8add43679` plus the captured working-tree changes.
Tester ZIP SHA-256:
`2e28b4adf29df39800a52f2f79a43152b9c63b9f3d30e43dcde86a54ce0063d4`.
Matching source ZIP SHA-256:
`f9ae4763766f26236fd214eb11346e3c4c94ba029d00495995fc2b98f5d573db`.
Artifacts: `artifacts/mini-c654be26/`. The existing dedicated tester identity
was retained. Fresh-build tests, INF/GPD checks, archive verification and Windows
catalog verification of all six installed inputs passed. Firmware is excluded
from the ZIP and supplied privately for installation.

With HP off, a fresh 32-file recovery manifest was verified under
`C:\temp\lj1005-rec\benteveo-before-012-20261114`, preserving older backups.
The 0.1.1 queue and package were removed, but DeleteMonitor returned error 3008.
Installation paused for the documented host restart and remaining cleanup.
Maintenance evidence:
`artifacts/physical-benteveo-012-20261114/`.

After the host restart, the old DLL was absent but its registration remained.
With all queues idle, temporarily restoring the identity-verified backup DLL
and restarting the spooler allowed DeleteMonitor and cleanup to pass. The
maintainer then completed the actual Have Disk wizard for 0.1.2 on the resolved
USB004 port without printing. Installed payload hashes match the signed
candidate, the monitor reports 0.1.2.0, bidirectional support is enabled, A4
monochrome/simplex is selected, EPSON is default and the queue is empty.
Installation evidence: `artifacts/physical-benteveo-012-20261114/installation-verification.json`.
The 0.1.2 baseline Windows test page (job 2) printed exactly one correct,
fully ejected sheet without extras or warnings, according to the maintainer.
FirmwareReady and CompletionAcknowledged were recorded and the queue cleared
without cancellation. Evidence:
`artifacts/physical-benteveo-012-20261114/baseline-180414821/`.
The physical USB-interruption retry (job 3) produced four sheets in order
1, 2, 1, 2 according to the maintainer, who confirmed unplugging while the first
sheet fed. CompletionUsbDisconnected was recorded at 15:55:05 local time and
CompletionUsbReconnectTimeout at 15:55:36, followed by CompletionUnconfirmed.
There was no completion acknowledgement; the helper canceled the lingering
job at its 90-second deadline. The live device subsequently resolved to the
same USB004 path, with no duplicate queue, an empty queue and EPSON default.
On November 16, the maintainer clarified that USB may have remained unplugged
for more than 30 seconds. This timing uncertainty means the run does not
establish recovery within the intended retry window. It does not physically
validate the fix; no automatic resubmission was performed. Evidence:
`artifacts/physical-benteveo-012-20261114/usb-active-185458834/` and
`artifacts/physical-benteveo-012-20261114/usb-after-retry.json`.

On November 16, a controlled repeat used a stopwatch for the requested
ten-second interruption during first-sheet feed. The maintainer confirmed
correct four-sheet output in response to the 1, 2, 1, 2/full-ejection check.
Job 13 recorded CompletionUsbDisconnected, CompletionUsbReconnected and
CompletionAcknowledged; the queue cleared without cancellation. This validates
the retry path for that timed interruption on the exact signed 0.1.2 package
and host. The trace timestamps show 11.23 seconds between disconnect detection
and successful reconnection. A separate follow-up page (job 14) printed exactly
one correct, fully ejected sheet without extra pages or warning lights, as
confirmed by the maintainer, without power-cycling. Its trace recorded
CompletionAcknowledged and its queue cleared without cancellation. EPSON
remained default. Follow-up evidence:
`artifacts/physical-benteveo-012-20261116/usb-followup-135114244/`.
Evidence: `artifacts/physical-benteveo-012-20261116/usb-active-134936961/`.

Job 23's trace identified an immediate exit from completion monitoring when
opening the USB status reader returned Windows error 2 during unplug/replug.
The four sheets printed, but no matching END report was acknowledged and the
test helper eventually canceled the lingering job.

The source now distinguishes disconnect-related USB read errors from permanent
failures. While awaiting completion, the monitor retries the original interface
for up to 30 seconds per interruption, remains cancellable, and still requires
the same job's END report before acknowledgement. It never retransmits raster
data or the PJL trailer and never selects another port. A missing/lost END
report remains unconfirmed; the fix cannot infer physical ejection.

Release build and all seven driver tests passed, including simulated reconnect
followed by completion, cancellation during disconnect, persistent disconnect
timeout, and assertions that recovery does not replay raster or trailer bytes.
The monitor callback test took 30.64 seconds, exercising the production retry
bound. clang-format 22.1.3 and `git diff --check` passed. Evidence:
`artifacts/usb-completion-fix-20261114/`.

The signed 0.1.2 package is physically validated for this timed same-socket
interruption and follow-up on Windows build 26300.9974. This does not establish
recovery after the 30-second retry window, other ports or broader Windows
compatibility. VM acceptance and other 0.1.2 release checks remain separate.
The tested archives retain their pre-test documentation; subsequent evidence
updates do not change those archive digests or transfer acceptance to a rebuild.

## Physical Benteveo installation — November 11, 2026 (in progress)

Target: Windows build 26300.9974, original LaserJet 1005, Benteveo 0.1.1 tester
ZIP SHA-256 `60ef32f6e5bdd3b41f5e060fdbd18098cc46f634e43819a1abce2c91bb59fbad`,
source commit `994a4fceb346b88cdc5102137fb446f3559bba26`. VM acceptance for this
archive remains pending.

With the printer switched off and both HP queues empty, a fresh private recovery
bundle was created at `C:\temp\lj1005-rec\benteveo-20261111`; the October backup
was preserved. The bundle contains installed driver-store files, matching old
package references, live monitor/renderer/firmware, queue/settings exports and a
verified hash manifest. The new ZIP digest and Windows catalog membership of all
six supplied driver/firmware files passed verification using the retained tester
identity. No new certificate trust was added.

The old USB003 and USB004 HP queues and their driver package were removed.
Manual preflight passed, but DeleteMonitor returned Windows error 3008. After
the host restart, the old DLL was absent while its registration remained, and
DeleteMonitor returned error 3000. With all printer queues idle, restoring the
verified old DLL temporarily and restarting the spooler allowed DeleteMonitor
and the documented file cleanup to succeed. This additional recovery step is
recorded in `artifacts/physical-benteveo-20261111/repair-cleanup.log`.

The actual Add Printer Wizard then displayed the Benteveo model with an
Authenticode signature and reported successful installation on the verified live
USB004 port. Verification resumed November 13 after the session interruption:
installed monitor, renderer, GPD, INI and firmware hashes match the selected
package; bidirectional support is enabled, sharing is disabled and the queue is
empty. Initial paper size was Letter; A4, monochrome and simplex were explicitly
selected. EPSON L395 Series remains the default, and Microsoft Print to PDF is
present. Evidence: `artifacts/physical-benteveo-20261113/installation-verification.json`.
On November 13, after a confirmed power cycle, Windows test-page job 2 printed
successfully according to the maintainer. The trace recorded FirmwareReady and
CompletionAcknowledged; the queue cleared without cancellation. Evidence:
`artifacts/physical-benteveo-20261113/windows-page-142108336/`. This passes the
first cold-start Windows test page for the exact Benteveo package. Without a
power cycle, a second Windows test page (job 3) also printed correctly and fully
ejected according to the maintainer. CompletionAcknowledged was recorded and
the queue cleared without cancellation. Evidence:
`artifacts/physical-benteveo-20261113/windows-page-142339283/`.
Consecutive Windows test pages pass.

Inkscape 1.4.4 printed the existing A4 portrait SVG through its actual print
dialog as job 4. The maintainer confirmed Benteveo, A4, portrait and one copy in
the dialog, clicked Print while scripted tracing ran, and reported successful
output. CompletionAcknowledged was recorded and the queue cleared without
cancellation. The physical 100 mm ruler measurement was not reported. Evidence:
`artifacts/physical-benteveo-20261113/inkscape-portrait-142828570/`.
The landscape SVG then printed through the same application/dialog as job 5.
The maintainer confirmed correct output and reported the 100 mm line measured.
CompletionAcknowledged was recorded, the queue cleared without cancellation,
and no paper stoppage was reported. Evidence:
`artifacts/physical-benteveo-20261113/inkscape-landscape-205542837/`.
Inkscape portrait and landscape output pass.

SumatraPDF 3.6.1 x86 printed the A4 portrait PDF fixture with portrait/noscale
settings as job 6. The maintainer confirmed correct output, all border edges,
and a measured ruler length of 100 mm. CompletionAcknowledged was recorded and
the queue cleared without cancellation. Evidence:
`artifacts/physical-benteveo-20261113/pdf-single-205830087/`.
The three-page blank-middle PDF then printed as job 7. The maintainer confirmed
exactly three correctly printed sheets in order: page 1 of 3, blank sheet,
page 3 of 3. CompletionAcknowledged was recorded and the queue cleared without
cancellation. Evidence:
`artifacts/physical-benteveo-20261113/pdf-multipage-210415565/`.
Multi-page PDF and blank-middle handling pass.

SumatraPDF then requested two copies of the two-page fixture using `2x` as
job 8. The maintainer confirmed four correctly printed sheets fully emerged in
order 1, 2, 1, 2, without reported stoppage or warning lights. The trace recorded
CompletionAcknowledged and the queue cleared without cancellation. Evidence:
`artifacts/physical-benteveo-20261113/pdf-copies-210619393/`.
This passes the viewer's two-copy request and observed collated output; it does
not test an independently selectable uncollated mode. Without power-cycling,
the identical two-copy request was repeated as job 9. The maintainer reported
correct output; CompletionAcknowledged was recorded and the queue cleared
without cancellation. Evidence:
`artifacts/physical-benteveo-20261113/pdf-copies-repeat-210852593/`.
The consecutive four-sheet repeat passes.

An isolated diagnostic derived from `queue_test.cpp` exercised ResetDC within
one job while keeping every page A4: portrait, landscape, portrait. Its explicit
file-output preflight validated three pages before physical submission. Job 11
then printed the three labelled pages; the maintainer confirmed correct output.
CompletionAcknowledged was recorded and the queue cleared without cancellation.
Evidence: `artifacts/physical-benteveo-20261113/orientation-helper/` and
`orientation-a4-212916032/`. The installed driver was unchanged. The intervening
job number was the file-output preflight, not an additional physical print.
Physical in-job orientation passes.

Cancellation before transmission passed: the Benteveo queue was paused while
empty, Windows test-page job 12 was submitted and canceled, and the empty queue
was resumed successfully. The maintainer explicitly confirmed no sheet fed.
Follow-up job 13 then printed exactly one correct Windows test page and fully
ejected; CompletionAcknowledged was recorded and the queue cleared without
cancellation. Evidence: `artifacts/physical-benteveo-20261113/` directories
`cancel-before-send-213051617/` and `cancel-before-followup-213140575/`.
For active-job cancellation, two copies of the two-page PDF were submitted as
job 14. After five seconds of spooler-reported printing, only that job was
canceled and the queue cleared. The trace recorded QueueCancelled and
CompletionUnconfirmed. The maintainer reported all four sheets printed in order
1, 2, 1, 2. Already-buffered output is a possible explanation; cancellation did
not prevent those sheets from emerging. Physical activity at the precise moment
of cancellation was not confirmed. Evidence:
`artifacts/physical-benteveo-20261113/cancel-active-213349256/`.
The maintainer reported no noticeable change when cancellation occurred and
confirmed readiness for a follow-up without power-cycling. Job 15 then printed
exactly one correct Windows test page without reported extras or warning lights.
CompletionAcknowledged was recorded and the queue cleared without cancellation.
Evidence: `artifacts/physical-benteveo-20261113/cancel-active-followup-213557438/`.
Queue cancellation and subsequent printing recover successfully; this is not
a pass for promptly stopping already-buffered physical output. Other recovery
checks are pending.

During idle USB reconnection, the maintainer used a different socket or hub.
Windows assigned USB005 and created `HP Laserjet 1005 (Benteveo Driver) (Copy 1)`;
the original queue remained on USB004. Both queues were empty, EPSON remained
the default, and no follow-up page was submitted. This records a different-port
move, not a completed same-socket reconnect test. Same-socket recovery and its
follow-up page remain pending. Evidence:
`artifacts/physical-benteveo-20261113/usb-idle-reconnect-inventory.json`.

On November 14, the maintainer reported reinstalling on USB004 and requested
restarting this test. The live device resolved to USB004, only one Benteveo
queue remained, and it was empty. Reinstallation had selected Letter and made
HP the default; A4 and the EPSON default were restored. Installed payload hashes
matched the previously verified package. The maintainer unplugged USB for ten
seconds and reconnected to the same socket without power-cycling. USB004
returned without a duplicate queue. Windows test-page job 22 then printed
correctly and fully ejected according to the maintainer; the trace recorded
CompletionAcknowledged and the queue cleared without cancellation. Same-socket
idle USB reconnect and follow-up printing pass. Evidence:
`artifacts/physical-benteveo-20261114/usb-idle-followup-042454882/`.
Two earlier preflight attempts submitted no job: the helper's firmware filename
and obsolete pre-reinstallation PnP-instance check were corrected to use the
installed firmware name and live device-to-port resolution.

USB interruption during output was exercised as job 23 on November 14. The
maintainer unplugged USB while the first sheet was feeding, waited ten seconds,
and reconnected; four sheets printed in order 1, 2, 1, 2. However, the trace
recorded CompletionFailure with `Opening USB status reader (Win32 error 2)`
and no completion acknowledgement. The job remained listed until the bounded
test helper canceled it at 90 seconds. The queue was then empty, USB004 was
live, no duplicate queue existed, and EPSON remained default. Physical output
passed, but automatic queue/completion recovery did not pass; a fresh follow-up
page without power-cycling remains pending. Evidence:
`artifacts/physical-benteveo-20261114/usb-active-045406983/`.

The subsequent Windows test page (job 24) printed correctly without a power
cycle, with exactly one fully ejected sheet and no extras or warning lights,
according to the maintainer. CompletionAcknowledged was recorded and the queue
cleared without cancellation; EPSON remained default. Follow-up printing passes,
but this does not resolve the interrupted job's completion failure or its need
for cancellation. Evidence:
`artifacts/physical-benteveo-20261114/usb-active-followup-045709058/`.

Idle and queued-job sleep/wake checks could not be exercised on this host.
The maintainer reported that Windows offers Hibernate but no Sleep option.
`powercfg /a` confirmed that S1, S2, S3 and S0 Low Power Idle are unavailable
because system firmware does not support them; Hibernate and Fast Startup are
available. No sleep transition or follow-up print was performed. Sleep/wake
coverage remains untested and requires a suitable host; hibernation is not
counted as equivalent coverage. Evidence:
`artifacts/physical-benteveo-20261114/power-states.txt` and
`artifacts/physical-benteveo-20261114/sleep-idle-before.json`.

Paper-out was exercised with Windows test-page job 25 and an empty input tray.
The maintainer observed a flashing yellow light and no sheet feed. After the
pending job was confirmed, the maintainer loaded one A4 sheet and reported that
the original job resumed and the test page printed. CompletionAcknowledged was
recorded and the queue cleared without cancellation or resubmission. The
maintainer then confirmed full ejection and that the warning cleared. Evidence:
`artifacts/physical-benteveo-20261114/paper-out-050405170/`.
Without power-cycling, follow-up job 26 also printed correctly according to the
maintainer. CompletionAcknowledged was recorded and its queue cleared without
cancellation. Paper-out recovery and subsequent printing pass. Evidence:
`artifacts/physical-benteveo-20261114/paper-out-followup-050812268/`.

For front-cover recovery, the maintainer opened the normal accessible cover
and reported blinking yellow lights. Windows test-page job 27 remained pending
while the cover was open. After closing it, the maintainer reported the test
page printed correctly. CompletionAcknowledged was recorded and the queue
cleared without cancellation or resubmission. The maintainer explicitly
confirmed no feed while open, full ejection after closing, and cleared warning.
Evidence:
`artifacts/physical-benteveo-20261114/door-open-051300938/`.
Follow-up job 28 printed exactly one correct page, fully ejected without extra
pages or warning lights, according to the maintainer. CompletionAcknowledged
was recorded and the queue cleared without cancellation. Front-cover recovery
and subsequent printing pass. Evidence:
`artifacts/physical-benteveo-20261114/door-open-followup-051520700/`.

Cold power cycle 1 of 5: after the requested ten-second power-off interval,
the maintainer confirmed readiness and Windows test-page job 29 printed
correctly according to the maintainer. FirmwareReady and CompletionAcknowledged
were recorded, and the queue cleared without cancellation. Cycle 1 passes;
subsequent cycles are recorded below. Evidence:
`artifacts/physical-benteveo-20261114/cold-cycle-1-131315634/`.
Cold cycle 2 also passed: job 30 printed exactly one correct, fully ejected
Windows test page without extras or warning lights, according to the maintainer.
FirmwareReady and CompletionAcknowledged were recorded and the queue cleared
without cancellation. Evidence:
`artifacts/physical-benteveo-20261114/cold-cycle-2-131551503/`.
Cold cycle 3 passed: the maintainer reported correct output for job 31.
FirmwareReady and CompletionAcknowledged were recorded and the queue cleared
without cancellation. Evidence:
`artifacts/physical-benteveo-20261114/cold-cycle-3-131734653/`.
Cold cycle 4 passed: job 32 printed exactly one correct, fully ejected page
without extras or warning lights, according to the maintainer. FirmwareReady
and CompletionAcknowledged were recorded and the queue cleared without
cancellation. Evidence:
`artifacts/physical-benteveo-20261114/cold-cycle-4-132226081/`.
Cold cycle 5 passed: job 33 printed exactly one correct, fully ejected page
without extras or warning lights, according to the maintainer. FirmwareReady
and CompletionAcknowledged were recorded and the queue cleared without
cancellation. Evidence:
`artifacts/physical-benteveo-20261114/cold-cycle-5-132620902/`.
All five cycles passed using separate Windows test pages, correlated by job IDs
29–33 rather than printed cycle numbers. This verifies firmware readiness and
printing after each user-confirmed power cycle; FirmwareReady alone is not
claimed as proof of a firmware upload during each captured trace.

## Physical four-page retry — November 11, 2026

On Windows build 26300.9974 (DisplayVersion 26H2), the existing Open ZJS
0.1.1.0 installation printed the same four-page PDF that had previously stopped
with a sheet partly ejected. No driver replacement or code change was installed.
After confirming empty queues, a clear paper path and a cold power cycle, the
maintainer loaded four fresh A4 sheets. SumatraPDF 3.6.1 x86 submitted one copy
with A4, portrait and noscale to the verified live USB004 queue, as job 2.

The maintainer confirmed all four sheets fully emerged in order **1, 2, 1, 2**,
with no jam. The trace recorded PrinterEnd, successful LowerEndDoc and
CompletionAcknowledged for token `lj1005-2-46417093-1`. The queue cleared without
cancellation; both HP queues were empty afterward. Windows changed the default
to the HP during printing; the recorded EPSON L395 Series default was restored.

This passes this single four-page retry, not repeated reliability or the PDF
viewer's copies/collation setting. Following the successful retry, the maintainer
identified the earlier stoppage as a hardware solenoid problem during paper loading
and confirmed it was repaired. This is the maintainer's hardware diagnosis;
the trace independently confirms completion of the post-repair job.
This was the earlier installed driver, not acceptance of the new
Benteveo Have Disk ZIP. No additional physical job was submitted.

Local evidence: `artifacts/physical-print-20261111/before.json`,
`defaults-restored.json`, and `pdf-explicit-four-182557418/` (trace, queue history,
result and maintainer observation). The PDF SHA-256 was
`dcab764c2681798a93c39e186bfee5ace6553f9dab43fefb7c6ccbc10f131f34`.
Installed monitor SHA-256:
`6bad5e59f5654acd7163ccd459980977750d1c900e428bd7c22c9c9bd497f87a`;
renderer SHA-256:
`4e324a6090f41f4165a20de7a78c09200c8d9b049028bc33a8c0c7eb59ef6878`.

## Benteveo branding checkpoint — November 11, 2026

The project and visible driver model have been renamed to Benteveo. The rebuilt
package requires fresh VM installation acceptance. All VM and physical results
below predate that change and retain their original names and archive digests.

## Have Disk package

### November 10â€“11, 2026: installation and manual maintenance

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
another wizard run against that rebuild. Physical USB, paper handling and public
signing remain separate release prerequisites. The earlier four-sheet stoppage
was subsequently attributed to a repaired paper-loading solenoid; see the
November 11 physical retry above.

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
