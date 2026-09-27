# HP LaserJet 1005 Series — Windows x64

C++20 user-mode driver project for the **original HP LaserJet 1005 Series**, USB ID `03f0:1317`, ZJS/ZjStream. This is not the P1005 or M1005 MFP.

## Current status

**2026-09-13 repair:** paper-size changes within a job could insert duplicate ZJS document headers and were implicated in a report of repeated blank sheets. The installed repair preserves the document across `ResetDC`; a Letter → A4 landscape → Letter capture now passes decoding and Linux reference-byte comparison. The user confirmed successful physical printing of the A4 landscape bird image through Windows, and the queue cleared automatically. Printing directly from Inkscape after restarting it remains to be confirmed. Restart applications that had loaded the old driver before trying the update. See [test status](docs/TEST_STATUS.md) for evidence and limits.

Working C++20 development driver, installed and physically tested on Windows x64 build 26200 with the original LaserJet 1005 over USB. On 2026-09-10, the user confirmed correct A4 portrait and landscape output, solid graphics and text, and a three-page job with an intentionally blank middle page.

A clean power-on test verified automatic firmware loading, a matching printer job-end report, and automatic Windows queue removal. Separate landscape and three-page jobs also received matching END reports with page counts 1 and 3, and cleared automatically. The driver waits for a unique setup echo before sending page data; this synchronization was essential to the successful completion tests.

Use the installed **HP LaserJet 1005 Open ZJS (development)** queue from an application's Print dialog. The current connection is USB003. The monitor reloads the privately installed original firmware when needed. C++ was selected for direct Unidrv/COM integration and reuse of the proven C JBIG codec; no custom USB kernel driver is needed.

This is an unsigned development release. USB hardware is verified; parallel support is implemented but has not been exercised on parallel hardware. Letter is checked through file capture, not physical Letter paper. Paper/toner status presentation and signed production deployment are outside this release. Earlier failed tests and their corrections are documented in `docs/TEST_STATUS.md`.

To reinstall from an administrator PowerShell on the development machine, with the original printer connected:

```powershell
./scripts/install-dev.ps1 -ConnectUsb -WithFirmwareMonitor -FirmwarePath C:/path/to/sihp1005.img
```

The installer verifies the original firmware hash, deploys it to the administrator-managed System32 location, enables bidirectional communication, and selects the current USB port. The bundle does not include HP firmware or Windows components. Include `-WithFirmwareMonitor` when reinstalling to retain automatic firmware loading and completion handling. A plain installer invocation selects FILE: for offline development.

The administrator-only `scripts/update-monitor-dev.ps1` updates an already registered monitor. It checks for unrelated jobs and dependent services, keeps a backup DLL, and restarts the spooler after replacement. `monitor_fixture.dll` is test-only and is never installed as a monitor. See `docs/DIAGNOSTICS.md` for optional tracing.

## Build

Project-owned C++ follows [the coding style guide](docs/CODING_STYLE.md), derived from the maintainer's examples. Run `./scripts/format.ps1 -Check` to check layout with clang-format 22, or omit `-Check` to apply it. Naming conventions and external API exceptions are documented in the guide.

Requires Visual Studio Desktop development with C++, Windows SDK, CMake and PowerShell 7. No Rust runtime, Linux environment or Ghostscript is needed to build or run the native components.

```powershell
./scripts/build.ps1
```

Outputs are in `build/Release`, including `lj1005.exe`, `lj1005render.dll`, `lj1005monitor.dll`, the GPD/INF/INI files and `queue_test.exe`. The build runs all seven automated test suites. Building and running these tests requires neither HP firmware nor a connected printer, and does not install a driver or submit print jobs. Installation, file-capture integration tests and physical printing are separate steps.

The optional Linux-reference checkout is excluded from Git; to enable its comparison executables:

```powershell
git clone https://github.com/OpenPrinting/foo2zjs.git third_party/foo2zjs
git -C third_party/foo2zjs checkout 80499ed5bf6caa2963ad337e37cfda78a80aab1e
./scripts/build.ps1
```

## Development bundle

