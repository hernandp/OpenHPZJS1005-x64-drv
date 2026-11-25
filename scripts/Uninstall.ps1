# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
#requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($args.Count -gt 1 -or ($args.Count -eq 1 -and $args[0] -notin '/CHECK', '/REMOVE')) {
    Write-Host 'Usage: powershell.exe -NoProfile -File .\Uninstall.ps1 [/CHECK | /REMOVE]'
    exit 2
}

$remove = $args.Count -eq 1 -and $args[0] -eq '/REMOVE'
$packageVersion = '0.4.0.0'
$model = 'HP Laserjet 1005 (Project Benteveo Driver)'
$provider = 'Project Benteveo'
$monitorName = 'HP LaserJet 1005 Firmware Monitor (Project Benteveo)'
$environment = 'Windows x64'
$knownModels = @($model, 'HP Laserjet 1005 (Benteveo Driver)', 'HP LaserJet 1005 Series (Open ZJS)')
$knownProviders = @($provider, 'Benteveo Project', 'Open ZJS Project')
$thumbprint = 'DEAD78F5D4F2A4D8A424726555A6C3D536E445FC'
$certificateHash = 'E0EA91F75FAD36262A1BF580EFE384C7002AB69DD87F6012240A64A600A4EB19'
$firmwareHash = '638B9AD4A2CF6888208926EF7C7DE073DA8BA4CDED25D31000A585C4526A9CA1'
# lj1005.inf copies the original sihp1005.img to System32 under this name.
$installedFirmwareName = 'lj1005-firmware.img'
$monitorFile = 'lj1005monitor.dll'
$driverFiles = @('lj1005.gpd', 'lj1005.ini', 'lj1005render.dll', 'lj1005ui.dll')
$packageFiles = @('lj1005.inf', 'lj1005.cat', $monitorFile) + $driverFiles
$systemDirectory = [Environment]::SystemDirectory
$repository = Join-Path $systemDirectory 'DriverStore\FileRepository'
$monitorRegistry = 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors'

function Get-OrdinaryFile([string]$Path) {
    $file = Get-Item -LiteralPath $Path -Force
    if ($file -isnot [IO.FileInfo] -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Unexpected file type at $Path."
    }
    $file
}

function Get-Signers([string]$Path, [switch]$VerifySignature) {
    $catalog = [Security.Cryptography.Pkcs.SignedCms]::new()
    $catalog.Decode([IO.File]::ReadAllBytes((Get-OrdinaryFile $Path).FullName))
    if ($VerifySignature) { $catalog.CheckSignature($true) }
    foreach ($signer in $catalog.SignerInfos) {
        if (!$signer.Certificate) { throw "Signer certificate unavailable in $Path." }
        $signer.Certificate
    }
}

function Get-CertificateHash($Certificate) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        [BitConverter]::ToString($algorithm.ComputeHash($Certificate.RawData)).Replace('-', '')
    } finally {
        $algorithm.Dispose()
    }
}

function Get-PackageReference {
    $hashes = @{}
    foreach ($name in $packageFiles) {
        $file = Get-OrdinaryFile (Join-Path $PSScriptRoot $name)
        $hashes[$name] = (Get-FileHash -LiteralPath $file.FullName).Hash
    }
    $inf = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'lj1005.inf') -Raw
    if ($inf -notmatch '(?m)^DriverVer=[^,\r\n]+,([0-9.]+)\s*$' -or $Matches[1] -ne $packageVersion) {
        throw "This script belongs to Benteveo $packageVersion. Run it from its original extracted package."
    }
    $signers = @(Get-Signers (Join-Path $PSScriptRoot 'lj1005.cat') -VerifySignature)
    if ($signers.Count -ne 1 -or $signers[0].Thumbprint -ne $thumbprint -or
        (Get-CertificateHash $signers[0]) -ne $certificateHash) {
        throw 'The retained catalog does not match the Benteveo tester signing identity.'
    }
    [pscustomobject]@{ Version = $packageVersion; Hashes = $hashes; Certificate = $signers[0] }
}

function Assert-FileHash([string]$Path, [string]$Expected) {
    $file = Get-OrdinaryFile $Path
    if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne $Expected) {
        throw "File identity differs at $Path. Use the intact package for the installed version."
    }
}

