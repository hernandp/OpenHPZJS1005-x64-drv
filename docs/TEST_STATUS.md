# Development verification — updated 2026-09-16

## Coding style and edit verification (2026-09-16)

The maintainer's formatting examples established the conventions in `CODING_STYLE.md` and `.clang-format`. Project-owned C++ functions and data names were updated consistently with their declarations and callers, and 22 source/header files were formatted. Windows/COM/standard-library/JBIG names and vendored source bytes were preserved.

The initial edited `firmware.cpp` failed the build: assignments following a throw were unreachable, and the device-ID parsing function could exit without returning a value. Misplaced braces also moved model validation and the return inside the field loop. These were corrected while keeping the requested style. File-local helper namespaces were restored in the firmware and USB implementations. Additional firmware tests cover field ordering, ignored fields, a missing final separator, empty firmware versions, and duplicate manufacturer/model fields.

The repaired and restyled build passes all six CTest suites. Formatting checks pass with clang-format 22.1.3, and a token comparison confirmed that formatting itself did not alter C++ tokens. Offline CLI comparisons against the earlier clean-build executable produced identical single/multi-page A4/Letter ZJS bytes, inspection output, synthetic firmware envelopes, and malformed-PBM errors; decoding preserved the original PBM bytes. No physical printing or driver installation was performed for this change. The installed driver's hardware evidence below refers to the earlier builds, not the newly restyled binaries.

## Paper-change regression and repair (2026-09-13)

The user reported repeated blank sheets after changing Letter to A4 in Inkscape; subsequent Windows test pages also fed blanks. The printer was switched off, the development queue paused, and the failed Windows test entries removed after preserving a spool-file sample. The original failure was not captured at the USB boundary, so the following is a reproduced driver defect, not proof of every step in that incident.

Offline reproduction using an EMF export of the user's SVG and `queue_test ... emf-reset input.emf` switches Letter portrait → A4 landscape → Letter portrait within one job. The old installed driver produced a 742448-byte stream with `JZJZ` headers at offsets 0, 245508 and 496924. The decoder rejected the second header as an invalid ZJS chunk. Each paper change reset the document header and page number instead of continuing the job.

Unidrv repeats DOC_SETUP following ResetDC, as described in Microsoft's [command execution order documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/command-execution-order). CmdStartDoc now belongs to JOB_SETUP and CmdEndDoc to JOB_FINISH. The renderer also preserves an already-active document if the start callback repeats. ResetPDEV continues carrying document state while retaining the new paper geometry.

The renderer regression test failed before the repair and passes afterward. It exercises Letter/A4-landscape/Letter PDEV transitions, repeated setup, page numbering, and a subsequent independent job. All six CTest suites pass. The patched renderer and GPD are installed and their hashes match the build. Repeating the same Windows file capture now decodes as one document with three correctly sized pages; each page matches the Linux reference encoder byte for byte, including framing and sequence after adjustment for its position in the job. `tests/verify_reset_capture.py` reproduces this check. Separate image-heavy A4 and Windows test-page captures also matched the reference before the repair; those tests alone did not exercise ResetDC.

**Physical A4 landscape retesting passed.** The queue was emptied before resuming for single-sheet tests. Job 27 loaded firmware and received its setup echo and START; no paper was available, and a USB disconnect ended completion monitoring. The printer subsequently identified without firmware, confirming a new startup state. At the user's request, job 28 retried the single A4 landscape bird page. Its trace shows firmware upload, setup echo and START. Initially the user reported yellow blinking with green off; after paper/door checks were requested, the user confirmed that the birds printed successfully. A subsequent read-only Windows query found the queue Normal on USB003 with no remaining jobs, without manual cancellation of job 28. The trace was stopped before recovery, so this run did not capture its final END event. Local evidence: `artifacts/monitor-reset-fix-20260913.xml`, `artifacts/monitor-reset-retry-20260913.xml`, and `artifacts/reset-fix-final-queue.json`.

This physical test used an EMF export of the user's SVG through `queue_test emf-landscape`, not Inkscape's own Print dialog. Paper changes within a job were verified offline; a direct Inkscape print after restarting the application is still unconfirmed. The five earlier portrait, landscape, blank, multi-page and Letter captures were repeated with the installed repair and remained byte-identical to their verified reference captures. WDK GPDCheck accepted the updated command ordering.

The September 10 hardware results below describe the earlier version and remain limited to the scenarios actually tested. Restart applications that had loaded the old driver before testing the installed repair.

Target: original HP LaserJet 1005 Series, USB VID 03F0 / PID 1317, on Windows x64 build 26200. Implementation: C++20 Unidrv renderer and user-mode language monitor, with C JBIG-KIT. Firmware: the user's original image, version 20021030. Installed queue: HP LaserJet 1005 Open ZJS (development), USB003.

## Verified hardware behavior

