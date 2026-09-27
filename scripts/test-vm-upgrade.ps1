#requires -Version 7.0
#requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PreviousPackage,
    [Parameter(Mandatory)][string]$CandidatePackage,
    [Parameter(Mandatory)][string]$FaultSetup,
    [Parameter(Mandatory)][string]$QueueTestPath,
    [Parameter(Mandatory)][string]$FirmwarePath
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lab = Join-Path $root 'artifacts\OpenZJS-Win11-25H2'
$info = Get-Content (Join-Path $lab 'vm.json') -Raw | ConvertFrom-Json
$vm = Get-VM -Id $info.vmId
if ($vm.Id.ToString() -ne '313693d0-ac6f-4d9e-80df-746e5980d194' -or $vm.Name -ne 'OpenZJS-Win11-25H2') { throw 'Unexpected VM identity.' }
if (@(Get-VMNetworkAdapter -VM $vm | Where-Object SwitchName).Count) { throw 'VM must remain network-isolated.' }
foreach ($package in @($PreviousPackage,$CandidatePackage)) {
    foreach ($entry in (Get-Content (Join-Path $package 'SHA256-MANIFEST.json') -Raw | ConvertFrom-Json)) {
        $file = Get-Item -LiteralPath (Join-Path $package $entry.path)
        if ($file.Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne $entry.sha256) { throw "Candidate manifest mismatch: $($entry.path)" }
    }
}
if ((Get-FileHash -LiteralPath $FirmwarePath).Hash -ne '638B9AD4A2CF6888208926EF7C7DE073DA8BA4CDED25D31000A585C4526A9CA1') { throw 'Unexpected firmware.' }
$productionBytes = [IO.File]::ReadAllBytes((Join-Path $CandidatePackage 'Setup.exe'))
if ([Text.Encoding]::Unicode.GetString($productionBytes).Contains('OPENZJS_SETUP_STOP_AT') -or
    [Text.Encoding]::Unicode.GetString($productionBytes).Contains('OPENZJS_SETUP_FAIL_AT')) { throw 'Production Setup contains fault hooks.' }
