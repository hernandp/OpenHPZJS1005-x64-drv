# Remaining release acceptance

Updated 2026-10-20. Physical installation stays deferred while the signing route
is undecided. Preparing files or procedures is not a pass for hardware testing.
Use [test status](TEST_STATUS.md) for historical results and exact candidate IDs.

## Windows scope

The initial beta target is **Windows 11 Pro 25H2 x64, build family 26200**, one
original HP LaserJet 1005 over USB, A4 monochrome simplex at 600 dpi. Record the
complete build and revision for every run. An OS build family is not a claim
that every cumulative update was tested. Lab certificate results do not establish
production trust. Home, Enterprise and Education are unvalidated editions.

Setup currently allows Windows 11 24H2 or later. This technical minimum is wider
than the accepted release scope. Do not advertise 24H2 until its separate matrix
passes. Do not infer support for later releases from the installer's version check.
ARM64, Windows Server, parallel connections, sharing and multiple printers are
outside the initial beta scope. Letter remains experimental until physical testing.

| Environment | Install/repair/remove | Upgrade/rollback | Application output | Release status |
| --- | --- | --- | --- | --- |
| Windows 11 Pro 25H2 x64, 26200.6584, isolated VM | Earlier lab candidates passed | Lab upgrade/recovery/rollback passed; latest retry acceptance on rc-54d87b83 | Inkscape x64 SVG/imported-PDF, SumatraPDF x86 single/multiple-page PDF and five x86 GDI-helper captures passed on rc-54d87b83; Edge PDF content/order passed, precise scale remains a known limitation; Office deferred | Final signed-candidate regression pending |
| Windows 11 Pro 25H2 x64, physical printer PC | Pending migration and production trust | Pending physical acceptance | All packaged-driver physical rows below pending | Deferred by maintainer |
| Windows 11 Pro 24H2 x64, 26100, clean VM | Pending | Pending | Pending, including x86 application path | Media unavailable locally; no support claim |

For each intended supported build, run fresh install, same-build repair,
different-build upgrade, explicit rollback and removal on clean snapshots, with
Secure Boot and normal signature enforcement. Recheck unrelated printers and
pending-deletion recovery. Record OS edition/build/revision, candidate commit,
archive hash, signature identity and application version/architecture. The final
candidate needs production-trust acceptance even where lab tests already passed.

### Application coverage

Record separate results for 64-bit and 32-bit Windows applications running on
x64 Windows. An x64 capture helper does not demonstrate the x86 application path.
Use an actual x86 application and record its bitness. Use the real Inkscape Print
dialog and a PDF viewer with harmless test documents. Preserve layout references
without attaching personal documents. Microsoft Office is **not tested** and,
by maintainer decision on October 17, is deferred to a future release rather than
required for the initial beta. Do not claim Office compatibility from other apps.

On October 15 an I386 build of the existing GDI helper passed portrait,
landscape, blank, three-page and Letter captures in the 25H2 VM against
rc-54d87b83. Protocol validation, decoded geometry and reference-byte comparison
passed; queue metadata/permissions and the PDF printer were preserved. This
demonstrates the x86 GDI path only. It does not mark PDF, Office, physical Letter
or physical printing as passed.

Real Inkscape 1.4.4 Print-dialog captures subsequently passed A4 portrait and
landscape on the same packaged-driver VM. Protocol validation, decoded page
count, orientation, full-border visibility and measured geometry passed. Edge
140.0.3485.66 subsequently passed single/multiple-page PDF content, page order,
protocol validation and queue completion. Its Actual size and explicit 100%
captures are identical, but border deviation up to 0.499 mm fails the strict
geometry check. Bypassing Edge's preview through the system dialog still gives
0.414 mm deviation and the same ruler scale difference. On October 17, the same
three-page PDF printed through Inkscape's Internal import path passed the same
tolerances (0.033 mm maximum border deviation) on the unchanged packaged driver.
Edge also enlarged the 100 mm vector ruler to 100.190 mm through the guest-local
Microsoft Print to PDF reference printer. The difference therefore reproduces
without the project driver; do not compensate in driver code. Device metrics
were recorded, but the exact Edge/Windows cause remains unproven. Keep Edge
precise-scale acceptance open as a known application-path limitation. Edge reset
scaling to Fit to printable area when reopening its dialog; recheck each job.