function Get-StoreInf($Package) {
    $inf = [IO.Path]::GetFullPath($Package.OriginalFileName)
    if (!$inf.StartsWith("$repository\", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected Driver Store INF path for $($Package.Driver)."
    }
    $inf
}

function Get-RemovalState($Reference) {
    $packages = @(Get-WindowsDriver -Online | Where-Object {
        $_.ProviderName -in $knownProviders -or [IO.Path]::GetFileName($_.OriginalFileName) -eq 'lj1005.inf'
    })
    if ($packages.Count -gt 1) { throw 'Multiple Benteveo packages are staged. Use the manual removal guide.' }
    $package = $packages | Select-Object -First 1
    $storeInf = $null
    if ($package) {
        if ($package.ClassName -ne 'Printer' -or $package.ProviderName -ne $provider -or
            $package.Driver -notmatch '^oem[0-9]+\.inf$') {
            throw 'An older or unrecognized Benteveo driver is staged. Use the manual removal guide.'
        }
        $storeInf = Get-StoreInf $package
        if ([IO.Path]::GetFileName($storeInf) -ne 'lj1005.inf') {
            throw 'The staged INF has an unexpected name.'
        }
        foreach ($name in $packageFiles) {
            Assert-FileHash (Join-Path (Split-Path -Path $storeInf -Parent) $name) $Reference.Hashes[$name]
        }
        Assert-FileHash (Join-Path $env:windir "INF\$($package.Driver)") $Reference.Hashes['lj1005.inf']
    }

    $drivers = @(Get-PrinterDriver | Where-Object {
        $paths = @($_.InfPath, $_.DataFile, $_.ConfigFile, $_.Path) + @($_.DependentFiles)
        $_.Name -in $knownModels -or $_.Monitor -eq $monitorName -or
            ($paths -join '|' -match '(?:^|[\\/])lj1005[^\\/|]*\.(?:inf|gpd|ini|dll|img)(?:\||$)')
    })
    if ($drivers.Count -gt 1) { throw 'Multiple registered drivers reference Benteveo. Use the manual removal guide.' }
    $driver = $drivers | Select-Object -First 1
    if ($driver) {
        if (!$package -or $driver.Name -ne $model -or $driver.PrinterEnvironment -ne $environment -or
            $driver.Monitor -ne $monitorName -or $driver.InfPath -ne $storeInf) {
            throw 'The registered printer driver does not match this Benteveo package. Use the manual removal guide.'
        }
        $liveFiles = @($driver.DataFile) + @($driver.DependentFiles)
        foreach ($name in $driverFiles) {
            $paths = @($liveFiles | Where-Object { [IO.Path]::GetFileName($_) -eq $name } | Sort-Object -Unique)
            if ($paths.Count -ne 1) { throw "Cannot identify the installed $name." }
            Assert-FileHash $paths[0] $Reference.Hashes[$name]
        }
    }

    $queues = @(Get-Printer | Where-Object DriverName -In $knownModels | Sort-Object Name)
    foreach ($queue in $queues) {
        if (!$driver -or $queue.DriverName -ne $driver.Name -or $queue.Type -ne 'Local') {
            throw "Queue '$($queue.Name)' is outside the verified local package."
        }
    }

    $monitor = $null
    foreach ($key in (Get-ChildItem -LiteralPath $monitorRegistry)) {
        $dll = [string]$key.GetValue('Driver')
        if ($key.PSChildName -eq $monitorName) {
            if ($dll -ne $monitorFile) { throw 'The Benteveo monitor registration has an unexpected DLL.' }
            $monitor = $key.PSChildName
        } elseif ([IO.Path]::GetFileName($dll) -eq $monitorFile) {
            throw "Another print monitor uses $monitorFile. Use the manual removal guide."
        }
    }

    $files = @(
        foreach ($name in $monitorFile, $installedFirmwareName) {
            $path = Join-Path $systemDirectory $name
            if (Test-Path -LiteralPath $path) {
                $expected = if ($name -eq $installedFirmwareName) { $firmwareHash } else { $Reference.Hashes[$monitorFile] }
                Assert-FileHash $path $expected
                [pscustomobject]@{ Path = $path; Hash = $expected }
            }
        }
    )
    if (!$package -and @($files | Where-Object { [IO.Path]::GetFileName($_.Path) -eq $monitorFile }).Count) {
        # Once Windows removes the stored package, authenticate the retained DLL
        # itself before using its hash to authorize deletion of a system file.
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $PSScriptRoot $monitorFile)
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $thumbprint -or
            (Get-CertificateHash $signature.SignerCertificate) -ne $certificateHash) {
            throw 'The retained monitor DLL signature could not be verified. Use the manual removal guide.'
        }
    }
    [pscustomobject]@{ Queues = $queues; Driver = $driver; Package = $package; Monitor = $monitor; Files = $files }
}

