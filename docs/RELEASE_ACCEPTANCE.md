# Physical acceptance for the Have Disk package

The supported setup is Windows 10 22H2 x64 or Windows 11 x64 with one original LaserJet 1005 on USB, using A4 monochrome simplex at 600 dpi. The maintainer confirmed that 0.1.2 works on Windows 10 22H2. The detailed physical results below apply to Windows build 26300.9974. The full matrix still needs to be run on Windows 10 22H2 and Windows 11 Pro 25H2 (build family 26200). Record the full build, revision, package SHA-256 and signer for each test. ARM64, parallel ports, shared printers and multiple devices are outside the supported setup. Office is deferred.

Use a clean physical target with an independently verified private recovery backup. Follow the documented complete removal procedure before reinstalling or updating. Earlier USB and application results guide these tests. The maintainer reports that the paper-loading solenoid responsible for the earlier stoppage is repaired; a four-page retry passed on the earlier installed driver. The branded package now has physical printing evidence, with the recovery gaps recorded below. [Earlier results](TEST_STATUS.md)

## Physical printing matrix

Results for the exact Benteveo tester package are recorded below. Earlier
development-driver output is supporting history. Submit one controlled job at a
time, record the starting queue/job count, and wait for both physical output and
queue completion before moving on.

| ID | Procedure | Pass evidence |
| --- | --- | --- |
| P01 | Fresh Have Disk install with printer connected | Correct USB port/device, one intended queue, A4 selected, no unrelated changes |
| P02 | Power off/on, then submit a one-page Windows test page | Firmware loads after cold start; one correct page; job completes |
| P03 | Inkscape Print dialog, A4 portrait and landscape | Correct scale, orientation, margins and clipping; one page each |
| P04 | PDF viewer, one selected page and a three-page document | Fonts/graphics, scale and page order correct; three-page job completes once |
| P05 | Two copies of a numbered two-page document | Exactly four pages in the application's requested order |
| P06 | Blank page between numbered pages | Blank-page handling and page count match requested output |
| P07 | Change A4 portrait/landscape between jobs; exercise in-job ResetDC separately | No stale paper/orientation, corruption or lost pages |
| P08 | Load physical Letter and select Letter, then restore A4 | Correct margins and page feed on actual paper; correct A4 restoration |
| P09 | Repeat representative PDF output through an x86 application | Application version/bitness recorded; correct output and completion |
| P10 | Windows queue and package removal on the physical target | Record leftover monitor/firmware and the state of unrelated printers |

## Hardware recovery matrix

Use numbered, non-sensitive pages and bounded jobs. Before repeating a failed job,
record which pages actually emerged and inspect the queue. Never blindly resubmit
a partly printed job. Stop for unexpected repeated output, crashes, loss of
unrelated printers or a queue that cannot recover. Preserve the failure result.

| ID | Procedure | Pass evidence |
| --- | --- | --- |
| R01 | Cancel a queued job before transmission, then a separate job during output | Queue clears; in-flight pages accounted for; next controlled job succeeds |
| R02 | Disconnect/reconnect USB while idle, then during a separate bounded job | State is understandable; no silent duplicate submission; subsequent job succeeds |
| R03 | Sleep/wake the PC with printer idle, then with a separate queued job | Firmware/device handling recovers; page accounting and completion are correct |
| R04 | Start a one-page job with no paper, then load paper | Existing job resumes or reports a clear failure; no duplicate page |
| R05 | Open/close the accessible printer door during a controlled job, following printer safety instructions | Recoverable behavior with no duplicate/lost page claim unsupported by observation |
| R06 | Five cold power cycles, one numbered page after each | Firmware reload and queue completion on every cycle; exactly five pages |

Use bounded tracing from [diagnostics](DIAGNOSTICS.md) when needed. Do not run a
second USB status reader during a monitored job. Do not manipulate internal
mechanisms or touch hot parts to provoke an error. If a particular status is not
shown to users, record that limitation and decide whether it blocks the beta.

## Recorded results

### November 11–14, 2026 physical results

