# Benteveo Have Disk package — self-signed testing build

For the original HP LaserJet 1005 Series on Windows 10 22H2 x64 or Windows 11 x64.
P1005 and M1005 are different printers. This package uses a self-signed tester
certificate that you explicitly trust during setup.
The earlier Open ZJS package passed wizard installation, file output and manual
removal/reinstallation in a Windows 11 Pro 25H2 x64 VM on November 10–11, 2026.
The Benteveo-branded package awaits its own VM installation acceptance. Physical A4 printing has passed on build 26300.9974 for the exact tester ZIP recorded in [test status](TEST_STATUS.md). The signed 0.1.2 USB retry fix also passed a timed interruption and follow-up on that build. Interruptions beyond the 30-second retry window and sleep/wake remain unverified. Selecting Letter with A4 paper loaded produced repeated blank sheets. Physical printing with matching Letter paper remains unverified.
Use a disposable VM for the initial installation rehearsal.

## Prepare with the prerequisite wizard

1. Extract the whole ZIP. Connect the original HP LaserJet 1005 to an available
   USB port and turn it on.
2. Open **BenteveoPrerequisites.exe** beside `lj1005.inf` and click **Next** to
   check tester trust, firmware, and the live USB print port.
3. Continue if a prerequisite needs attention. Approve tester trust only if
   you intend to add the listed certificate to the local computer. The wizard
   can download and verify the original HP firmware, or you can select a
   local `sihp1005.img` using **Browse for firmware**. If the printer is missing,
   connect it and retry.
4. Note the USB port and INF path on the ready screen. **Open Add Printer
   Wizard** opens **Find a printer by other options**. Choose **Add a local
   printer or network printer with manual settings**, then use the reported
   port and Have Disk as described below. Choose **Exit** to install later.

## Prepare with Preinstall.ps1

Extract the entire ZIP, connect and turn on the original LaserJet 1005, and open
Windows PowerShell in the folder containing `lj1005.inf`. Use the Windows
PowerShell 5.1 included with Windows 10 and 11.

```powershell
powershell.exe -NoProfile -File .\Preinstall.ps1 /CHECK
```

`/CHECK` is the default and reports certificate, firmware and USB readiness.
If it reports missing tester trust or firmware, run:

```powershell
powershell.exe -NoProfile -File .\Preinstall.ps1 /INSTALL
```

`/INSTALL` downloads the original HP archive, verifies the required firmware,
and saves it beside the INF. To add missing tester trust, run the same command
from **Windows PowerShell → Run as administrator** in the extracted folder
and type **YES** at the certificate prompt. The prompt lists the exact
certificate and local computer stores. This changes trust for the whole
computer. Either mode reports the USB port to select in Have Disk when it can
identify one live match. If it cannot, reconnect the printer and repeat the check.

