# Manual VM rehearsal for the self-signed test package

Run these steps **inside the disposable VM**, not on the physical-printer PC.
The user performs the actions; no Computer Use automation is required. Stop at
any unexpected result and report the step, exit code and exact error text.
Do not complete later steps to work around an earlier failure.

## 1. Fresh baseline and files

In Hyper-V Manager, preserve any needed current VM state in a checkpoint, then
apply `Clean-Windows-before-driver` to `OpenZJS-Win11-25H2`, or use a separate
clean Windows 11 Pro 25H2 x64 VM. Applying the clean checkpoint discards later
guest changes. Keep its network disconnected and Secure Boot enabled.
Sign in using the credentials appropriate to that checkpoint.

Copy the identified self-signed-test ZIP and public `TEST-CERTIFICATE.cer` into
`C:\OpenZJS-Tester`. Copy your original firmware separately as
`C:\OpenZJS-Tester\sihp1005.img`; firmware is not in the release. Extract the ZIP
so `C:\OpenZJS-Tester\HPLJ1005x64\Setup.exe` exists. Keep the matching source ZIP
with your host evidence; do not copy any private signing key to the guest.

Open **Windows PowerShell as administrator** in the VM. Check its clock is
correct and falls inside the certificate validity period. Run:

```powershell
$base = 'C:\OpenZJS-Tester'
$pkg = "$base\HPLJ1005x64"
$queue = 'HP LaserJet 1005 (Open ZJS)'
$signing = Get-Content "$pkg\SIGNING-INFO.json" -Raw | ConvertFrom-Json
$thumb = $signing.thumbprintSha1
New-Item "$base\results" -ItemType Directory -Force | Out-Null
Start-Transcript "$base\results\rehearsal.txt" -Append
Get-ComputerInfo | Select-Object WindowsProductName,OsVersion,OsBuildNumber
Confirm-SecureBootUEFI
bcdedit /enum '{current}'
Get-Printer | Select-Object Name,DriverName,PortName
Get-FileHash "$base\TEST-CERTIFICATE.cer", "$base\*-self-signed-test.zip"
Get-FileHash "$base\sihp1005.img"
$signing | Format-List
```

Expected: Secure Boot `True`; no `testsigning Yes` or `nointegritychecks Yes`;
no Open ZJS installation. Compare archive hash, certificate SHA-256 and SHA-1
thumbprint with the maintainer's separately supplied values, not just the
download's own JSON. Firmware SHA-256 must be
`638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1`.

```powershell
$stores = @('Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPublisher',
            'Cert:\CurrentUser\Root','Cert:\CurrentUser\TrustedPublisher')
$before = @($stores | ForEach-Object {
    $store = $_
    Get-ChildItem $store | ForEach-Object { "$store\$($_.Thumbprint)" }
})
$before | Set-Content "$base\results\certificates-before.txt"
if ($before -match [regex]::Escape($thumb)) { throw 'Test identity already trusted: not a fresh baseline.' }
if (Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005') { throw 'Existing Open ZJS state.' }
if (Get-Printer -Name $queue -ErrorAction SilentlyContinue) { throw 'Existing Open ZJS queue.' }
$pdfBefore = Get-Printer -Name 'Microsoft Print to PDF' | Select-Object Name,DriverName,PortName,Shared
$pdfBefore | Export-Clixml "$base\results\pdf-before.xml"
```

Keep this PowerShell window open. The following helper runs only the Setup
path you explicitly pass, saves stderr and reports the process exit code:

```powershell
function Run-Setup($label, $exe, $arguments) {
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru `
        -WindowStyle Hidden -RedirectStandardError "$base\results\$label-error.txt"
    "$label exit code: $($p.ExitCode)"
    Get-Content "$base\results\$label-error.txt"
}
$installArgs = "--quiet --install --offline --firmware $base\sihp1005.img"
```

## 2. Refusal before trust

```powershell
Get-AuthenticodeSignature "$pkg\Setup.exe", "$pkg\driver\lj1005.cat" |
    Select-Object Path,Status,StatusMessage
Run-Setup 'before-trust' "$pkg\Setup.exe" $installArgs
```

Expected: signatures are not trusted, Setup exit **1**, with the package-trust
error. No owned queue, cache or ownership state should be created:

```powershell
Get-Printer -Name $queue -ErrorAction SilentlyContinue
Test-Path 'C:\Program Files\OpenZJS LaserJet 1005'
Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005'
```

Expected: no printer output and both paths `False`. If Windows policy blocks
Setup from executing, report that; do not disable policy or treat another error
as proof of this trust-refusal test.

## 3. Explicit VM-only certificate trust

After verifying the separately supplied certificate identity and deciding to
trust it in this VM, run these two imports yourself:

```powershell
Import-Certificate -FilePath "$base\TEST-CERTIFICATE.cer" -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath "$base\TEST-CERTIFICATE.cer" -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
$signedFiles = @("$pkg\Setup.exe", "$pkg\tools\lj1005.exe",
                 "$pkg\driver\lj1005render.dll", "$pkg\driver\lj1005monitor.dll",
                 "$pkg\driver\lj1005.cat")