if (![Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($FaultSetup)).Contains('OPENZJS_SETUP_STOP_AT')) { throw 'Expected the separately built fault fixture.' }
$run = Join-Path $lab ('upgrade-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $run | Out-Null
$run | Set-Content (Join-Path $lab 'last-upgrade-run.txt')
$credential = Import-Clixml (Join-Path $lab 'credential.clixml')
$session = $null
$restarts = 0
function Phase([string]$Message) {
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
function Restart-Guest {
    $script:restarts++
    Phase 'Restarting only the disposable guest.'
    $boot = Invoke-Command -Session $script:session -ScriptBlock { (Get-CimInstance Win32_OperatingSystem).LastBootUpTime }
    Invoke-Command -Session $script:session -ScriptBlock {
        & shutdown.exe /r /t 1
        if ($LASTEXITCODE) { throw 'Guest restart failed.' }
    }
    Remove-PSSession $script:session
    $script:session = $null
    $deadline = (Get-Date).AddMinutes(6)
    do {
        Start-Sleep -Seconds 5
        $connection = $null
        try {
            $connection = Connect-Guest
            $newBoot = Invoke-Command -Session $connection -ScriptBlock { (Get-CimInstance Win32_OperatingSystem).LastBootUpTime }
            if ($newBoot -gt $boot) { $script:session = $connection; return }
        } catch {
            # A connection during shutdown is transient, including CIM errors.
        }
        if ($connection) { Remove-PSSession $connection }
    } while ((Get-Date) -lt $deadline)
    throw 'Guest did not restart.'
}
function Run-Once([string]$Label, [string]$Package, [string]$Operation, [string]$StopAt = '', [string]$FailAt = '') {
    Phase $Label
    $result = Invoke-Command -Session $session -ArgumentList $Label,$Package,$Operation,$StopAt,$FailAt -ScriptBlock {
        param($label,$package,$operation,$stopAt,$failAt)
        $ErrorActionPreference = 'Stop'
        $base = 'C:\OpenZJS-Upgrade-Test'
        $tx = 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005Update'
        $state = 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005'
        $arguments = "--quiet $operation --offline --firmware $base\firmware.img"
        if ($stopAt) { $env:OPENZJS_SETUP_STOP_AT = $stopAt }
        if ($failAt) { $env:OPENZJS_SETUP_FAIL_AT = $failAt }
        try {
            $start = [Diagnostics.ProcessStartInfo]::new()
            $start.FileName = "$base\$package\Setup.exe"
            $start.Arguments = $arguments
            $start.UseShellExecute = $false
            $start.CreateNoWindow = $true
            $start.RedirectStandardError = $true
            $process = [Diagnostics.Process]::Start($start)
            $stderr = $process.StandardError.ReadToEndAsync()
        } finally {
            Remove-Item Env:\OPENZJS_SETUP_STOP_AT,Env:\OPENZJS_SETUP_FAIL_AT -ErrorAction SilentlyContinue
        }
        $deadline = (Get-Date).AddMinutes(2)
        $killed = $false
        do {
            if ($process.HasExited) { break }
            if ($stopAt -and (Get-ItemProperty -LiteralPath $tx -ErrorAction SilentlyContinue).Phase -eq $stopAt) {
                # This is the explicitly built fixture, paused after a durable
                # phase write. Terminate only the exact guest process we started.
                if ($package -ne 'fault') { throw 'Refusing to terminate a production executable.' }
                Stop-Process -Id $process.Id -Force
                $process.WaitForExit()
                $killed = $true
                break
            }
            Start-Sleep -Milliseconds 50
            $process.Refresh()
        } while ((Get-Date) -lt $deadline)
        if (!$process.HasExited) { throw "Setup timed out: $label" }
        $process.WaitForExit()
        $errorText = $stderr.GetAwaiter().GetResult()
        $errorText | Set-Content "$base\results\$label-error.txt"
        $record = [pscustomobject]@{
            label=$label; exitCode=$(if ($killed) {-1} else {$process.ExitCode}); killed=$killed
            phase=(Get-ItemProperty -LiteralPath $tx -ErrorAction SilentlyContinue).Phase
            status=(Get-ItemProperty -LiteralPath $state -ErrorAction SilentlyContinue).Status
            errorText=$errorText
        }
        $record | ConvertTo-Json -Compress | Add-Content "$base\results\steps.jsonl"
        return $record
    }
    $result | Select-Object label,exitCode,killed,phase,status,errorText | ConvertTo-Json -Compress | Add-Content (Join-Path $run 'steps.jsonl')
    return $result
}
function Run-Recovery([string]$Label, [string]$Package, [string]$Operation, [string]$StopAt = '', [string]$FailAt = '') {
    for ($attempt=1; $attempt -le 6; $attempt++) {
        $result = Run-Once "$Label-$attempt" $Package $Operation $StopAt $FailAt
        if ($result.killed) { return $result }
        if ($result.exitCode -eq 0) {
            if ($StopAt) { throw "Did not reach the interruption point: $StopAt" }
            return $result
        }
        if ($FailAt -and $result.exitCode -eq 1 -and $result.errorText -match 'previous build and printer settings were restored') { return $result }
        if ($FailAt -and $result.phase -like 'Rollback*') {
            $Operation = '--rollback'
            Restart-Guest
            continue
        }
        if ($result.exitCode -eq 3010 -or
            ($result.exitCode -eq 1 -and $result.errorText -match 'Windows error: 3008' -and $result.status -eq 'Removing')) {
            Restart-Guest
            continue
        }
        throw "Unexpected result for $Label : $($result | ConvertTo-Json -Compress)"
    }
    throw "Too many recovery attempts: $Label"
}
function Assert-Installed([string]$Package, [string]$Label) {
    Invoke-Command -Session $session -ArgumentList $Package,$Label -ScriptBlock {
        param($package,$label)
        $ErrorActionPreference = 'Stop'
        $base = 'C:\OpenZJS-Upgrade-Test'
        $root = 'C:\Program Files\OpenZJS LaserJet 1005'
        $state = Get-ItemProperty 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005'
        if ($state.Status -ne 'Complete') { throw 'Installation is not complete.' }
        foreach ($file in Get-ChildItem "$base\$package\driver" -File) {
            if ((Get-FileHash -LiteralPath "$root\driver\$($file.Name)").Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Cached file differs: $($file.Name)" }
        }
        if ((Get-FileHash "$root\Setup.exe").Hash -ne (Get-FileHash "$base\$package\Setup.exe").Hash) { throw 'Installed Setup differs.' }
        if ((Get-FileHash 'C:\Windows\System32\lj1005monitor.dll').Hash -ne (Get-FileHash "$base\$package\driver\lj1005monitor.dll").Hash) { throw 'Monitor differs.' }
        if ((Get-FileHash 'C:\Windows\System32\lj1005-firmware.img').Hash -ne (Get-FileHash "$base\firmware.img").Hash) { throw 'Firmware differs.' }
        $driver = Get-PrinterDriver -Name 'HP LaserJet 1005 Series (Open ZJS)'
        if ($driver.InfPath -ne $state.InfPath) { throw 'Registered INF differs.' }
        foreach ($name in @('lj1005.gpd','lj1005.ini','lj1005render.dll')) {
            if ((Get-FileHash (Join-Path (Split-Path $driver.DataFile) $name)).Hash -ne (Get-FileHash "$base\$package\driver\$name").Hash) { throw "Live file differs: $name" }
        }
        $before = Get-Content "$base\results\queue-before.json" -Raw | ConvertFrom-Json
        $after = Get-Printer -Name 'HP LaserJet 1005 (Open ZJS)' -Full | Select-Object Name,PortName,Comment,Location,Priority,Shared,PermissionSDDL
        if (($before | ConvertTo-Json -Compress) -ne ($after | ConvertTo-Json -Compress)) { throw 'Queue preferences or permissions changed.' }
        if ((Get-PrintConfiguration -PrinterName $after.Name).PaperSize -ne 'Letter') { throw 'Saved paper default was not restored.' }
        $pdfBefore = Get-Content "$base\results\pdf-before.json" -Raw | ConvertFrom-Json
        $pdfAfter = Get-Printer -Name 'Microsoft Print to PDF' -Full | Select-Object Name,DriverName,PortName,PermissionSDDL
        if (($pdfBefore | ConvertTo-Json -Compress) -ne ($pdfAfter | ConvertTo-Json -Compress)) { throw 'Unrelated PDF printer changed.' }
        @{label=$label;package=$package;status='passed';inf=$state.InfPath;version=$state.Version} | ConvertTo-Json -Compress | Add-Content "$base\results\assertions.jsonl"
    }
}
try {
    Phase 'Restoring the dedicated clean checkpoint.'
    $checkpoint = Get-VMSnapshot -VM $vm -Name 'Clean-Windows-before-driver'
    if ($vm.State -ne 'Off') { Stop-VM -VM $vm -TurnOff -Force }
    Restore-VMSnapshot -VMSnapshot $checkpoint -Confirm:$false
    if ((Get-VM -Id $vm.Id).State -ne 'Running') { Start-VM -VM $vm }
    $session = Connect-Guest
    Invoke-Command -Session $session -ScriptBlock {
        if (!(Confirm-SecureBootUEFI)) { throw 'Secure Boot must remain enabled.' }
        New-Item -ItemType Directory -Path 'C:\OpenZJS-Upgrade-Test\results','C:\OpenZJS-Upgrade-Test\captures' -Force | Out-Null
        if (Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005') { throw 'Not a clean baseline.' }
    }
    Copy-Item -LiteralPath (Resolve-Path $PreviousPackage).Path -Destination 'C:\OpenZJS-Upgrade-Test\old' -Recurse -ToSession $session
    Copy-Item -LiteralPath (Resolve-Path $CandidatePackage).Path -Destination 'C:\OpenZJS-Upgrade-Test\new' -Recurse -ToSession $session
    Copy-Item -LiteralPath $FirmwarePath -Destination 'C:\OpenZJS-Upgrade-Test\firmware.img' -ToSession $session
    Copy-Item -LiteralPath $FaultSetup -Destination 'C:\OpenZJS-Upgrade-Test\fault-setup.exe' -ToSession $session
    Copy-Item -LiteralPath $QueueTestPath -Destination 'C:\OpenZJS-Upgrade-Test\queue_test.exe' -ToSession $session
    Invoke-Command -Session $session -ScriptBlock {
        $base = 'C:\OpenZJS-Upgrade-Test'
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=OpenZJS Upgrade VM Lab Only' -CertStoreLocation Cert:\CurrentUser\My -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddMonths(3)
        Export-Certificate -Cert $cert -FilePath "$base\lab.cer" | Out-Null
        foreach ($store in @('Root','TrustedPublisher')) {
            & certutil.exe -addstore -f $store "$base\lab.cer" > "$base\results\trust-$store.txt"
            if ($LASTEXITCODE) { throw 'Guest-only trust failed.' }
        }
        foreach ($package in @('old','new')) {
            if ((Set-AuthenticodeSignature "$base\$package\driver\lj1005.cat" -Certificate $cert -HashAlgorithm SHA256).Status -ne 'Valid') { throw 'Lab signing failed.' }
        }
        if ((Get-FileHash "$base\old\driver\lj1005monitor.dll").Hash -eq (Get-FileHash "$base\new\driver\lj1005monitor.dll").Hash) { throw 'Test requires different monitor binaries.' }
        Copy-Item "$base\new" "$base\fault" -Recurse
        Copy-Item "$base\fault-setup.exe" "$base\fault\Setup.exe" -Force
        Copy-Item "$base\new" "$base\tampered" -Recurse
        Add-Content "$base\tampered\driver\lj1005.gpd" '*% Tampered upgrade payload'
        Copy-Item "$base\old" "$base\downgrade" -Recurse
        Copy-Item "$base\new\Setup.exe" "$base\downgrade\Setup.exe" -Force
        Get-Printer -Name 'Microsoft Print to PDF' -Full | Select-Object Name,DriverName,PortName,PermissionSDDL | ConvertTo-Json | Set-Content "$base\results\pdf-before.json"
    }
    $null = Run-Recovery 'install-old' 'old' '--install'
    Invoke-Command -Session $session -ScriptBlock {
        Set-Printer -Name 'HP LaserJet 1005 (Open ZJS)' -Comment 'Preserve upgrade comment' -Location 'VM lab' -Priority 7
        Set-PrintConfiguration -PrinterName 'HP LaserJet 1005 (Open ZJS)' -PaperSize Letter
        Get-Printer -Name 'HP LaserJet 1005 (Open ZJS)' -Full | Select-Object Name,PortName,Comment,Location,Priority,Shared,PermissionSDDL | ConvertTo-Json | Set-Content 'C:\OpenZJS-Upgrade-Test\results\queue-before.json'
    }
    Assert-Installed 'old' 'baseline'
    $bad = Run-Once 'tampered-upgrade' 'tampered' '--upgrade'
    if ($bad.exitCode -ne 1) { throw 'Tampered upgrade was accepted.' }
    Assert-Installed 'old' 'tampered-preserved'
    $null = Run-Recovery 'cancel-preparation' 'new' '--rollback'
    Invoke-Command -Session $session -ScriptBlock { Add-Printer -Name 'OpenZJS Upgrade Foreign' -DriverName 'HP LaserJet 1005 Series (Open ZJS)' -PortName 'FILE:' }
    $bad = Run-Once 'dependent-queue' 'new' '--upgrade'
    if ($bad.exitCode -ne 1) { throw 'Dependent queue was accepted.' }
    Invoke-Command -Session $session -ScriptBlock { Remove-Printer -Name 'OpenZJS Upgrade Foreign' }
    Assert-Installed 'old' 'foreign-preserved'
    Invoke-Command -Session $session -ScriptBlock {
        $base = 'C:\OpenZJS-Upgrade-Test'
        $global:monitorLock = Start-Job -ScriptBlock {
            $ErrorActionPreference = 'Stop'
            $file = [IO.File]::Open('C:\Windows\System32\lj1005monitor.dll', [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
            try {
                'ready' | Set-Content 'C:\OpenZJS-Upgrade-Test\monitor-lock-ready.txt'
                Start-Sleep -Seconds 300
            } finally { $file.Dispose() }
        }
        $deadline = (Get-Date).AddSeconds(20)
        while (!(Test-Path "$base\monitor-lock-ready.txt")) {
            if ((Get-Date) -gt $deadline) {
                Receive-Job $global:monitorLock -ErrorAction Continue | Out-String | Set-Content "$base\results\monitor-lock-error.txt"
                throw 'Monitor lock helper failed.'
            }
            Start-Sleep -Milliseconds 100
        }
    }
    $null = Run-Recovery 'upgrade' 'new' '--upgrade'
    if ($restarts -lt 1) { throw 'Locked monitor did not exercise restart recovery.' }
    Assert-Installed 'new' 'upgrade'
    $bad = Run-Once 'reject-downgrade' 'downgrade' '--upgrade'
    if ($bad.exitCode -ne 1 -or $bad.errorText -notmatch 'older than the installed version') { throw 'Unrequested downgrade was accepted.' }
    Assert-Installed 'new' 'downgrade-preserved'
    Invoke-Command -Session $session -ScriptBlock {
        Add-Content -LiteralPath 'C:\Program Files\OpenZJS LaserJet 1005 Update\previous\driver\lj1005.gpd' '*% Corrupt saved rollback data'
    }
    $bad = Run-Once 'damaged-rollback' 'new' '--rollback'
    if ($bad.exitCode -ne 1 -or $bad.phase -ne 'Available') { throw 'Damaged rollback backup was accepted or changed the transaction.' }
    Assert-Installed 'new' 'damaged-backup-preserved'
    Invoke-Command -Session $session -ScriptBlock {
        Copy-Item -LiteralPath 'C:\OpenZJS-Upgrade-Test\old\driver\lj1005.gpd' -Destination 'C:\Program Files\OpenZJS LaserJet 1005 Update\previous\driver\lj1005.gpd' -Force
    }
    Invoke-Command -Session $session -ScriptBlock {
        if ((Get-ItemProperty 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005Update').Phase -ne 'Available') { throw 'Rollback backup not retained.' }
        & 'C:\OpenZJS-Upgrade-Test\queue_test.exe' 'HP LaserJet 1005 (Open ZJS)' 'C:\OpenZJS-Upgrade-Test\captures\upgraded-portrait.zjs' portrait
        if ($LASTEXITCODE) { throw 'Upgraded capture failed.' }
    }
    $null = Run-Recovery 'rollback' 'new' '--rollback'
    Assert-Installed 'old' 'rollback'
    $null = Run-Recovery 'interrupt-upgrade' 'fault' '--upgrade' 'UpgradeInstalling'
    $null = Run-Recovery 'recover-interrupted-upgrade' 'new' '--upgrade'
    Assert-Installed 'old' 'interrupted-upgrade-rolled-back'
    $null = Run-Recovery 'fail-after-install' 'fault' '--upgrade' '' 'UpgradeRestoringQueue'
    Assert-Installed 'old' 'failed-upgrade-rolled-back'
    $null = Run-Recovery 'upgrade-before-rollback-interruption' 'new' '--upgrade'
    Assert-Installed 'new' 'second-upgrade'
    $null = Run-Recovery 'interrupt-rollback' 'fault' '--rollback' 'RollbackInstalling'
    $null = Run-Recovery 'recover-interrupted-rollback' 'new' '--rollback'
    Assert-Installed 'old' 'interrupted-rollback-recovered'
    Invoke-Command -Session $session -ScriptBlock {
        if ((Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005Update') -or (Test-Path 'C:\Program Files\OpenZJS LaserJet 1005 Update')) { throw 'Update backup residue remains after rollback.' }
        & 'C:\OpenZJS-Upgrade-Test\queue_test.exe' 'HP LaserJet 1005 (Open ZJS)' 'C:\OpenZJS-Upgrade-Test\captures\rolled-back-portrait.zjs' portrait
        if ($LASTEXITCODE) { throw 'Rollback capture failed.' }
    }
    $null = Run-Recovery 'upgrade-before-final-remove' 'new' '--upgrade'
    Assert-Installed 'new' 'uninstall-with-backup'
    Invoke-Command -Session $session -ScriptBlock {
        'Unrecognized file must survive removal.' | Set-Content 'C:\Program Files\OpenZJS LaserJet 1005 Update\foreign.txt'
    }
    $bad = Run-Once 'backup-cleanup-failure' 'new' '--remove'
    if ($bad.exitCode -ne 1 -or $bad.status -ne 'Removing' -or $bad.errorText -notmatch 'Unexpected files in the update directory') {
        throw 'Backup cleanup failure did not retain a retryable ownership record.'
    }
    Invoke-Command -Session $session -ScriptBlock {
        $file = 'C:\Program Files\OpenZJS LaserJet 1005 Update\foreign.txt'
        if ((Get-Content -LiteralPath $file -Raw).Trim() -ne 'Unrecognized file must survive removal.') { throw 'Unrecognized file changed.' }
        Remove-Item -LiteralPath $file
    }
    $null = Run-Recovery 'final-remove' 'new' '--remove'
    Invoke-Command -Session $session -ScriptBlock {
        foreach ($path in @('HKLM:\SOFTWARE\OpenZJS\LaserJet1005','HKLM:\SOFTWARE\OpenZJS\LaserJet1005Update',
                'C:\Program Files\OpenZJS LaserJet 1005','C:\Program Files\OpenZJS LaserJet 1005 Update',
                'C:\Windows\System32\lj1005monitor.dll','C:\Windows\System32\lj1005-firmware.img')) {
            if (Test-Path -LiteralPath $path) { throw "Final removal residue: $path" }
        }
        if (Get-Printer -Name 'HP LaserJet 1005 (Open ZJS)' -ErrorAction SilentlyContinue) { throw 'Owned queue remains.' }
        if (Get-PrinterDriver -Name 'HP LaserJet 1005 Series (Open ZJS)' -ErrorAction SilentlyContinue) { throw 'Owned driver remains.' }
        foreach ($inf in (Get-Content 'C:\OpenZJS-Upgrade-Test\results\assertions.jsonl' | ForEach-Object { ($_ | ConvertFrom-Json).inf } | Sort-Object -Unique)) {
            if (Test-Path -LiteralPath $inf) { throw 'An exercised driver-store package remains.' }
        }
        if (!(Get-Printer -Name 'Microsoft Print to PDF') -or !(Confirm-SecureBootUEFI)) { throw 'Guest baseline changed.' }
    }
    @{ status='passed'; previousPackage=$PreviousPackage; candidatePackage=$CandidatePackage; faultFixture=$FaultSetup; guestRestarts=$restarts; labOnly=$true; physicalUsb='not tested' } | ConvertTo-Json | Set-Content (Join-Path $run 'result.json')
    Phase 'Upgrade/rollback acceptance passed.'
} catch {
    $_ | Out-String | Set-Content (Join-Path $run 'failure.txt')
    throw
} finally {
    if ($session) {
        try { Copy-Item -Path 'C:\OpenZJS-Upgrade-Test\results','C:\OpenZJS-Upgrade-Test\captures' -Destination $run -Recurse -FromSession $session }
        finally { Remove-PSSession $session }
    }
}