The script is included starting with 0.3.0. For the 0.2.0 ZIP with the same
tester certificate, [download the script from the project](https://github.com/hernandp/OpenHPZJS1005-x64-drv#check-and-prepare-for-have-disk)
and save it beside the extracted INF. Obtain the ZIP and script from the official
project. If Windows blocks the verified downloaded script, unblock only that file
and use a process-scoped policy:

```powershell
Unblock-File -LiteralPath .\Preinstall.ps1
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\Preinstall.ps1 /CHECK
```

Use the manual preparation steps below if an organization policy blocks scripts.

## Prepare the extracted folder

1. Extract the ZIP completely. Keep all files together.
2. Use the optional [HP firmware helper](https://github.com/hernandp/OpenHPZJS1005-x64-drv#get-the-hp-firmware)
   to download and verify the firmware, or copy the original `sihp1005.img`
   from your own HP software package beside `lj1005.inf`. Keep that exact filename
   and leave the INF and CAT intact.
   Firmware is not included in this download and is not covered by our GPL license.
3. Check that the image is 116892 bytes and its SHA-256 is:
   `638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1`.
   PowerShell: `Get-FileHash -Algorithm SHA256 .\sihp1005.img`.
4. For this self-signed tester build, explicitly trust `TEST-CERTIFICATE.cer`
   in the local computer Trusted Root Certification Authorities and Trusted
   Publishers stores, after verifying its identity against the separately supplied
   release identity. The subject must be `OpenZJS Technical Tester Signing`;
   this retained signing identity predates the Benteveo branding.
   Compare the thumbprint and expiry in `BUILD-INFO.json`, not just the name.
   Certificate installation requires administrator permission. Never install
   a private key or disable signature enforcement. No certificate is imported
   automatically. This build has no timestamp; do not assume validity after expiry.

   Open `certlm.msc` as administrator. In each of the two stores above,
   right-click **Certificates**, choose **All Tasks > Import**, select the
   verified CER and confirm the destination store. Save the thumbprint for cleanup.

The catalog was generated with this exact firmware image present. Supplying
the matching file completes the package without changing its signature.
The ZIP's SHA256-MANIFEST lists distributed files only; BUILD-INFO records the
required, omitted firmware.

## Install with Windows

For reinstallation or an update, first complete the removal procedure below.
Retain the currently installed version's extracted package until cleanup is verified.

1. Connect and power on the printer. Windows protected print mode must be off;
   this legacy third-party driver cannot operate in that mode.
2. Open Printers & scanners, Add device, then the option to add manually.
   Choose a local printer with manual settings and the USB port reported by
   Preinstall.ps1. For manual preparation, record the port from an existing working
   queue when available. With several USB devices, compare the device entries in
   Device Manager before and after reconnecting this printer. Its hardware ID
   includes `VID_03F0&PID_1317`. If the port association remains unclear, stop and
   collect a support report before submitting jobs to a guessed port.
3. Choose Have Disk, browse to `lj1005.inf`, and select
   **HP Laserjet 1005 (Project Benteveo Driver)**. Approve administrator prompts.
   If firmware is missing or incorrect, cancel, correct the extracted folder,
   and retry; do not skip files or accept an altered package.
4. Finish the wizard before printing a test page. In **Printer properties →
   Ports**, confirm the correct USB port and enable bidirectional support.
   For A4 paper, set **Printer properties → Advanced → Printing Defaults** to A4,
   then confirm A4 in **Printing preferences** for the current user. Confirm
   portrait, monochrome and simplex. Keep your preferred default printer selected.
   The driver declares A4 as its default, and Windows Unidrv can select Letter
   on a non-metric system. [Windows paper default rules](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/feature-attributes)
5. Load one A4 sheet and select **Print Test Page** from Printer properties.
   Use A4 in both the application and printer preferences for physical printing.

Windows installs the image as `%SystemRoot%\System32\lj1005-firmware.img`.
The monitor uses this installed copy, so the extracted folder can be moved
after successful installation. Retain the original ZIP for future reinstalls.
The firmware loads when needed for printing after printer power-on.

## Shift the printed image

In **Printing Preferences → Alignment**, choose millimeters or inches
and enter a horizontal and vertical shift. Both start at zero. Positive values
move the image right or down as you view the print, and negative values move it
left or up. Each axis supports up to 200 mm in either direction. Changing units
keeps the same physical shift.

Open Printing Preferences from the printer settings to save your defaults, or
from an application's print dialog to change the current job. The shift keeps
the original print scale. Content moved past the edge of the sheet is clipped,
and the printer's physical margins still apply.

The **About Benteveo** tab in Printing Preferences shows the installed driver
version, copyright notice, and full GPL license.

## Troubleshooting

- **Missing file or catalog hash error.** Cancel the wizard, extract a fresh ZIP and check the name, size and SHA-256 of the image before retrying.
- **Signature rejected.** Check the catalog signer, expiry and both local-computer stores against the independently supplied tester identity. Stop if the identity differs.
- **Repeated blank sheets with Letter selected and A4 loaded.** Cancel the job and check that the queue is empty. Set both the application and printer preferences to A4 before retrying on A4 paper.
- **Blank sheet or paper stops partway.** Record which sheets actually emerged, the lights and selected USB port. Cancel any remaining job before reloading paper. The earlier four-sheet stoppage was attributed by the maintainer to a paper-loading solenoid problem; a retry passed after hardware repair.
- **Reporting a problem.** Include the Windows build, package digest, application, paper settings and exact error. Keep firmware, spool data and private documents out of reports.

## Record settings before maintenance

Keep the extracted package for the **currently installed version**, including its
catalog and monitor DLL. An update needs the old package for cleanup and the new
package for installation. Keep your original firmware privately.

In Printer properties and Printing preferences, record the queue name, selected
port, bidirectional support, paper size, orientation, monochrome/simplex settings
and custom preferences. Record the current default printer and whether Windows
manages the default automatically. Screenshots are sufficient for restoring these
settings manually. Close printing applications and finish or cancel this printer's
pending jobs before removal.

## Remove with Uninstall.ps1

Open **64-bit Windows PowerShell as administrator** in the extracted folder for
the installed Benteveo release. If your 0.2.0 or 0.3.0 ZIP predates this helper,
download [Uninstall.ps1 from the project](https://github.com/hernandp/OpenHPZJS1005-x64-drv#uninstall)
and put it beside the original INF, catalog and driver files.

```powershell
powershell.exe -NoProfile -File .\Uninstall.ps1 /CHECK
powershell.exe -NoProfile -File .\Uninstall.ps1 /REMOVE
```

`/CHECK` is a read-only preview. Review all listed queues before running
`/REMOVE`. Finish or cancel their print jobs, then type **REMOVE** when prompted
to confirm. If Windows reports an in-use component, restart and rerun
`/REMOVE` from the same folder. Keep the ZIP and original firmware for reinstalling.

After verified removal, press Enter to keep tester trust or type **REMOVE
CERTIFICATE** to request its removal. The script keeps the certificate if a
driver catalog still uses it or the dependency check cannot finish. Use the
manual procedure below if the installed package is older or cannot be matched.

## Remove the queue and driver package

1. In Settings, Printers & scanners, select the Benteveo queue and choose **Remove**.
   Repeat for any other queue using this driver.
2. Open **Print server properties**, choose **Drivers**, select
   **HP Laserjet 1005 (Project Benteveo Driver)**, or **HP Laserjet 1005 (Benteveo Driver)**
   for an earlier ZIP, and click **Remove**. Choose
   **Remove driver and driver package** and approve administrator prompts.
   Confirm the named Benteveo package, then choose **Delete** on the file-list
   page. Read the final result before closing the dialog.
   An administrator can also open this dialog with the command below.

```powershell
rundll32.exe printui.dll,PrintUIEntry /s /t2
```

3. If Windows reports that the driver is in use, close applications and printer
   dialogs, restart Windows, and repeat package removal.
4. Continue below to remove the monitor and firmware left behind by Windows.

## Clean up the remaining monitor and firmware

Open **64-bit Windows PowerShell as administrator**. Set `$package` below to the
extracted folder for the version you just removed. Run the entire first block,
then keep that window open for the second block. Stop on an error and retain the
console output. The original firmware can be absent from the extracted folder
during cleanup. For an earlier ZIP whose INF lists the monitor with the
`(development)` suffix, set `$monitorName` to
`HP LaserJet 1005 Firmware Monitor (development)` before running the block.

```powershell
$ErrorActionPreference = 'Stop'
$openZjsPreflight = $false
$package = (Resolve-Path -LiteralPath 'C:\path\to\HPLJ1005-mini').Path
$monitorName = 'HP LaserJet 1005 Firmware Monitor (Project Benteveo)'
$driverNames = @('HP Laserjet 1005 (Project Benteveo Driver)', 'HP Laserjet 1005 (Benteveo Driver)')
$providerNames = @('Project Benteveo', 'Benteveo Project')
$monitorKey = "HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\$monitorName"
$firmwareHash = '638B9AD4A2CF6888208926EF7C7DE073DA8BA4CDED25D31000A585C4526A9CA1'
if (![Environment]::Is64BitProcess) { throw 'Open 64-bit Windows PowerShell.' }
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open Windows PowerShell as administrator.'
}
function Assert-OpenZjsUnused {
    if (@(Get-Printer | Where-Object { $_.DriverName -in $driverNames }).Count) {
        throw 'Remove all queues using this driver first.'
    }
    $otherMonitor = @('HP LaserJet 1005 Firmware Monitor (development)',
                      'HP LaserJet 1005 Firmware Monitor (Project Benteveo)') |
        Where-Object { $_ -ne $monitorName }
    if (Test-Path -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\$otherMonitor") {
        throw 'A second Benteveo monitor is registered and may share the files. Stop and collect a support report.'
    }
    $drivers = Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Environments' -Recurse |
        ForEach-Object { Get-ItemProperty -LiteralPath $_.PSPath }
    foreach ($driver in $drivers) {
        if ($driver.Monitor -eq $monitorName -or
            $driver.'Language Monitor' -eq $monitorName -or
            $driver.PSChildName -in $driverNames -or
            [string]$driver.'Configuration File' -match 'lj1005' -or
            [string]$driver.'Dependent Files' -match 'lj1005') {
            throw 'A registered driver still references Benteveo. Remove its package first.'
        }
    }
    $packages = @(Get-WindowsDriver -Online | Where-Object { $_.ProviderName -in $providerNames })
    if ($packages.Count) {
        $packages | Format-Table Driver,OriginalFileName,ProviderName
        throw 'A Benteveo driver-store package remains. Finish Windows package removal first.'
    }
}
Assert-OpenZjsUnused
$catalog = Join-Path $package 'lj1005.cat'
$reference = Join-Path $package 'lj1005monitor.dll'
$catSignature = Get-AuthenticodeSignature -LiteralPath $catalog
$dllSignature = Get-AuthenticodeSignature -LiteralPath $reference
if ($catSignature.Status -ne 'Valid' -or $dllSignature.Status -ne 'Valid' -or
    $catSignature.SignerCertificate.Thumbprint -ne $dllSignature.SignerCertificate.Thumbprint) {
    throw 'Use the intact, trusted package for the installed version.'
}
$files = @(
    @{Path=(Join-Path $env:windir 'System32\lj1005monitor.dll');
      Hash=(Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash},
    @{Path=(Join-Path $env:windir 'System32\lj1005-firmware.img'); Hash=$firmwareHash}
)
foreach ($file in $files) {
    if (Test-Path -LiteralPath $file.Path) {
        $item = Get-Item -LiteralPath $file.Path -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Unexpected redirected file. Stop for review.'
        }
        if ((Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne $file.Hash) {
            throw "File identity differs from the retained package: $($file.Path)"
        }
    }
}
if (Test-Path -LiteralPath $monitorKey) {
    if ((Get-ItemProperty -LiteralPath $monitorKey).Driver -ne 'lj1005monitor.dll') {
        throw 'Unexpected monitor registration. Stop for review.'
    }
}
$openZjsPreflight = $true
'Preflight passed. Both designated files have been checked.'
```

After preflight succeeds, run the next block in the same window. It unregisters
the monitor and deletes the two verified files. If Windows returns error 3008,
restart Windows and repeat both blocks. This can require another restart even
if you already restarted to remove the driver package. Keep printing applications closed and
avoid concurrent printer installation during cleanup.

```powershell
if ($openZjsPreflight -ne $true) { throw 'Run the complete preflight block successfully first.' }
Assert-OpenZjsUnused
if (Test-Path -LiteralPath $monitorKey) {
    if (!('OpenZjsManualRemoval' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OpenZjsManualRemoval {
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool DeleteMonitor(string server, string environment, string name);
}
'@
    }
    if (![OpenZjsManualRemoval]::DeleteMonitor($null, 'Windows x64', $monitorName)) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Monitor removal failed (Windows error $code). Restart and repeat if it is in use."
    }
}
foreach ($file in $files) {
    if (!(Test-Path -LiteralPath $file.Path)) { continue }
    Assert-OpenZjsUnused
    if (Test-Path -LiteralPath $monitorKey) { throw 'Monitor is registered again.' }
    $item = Get-Item -LiteralPath $file.Path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        (Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne $file.Hash) {
        throw 'File changed after preflight. Stop for review.'
    }
    try {
        Remove-Item -LiteralPath $file.Path -Force -ErrorAction Stop
    } catch {
        # Windows PowerShell also reports this protected-file denial as ArgumentError.
        $errorId = $_.FullyQualifiedErrorId.Split(',')[0]
        if ($errorId -notin @('RemoveFileSystemItemUnAuthorizedAccess', 'RemoveFileSystemItemArgumentError')) {
            throw
        }
        # Windows may leave these exact files owned by SYSTEM without admin delete access.
        $acl = Get-Acl -LiteralPath $file.Path
        $aclBackup = Join-Path $env:TEMP ([guid]::NewGuid().ToString() + '-openzjs-acl.xml')
        $acl | Export-Clixml -LiteralPath $aclBackup
        try {
            & takeown.exe /f $file.Path /a
            if ($LASTEXITCODE) { throw 'Ownership change failed.' }
            & icacls.exe $file.Path /grant '*S-1-5-32-544:F'
            if ($LASTEXITCODE) { throw 'Permission change failed.' }
            Remove-Item -LiteralPath $file.Path -Force -ErrorAction Stop
        } catch {
            $failure = $_
            try { Set-Acl -LiteralPath $file.Path -AclObject $acl } catch {
                Write-Warning "ACL restoration needs administrator review. Saved ACL: $aclBackup"
            }
            throw $failure
        }
    }
}
Assert-OpenZjsUnused
if ((Test-Path -LiteralPath $monitorKey) -or
    @($files | Where-Object { Test-Path -LiteralPath $_.Path }).Count) {
    throw 'Cleanup is incomplete.'
}
'Queue, driver package, monitor registration and designated files are absent.'
```

Both blocks can be repeated after partial cleanup. A hash mismatch requires the
matching old package or a reviewed diagnosis. Keep the files and report the error.
Other failures can require a restart. Never delete the spooler directory or remove
unrelated packages to resolve this printer's problem.

## Reinstall or update

After complete removal, extract the desired package, supply the firmware and
follow **Install with Windows**. Select the recorded port and restore the queue
name, printing preferences and bidirectional support. Restore the recorded default
printer and automatic-default setting in Printers & scanners. Windows can change
the default during installation or printing, so check it again after the test.
Reopen applications and test one page with paper
ready. Keep the old package until the replacement works.

## Remove tester certificate trust

For permanent removal, first verify that all packages using this tester identity
have been removed. For immediate reinstallation using the same signing identity,
retain trust until testing is finished.

Open `certlm.msc` as administrator. In **Trusted Publishers > Certificates** and
**Trusted Root Certification Authorities > Certificates**, inspect the certificate
details and match its full SHA-1 thumbprint to `signing.thumbprintSha1` in the
retained package's `BUILD-INFO.json`. Remove only that certificate from each store.
Match the complete thumbprint and confirm its absence in both stores afterward.

## Build locally

Run `scripts/package-candidate.ps1` to build and validate a fresh source snapshot.
This engineering candidate contains the driver files and build provenance. It can
be built without HP firmware. The catalog is created during the signing step.
Then run `scripts/package-mini.ps1 -CandidatePath <candidate-directory>
-CertificateThumbprint <dedicated-tester-thumbprint> -FirmwarePath <private-img>`.
If FirmwarePath is omitted, the generator prompts for it. Use PowerShell 7.5+, the
WDK and Python as for the existing package scripts. The supplied image is checked
before signing, staged only while generating the catalog, and removed before ZIP
creation, including on catalog failure. The original firmware file is untouched.
Only the dedicated tester identity is currently supported; public signing remains
a separate release task. Distribute the matching source ZIP with the binary ZIP.
