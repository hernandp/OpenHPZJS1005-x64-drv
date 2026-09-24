#requires -Version 7.0
#requires -RunAsAdministrator
param(
    [Parameter(Mandatory)][string]$CandidatePath,
    [Parameter(Mandatory)][string]$FirmwarePath,
    [Parameter(Mandatory)][string]$QueueTestPath,
    [string]$VMName = 'OpenZJS-Win11-25H2'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$evidence = Join-Path $root "artifacts\$VMName"
$info = Get-Content (Join-Path $evidence 'vm.json') -Raw | ConvertFrom-Json
$vm = Get-VM -Name $VMName
if ($vm.Id.ToString() -ne $info.vmId) { throw 'VM identity mismatch.' }
if (@(Get-VMNetworkAdapter -VM $vm | Where-Object SwitchName).Count) { throw 'VM must be isolated.' }
if ((Get-FileHash -LiteralPath $FirmwarePath -Algorithm SHA256).Hash -ne '638B9AD4A2CF6888208926EF7C7DE073DA8BA4CDED25D31000A585C4526A9CA1') { throw 'Unexpected firmware.' }
$run = Join-Path $evidence ('installer-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $run | Out-Null
$credential = Import-Clixml (Join-Path $evidence 'credential.clixml')
$session = $null
function Write-Phase([string]$Message) {
    $line = [DateTime]::UtcNow.ToString('o') + ' ' + $Message
    $line | Add-Content (Join-Path $run 'progress.log')
    Write-Host $line
}
function Connect-Guest {
    $deadline = (Get-Date).AddMinutes(6)
    do {
        try { return New-PSSession -VMId $vm.Id -Credential $credential -ErrorAction Stop } catch { Start-Sleep -Seconds 3 }
    } while ((Get-Date) -lt $deadline)
    throw 'Guest connection timed out.'
}
function Restart-Guest($CurrentSession) {
    Write-Phase 'Restarting the guest for removal cleanup.'
    $previousBoot = Invoke-Command -Session $CurrentSession -ScriptBlock { (Get-CimInstance Win32_OperatingSystem).LastBootUpTime }
    Invoke-Command -Session $CurrentSession -ScriptBlock {
        & shutdown.exe /r /t 1
        if ($LASTEXITCODE) { throw 'Guest restart request failed.' }
    }
    Remove-PSSession $CurrentSession
    $restartDeadline = (Get-Date).AddMinutes(4)
    do {
        Start-Sleep -Seconds 5
        $connection = Connect-Guest
        $currentBoot = Invoke-Command -Session $connection -ScriptBlock { (Get-CimInstance Win32_OperatingSystem).LastBootUpTime }
        if ($currentBoot -gt $previousBoot) { return $connection }
        Remove-PSSession $connection
    } while ((Get-Date) -lt $restartDeadline)
    throw 'Guest did not restart before the deadline.'
}
try {
    Write-Phase 'Restoring the isolated clean checkpoint.'
    $checkpoint = Get-VMSnapshot -VM $vm -Name 'Clean-Windows-before-driver'
    if ($vm.State -ne 'Off') { Stop-VM -VM $vm -TurnOff -Force }
    Restore-VMSnapshot -VMSnapshot $checkpoint -Confirm:$false
    if ((Get-VM -Id $vm.Id).State -ne 'Running') { Start-VM -VM $vm }
    $session = Connect-Guest
    Write-Phase 'Guest connected; checking security and copying the candidate.'
    Invoke-Command -Session $session -ScriptBlock {
        if (!(Confirm-SecureBootUEFI)) { throw 'Secure Boot must be enabled.' }
        New-Item -ItemType Directory -Path 'C:\OpenZJS-Installer-Test\results', 'C:\OpenZJS-Installer-Test\captures' -Force | Out-Null
        if (Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005') { throw 'Not a clean installer baseline.' }
    }
    Copy-Item -LiteralPath (Resolve-Path $CandidatePath).Path -Destination 'C:\OpenZJS-Installer-Test\package' -Recurse -ToSession $session
    Copy-Item -LiteralPath (Resolve-Path $FirmwarePath).Path -Destination 'C:\OpenZJS-Installer-Test\sihp1005.img' -ToSession $session
    Copy-Item -LiteralPath (Resolve-Path $QueueTestPath).Path -Destination 'C:\OpenZJS-Installer-Test\queue_test.exe' -ToSession $session
    $removeExit = Invoke-Command -Session $session -ScriptBlock {
        $ErrorActionPreference = 'Stop'
        $base = 'C:\OpenZJS-Installer-Test'
        $results = "$base\results"
        $queue = 'HP LaserJet 1005 (Open ZJS)'
        $state = 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005'
        $installRoot = 'C:\Program Files\OpenZJS LaserJet 1005'
        function Run-Setup($label, $exe, $arguments) {
            Write-Host "Running installer check: $label"
            $process = Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden -RedirectStandardError "$results\$label-error.txt"
            @{ label = $label; exitCode = $process.ExitCode } | ConvertTo-Json -Compress | Add-Content "$results\steps.jsonl"
            return $process.ExitCode
        }
        function Assert-Clean {
            if ((Test-Path $state) -or (Test-Path $installRoot) -or
                (Get-Printer -Name $queue -ErrorAction SilentlyContinue)) { throw 'Unexpected installation residue.' }
        }
        $setup = "$base\package\Setup.exe"
        $argsInstall = "--quiet --install --offline --firmware $base\sihp1005.img"
        if ((Run-Setup 'unsigned' $setup $argsInstall) -eq 0) { throw 'Unsigned package accepted.' }
        Assert-Clean
        [IO.File]::WriteAllBytes("$base\invalid.img", [byte[]](1,2,3))
        if ((Run-Setup 'invalid-firmware' $setup "--quiet --install --offline --firmware $base\invalid.img") -eq 0) { throw 'Invalid firmware accepted.' }
        Assert-Clean
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=OpenZJS Installer VM Lab Only' -CertStoreLocation Cert:\CurrentUser\My -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddMonths(3)
        Export-Certificate -Cert $cert -FilePath "$base\lab.cer" | Out-Null
        foreach ($store in @('Root','TrustedPublisher')) {
            & certutil.exe -addstore -f $store "$base\lab.cer" > "$results\trust-$store.txt"
            if ($LASTEXITCODE) { throw 'Guest certificate import failed.' }
        }
        $signature = Set-AuthenticodeSignature -LiteralPath "$base\package\driver\lj1005.cat" -Certificate $cert -HashAlgorithm SHA256
        if ($signature.Status -ne 'Valid') { throw 'Lab signing failed.' }
        Copy-Item -LiteralPath "$base\package" -Destination "$base\tampered" -Recurse
        Add-Content "$base\tampered\driver\lj1005.gpd" '*% Tampered member for rollback test'
        if ((Run-Setup 'tampered-package' "$base\tampered\Setup.exe" $argsInstall) -eq 0) { throw 'Altered package accepted.' }
        Assert-Clean
        Add-Printer -Name $queue -DriverName 'Microsoft Print To PDF' -PortName 'PORTPROMPT:'
        if ((Run-Setup 'foreign-queue' $setup $argsInstall) -eq 0) { throw 'Foreign queue accepted.' }
        if ((Get-Printer -Name $queue).DriverName -ne 'Microsoft Print To PDF') { throw 'Foreign queue modified.' }
        Remove-Printer -Name $queue
        Assert-Clean
        if ((Run-Setup 'install' $setup $argsInstall) -ne 0) { throw 'Native installation failed.' }
        if ((Get-ItemProperty $state).Status -ne 'Complete') { throw 'Ownership journal incomplete.' }
        if ((Get-PrintConfiguration -PrinterName $queue).PaperSize -ne 'A4') { throw 'A4 default missing.' }
        if ((Get-FileHash 'C:\Windows\System32\lj1005-firmware.img').Hash -ne (Get-FileHash "$base\sihp1005.img").Hash) { throw 'Firmware differs.' }
        if ((Run-Setup 'repair' $setup $argsInstall) -ne 0) { throw 'Repair failed.' }
        $pdfBefore = Get-Printer -Name 'Microsoft Print to PDF' | Select-Object Name,DriverName,PortName,Shared
        $pdfBefore | ConvertTo-Json | Set-Content "$results\unrelated-printer-before.json"
        if ((Get-ItemProperty $state).RepairSchema -ne '1') { throw 'Repair identity missing.' }
        function Assert-Repaired {
            if ((Get-ItemProperty $state).Status -ne 'Complete') { throw 'Repair journal incomplete.' }
            $printer = Get-Printer -Name $queue
            if ($printer.DriverName -ne 'HP LaserJet 1005 Series (Open ZJS)' -or $printer.PortName -ne 'FILE:') { throw 'Queue was not restored.' }
            if ((Get-PrintConfiguration -PrinterName $queue).PaperSize -ne 'A4') { throw 'Repaired queue is not A4.' }
            foreach ($file in Get-ChildItem "$base\package\driver" -File) {
                if ((Get-FileHash -LiteralPath "$installRoot\driver\$($file.Name)").Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Cache repair differs: $($file.Name)" }
            }
            if ((Get-FileHash -LiteralPath "$installRoot\Setup.exe").Hash -ne (Get-FileHash -LiteralPath $setup).Hash) { throw 'Installed uninstaller differs.' }
            if ((Get-FileHash 'C:\Windows\System32\lj1005-firmware.img').Hash -ne (Get-FileHash "$base\sihp1005.img").Hash) { throw 'Repaired firmware differs.' }
            $driver = Get-PrinterDriver -Name $printer.DriverName
            $liveDirectory = Split-Path $driver.DataFile -Parent
            foreach ($name in @('lj1005.gpd','lj1005.ini','lj1005render.dll')) {
                if ((Get-FileHash -LiteralPath (Join-Path $liveDirectory $name)).Hash -ne (Get-FileHash -LiteralPath "$base\package\driver\$name").Hash) { throw "Live driver repair differs: $name" }
            }
            $pdfAfter = Get-Printer -Name 'Microsoft Print to PDF' | Select-Object Name,DriverName,PortName,Shared
            if (($pdfBefore | ConvertTo-Json -Compress) -ne ($pdfAfter | ConvertTo-Json -Compress)) { throw 'Unrelated printer changed.' }
        }
        Assert-Repaired
        if ((Run-Setup 'repair-tampered-source' "$base\tampered\Setup.exe" $argsInstall) -ne 1) { throw 'Repair accepted changed source.' }
        Assert-Repaired
        # A foreign queue must be rejected before even damaged owned files are restored.
        Remove-Printer -Name $queue
        Add-Printer -Name $queue -DriverName 'Microsoft Print To PDF' -PortName 'PORTPROMPT:'
        [IO.File]::WriteAllBytes('C:\Windows\System32\lj1005-firmware.img', [byte[]](1,2,3))
        $damagedFirmware = (Get-FileHash 'C:\Windows\System32\lj1005-firmware.img').Hash
        if ((Run-Setup 'repair-foreign-queue' $setup $argsInstall) -ne 1) { throw 'Repair accepted foreign queue.' }
        if ((Get-Printer -Name $queue).DriverName -ne 'Microsoft Print To PDF' -or
            (Get-FileHash 'C:\Windows\System32\lj1005-firmware.img').Hash -ne $damagedFirmware -or
            (Get-ItemProperty $state).Status -ne 'Complete') { throw 'Rejected repair made changes.' }
        Remove-Printer -Name $queue
        Add-Content -LiteralPath "$installRoot\driver\lj1005.gpd" '*% Corrupt cache for repair test'
        Remove-Item -LiteralPath "$installRoot\driver\lj1005.ini", "$installRoot\Setup.exe"
        if ((Run-Setup 'repair-deleted-queue-cache-firmware' $setup $argsInstall) -ne 0) { throw 'Queue/cache/firmware repair failed.' }
        Assert-Repaired
        # Service stops are fault injection in this disposable guest only. Setup
        # itself must never restart the shared spooler or edit live files directly.
        $liveDirectory = Split-Path (Get-PrinterDriver -Name 'HP LaserJet 1005 Series (Open ZJS)').DataFile -Parent
        if ($liveDirectory -ne 'C:\Windows\system32\spool\DRIVERS\x64\3') { throw 'Unexpected live driver directory.' }
        Stop-Service Spooler
        try {
            Remove-Item -LiteralPath (Join-Path $liveDirectory 'lj1005.gpd'), (Join-Path $liveDirectory 'lj1005render.dll')
            # Recreate after unlinking so a potential store hard link is not modified.
            [IO.File]::WriteAllBytes((Join-Path $liveDirectory 'lj1005.gpd'), [byte[]](1,2,3))
        } finally { Start-Service Spooler }
        if ((Run-Setup 'repair-live-driver-files' $setup $argsInstall) -ne 0) { throw 'Live driver repair failed.' }
        Assert-Repaired
        # A sharing violation exercises partial repair and a subsequent retry.
        $lock = [IO.File]::Open("$installRoot\driver\lj1005.ini", [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        try {
            if ((Run-Setup 'repair-locked-cache' $setup $argsInstall) -ne 1) { throw 'Locked file was not reported.' }
            if ((Get-ItemProperty $state).Status -ne 'NeedsRepair') { throw 'Failed repair was reported complete.' }
        } finally { $lock.Dispose() }
        if ((Run-Setup 'repair-retry' $setup $argsInstall) -ne 0) { throw 'Repair retry failed.' }
        Assert-Repaired
        Set-ItemProperty -LiteralPath $state -Name Status -Value Repairing
        Remove-Item -LiteralPath 'C:\Windows\System32\lj1005-firmware.img'
        if ((Run-Setup 'repair-interrupted-journal' $setup $argsInstall) -ne 0) { throw 'Interrupted repair could not resume.' }
        Assert-Repaired
        $pdfBefore | ConvertTo-Json | Set-Content "$results\unrelated-printer-preserved.json"
        foreach ($scenario in @('portrait','landscape','blank','multi','letter')) {
            & "$base\queue_test.exe" $queue "$base\captures\$scenario.zjs" $scenario > "$results\capture-$scenario.txt"
            if ($LASTEXITCODE) { throw "Capture failed: $scenario" }
        }
        Get-Printer -Name $queue | Select-Object Name,DriverName,PortName | ConvertTo-Json | Set-Content "$results\installed-queue.json"
        Get-PrinterDriver -Name 'HP LaserJet 1005 Series (Open ZJS)' | Select-Object Name,IsPackageAware,InfPath,Path | ConvertTo-Json | Set-Content "$results\installed-driver.json"
        if (@(Get-PrintJob -PrinterName $queue).Count) { throw 'Capture jobs remain.' }
        # An interrupted atomic file write can leave a temporary sibling. Removal
        # must clean only these known owned names, without deleting a directory tree.
        foreach ($temporary in @("$installRoot\driver\lj1005.gpd.repair", "$installRoot\Setup.exe.repair",
                                  'C:\Windows\System32\lj1005-firmware.img.repair')) {
            [IO.File]::WriteAllBytes($temporary, [byte[]](1,2,3))
        }
        $exit = Run-Setup 'remove' "$installRoot\Setup.exe" '--quiet --remove'
        if ($exit -eq 0) {
            Assert-Clean
            if (Test-Path 'C:\Windows\System32\lj1005-firmware.img') { throw 'Firmware remains.' }
            if (!(Get-Printer -Name 'Microsoft Print to PDF')) { throw 'Unrelated printer removed.' }
        }
        return $exit
    }
    if ($removeExit -eq 1) {
        # Windows can retain monitor references after the live-file repair and
        # captures. Exercise the exact restart-and-retry instruction; unrelated
        # removal failures must still fail the run.
        $monitorInUse = Invoke-Command -Session $session -ScriptBlock {
            $record = Get-ItemProperty 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005'
            $message = (Get-Content 'C:\OpenZJS-Installer-Test\results\remove-error.txt' -Raw).Trim()
            return $record.Status -eq 'Removing' -and $record.Driver -eq '0' -and $record.Monitor -eq '1' -and
                $message -eq "Windows could not remove the firmware monitor. Try removal again after restarting Windows.`nWindows error: 3008".Replace("`n", [Environment]::NewLine)
        }
        if ($monitorInUse) {
            Write-Phase 'Monitor in use (3008); verifying the documented removal retry.'
            $session = Restart-Guest $session
            $removeExit = Invoke-Command -Session $session -ScriptBlock {
                $base = 'C:\OpenZJS-Installer-Test'
                $process = Start-Process 'C:\Program Files\OpenZJS LaserJet 1005\Setup.exe' -ArgumentList '--quiet --remove' -Wait -PassThru -WindowStyle Hidden -RedirectStandardError "$base\results\remove-retry-error.txt"
                @{ label = 'remove-retry'; exitCode = $process.ExitCode } | ConvertTo-Json -Compress | Add-Content "$base\results\steps.jsonl"
                return $process.ExitCode
            }
        }
    }
    if ($removeExit -eq 3010) {
        Invoke-Command -Session $session -ScriptBlock {
            if ((Get-ItemProperty 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005').Status -ne 'RebootPending') { throw 'Missing restart journal.' }
        }
        $session = Restart-Guest $session
        Invoke-Command -Session $session -ScriptBlock {
            $base = 'C:\OpenZJS-Installer-Test'
            if (Test-Path 'C:\Windows\System32\lj1005monitor.dll') { throw 'Monitor still present after restart.' }
            if (Test-Path 'C:\Program Files\OpenZJS LaserJet 1005\Setup.exe') { throw 'Setup still present after restart.' }
            $process = Start-Process "$base\package\Setup.exe" -ArgumentList '--quiet --remove' -Wait -PassThru -WindowStyle Hidden -RedirectStandardError "$base\results\remove-after-restart-error.txt"
            @{ label = 'remove-after-restart'; exitCode = $process.ExitCode } | ConvertTo-Json -Compress | Add-Content "$base\results\steps.jsonl"
            if ($process.ExitCode) { throw 'Removal cleanup failed after restart.' }
        }
    } elseif ($removeExit -ne 0) { throw 'Removal needs investigation; see guest results.' }
    Invoke-Command -Session $session -ScriptBlock {
        foreach ($path in @('HKLM:\SOFTWARE\OpenZJS\LaserJet1005',
                'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OpenZJS-LaserJet1005',
                'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\HP LaserJet 1005 Firmware Monitor (development)',
                'C:\Program Files\OpenZJS LaserJet 1005', 'C:\Windows\System32\lj1005-firmware.img',
                'C:\Windows\System32\lj1005-firmware.img.repair',
                'C:\Windows\System32\lj1005monitor.dll')) {
            if (Test-Path $path) { throw "Removal residue: $path" }
        }
        if (Get-Printer -Name 'HP LaserJet 1005 (Open ZJS)' -ErrorAction SilentlyContinue) { throw 'Queue remains.' }
        if (Get-PrinterDriver -Name 'HP LaserJet 1005 Series (Open ZJS)' -ErrorAction SilentlyContinue) { throw 'Driver remains.' }
        $installed = Get-Content 'C:\OpenZJS-Installer-Test\results\installed-driver.json' -Raw | ConvertFrom-Json
        if (Test-Path $installed.InfPath) { throw 'Stored driver package remains.' }
        if (!(Get-Printer -Name 'Microsoft Print to PDF')) { throw 'Unrelated printer removed.' }
    }
    @{ status = 'passed'; labOnly = $true; secureBoot = $true; physicalUsb = 'not tested'; evidence = $run } |
        ConvertTo-Json | Set-Content (Join-Path $run 'result.json')
} catch {
    $_ | Out-String | Set-Content (Join-Path $run 'failure.txt')
    throw
} finally {
    if ($session) {
        try {
            Copy-Item -Path 'C:\OpenZJS-Installer-Test\results','C:\OpenZJS-Installer-Test\captures' -Destination $run -Recurse -FromSession $session
        } finally { Remove-PSSession $session }
    }
    $run | Set-Content (Join-Path $evidence 'last-installer-run.txt')
}