function Assert-EmptyQueues($Queues) {
    foreach ($queue in $Queues) {
        if (@(Get-PrintJob -PrinterObject $queue).Count) {
            throw "Finish or cancel pending jobs on '$($queue.Name)', then rerun /REMOVE."
        }
    }
}

function Show-RemovalState($State) {
    foreach ($queue in $State.Queues) { Write-Host "Queue: $($queue.Name) ($($queue.PortName))" }
    if ($State.Driver) { Write-Host "Driver: $($State.Driver.Name)" }
    if ($State.Package) { Write-Host "Driver Store package: $($State.Package.Driver)" }
    if ($State.Monitor) { Write-Host "Monitor: $($State.Monitor)" }
    foreach ($file in $State.Files) { Write-Host "File: $($file.Path)" }
}

function Remove-VerifiedFile($File) {
    Assert-FileHash $File.Path $File.Hash
    try {
        [IO.File]::Delete($File.Path)
    } catch {
        if ($_.Exception.GetBaseException() -isnot [UnauthorizedAccessException]) { throw }
        $acl = Get-Acl -LiteralPath $File.Path
        $backup = Join-Path ([IO.Path]::GetTempPath()) ("benteveo-acl-" + [guid]::NewGuid() + '.xml')
        $acl | Export-Clixml -LiteralPath $backup
        try {
            & (Join-Path $systemDirectory 'takeown.exe') /f $File.Path /a | Out-Host
            if ($LASTEXITCODE) { throw 'Could not take ownership of the verified file.' }
            & (Join-Path $systemDirectory 'icacls.exe') $File.Path /grant '*S-1-5-32-544:F' | Out-Host
            if ($LASTEXITCODE) { throw 'Could not grant access to the verified file.' }
            Assert-FileHash $File.Path $File.Hash
            [IO.File]::Delete($File.Path)
        } catch {
            try { Set-Acl -LiteralPath $File.Path -AclObject $acl } catch {
                Write-Warning "Could not restore the file permissions. Saved ACL: $backup"
            }
            throw
        }
        [IO.File]::Delete($backup)
    }
}

