# Install and troubleshoot the original LaserJet 1005

This guide describes the intended packaged installation. Public signing and
physical acceptance are still pending; the current unsigned engineering ZIP is
not ready for ordinary installation. Use the release's tested-Windows list rather
than assuming every version accepted by Setup has been validated.

## Before installation

- Confirm the printer is the original **HP LaserJet 1005 Series**, not P1005 or
  M1005. The initial beta targets one USB printer on Windows 11 x64.
- Obtain `sihp1005.img` from your original HP driver package. Firmware is not
  included in Open ZJS downloads. Setup checks the exact supported image.
- Download the complete signed release ZIP and check its published SHA-256.
  Extract all files together; keep the extracted folder for repair or recovery.
- An existing development installation needs the maintainer migration procedure.
  Do not remove its files manually or try to adopt it through Upgrade.

## Install and print

1. Connect the printer directly by USB, turn it on and load A4 paper.
2. Open `Setup.exe` and choose **Install or repair**.
3. Select your original `sihp1005.img`, then approve Windows elevation. Confirm
   the publisher matches the release instructions.
4. Wait for completion. Select **HP LaserJet 1005 (Open ZJS)** in Windows or your
   application's Print dialog. Start with one test page and wait for it to finish.

Keep the printer on while a job completes. Firmware is volatile and is loaded
again when needed after power-on. The initial supported output is monochrome,
simplex, A4 at 600 dpi. Physical Letter acceptance is still pending.

## Repair, upgrade and removal

Use the same complete package and **Install or repair** to repair an owned
installation. Repair resets the queue port and A4 defaults. Use a newer complete
package and **Upgrade** for a version change. **Roll back** restores the saved
previous owned build and global settings; it does not restore a development
installation. Keep the new extracted package until recovery finishes.

Choose **Remove** or use Windows installed apps to uninstall. Let queued jobs
finish first. If Setup requests a restart, restart Windows and follow the stated
action. Avoid restarting the shared Print Spooler, which affects other printers.

## What to do when something fails

| Message or symptom | Next action |
| --- | --- |
| Firmware does not match | Select the original `sihp1005.img` from the supported HP package. Renaming another file will not work. |
| Administrator permission not granted / 1223 | Elevation was cancelled. Reopen Setup when ready and approve the prompt. |
| Connect and turn on the printer | Check power and USB, reconnect, then retry. Confirm the printer model. |
| Windows could not remove the monitor / 3008 during upgrade | Restart Windows, run the same new Setup and choose **Upgrade** or **Roll back** as instructed. |
| Restart requested during removal | Restart, then follow the removal instruction. Do not manually delete locked files. |
| Windows Protected Print Mode blocks the driver | This legacy driver is incompatible with that mode. Review Windows printer settings with your administrator; Setup does not change the policy. |
| Windows rejects the package or publisher | Check the release, signature and extraction. Do not import a downloaded lab certificate or disable signature enforcement. |
| Paper runs out or output stops | Check paper, door and USB. Inspect the existing job and printed pages before resubmitting; a partially printed job may produce duplicates if sent again. |
| A job stays in the queue | Record the error and pages printed. Cancel only your intended job if necessary, then report the failure. Avoid repeated Print clicks. |

Paper/door status presentation and physical recovery are still under acceptance
testing. Do not assume a queue reporting completion guarantees correct output.

## Ask for help

Include the release version, Windows edition/build, printer model, application
version/bitness, steps and exact error message. State the expected and actual page
counts. Use harmless reproduction files and crop screenshots to remove personal
information.

A read-only report helper is available in the matching source archive:
`scripts/export-support-report.ps1`. In 64-bit Windows PowerShell, run:

~~~powershell
./scripts/export-support-report.ps1 -OutputPath ./openzjs-support.json
~~~

Use a new filename for each report. Run it from a directory you can write to.
The helper works with Windows PowerShell 5.1 or PowerShell 7. It does not change
execution policy; if your system blocks scripts, use the manual information above
or contact your administrator. Administrator access can expose otherwise
unavailable diagnostic fields but is not automatically requested.

The report contains OS build, selected driver hashes/versions, queue status/job
counts and installation state. It excludes document contents, firmware bytes,
job titles/owners, queue names, machine/user names, full paths and device serials.
Missing fields and collection error codes mean information was unavailable.
Review the JSON before attaching it. Never attach a private recovery backup.
