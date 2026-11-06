# HP LaserJet 1005 Series driver for Windows x64

A Windows printer driver for the **original HP LaserJet 1005 Series** (USB ID `03f0:1317`). The initial target is Windows 11 Pro 25H2 x64 with one USB printer and A4 paper. The P1005 and M1005 use different drivers.

## Install with Have Disk

The end-user package is a ZIP for the Windows Add Printer Wizard. Extract it, place the original `sihp1005.img` from your HP software beside `lj1005.inf`, and use **Add manually → Local printer → Have Disk**. Select the USB port assigned to the connected printer. Follow the guide for firmware verification, tester certificate trust, paper settings and removal. [Installation guide](docs/MINI_INSTALL.md)

**Release status:** the available local builds use a dedicated self-signed tester certificate. Public distribution awaits trusted signing and release acceptance. CI produces unsigned engineering archives. The wizard route has passed VM testing. A physical four-sheet job on the earlier packaged driver repeatedly stopped with sheet two partly ejected. Physical copies/collation, recovery and Letter remain open. [Test evidence](docs/TEST_STATUS.md) · [Release plan](docs/RELEASE_PLAN.md)

## Build and test

Install Visual Studio Desktop development with C++, the Windows SDK and PowerShell 7. From the repository root run:

```powershell
./scripts/build.ps1
```

The Release build runs seven driver tests. It produces the two driver DLLs, printer descriptions, a development CLI and the queue capture helper in `build/Release`. For the source and driver archives, use the WDK candidate build and then the dedicated tester signing step. [CI and local build](docs/CI.md) · [Tester package build](docs/MINI_INSTALL.md#build-locally)

The optional Linux-reference checkout is excluded from Git; to enable its comparison executables:

```powershell
git clone https://github.com/OpenPrinting/foo2zjs.git third_party/foo2zjs
git -C third_party/foo2zjs checkout 80499ed5bf6caa2963ad337e37cfda78a80aab1e
./scripts/build.ps1
```

For offline integration checks, install a FILE: queue in the lab and run `./scripts/capture-tests.ps1 -QueueName '<lab queue>'` with Pillow and the reference encoder available. The helper writes explicit local capture files. [VM rehearsal](docs/VM_TESTING.md) · [Print validation](docs/PRINT_VALIDATION.md) · [Diagnostics](docs/DIAGNOSTICS.md)

## Driver architecture

Applications → Windows GDI/Unidrv rasterization and halftoning → `lj1005render.dll` → JBIG + ZJS → Windows print spooler → `lj1005monitor.dll` (firmware, synchronization and completion) → inbox USB or local parallel port.

The rendering DLL implements `IPrintOemUni`. A GPD enables full raster delivery; the plug-in assembles a page and uses the same tested encoder as the USB utility. Margins are embedded in the physical-sheet raster; no ZJS offset commands are emitted by the plug-in. Landscape uses Unidrv's `LANDSCAPE_CC270` rotation into portrait device coordinates. A4 and Letter, 600 dpi, monochrome and simplex are the initial feature set. C++ provides direct Windows printer COM/GDI integration and reuses the existing C JBIG codec. No custom kernel driver is necessary for the verified USB path.

## License

Project code is GPL-2.0-or-later. The ZIP includes the project license and third-party notices. Users supply HP firmware from their original software. [License](LICENSE) · [Third-party notices](THIRD_PARTY.md) · [Contributor style guide](docs/CODING_STYLE.md)