function Remove-BenteveoMonitor {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class BenteveoUninstallSpooler {
    [DllImport("winspool.drv", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool DeleteMonitor(string server, string environment, string monitorName);
}
'@
    if (![BenteveoUninstallSpooler]::DeleteMonitor($null, $environment, $monitorName)) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Windows could not unregister the monitor (error $code)."
    }
}

function Remove-TesterTrust($Certificate) {
    $stores = @('TrustedPublisher', 'Root') | Where-Object {
        Test-Path -LiteralPath "Cert:\LocalMachine\$_\$thumbprint"
    }
    if (!$stores) { return }
    if ((Read-Host 'Press Enter to keep tester trust or type REMOVE CERTIFICATE') -ne 'REMOVE CERTIFICATE') {
        Write-Host 'Tester trust retained.'
        return
    }
    try {
        $directories = @(Get-WindowsDriver -Online | ForEach-Object {
            Split-Path -Path (Get-StoreInf $_) -Parent
        } | Sort-Object -Unique)
        foreach ($directory in $directories) {
            $catalogs = @(Get-ChildItem -LiteralPath $directory -Filter '*.cat' -File)
            if (!$catalogs.Count) { throw "No catalog could be inspected in $directory." }
            foreach ($catalog in $catalogs) {
                $signers = @(Get-Signers $catalog.FullName)
                if (!$signers.Count) { throw "No signer could be inspected in $($catalog.Name)." }
                if ($signers.Thumbprint -contains $thumbprint) {
                    throw "Staged driver catalog $($catalog.Name) still uses this certificate."
                }
            }
        }
    } catch {
        Write-Host "Tester trust retained. $($_.Exception.Message)"
        return
    }
    foreach ($name in $stores) {
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($name, 'LocalMachine')
        try {
            $store.Open('ReadWrite, OpenExistingOnly')
            $matching = @($store.Certificates | Where-Object {
                $_.GetRawCertDataString() -eq $Certificate.GetRawCertDataString()
            })
            foreach ($entry in $matching) { $store.Remove($entry) }
        } finally {
            $store.Close()
        }
        if (Test-Path -LiteralPath "Cert:\LocalMachine\$name\$thumbprint") {
            throw "Tester trust remains in $name. Check whether it is managed by policy."
        }
    }
    Write-Host 'Tester certificate is absent from both local computer stores.'
}

function Invoke-DriverRemoval($Confirmed, $Reference) {
    $state = Get-RemovalState $Reference
    $confirmedNames = @($Confirmed.Queues | ForEach-Object Name)
    $currentNames = @($state.Queues | ForEach-Object Name)
    $confirmedPackage = if ($Confirmed.Package) { $Confirmed.Package.Driver }
    $currentPackage = if ($state.Package) { $state.Package.Driver }
    if (($currentNames -join '|') -ne ($confirmedNames -join '|') -or $currentPackage -ne $confirmedPackage) {
        throw 'The installed queues or package changed. Review them again with /CHECK.'
    }
    Assert-EmptyQueues $state.Queues
    foreach ($queue in $state.Queues) {
        Assert-EmptyQueues @($queue)
        Remove-Printer -InputObject $queue
    }
    $state = Get-RemovalState $Reference
    if ($state.Queues.Count) { throw 'A Benteveo queue still uses this package.' }
    if ($state.Driver) { Remove-PrinterDriver -InputObject $state.Driver }
    $state = Get-RemovalState $Reference
    if ($state.Queues.Count -or $state.Driver) { throw 'The driver is still registered or in use.' }
    if ($state.Package) {
        & (Join-Path $systemDirectory 'pnputil.exe') /delete-driver $state.Package.Driver | Out-Host
        if ($LASTEXITCODE) { throw "Windows could not remove the Driver Store package (exit $LASTEXITCODE)." }
    }
    $state = Get-RemovalState $Reference
    if ($state.Queues.Count -or $state.Driver -or $state.Package) { throw 'A driver dependency remains.' }
    if ($state.Monitor) { Remove-BenteveoMonitor }
    foreach ($file in $state.Files) {
        $current = Get-RemovalState $Reference
        if ($current.Queues.Count -or $current.Driver -or $current.Package -or $current.Monitor) {
            throw 'A printer component still references the shared files.'
        }
        $remainingPaths = @($current.Files | ForEach-Object Path)
        if ($remainingPaths -contains $file.Path) { Remove-VerifiedFile $file }
    }
}

$startedRemoval = $false
try {
    if (![Environment]::Is64BitProcess) { throw 'Open 64-bit Windows PowerShell as administrator.' }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'Open Windows PowerShell as administrator and rerun this command.'
        }
    } finally {
        $identity.Dispose()
    }
    Add-Type -AssemblyName System.Security
    $reference = Get-PackageReference
    $state = Get-RemovalState $reference
    Write-Host "Benteveo $($reference.Version)"
    Show-RemovalState $state
    Assert-EmptyQueues $state.Queues
    $hasDriver = $state.Driver -or $state.Package -or $state.Monitor -or $state.Files.Count
    if (!$remove) {
        if ($hasDriver) { Write-Host 'Preview complete. Run /REMOVE to confirm removal.' }
        else { Write-Host 'No matching driver components remain.' }
        exit 0
    }
    if ($hasDriver) {
        if ((Read-Host 'Type REMOVE to confirm the listed driver and queue removal') -ne 'REMOVE') {
            Write-Host 'Removal canceled.'
            exit 1
        }
        $startedRemoval = $true
        Invoke-DriverRemoval $state $reference
    }
    $state = Get-RemovalState $reference
    if ($state.Queues.Count -or $state.Driver -or $state.Package -or $state.Monitor -or $state.Files.Count) {
        throw 'Driver cleanup is incomplete.'
    }
    Write-Host 'Verified that the Benteveo queue, driver, package, monitor and installed files are absent.'
    Remove-TesterTrust $reference.Certificate
    exit 0
} catch {
    Write-Host "Uninstall: $($_.Exception.Message)"
    if ($startedRemoval) {
        Write-Host 'If Windows reports a component in use, close printing apps, restart Windows and rerun /REMOVE.'
        try {
            Write-Host 'Remaining components:'
            Show-RemovalState (Get-RemovalState $reference)
        } catch {
            Write-Host "Could not verify remaining components: $($_.Exception.Message)"
        }
    }
    exit 1
}
