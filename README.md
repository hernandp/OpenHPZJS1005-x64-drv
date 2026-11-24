# Project Benteveo 

## HP Laserjet 1005 driver for Windows 10/11 (x64)

<p align="center">
  <img src="docs/benteveo-and-hp.png" alt="A benteveo bird perched on an HP LaserJet 1005 printer" width="720">
</p>

A Windows printer driver for the **original HP LaserJet 1005 Series** (USB ID `03f0:1317`). 

⚠️ The supported setup is **Windows 10 22H2 x64 or Windows 11 x64 with one USB printer and A4 paper**.

⚠️ The **Laserjet P1005 / M1005** use different drivers.

## Purpose of this project

I purchased this printer back in the early 2000s, and it served me well until HP decided to phase it out, ending software support in the Windows XP era.

Over the following two decades, I moved between houses, jobs, machines, and operating systems. This good old battleship ended up abandoned in the back of my parents' house, until I decided to rescue it from under a pile of dirt, spiderwebs, and dead insects to pursue some toner transfer printing that my girlfriend needed for her artwork.

She wanted to transfer an image of a benteveo bird (the one that brands this project), so that's where the name comes from.

The printer got a new toner cartridge and some mechanical fixes, and it works quite well. But it was lacking a printer driver for Windows 11, so I decided to code one using Codex, relying on specs from the web and the Zenographics ZjStream open protocol implementation for CUPS.

It successfully prints text, bitmaps, and multiple-page documents without a hassle.

**Lesson:** Don't throw away your old hardware! With current AI tools, you can do some vibe software and hardware hacking to *hopefully* suit your needs.

## Current-state-of-development docs
* [Changelog](CHANGELOG.md)
* [Test evidence](docs/TEST_STATUS.md)
* [Release plan](docs/RELEASE_PLAN.md)

## Installation using the Prerequisites Wizard

The tester ZIP uses a self-signed certificate. The prerequisite wizard asks for
your consent before adding tester trust to the local computer.

1. Extract the entire ZIP. Connect the original HP LaserJet 1005 to USB and turn it on.
2. Run **BenteveoPrerequisites.exe** beside `lj1005.inf` and click **Next** to check the prerequisites.
3. Follow the prompts to prepare the tester certificate and HP firmware. Note the USB port and INF path on the ready screen.
4. Click **Open Add Printer Wizard** to continue, or **Exit** to install later.

### Continue with Add Printer Wizard

1. On **Find a printer by other options**, select **Add a local printer or network printer with manual settings** and click **Next**.

   <img width="500" alt="Add Printer Wizard with manual printer settings selected" src="https://github.com/user-attachments/assets/0ff1a57e-048a-49af-94a3-29887dd23ffe" />

2. Select **Use an existing port**, choose the exact USB port reported by the prerequisite wizard, and click **Next**. If the port is missing, reconnect the printer and rerun the prerequisite check.

   <img width="500" alt="The existing printer port selector" src="https://github.com/user-attachments/assets/99d19c1e-21e0-4735-b881-de62963bd23d" />

3. Click **Have Disk**, then **Browse**. Select `lj1005.inf` in the extracted folder, click **Open**, then **OK**.

   <img width="500" alt="Have Disk dialog for browsing to lj1005.inf" src="https://github.com/user-attachments/assets/fa1c124b-f872-4fb3-a425-728c9019559a" />

4. Select **HP Laserjet 1005 (Project Benteveo Driver)** and click **Next**. If Windows rejects the signature, cancel and check the certificate and extracted files. The screenshot shows the name used by an earlier tester ZIP. [Troubleshooting](docs/MINI_INSTALL.md#troubleshooting)

   <img width="500" alt="Benteveo driver model in the Add Printer Wizard" src="https://github.com/user-attachments/assets/58344dfb-f9b7-4f54-aac2-8af06387ae6d" />

5. Confirm the printer name and approve the Windows administrator prompt if shown. Choose **Do not share this printer** if asked. Click **Finish** before printing a test page.
6. Open the new queue's **Printer properties**. On the **Ports** tab, confirm the reported USB port and tick **Enable bidirectional support**. Set the paper size to **A4** in **Advanced → Printing Defaults** and in **Printing preferences**.
7. Load one A4 sheet and click **Print Test Page** in **Printer properties**.

For Windows Protected Print Mode, reinstallation, or driver cleanup, see the [installation guide](docs/MINI_INSTALL.md).

## Manual installation

Use these steps to prepare the package if the prerequisite wizard is unavailable.

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

### Install the tester certificate

Verify that `TEST-CERTIFICATE.cer` matches the published tester certificate identity and is within its validity period. If you choose to trust it, double-click the certificate and click **Install Certificate**. Import it into both **Trusted Root Certification Authorities** and **Trusted Publishers** under **Local Machine**. [Certificate verification and manual preparation](docs/MINI_INSTALL.md#prepare-the-extracted-folder)

You can check both stores in PowerShell. Both commands should return `True`.

```powershell
$t = 'DEAD78F5D4F2A4D8A424726555A6C3D536E445FC'
Test-Path "Cert:\LocalMachine\Root\$t"
Test-Path "Cert:\LocalMachine\TrustedPublisher\$t"
```

### Launch the Add Printer Wizard

Press **Windows key + R**, run `printui /im`, and follow the [Add Printer Wizard steps above](#continue-with-add-printer-wizard). You can also open **Printers & scanners** from Start, choose to add a printer, then select the manual option. If you prepared the package without the prerequisite wizard, confirm the port before continuing. [Manual port selection](docs/MINI_INSTALL.md#install-with-windows)

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

## Uninstall

Run [Uninstall.ps1](scripts/Uninstall.ps1) from the extracted ZIP for the
installed Benteveo version. Open 64-bit Windows PowerShell as administrator:

```powershell
powershell.exe -NoProfile -File .\Uninstall.ps1 /CHECK
powershell.exe -NoProfile -File .\Uninstall.ps1 /REMOVE
```

Review the queues and files in the preview before confirming removal. The
script offers a separate choice to remove tester trust after driver cleanup.
If your ZIP predates the helper, download it from the link above and place
it beside the extracted INF. [Installation and manual removal guide](docs/MINI_INSTALL.md#remove-with-uninstallps1)

## Building the mini ZIP

In PowerShell 7.5 or later, start from a clean checkout with Visual Studio C++, the Windows SDK and WDK 10.0.26100.0, and Python 3 on PATH. To sign the tester ZIP, use the original `sihp1005.img` and the project's `OpenZJS Technical Tester Signing` certificate with its private key in your current user's Personal store. The prerequisite wizard and uninstaller pin this tester identity. The candidate build and its tests can run without the signing key.

From the repository root, run the commands below with your tester certificate thumbprint and firmware path.

```powershell
./scripts/package-candidate.ps1 -CatalogOS '10_GE_X64,10_VB_X64' -OutputPathFile artifacts/candidate-path.txt
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
