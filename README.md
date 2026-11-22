# Project Benteveo 

**HP LaserJet 1005 driver for Windows 10 22H2 and Windows 11 (x64)**

<p align="center">
  <img src="docs/benteveo-and-hp.png" alt="A benteveo bird perched on an HP LaserJet 1005 printer" width="720">
</p>

A Windows printer driver for the **original HP LaserJet 1005 Series** (USB ID `03f0:1317`). 

⚠️ The supported setup is Windows 10 22H2 x64 or Windows 11 x64 with one USB printer and A4 paper.

⚠️ The **Laserjet P1005 / M1005** use different drivers.

## Purpose of this project

I purchased this printer back in the early 2000s, and it served me well until HP decided to phase it out, ending software support in the Windows XP era.

Over the following two decades, I moved between houses, jobs, machines, and operating systems. This good old battleship ended up abandoned in the back of my parents' house, until I decided to rescue it from under a pile of dirt, spiderwebs, and dead insects to pursue some toner transfer printing for my girlfriend.

We wanted to transfer an image of a benteveo bird (the one that brands this project), so that's where the name comes from.

The printer got a new toner cartridge and some mechanical fixes, and it works quite well. But it was lacking a printer driver for Windows 11, so I decided to code one using Codex, relying on specs from the web and the Zenographics ZjStream open protocol implementation for CUPS.

It successfully prints text, bitmaps, and multiple-page documents without a hassle.

**Lesson:** Don't throw away your old hardware! With current AI tools, you can do some vibe software and hardware hacking to *hopefully* suit your needs.

## Current-state-of-development docs
* [Changelog](CHANGELOG.md)
* [Test evidence](docs/TEST_STATUS.md)
* [Release plan](docs/RELEASE_PLAN.md)


## Install with Have Disk

The end-user package is a ZIP for the Windows Add Printer Wizard. Extract it, place the original `sihp1005.img` beside `lj1005.inf`, and use **Add manually → Local printer → Have Disk**. See below for details. 