`./scripts/package-dev.ps1` creates `artifacts/HPLJ1005x64-0.1.0-development.zip` with source, x64 binaries, installer scripts, tests and a SHA-256 file manifest. The bundle excludes original HP firmware, Windows components and downloaded reference sources. See `docs/TEST_STATUS.md` for passed checks and unresolved hardware tests.

The bundle has no signed catalog and uses the development installer described below. New builds statically link the MSVC runtime; earlier development builds required the Visual C++ redistributable. The installed development driver has not been replaced by this packaging work.

## Public release preparation

The [release plan](docs/RELEASE_PLAN.md) tracks the installer, signing, firmware import and clean-machine acceptance work. `./scripts/package-candidate.ps1` builds matching source and production-binary archives from a fresh source snapshot, runs the automated tests and WDK checks, and generates an unsigned catalog and SHA-256 manifests. It requires the WDK in addition to the build tools. Its default catalog target is Windows 11 24H2 x64 (`10_GE_X64`).

The candidate now contains a native **Setup.exe** preview. Choose **Install or repair** to open a file dialog for **sihp1005.img** from your original HP driver package. Setup validates the image before requesting elevation, installs the package, selects the USB port and configures A4. Same-build repair restores the owned queue, cache and firmware and asks Windows to reinstall driver files. Setup 0.1.1 also adds **Upgrade** and **Roll back** with a saved previous package and resumable recovery. See [Setup instructions and limitations](docs/DEPLOYMENT.md) and [upgrade/rollback](docs/UPGRADE_ROLLBACK.md). Existing development installations are not migrated by this preview.

The INF declares Unidrv as a Windows core-driver dependency. Installation, extended same-build repair, different-build upgrade/rollback, removal and offline GDI captures passed on a fresh Windows 11 Pro 25H2 x64 VM with Secure Boot enabled and a certificate trusted only inside that VM. Interactive cancellation, firmware/error/elevation dialogs and removal also passed VM checks; the new upgrade/recovery dialogs still need interactive testing. Verified captures matched the Linux reference encoder. See [VM testing](docs/VM_TESTING.md) and [test evidence](docs/TEST_STATUS.md). Public signing and physical testing of this package remain pending. The unsigned download cannot install on an ordinary clean PC yet. Neither HP firmware nor Windows components are bundled.

## Verified USB diagnostic path

Connect and power on the printer, with A4 paper loaded. Supply `sihp1005.img` from your original driver package. Firmware is loaded into the printer's volatile memory and must be reloaded after a power cycle.

```powershell
./build/Release/lj1005.exe usb-list
./build/Release/lj1005.exe usb-firmware C:/path/to/sihp1005.img
New-Item -ItemType Directory -Force artifacts | Out-Null
./build/Release/lj1005.exe testpage artifacts/testpage.pbm
./build/Release/lj1005.exe encode artifacts/testpage.pbm artifacts/testpage.zjs a4
./build/Release/lj1005.exe usb-print artifacts/testpage.zjs
```

`usb-firmware` and `usb-print` physically transmit data. They require exactly one connected original 1005, identified through the Windows USB printer interface and its hardware ID. They do not replace `usbprint.sys`. Do not submit concurrent jobs through another queue during direct USB testing.

`usb-firmware` now checks the IEEE 1284 identity and skips upload when firmware is already running. After an upload it polls for `FWVER` before reporting success. `usb-print` refuses to send a job when firmware is missing. These checks do not prove the paper result; physical test confirmation remains necessary.

The encoder accepts binary PBM/P4, 1=black, MSB first, at 600 dpi. It accepts concatenated pages, with no extra padding between PBM images. The PBM separator is exactly one whitespace byte; use LF before the binary raster. `encode` supports `a4` and `letter` media codes; input dimensions are pixels, not rescaled automatically. `inspect` lists ZJS records for diagnosis.

Use `lj1005 validate input.zjs` for offline print validation. `usb-print` performs the same checks before accessing the printer: complete document/page boundaries, supported settings, paper geometry, consecutive page numbers, JBIG decoding and exact padding. Limits are 128 MiB of input, 128 MiB of decoded rasters and 1000 sheets including copies. Unsupported ZJS profiles are rejected. See [print validation](docs/PRINT_VALIDATION.md) for scope and compatibility.