These results apply only to Benteveo 0.1.1, source commit
`994a4fceb346b88cdc5102137fb446f3559bba26`, tester ZIP SHA-256
`60ef32f6e5bdd3b41f5e060fdbd18098cc46f634e43819a1abce2c91bb59fbad`,
on Windows build 26300.9974. The retained signer is
`CN=OpenZJS Technical Tester Signing`, thumbprint
`DEAD78F5D4F2A4D8A424726555A6C3D536E445FC`. VM acceptance of this ZIP remains
pending; these results do not establish support for other Windows builds.
See the [dated evidence](TEST_STATUS.md) for user observations and trace results.

| Checks | Result |
| --- | --- |
| P01 | Passed Have Disk installation, installed payload hashes, USB004, A4 and preserved unrelated printers/default. User reinstalled before the November 14 recovery tests; hashes were checked again. |
| P02 | Passed cold and consecutive Windows test pages. |
| P03 | Passed Inkscape 1.4.4 portrait/landscape; landscape ruler measured, portrait ruler not measured. |
| P04, P06, P09 | Passed SumatraPDF 3.6.1 x86 geometry (100 mm measured), marked/blank/marked and complete ejection. |
| P05 | Passed two copies in order 1, 2, 1, 2 and a repeat without power-cycling. Uncollated output not tested. |
| P07 | Passed orientation changes between jobs and A4 portrait/landscape/portrait in one diagnostic ResetDC job. |
| P08 | Not tested: physical Letter deferred. |
| P10 | Old package removal recorded; removal of the current Benteveo package not tested on this host, which is left installed. |
| R01 | Queued cancellation and follow-up passed. Active cancellation cleared the queue; all four buffered sheets emerged, and the follow-up passed. Exact physical activity at cancellation was not confirmed; prompt cessation of output is not established. |
| R02 | Idle same-socket reconnect passed. During-output interruption on 0.1.1 produced four correct pages but failed completion recovery: USB status reader error 2, lingering job canceled by the helper at 90 seconds. A fresh page then passed without a power cycle. See the separate 0.1.2 validation below. |
| R03 | Not tested: firmware exposes neither S1/S2/S3 nor S0 standby. Hibernate is not equivalent coverage. |
| R04 | Passed paper-out, resume of original job on loading paper, cleared warning and separate follow-up. |
| R05 | Passed cover-open before submission, waiting job, close/resume, cleared warning and follow-up. Opening the cover mid-feed was not tested. |
| R06 | Passed five power cycles with one Windows test page each, jobs 29–33; FirmwareReady, completion acknowledgement and correct physical output each time. Pages were identified by job IDs, not printed cycle numbers. |

### November 16, 2026: 0.1.2 USB retry validation

Tester ZIP SHA-256
`2e28b4adf29df39800a52f2f79a43152b9c63b9f3d30e43dcde86a54ce0063d4`,
on build 26300.9974, passed the timed same-socket USB interruption during output.
The maintainer used a stopwatch for the requested ten seconds and confirmed
four fully ejected sheets in order 1, 2, 1, 2. Job 13 recorded disconnect,
reconnect (11.23 seconds apart) and completion acknowledgement, with no
cancellation. Follow-up job 14 printed one correct page without power-cycling,
received acknowledgement and cleared the queue. EPSON remained default.

This closes the within-window completion-recovery defect for this exact package
and scenario. The earlier uncertain-duration timeout remains recorded;
interruptions beyond 30 seconds and missing END reports remain unconfirmed.
Other 0.1.1 matrix results are not automatically transferred to 0.1.2, and VM
acceptance remains pending. [Full evidence](TEST_STATUS.md)

## Evidence record template

Copy this table into a private run record and repeat one row per test. Use
`passed`, `failed`, `not tested` or `blocked`; do not leave ambiguous checkmarks.

| Field | Value |
| --- | --- |
| Test ID / date / operator | |
| OS edition, full build, architecture | |
| Candidate version, commit and archive SHA-256 | |
| Signing identity and verification result | |
| Application version and x86/x64 | |
| Paper / orientation / copies / expected pages | |
| Starting queue and printer state | |
| Actions, prompts and exact error code | |
| Observed pages, order and visual quality | |
| Final queue state / time to completion | |
| Result and evidence filenames | |

Review support reports before sharing. Keep recovery bundles, firmware, spool
files, private documents and registry exports out of issues and public releases.
