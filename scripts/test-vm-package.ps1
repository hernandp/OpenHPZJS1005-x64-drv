#requires -Version 7.0
#requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CandidatePath,
    [Parameter(Mandatory)][string]$QueueTestPath,
    [ValidatePattern('^OpenZJS-[A-Za-z0-9-]+$')][string]$VMName = 'OpenZJS-Win11-25H2',
    [switch]$RestoreCleanCheckpoint
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$evidence = Join-Path $projectRoot "artifacts\$VMName"
$identity = Get-Content -LiteralPath (Join-Path $evidence 'vm.json') -Raw | ConvertFrom-Json
$vm = Get-VM -Name $VMName
if ($vm.Id.ToString() -ne $identity.vmId) { throw 'VM identity does not match the VM created for this project.' }
if (@(Get-VMNetworkAdapter -VM $vm | Where-Object SwitchName).Count) { throw 'This test requires an isolated VM.' }
$candidate = (Get-Item -LiteralPath $CandidatePath).FullName
$queueTest = (Get-Item -LiteralPath $QueueTestPath).FullName
$manifest = Get-Content -LiteralPath (Join-Path $candidate 'SHA256-MANIFEST.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest) {
    $file = [IO.Path]::GetFullPath((Join-Path $candidate $entry.path))
    if (!$file.StartsWith($candidate + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Manifest path escapes the candidate directory.'
    }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Candidate hash mismatch: $($entry.path)"
    }
}
$credential = Import-Clixml -LiteralPath (Join-Path $evidence 'credential.clixml')
$session = $null
function Write-Phase([string]$Message) {
    ([DateTime]::UtcNow.ToString('o') + ' ' + $Message) |
        Add-Content -LiteralPath (Join-Path $evidence 'test.log')
    Write-Host $Message
}
function Connect-Guest {
    $connected = $null
    $deadline = [DateTime]::UtcNow.AddMinutes(15)
    while (!$connected -and [DateTime]::UtcNow -lt $deadline) {
        try {
            $connected = New-PSSession -VMId $vm.Id -Credential $credential -ErrorAction Stop
            $ready = Invoke-Command -Session $connected -ScriptBlock {
                $setup = Get-ItemProperty 'HKLM:\SYSTEM\Setup'
                $setup.SystemSetupInProgress -eq 0 -and $setup.OOBEInProgress -eq 0
            }
            if (!$ready) { Remove-PSSession $connected; $connected = $null; Start-Sleep -Seconds 5 }
        } catch {
            if ($connected) { Remove-PSSession $connected; $connected = $null }
            Start-Sleep -Seconds 5
        }
    }
    if (!$connected) { throw 'Windows setup did not become available over PowerShell Direct.' }
    return $connected
}
$testFailed = $false
$runStarted = [DateTime]::UtcNow.ToString('o')
$runDirectory = Join-Path $evidence ('runs\' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $runDirectory | Out-Null
@{ startedUtc = $runStarted; status = 'running'; evidenceDirectory = $runDirectory } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $evidence 'run-status.json')
try {
    if ($RestoreCleanCheckpoint) {
        $checkpoint = Get-VMSnapshot -VM $vm -Name 'Clean-Windows-before-driver'
        if (!$checkpoint) { throw 'No clean checkpoint exists.' }
        Write-Phase 'Restoring the dedicated lab VM to its clean checkpoint; discarding previous guest test changes.'
        if ($vm.State -ne 'Off') { Stop-VM -VM $vm -TurnOff -Force }
        Restore-VMSnapshot -VMSnapshot $checkpoint -Confirm:$false
        if ((Get-VM -Id $vm.Id).State -ne 'Running') { Start-VM -VM $vm }
    }
    Write-Phase 'Waiting for the new Windows guest to finish setup (up to 15 minutes).'
    $session = Connect-Guest
    Write-Phase 'Recording clean baseline and removing temporary setup credentials from guest configuration.'
    Invoke-Command -Session $session -ScriptBlock {
        $ErrorActionPreference = 'Stop'
        if ($env:COMPUTERNAME -ne 'OPENZJS-LAB') { throw 'Unexpected guest computer.' }
        if (!(Confirm-SecureBootUEFI)) { throw 'Secure Boot is not enabled.' }
        if (Get-PrinterDriver -Name 'HP LaserJet 1005 Series (Open ZJS)' -ErrorAction SilentlyContinue) {
            throw 'This guest already has the driver installed.'
        }
        New-Item -ItemType Directory -Path 'C:\OpenZJS-Test\results', 'C:\OpenZJS-Test\captures' -Force | Out-Null
        $winlogon = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
        Set-ItemProperty -LiteralPath $winlogon -Name AutoAdminLogon -Value '0'
        Remove-ItemProperty -LiteralPath $winlogon -Name DefaultPassword, AutoLogonCount -ErrorAction SilentlyContinue
        foreach ($answer in @('C:\Windows\Panther\unattend.xml', 'C:\Windows\Panther\Unattend\unattend.xml')) {
            if (Test-Path -LiteralPath $answer) { Remove-Item -LiteralPath $answer -Force }
        }
        foreach ($file in @('OpenZJS-VMBootTarget.txt', 'OpenZJS-BootReady.txt', 'lab-bcdboot.log', 'lab-diskpart.log')) {
            if (Test-Path -LiteralPath "C:\$file") { Remove-Item -LiteralPath "C:\$file" -Force }
        }
        $boot = & bcdedit.exe /enum '{current}' | Out-String
        if ($boot -match '(?im)^\s*(testsigning|nointegritychecks)\s+Yes') { throw 'Signature enforcement was changed.' }
        [ordered]@{
            os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, OSArchitecture
            secureBoot = Confirm-SecureBootUEFI
            tpm = Get-Tpm | Select-Object TpmPresent, TpmReady
            bootConfiguration = $boot
            printerDrivers = @(Get-PrinterDriver | Select-Object Name, MajorVersion, InfPath, Path)
            printers = @(Get-Printer | Select-Object Name, DriverName, PortName)
            unidrvInSpoolDirectory = Test-Path 'C:\Windows\System32\spool\drivers\x64\3\UNIDRV.DLL'
            visualStudioPresent = Test-Path 'C:\Program Files\Microsoft Visual Studio'
            vcRuntimeRegistry = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64' -ErrorAction SilentlyContinue | Select-Object Installed, Version
        } | ConvertTo-Json -Depth 6 | Set-Content 'C:\OpenZJS-Test\results\baseline.json' -Encoding UTF8
    }
    if (!$RestoreCleanCheckpoint -and (Get-VMSnapshot -VM $vm -Name 'Clean-Windows-before-driver' -ErrorAction SilentlyContinue)) {
        throw 'Clean checkpoint already exists; restore it explicitly before repeating a full clean test.'
    }
    # Standard checkpoints briefly save the VM and invalidate PowerShell Direct
    # sessions. Do not preserve an open session in the clean checkpoint.
    Remove-PSSession $session
    $session = $null
    if (!$RestoreCleanCheckpoint) { Checkpoint-VM -VM $vm -SnapshotName 'Clean-Windows-before-driver' }
    $session = Connect-Guest
    Write-Phase 'Clean checkpoint created. Copying candidate and offline test utility into guest.'
    Copy-Item -LiteralPath $candidate -Destination 'C:\OpenZJS-Test\package' -Recurse -ToSession $session
    Copy-Item -LiteralPath $queueTest -Destination 'C:\OpenZJS-Test\queue_test.exe' -ToSession $session
    Write-Phase 'Testing unsigned rejection, then signing and trusting the catalog only inside the disposable guest.'
    Invoke-Command -Session $session -ScriptBlock {
        $ErrorActionPreference = 'Stop'
        $results = 'C:\OpenZJS-Test\results'
        $inf = 'C:\OpenZJS-Test\package\driver\lj1005.inf'
        $cat = 'C:\OpenZJS-Test\package\driver\lj1005.cat'
        & pnputil.exe /add-driver $inf > "$results\unsigned-stage.txt"
        $unsignedExit = $LASTEXITCODE
        if ($unsignedExit -eq 0) { throw 'Unexpected success staging unsigned package; investigate trust baseline.' }
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=OpenZJS Disposable VM Lab Only' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddMonths(3)
        Export-Certificate -Cert $cert -FilePath 'C:\OpenZJS-Test\lab.cer' | Out-Null
        # Run in a native process: the PKI PowerShell provider can fail when
        # writing TrustedPublisher under a PowerShell Direct impersonation token.
        foreach ($store in @('Root', 'TrustedPublisher')) {
            & certutil.exe -addstore -f $store 'C:\OpenZJS-Test\lab.cer' > "$results\trust-$store.txt"
            if ($LASTEXITCODE -ne 0) { throw "Lab certificate import failed: $store ($LASTEXITCODE)" }
        }
        $signature = Set-AuthenticodeSignature -LiteralPath $cat -Certificate $cert -HashAlgorithm SHA256
        [ordered]@{ unsignedStagingExit = $unsignedExit; signatureStatus = $signature.Status.ToString(); thumbprint = $cert.Thumbprint; labOnly = $true } |
            ConvertTo-Json | Set-Content "$results\signing.json" -Encoding UTF8
        if ($signature.Status -ne 'Valid') { throw "Lab catalog signature failed: $($signature.Status)" }
        & pnputil.exe /add-driver $inf > "$results\signed-stage.txt"
        if ($LASTEXITCODE -ne 0) { throw "Signed package staging failed: $LASTEXITCODE" }
        $installed = @(Get-WindowsDriver -Online -All | Where-Object { [IO.Path]::GetFileName($_.OriginalFileName) -eq 'lj1005.inf' })
        if ($installed.Count -ne 1) { throw 'Could not identify the staged INF uniquely.' }
        $installed | Select-Object Driver, OriginalFileName, ProviderName, Version |
            ConvertTo-Json | Set-Content "$results\driver-store.json" -Encoding UTF8
        Add-PrinterDriver -Name 'HP LaserJet 1005 Series (Open ZJS)' -InfPath $installed[0].OriginalFileName
        $driver = Get-PrinterDriver -Name 'HP LaserJet 1005 Series (Open ZJS)'
        $driver | Select-Object * | ConvertTo-Json -Depth 4 | Set-Content "$results\installed-driver.json" -Encoding UTF8
        if (!$driver.IsPackageAware) { throw 'Installed driver is not package aware.' }
        $registration = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Environments\Windows x64\Drivers\Version-3\HP LaserJet 1005 Series (Open ZJS)'
        $registration | Select-Object CoreDependencies, PrinterDriverAttributes, DriverVersion |
            ConvertTo-Json | Set-Content "$results\registration.json" -Encoding UTF8
        if ($registration.CoreDependencies -notcontains '{D20EA372-DD35-4950-9ED8-A6335AFE79F0}') { throw 'Unidrv core dependency missing.' }
        $monitorKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\HP LaserJet 1005 Firmware Monitor (development)'
        Get-ItemProperty -LiteralPath $monitorKey | Select-Object Driver |
            ConvertTo-Json | Set-Content "$results\monitor.json" -Encoding UTF8
        foreach ($file in @($driver.Path, $driver.ConfigFile, $driver.DataFile, $driver.HelpFile, 'C:\Windows\System32\lj1005monitor.dll') + @($driver.DependentFiles)) {
            if (!(Test-Path -LiteralPath $file)) { throw "Installed dependency missing: $file" }
        }
        foreach ($name in @('lj1005monitor.dll', 'lj1005render.dll')) {
            $installedPath = if ($name -eq 'lj1005monitor.dll') { "C:\Windows\System32\$name" } else { Join-Path (Split-Path $driver.Path) $name }
            if ((Get-FileHash -LiteralPath $installedPath).Hash -ne (Get-FileHash -LiteralPath "C:\OpenZJS-Test\package\driver\$name").Hash) {
                throw "Installed binary differs: $name"
            }
        }
        Add-Printer -Name 'OpenZJS VM File Test' -DriverName $driver.Name -PortName 'FILE:'
        Set-PrintConfiguration -PrinterName 'OpenZJS VM File Test' -PaperSize A4
        foreach ($scenario in @('portrait', 'landscape', 'blank', 'multi', 'letter')) {
            $output = "C:\OpenZJS-Test\captures\$scenario.zjs"
            & 'C:\OpenZJS-Test\queue_test.exe' 'OpenZJS VM File Test' $output $scenario > "$results\capture-$scenario.txt"
            if ($LASTEXITCODE -ne 0) { throw "GDI capture failed: $scenario ($LASTEXITCODE)" }
            $deadline = (Get-Date).AddSeconds(30)
            while (@(Get-PrintJob -PrinterName 'OpenZJS VM File Test').Count -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
            if (@(Get-PrintJob -PrinterName 'OpenZJS VM File Test').Count) { throw "Capture queue did not clear: $scenario" }
            if (!(Test-Path -LiteralPath $output) -or (Get-Item -LiteralPath $output).Length -lt 4) { throw "Empty capture: $scenario" }
            & 'C:\OpenZJS-Test\package\tools\lj1005.exe' inspect $output > "$results\inspect-$scenario.txt"
            if ($LASTEXITCODE -ne 0) { throw "Invalid ZJS capture: $scenario" }
        }
        [ordered]@{ packageInstallation = 'passed'; fileCaptures = 'passed'; physicalPrinting = 'not tested'; productionTrust = 'not tested'; secureBoot = Confirm-SecureBootUEFI } |
            ConvertTo-Json | Set-Content "$results\result.json" -Encoding UTF8
    }
    Write-Phase 'Package installation and offline GDI captures passed. Copying evidence for independent decoding.'
} catch {
    $testFailed = $true
    $failure = $_ | Out-String
    $failure | Set-Content -LiteralPath (Join-Path $evidence 'test-error.txt')
    $failure | Set-Content -LiteralPath (Join-Path $runDirectory 'test-error.txt')
    throw
} finally {
    if ($session) {
        try {
            Invoke-Command -Session $session -ScriptBlock {
                Copy-Item -LiteralPath 'C:\Windows\INF\setupapi.dev.log' -Destination 'C:\OpenZJS-Test\results\setupapi.dev.log' -ErrorAction SilentlyContinue
            }
            Copy-Item -Path 'C:\OpenZJS-Test\results', 'C:\OpenZJS-Test\captures' -Destination $runDirectory -Recurse -FromSession $session
        } catch {
            $_ | Out-String | Set-Content -LiteralPath (Join-Path $evidence 'evidence-export-error.txt')
            if (!$testFailed) { $testFailed = $true; throw }
        } finally { Remove-PSSession $session }
    }
    @{ startedUtc = $runStarted; finishedUtc = [DateTime]::UtcNow.ToString('o'); status = $(if ($testFailed) { 'failed' } else { 'passed' }); evidenceDirectory = $runDirectory } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'run-status.json')
}