⏯️ An installation demo video is available here: [Youtube](https://www.youtube.com/watch?v=YPfRTDhf5iE)


### Get the HP firmware

Extract the mini ZIP first. Open [get-hp-firmware.ps1](scripts/get-hp-firmware.ps1) on GitHub and use **Download raw file** to save the helper. Open PowerShell in the folder where you saved it, and replace the example path with the folder containing your extracted `lj1005.inf`.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\get-hp-firmware.ps1 -DriverFolder 'C:\path\to\HPLJ1005-mini'
```

The helper downloads the original LaserJet 1005 package directly from HP, reads it as an archive, and saves the verified `sihp1005.img` beside the INF. It needs no administrator rights. The execution policy option applies only to the PowerShell process running the helper. Continue with certificate setup and Have Disk when the helper reports success.

If you already have the original HP package, use it locally:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\get-hp-firmware.ps1 -DriverFolder 'C:\path\to\HPLJ1005-mini' -HpPackage 'C:\path\to\lj1005hostbased-en.exe'
```

If you already have `sihp1005.img` from your original HP software, copy it beside the INF and check the size and SHA-256 in the [installation guide](docs/MINI_INSTALL.md#prepare-the-extracted-folder).

### Test Certificate installation

This driver is signed by using a test certificate -In the future a proper solution would be provided for open-source projects such as SignPath. To install the Project Benteveo driver, import the certificate by double-clicking `TEST-CERTIFICATE.cer`, and clicking 'Install Certificate'. 
Add the certificate in **Local Machine** context, to the following stores: **Trusted Root Certification Authorities** and **Trusted Publishers**.

ℹ️ Optionally Check if the certificate is properly installed by running the following Powershell script (must return True for both stores):

```powershell
$t = 'DEAD78F5D4F2A4D8A424726555A6C3D536E445FC'
Test-Path "Cert:\LocalMachine\Root\$t"
Test-Path "Cert:\LocalMachine\TrustedPublisher\$t"
```

### Continue with Wizard

To launch the classic Add Printer Wizard in Windows, use one of these quick methods:

Through standard Windows Settings:

* Navigate to **Start > Settings > Bluetooth & devices > Printers & scanners > Add a printer & scanner > Add device** (or select Add a new device manually if the printer isn't listed automatically).

* Command Prompt / Run Command:
  - Press `Windows Key + R` to open the Run dialog box.
  - Type `rundll32 printui.dll,PrintUIEntry /il` and press Enter.
  
* Open the Run dialog box (Windows Key + R) and type `printui /im` to jump straight to the manual setup options.
  
* As Administrator:
  - Open Command Prompt as Administrator, then run `rundll32 printui.dll,PrintUIEntry /il` to install a printer with elevated privileges without logging out the current user.

* Choose local installation:
<img width="500" alt="image" src="https://github.com/user-attachments/assets/0ff1a57e-048a-49af-94a3-29887dd23ffe" />

* Make sure the printer is connected to your desired USB port:
<img width="500" alt="image" src="https://github.com/user-attachments/assets/99d19c1e-21e0-4735-b881-de62963bd23d" />

* Press the Have Disk button and browse to the folder where you extracted the Project Benteveo ZIP:
<img width="500" alt="image" src="https://github.com/user-attachments/assets/fa1c124b-f872-4fb3-a425-728c9019559a" />

* Make sure **HP Laserjet 1005 (Project Benteveo Driver)** appears with an Authenticode digital signature. The screenshot shows the name used by an earlier tester ZIP.
<img width="500" alt="image" src="https://github.com/user-attachments/assets/58344dfb-f9b7-4f54-aac2-8af06387ae6d" />

* Finish the wizard before printing. Confirm that the queue uses the USB port assigned to the connected printer. If you are using A4 paper, set A4 in the queue's **Printing Defaults** and **Printing preferences**, load one A4 sheet, then select **Print Test Page** from **Printer properties**. [Installation guide](docs/MINI_INSTALL.md)


## Build and test

Install Visual Studio Desktop development with C++, the Windows SDK and PowerShell 7. From the repository root run:

```powershell
pwsh -NoProfile -File ./scripts/build.ps1
```

The Release build runs seven driver tests. It produces the driver DLLs, printer descriptions, a development CLI and the queue capture helper in `build/Release`. [CI and local build](docs/CI.md)

The optional Linux-reference checkout is excluded from Git; to enable its comparison executables:

```powershell
git clone https://github.com/OpenPrinting/foo2zjs.git third_party/foo2zjs
git -C third_party/foo2zjs checkout 80499ed5bf6caa2963ad337e37cfda78a80aab1e
pwsh -NoProfile -File ./scripts/build.ps1
```

For offline integration checks, install a FILE: queue in the lab and run `./scripts/capture-tests.ps1 -QueueName '<lab queue>'` with Pillow and the reference encoder available. The helper writes explicit local capture files. [VM rehearsal](docs/VM_TESTING.md) · [Print validation](docs/PRINT_VALIDATION.md) · [Diagnostics](docs/DIAGNOSTICS.md)

## Build the mini ZIP

In PowerShell 7.5 or later, start from a clean checkout with Visual Studio C++, the Windows SDK and WDK 10.0.26100.0, and Python 3 on PATH. You also need the original `sihp1005.img` and the dedicated `OpenZJS Technical Tester Signing` certificate with its private key in your current user's Personal store. If you do not already have that tester identity, create it with `./scripts/new-test-signing-identity.ps1 -OutputDirectory artifacts/test-signing-identity` and read the thumbprint from its `IDENTITY.json`.

From the repository root, run the commands below with your tester certificate thumbprint and firmware path.

```powershell
./scripts/package-candidate.ps1 -OutputPathFile artifacts/candidate-path.txt
$candidate = Get-Content -LiteralPath artifacts/candidate-path.txt
./scripts/package-mini.ps1 -CandidatePath $candidate `
    -CertificateThumbprint '<40-character tester certificate thumbprint>' `
    -FirmwarePath 'C:\private\sihp1005.img'
```

The second script signs and verifies the mini ZIP and prints its location under `artifacts/mini-*/`. The directory contains `Benteveo-HPLJ1005-x64-<version>-mini-self-signed-test.zip`, `Benteveo-HPLJ1005-x64-<version>-source.zip`, and `SHA256SUMS.txt`. Firmware is used locally to generate the catalog and is omitted from the ZIP. This process produces a self-signed tester package. [Packaging and installation guide](docs/MINI_INSTALL.md#build-locally)

## Driver architecture

Applications → Windows GDI/Unidrv rasterization and halftoning → `lj1005render.dll` → JBIG + ZJS → Windows print spooler → `lj1005monitor.dll` (firmware, synchronization and completion) → inbox USB or local parallel port.

The rendering DLL implements `IPrintOemUni`. A GPD enables full raster delivery; the plug-in assembles a page and uses the same tested encoder as the USB utility. Margins are embedded in the physical-sheet raster; no ZJS offset commands are emitted by the plug-in. Landscape uses Unidrv's `LANDSCAPE_CC270` rotation into portrait device coordinates. Use A4, 600 dpi, monochrome, and simplex for physical printing. Letter rendering works in file captures. A Letter job produced repeated blank sheets with A4 paper loaded. Physical printing with matching Letter paper remains unverified. C++ provides direct Windows printer COM/GDI integration and reuses the existing C JBIG codec. No custom kernel driver is necessary for the verified USB path.

## License

Project code is GPL-2.0-or-later. The ZIP includes the project license and third-party notices. Users supply HP firmware from their original software. [License](LICENSE) · [Third-party notices](THIRD_PARTY.md) · [Contributor style guide](docs/CODING_STYLE.md)