| Test | Physical result | Windows/device evidence |
| --- | --- | --- |
| Native A4 diagnostic | User confirmed border, bars and checkerboard | Direct USB ZJS transfer |
| Bold A4 portrait over USB | User confirmed text, all 16 solid bars and complete border | 2072-byte file, SHA-256 `1f67bdb45a3adc6286e42715b2244eb05d0063197f0173892cc6dc438dccc0bd` |
| Windows portrait after power cycle | User confirmed correct bold page | Firmware absent before job 3; loaded automatically. Matching setup echo and END with PAGES=1; job removed automatically |
| Windows landscape | User confirmed correct orientation, text, bars and border | Job 6: matching END with PAGES=1; removed automatically |
| Windows three-page job | User confirmed Page 1, blank middle sheet, Page 3; no missing, clipped, duplicated or corrupted content | Job 7: matching END with PAGES=3; removed automatically |

The final tests used the same installed monitor DLL, SHA-256 `b501225d13bee88dc5448ce8982515eb2af91744521021a74d1bbae3a877677d`. Its hash was checked against the built binary. The new toner cartridge was installed for the final tests, and the user confirmed its sealing strip had been removed.

The cold-start trace is `artifacts/monitor-cold-echo-20260910.xml` (original ETL alongside it). Token `lj1005-3-47273140-2` received its setup echo, START, and END/PAGES=1, followed by LowerEndDoc success and CompletionAcknowledged. Firmware loaded at 23:43:30; the completion report arrived at 23:43:48 local time.

The layout trace is `artifacts/monitor-layouts-20260910.xml`. Tokens `lj1005-6-47488640-3` and `lj1005-7-47498531-4` received matching END/PAGES=1 and END/PAGES=3 respectively. Both have CompletionAcknowledged events. A separate Windows queue query confirmed no remaining entries. Diagnostic sessions were stopped and decoded.

## Software and packaging checks

- Six CTest suites pass: protocol, firmware state, PJL parsing, simulated monitor callbacks, production monitor load/initialization, and renderer COM initialization.
- Parser tests cover fragmented and stale setup echoes, incomplete/malformed/mismatched END reports, and cancellation. Setup readiness cannot acknowledge physical completion.
- Simulated monitor tests reject raster writes before the setup echo. Cancellation or read failure before synchronization sends no raster. Failure after page submission does not request automatic replay or falsely acknowledge success.
- A4 portrait, landscape, blank, three-page and Letter file captures pass geometry, margin and reference-byte checks. Bold landscape and bold three-page captures also match the OpenPrinting reference encoder and preserve the blank middle page.
- The optional reference encoder is OpenPrinting foo2zjs commit `80499ed5bf6caa2963ad337e37cfda78a80aab1e`. Protocol and wrapped-firmware bytes were compared against it.
- WDK GPDCheck accepted the GPD. InfVerif accepted the INF with legacy service/data-section warnings. Inf2Cat `/os:10_GE_X64 /nocat` reported no signability errors or warnings. These are metadata checks, not a signed catalog or hardware certification.
- The development ZIP allowlists source, native binaries, tests, scripts and licenses. SHA-256 manifest verification checks each archive entry. HP firmware, Windows binaries, downloaded WDK tools, reverse-analysis libraries and local experimental artifacts are excluded.

## Corrections and failed tests retained for context

An earlier cropped-raster GDI job caused repeated blank sheets. Padding the printable image onto the full physical sheet, without ZJS offsets, produced a successful direct USB page on 2026-09-08. The earlier 2080-byte portrait file has SHA-256 `b7067bde7bbcf1debf15a4e5de73103c40857d6ff277baeaf5f4633fec6de72b`. This confirms the workaround, without isolating dimensions from offsets as the cause.

An initial queue test was blocked by WORK_OFFLINE; clearing that attribute allowed it to print. The installer now clears it when connecting USB and matches the live USB instance rather than stale port registrations.

Earlier cold-start tests loaded firmware but failed to establish successful page output or queue cleanup. One produced a blank page. Later direct output showed text but almost invisible one-dot rectangle outlines. The bold test scenarios use solid bars and a 12-dot border to make hardware inspection clear.

The first completion monitor sent setup and raster without synchronization. Warm printing succeeded, but no status bytes arrived and the queue remained COMPLETE after its timeout. A unique setup ECHO before raster transmission enabled a verified synchronization point. The final clean-start and layout traces prove job completion and removal with this version.

Experimental empty PJL/ZJS jobs left pending job-name state: a later real page reported the earlier probe name `100501`. The parser correctly rejected this mismatch. The test entry was cancelled, the printer power-cycled, and final tests run without probes. The empty-job CLI command was removed. No monitor firmware modifications were made; analysis used the original firmware's symbols and code only to understand status behavior.

Guarded monitor updates removed only explicitly identified integration-test entries, retained backup DLLs, and verified that no unrelated queued jobs or running dependent services would be interrupted. Test entries were never replayed for maintenance.

## Release limits

This is a working USB development release, not a signed production package. Parallel transport is implemented through the inbox port monitor and simulated callbacks, but no parallel hardware test was available. Letter output was checked offline; physical tests used A4. The feature set is 600 dpi, monochrome, simplex, A4/Letter. Paper/toner status presentation, hardware certification, and signed deployment are not provided. A compatible x64 Visual C++ runtime and the user's original firmware are required.
