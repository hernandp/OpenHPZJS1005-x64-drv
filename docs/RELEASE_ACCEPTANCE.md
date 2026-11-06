# Physical acceptance for the Have Disk package

Target Windows 11 Pro 25H2 x64 (build family 26200), one original LaserJet 1005 on USB, A4 monochrome simplex at 600 dpi. Record the full build, revision, package SHA-256 and signer for each test. Build 26300 has offline evidence only. Other editions, ARM64, parallel ports, shared printers and multiple devices are outside the initial target. Office is deferred.

Use a clean physical target with an independently verified private recovery backup. Migration from earlier Setup or development installations requires a separate procedure. Earlier USB and application results guide these tests. The rebuilt wizard package still needs its own physical acceptance, especially the repeatable four-sheet stop. [Earlier results](TEST_STATUS.md)

## Physical printing matrix

All rows below are **pending**. Earlier development-driver output is supporting
history, not acceptance of the packaged driver. Submit one controlled job at a
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
