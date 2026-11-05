# Minimal Have Disk package — self-signed testing build

For the original HP LaserJet 1005 Series on Windows 11 x64. P1005 and M1005
are different printers. This is a tester package, not a publicly trusted release.
Wizard installation and five file-output scenarios passed in a Windows 11 Pro
25H2 x64 VM on November 5, 2026. Physical multi-page recovery remains
under investigation. Use a disposable VM for the initial installation rehearsal.

## Prepare the extracted folder

1. Extract the ZIP completely. Keep all files together.
2. Obtain the original `sihp1005.img` from your own HP software package and copy
   it beside `lj1005.inf`. Keep that exact filename; do not modify the INF or CAT.
   Firmware is not included in this download and is not covered by our GPL license.
3. Check that the image is 116892 bytes and its SHA-256 is:
   `638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1`.
   PowerShell: `Get-FileHash -Algorithm SHA256 .\sihp1005.img`.
4. For this self-signed tester build, explicitly trust `TEST-CERTIFICATE.cer`
   in the local computer Trusted Root Certification Authorities and Trusted
   Publishers stores, after verifying its identity against the separately supplied
   release identity. The subject must be `OpenZJS Technical Tester Signing`;
   compare the thumbprint and expiry in `BUILD-INFO.json`, not just the name.
   Certificate installation requires administrator permission. Never install
   a private key or disable signature enforcement. No certificate is imported
   automatically. This build has no timestamp; do not assume validity after expiry.

The catalog was generated with this exact firmware image present. Supplying
the matching file completes the package without changing its signature.
The ZIP's SHA256-MANIFEST lists distributed files only; BUILD-INFO records the
required, omitted firmware. No Setup executable or diagnostic utility is needed.

## Install with Windows

Start with no previous Open ZJS installation. This package shares the existing
driver and monitor identities; installing it over a Setup-managed installation
is not an accepted migration or upgrade path.

1. Connect and power on the printer. Windows protected print mode must be off;
   this legacy third-party driver cannot operate in that mode.
2. Open Printers & scanners, Add device, then the option to add manually.
   Choose a local printer with manual settings and the USB virtual printer port
   corresponding to the connected HP. Do not guess USB001 if several exist.
3. Choose Have Disk, browse to `lj1005.inf`, and select
   **HP LaserJet 1005 Series (Open ZJS)**. Approve administrator prompts.
   If firmware is missing or incorrect, cancel, correct the extracted folder,
   and retry; do not skip files or accept an altered package.
4. In Printer properties, Ports, confirm the correct USB port and enable
   bidirectional support. In Printing preferences, confirm A4, portrait,
   monochrome and simplex. Windows selected Letter initially in the VM rehearsal,
   despite the GPD's A4 default. Keep your preferred default printer selected.
5. Print one test page only when the physical printer and paper are ready.

Windows installs the image as `%SystemRoot%\System32\lj1005-firmware.img`.
The monitor uses this installed copy, so the extracted folder can be moved
after successful installation. Retain the original ZIP for future reinstalls.
The firmware loads when needed for printing after printer power-on.

## Removal and limitations

Remove the queue in Windows, then remove the driver/package through Print Server
Properties (Drivers) once no queue or job uses it. Windows may request a restart.
Do not run the full Setup uninstaller: it does not own this wizard installation.
The VM rehearsal confirmed that normal queue/driver removal leaves the language
monitor registration, monitor DLL and firmware image. Complete cleanup requires
unregistering the unused monitor through Windows' DeleteMonitor API, then removing
only the verified leftover files. In the VM, both files were SYSTEM-owned and
administrators lacked delete access; a restart did not resolve this. Cleanup
required narrowly granting access to those two files after verifying their hashes.
This ZIP has no cleanup helper. Do not delete shared system files on a working
PC or remove the monitor while another driver depends on it. The tested manual
maintenance procedure is an engineering operation, not an automatic uninstall.
After all tester packages have been removed, remove the exact tester certificate
from both local-computer stores if no other installed tester package needs it.

## Build locally

Run `scripts/package-candidate.ps1` to build and validate a fresh source snapshot.
Then run `scripts/package-mini.ps1 -CandidatePath <candidate-directory>
-CertificateThumbprint <dedicated-tester-thumbprint> -FirmwarePath <private-img>`.
If FirmwarePath is omitted, the generator prompts for it. Use PowerShell 7.5+, the
WDK and Python as for the existing package scripts. The supplied image is checked
before signing, staged only while generating the catalog, and removed before ZIP
creation, including on catalog failure. The original firmware file is untouched.
Only the dedicated tester identity is currently supported; public signing remains
a separate release task. Distribute the matching source ZIP with the binary ZIP.