SumatraPDF 3.6.1 portable x86 subsequently passed all three PDF pages and a
separate page-2 selection through the packaged driver, with A4 portrait, one
copy and its default shrink setting. Maximum border deviation was 0.033 mm,
and the selected page's raster exactly matched the corresponding multi-page
raster. Protocol validation, geometry, visual content, page order and empty
queue checks passed. This completes representative actual x86 PDF-viewer
coverage for this VM/candidate. Inkscape and Edge are x64.

The October 19–20 remaining VM matrix passed these bounded checks:

- Edge's actual Copies 2 dialog and Sumatra's CLI `2x` request produced four
  sheets in collated order `1,2,1,2`, with identical repeated rasters.
- Sumatra's explicit page ranges preserved both `1,2,1,2` and `1,1,2,2`.
  Neither tested dialog exposed an uncollated toggle; that selectable mode is
  not accepted merely from explicit-range sequencing.
- Sumatra's actual dialog preserved a truly blank middle sheet between two
  marked pages. The middle raster contains zero black pixels.
- Separate Sumatra CLI A4/Letter/A4 portrait jobs passed geometry and restored
  an identical A4 raster. Actual dialog landscape/portrait changes passed
  geometry; a CLI content-rotation attempt was excluded as incorrect settings.
- The x86 GDI helper's in-job Letter/A4-landscape/Letter transition matched the
  reference encoder byte for byte. This is helper coverage, not a mixed-paper
  PDF application's automatic paper-selection claim.

Sumatra's Windows dialog hid Copies for this queue; its successful CLI copy
request does not establish device-side copying. Edge precise scale remains open.
Both local queues ended empty and Normal, with saved defaults and Setup hash
preserved. All physical output remains pending.
See [test status](TEST_STATUS.md) for exact settings, evidence and limitations.
These file captures do not complete any physical acceptance row below.

## Physical migration gate

1. Choose a trusted candidate and record its exact identity. Host lab trust is
   not authorized; no installation until the maintainer chooses a signing route.
2. Run `scripts/backup-development-printer.ps1` as described in
   [development migration](DEVELOPMENT_MIGRATION.md). Verify every backup hash.
3. Copy the private recovery bundle to an independent private location. Confirm
   it can be read there. Rehearse recovery with this fresh bundle in the isolated
   VM before retiring the host installation.
4. Recheck jobs, dependencies, hashes and saved settings immediately before the
   change. Stage the trusted candidate before removing the working installation.
5. Follow the reviewed replacement/recovery procedure. Do not delete jobs,
   restart the shared spooler, or infer file ownership from a printer name.

## Packaged-driver physical matrix

All rows below are **pending**. Earlier development-driver output is supporting
history, not acceptance of the packaged driver. Submit one controlled job at a
time, record the starting queue/job count, and wait for both physical output and
queue completion before moving on.

| ID | Procedure | Pass evidence |
| --- | --- | --- |
| P01 | Fresh normal Setup install with printer connected | Correct USB port/device, one owned queue, A4 default, no unrelated changes |
| P02 | Power off/on, then submit a one-page Windows test page | Firmware loads after cold start; one correct page; job completes |
| P03 | Inkscape Print dialog, A4 portrait and landscape | Correct scale, orientation, margins and clipping; one page each |
| P04 | PDF viewer, one selected page and a three-page document | Fonts/graphics, scale and page order correct; three-page job completes once |
| P05 | Two copies of a numbered two-page document | Exactly four pages in the application's requested order |
| P06 | Blank page between numbered pages | Blank-page handling and page count match requested output |
| P07 | Change A4 portrait/landscape between jobs; exercise in-job ResetDC separately | No stale paper/orientation, corruption or lost pages |
| P08 | Load physical Letter and select Letter, then restore A4 | Correct margins and page feed on actual paper; correct A4 restoration |
| P09 | Repeat representative PDF output through an x86 application | Application version/bitness recorded; correct output and completion |
| P10 | Owned repair, upgrade/rollback and removal on physical target | Correct payload/settings after each operation; unrelated resources preserved |

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