## Windows driver architecture

Applications → Windows GDI/Unidrv rasterization and halftoning → `lj1005render.dll` → JBIG + ZJS → Windows print spooler → `lj1005monitor.dll` (firmware, synchronization and completion) → inbox USB or local parallel port.

The rendering DLL implements `IPrintOemUni`. A GPD enables full raster delivery; the plug-in assembles a page and uses the same tested encoder as the USB utility. Margins are embedded in the physical-sheet raster; no ZJS offset commands are emitted by the plug-in. Landscape uses Unidrv's `LANDSCAPE_CC270` rotation into portrait device coordinates. A4 and Letter, 600 dpi, monochrome and simplex are the initial feature set. C++ provides direct Windows printer COM/GDI integration and reuses the existing C JBIG codec. No custom kernel driver is necessary for the verified USB path.

The development installer registers a local Type 3 driver through `AddPrinterDriverEx` and creates a `FILE:` queue. It needs administrator privileges. Use it only while developing this project:

```powershell
./scripts/install-dev.ps1
./build/Release/queue_test.exe 'HP LaserJet 1005 Open ZJS (development)' 'C:/path/to/capture.zjs'
```

`install-dev.ps1 -ConnectUsb` selects the registered port belonging to USB ID `03f0:1317`; the default installer selects `FILE:` even for an existing development queue. `queue_test` supports `portrait`, `landscape`, `blank`, `multi`, `letter`, `bold`, `bold-landscape` and `bold-multi` as its optional final argument. An output path captures locally; `-` instead sends to the queue's configured port.

For offline integration checks, run `./scripts/capture-tests.ps1 -Python path/to/python.exe` (requires Pillow and the optional reference build). It captures each scenario as `artifacts/full-SCENARIO.zjs`, decodes to matching `.pbm` files, and runs `tests/verify_captures.py`. This checks geometry, blank-page preservation, page-specific markers and exact Linux-reference bytes. All jobs use explicit local output files.

This is not the production deployment path. The INF names a future signed catalog; the repository does not currently contain one. Windows Protected Print Mode excludes third-party legacy drivers. No script disables it or changes driver-signing enforcement. Microsoft's third-party driver servicing plan changes Windows Update distribution; existing Type 3/4 driver installation remains a separate matter.

## Evidence and sources

Repository contents are source, tests, scripts, documentation and the attributed JBIG codec. `driver/` holds the renderer, monitor and printer description; `src/` and `include/` hold the protocol library and utilities; `tests/` holds automated checks. Generated binaries, packages, firmware, spool files and local diagnostic evidence are excluded by `.gitignore`. Paths under `artifacts/` in the test history refer to local evidence, not files available in a fresh clone.

- `tests/protocol_tests.cpp`: malformed/truncated PBM rejection, preservation of whitespace-valued pixel bytes, JBIG encode/decode equality, multi-chunk splitting and firmware checksums.
- Diagnostic ZJS SHA-256: `A25CB60244166F8CB1BD4F67BFF80E05B648ABBFD0B4163D4ED31E93ABE76237`.
- Wrapped original firmware SHA-256: `B05161586ACA021289755730CF73751B642A643CFD7FDC80AB4BF23CB49B42D2`.
- [OpenPrinting foo2zjs](https://github.com/OpenPrinting/foo2zjs), including the original 1005 PPD and firmware loader.
- [Microsoft Unidrv FilterGraphics](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/prcomoem/nf-prcomoem-iprintoemuni-filtergraphics) and [raster data emission attributes](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/raster-data-emission-attributes).
- [Windows third-party printer driver servicing plan](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/end-of-servicing-plan-for-third-party-printer-drivers-on-windows) and [Protected Print Mode](https://learn.microsoft.com/en-us/windows/modern-print/windows-protected-print-mode/windows-protected-print-mode).

GPL-2.0-or-later. See `LICENSE` and `THIRD_PARTY.md`. Original HP firmware and Windows binaries are not distributed.