foreach ($file in $signedFiles) {
    $sig = Get-AuthenticodeSignature $file
    $sig | Select-Object Path,Status
    if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Thumbprint -ne $thumb) {
        throw "Signature did not validate: $file"
    }
}
```

Expected: all five signatures `Valid`, with the designated signer. No reboot or
test-mode change should be needed for these imports.

Before installing, verify that modifying a catalog member is rejected:

```powershell
if (Test-Path "$base\tampered") { throw 'Use a fresh tamper-test directory.' }
Copy-Item $pkg "$base\tampered" -Recurse
Add-Content "$base\tampered\driver\lj1005.gpd" '*% Deliberately changed test member'
Run-Setup 'tampered' "$base\tampered\Setup.exe" $installArgs
```

Expected: exit **1**, package/staging/integrity failure, no owned installation.
Keep the failed copy for evidence; all following steps use the original `$pkg`.

## 4. Install, repair and removal

There is no USB printer in this VM. The explicit `--offline` switch below is a
lab-only file-output installation; it does not establish physical USB acceptance.

```powershell
Run-Setup 'install' "$pkg\Setup.exe" $installArgs
Get-Printer -Name $queue | Select-Object Name,DriverName,PortName,PrinterStatus
Get-PrintConfiguration -PrinterName $queue | Select-Object PaperSize,Color,DuplexingMode
Run-Setup 'repair' "$pkg\Setup.exe" $installArgs
```

Expected: both exit **0**, owned Open ZJS queue on `FILE:`, A4 monochrome
simplex. Confirm catalog caching preserved its bytes:

```powershell
Get-FileHash "$pkg\driver\lj1005.cat", 'C:\Program Files\OpenZJS LaserJet 1005\driver\lj1005.cat'
```

Expected: identical hashes. For a minimal real print, use Notepad to create a
harmless line, choose the exact guest-local **HP LaserJet 1005 (Open ZJS)**
queue and save the FILE output to `C:\OpenZJS-Tester\results\smoke.prn`.
Do not select a redirected host printer.

```powershell
& "$pkg\tools\lj1005.exe" validate "$base\results\smoke.prn"
Get-PrintJob -PrinterName $queue
```

Expected: one validated page/sheet and an empty queue. Full application and
hardware regression remain separate from this trust rehearsal.

Close printing apps. Launch `Setup.exe` from Explorer, choose **Remove** and
approve elevation. Record the publisher shown (expected **OpenZJS Technical
Tester Signing**) and final text. Complete any requested restart and rerun
Remove if instructed; a restart-required result is not final cleanup.

If you prefer command-line removal:

```powershell
Run-Setup 'remove' "$pkg\Setup.exe" '--quiet --remove'
```

Expected: **0** or **3010** (restart required). For 3010, restart the guest and
complete its requested retry. Reopen elevated PowerShell and restore `$base`,
`$pkg`, `$queue`, `$signing`, `$thumb`, and `$stores` from step 1 if needed.
Restart the transcript with `-Append`. No driver files or registry state should
be deleted manually to make the test pass.

## 5. Remove only the new certificate and verify cleanup

First verify the owned queue and installation are gone and the unrelated PDF
printer is unchanged. If Setup still requests cleanup/restart, finish that first.

```powershell
Get-Printer -Name $queue -ErrorAction SilentlyContinue
Test-Path 'C:\Program Files\OpenZJS LaserJet 1005'
Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005'
$pdfBefore = Import-Clixml "$base\results\pdf-before.xml"
$pdfAfter = Get-Printer -Name 'Microsoft Print to PDF' | Select-Object Name,DriverName,PortName,Shared
Compare-Object $pdfBefore $pdfAfter -Property Name,DriverName,PortName,Shared
```

Expected: no owned queue, both paths `False`, no PDF difference.
Only after that, remove the exact certificates you imported:

```powershell
foreach ($store in @('Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPublisher')) {
    $path = "$store\$thumb"
    $cert = Get-Item -LiteralPath $path
    if ($cert.Subject -ne 'CN=OpenZJS Technical Tester Signing') { throw 'Unexpected certificate.' }
    Remove-Item -LiteralPath $path
}
$after = @($stores | ForEach-Object {
    $store = $_
    Get-ChildItem $store | ForEach-Object { "$store\$($_.Thumbprint)" }
})
Compare-Object (Get-Content "$base\results\certificates-before.txt") $after
Get-AuthenticodeSignature "$pkg\Setup.exe", "$pkg\driver\lj1005.cat" |
    Select-Object Path,Status,StatusMessage
Confirm-SecureBootUEFI
Stop-Transcript
```

Expected: no certificate-store difference, signatures no longer trusted, Secure
Boot still `True`. Keep `results` and report each phase's result to the maintainer.
Trust removal alone does not uninstall a driver.

Upgrade/rollback of differently signed packages requires an identified second
version and a separate rehearsal. Do not force an upgrade between equal-version
packages or claim upgrade acceptance from this install/repair/remove run.
