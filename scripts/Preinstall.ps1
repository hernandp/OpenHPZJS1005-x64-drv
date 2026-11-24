# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
#requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$infPath = Join-Path $PSScriptRoot 'lj1005.inf'
$versionLine = Select-String -LiteralPath $infPath -Pattern '^DriverVer=[^,]+,([0-9]+(?:\.[0-9]+){3})\s*$' `
    -ErrorAction SilentlyContinue | Select-Object -First 1
$driverVersion = if ($versionLine) { $versionLine.Matches[0].Groups[1].Value } else { 'version unavailable' }
Write-Host "Project Benteveo   $driverVersion  HP Laserjet 1005 Device Driver."
Write-Host 'Copyright (c) 2026 Hernán Di PIetro and collaborators.'
Write-Host 'Licensed under GPL 2.0.'
Write-Host ''

if ($args.Count -gt 1 -or ($args.Count -eq 1 -and $args[0] -notin '/CHECK', '/INSTALL')) {
    Write-Host 'Usage: powershell.exe -NoProfile -File .\Preinstall.ps1 [/CHECK | /INSTALL]'
    exit 2
}

$install = $args.Count -eq 1 -and $args[0] -eq '/INSTALL'
$firmwareName = 'sihp1005.img'
$firmwareBytes = 116892
$firmwareSha256 = '638B9AD4A2CF6888208926EF7C7DE073DA8BA4CDED25D31000A585C4526A9CA1'
$firmwarePath = Join-Path $PSScriptRoot $firmwareName
$hpUrl = 'https://ftp.hp.com/pub/softlib/software2/COL2222/lj-10067-2/lj1005hostbased-en.exe'
$certificateThumbprint = 'DEAD78F5D4F2A4D8A424726555A6C3D536E445FC'
$certificateSha256 = 'E0EA91F75FAD36262A1BF580EFE384C7002AB69DD87F6012240A64A600A4EB19'
$certificateStores = @('Root', 'TrustedPublisher')

function Write-CheckResult([string]$Message, [switch]$Passed) {
    if ($Passed) {
        Write-Host '✓ ' -ForegroundColor Green -NoNewline
    } else {
        Write-Host '✗ ' -ForegroundColor Red -NoNewline
    }
    Write-Host $Message
}

function Get-Sha256([byte[]]$Bytes) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        [BitConverter]::ToString($algorithm.ComputeHash($Bytes)).Replace('-', '')
    } finally {
        $algorithm.Dispose()
    }
}

function Get-VerifiedCertificate {
    foreach ($name in 'lj1005.inf', 'lj1005.cat', 'TEST-CERTIFICATE.cer') {
        if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $name) -PathType Leaf)) {
            throw "Missing $name. Extract the entire Benteveo mini ZIP and put this script beside the INF."
        }
    }

    $bytes = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'TEST-CERTIFICATE.cer'))
    if ((Get-Sha256 $bytes) -ne $certificateSha256) {
        throw 'Unexpected tester certificate. Obtain the intact ZIP from the official release.'
    }
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($bytes)
    $now = Get-Date
    if ($certificate.Thumbprint -ne $certificateThumbprint -or
        $now -lt $certificate.NotBefore -or $now -ge $certificate.NotAfter) {
        throw 'The tester certificate is outside its validity period or has the wrong thumbprint. Check the Windows clock and the release.'
    }

    Add-Type -AssemblyName System.Security
    $catalog = [Security.Cryptography.Pkcs.SignedCms]::new()
    $catalog.Decode([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'lj1005.cat')))
    $catalog.CheckSignature($true)
    if ($catalog.SignerInfos.Count -ne 1 -or
        $catalog.SignerInfos[0].Certificate.GetRawCertDataString() -ne $certificate.GetRawCertDataString()) {
        throw 'The catalog was signed by a different certificate. Obtain the intact ZIP from the official release.'
    }
    $certificate
}

function Get-MissingCertificateStores($Certificate) {
    foreach ($store in $certificateStores) {
        $installed = Get-ChildItem -LiteralPath "Cert:\LocalMachine\$store" |
            Where-Object { $_.GetRawCertDataString() -eq $Certificate.GetRawCertDataString() }
        if (!$installed) { $store }
    }
}

function Install-TesterCertificate($Certificate) {
    $missing = @(Get-MissingCertificateStores $Certificate)
    if (!$missing.Count) { return }

    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'Open Windows PowerShell as administrator, return to this folder, and rerun Preinstall.ps1 /INSTALL to add tester trust.'
        }
    } finally {
        $identity.Dispose()
    }

    Write-Host "Certificate: $($Certificate.Subject)"
    Write-Host "Thumbprint: $($Certificate.Thumbprint)"
    Write-Host "Missing local computer stores: $($missing -join ', ')"
    Write-Host 'Adding tester trust affects the whole computer.'
    if ((Read-Host 'Type YES to trust this certificate') -ne 'YES') {
        throw 'Certificate import declined.'
    }

    foreach ($name in $missing) {
        # The bytes have already been verified. Do not reopen the CER by path.
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($name, 'LocalMachine')
        try {
            $store.Open('ReadWrite, OpenExistingOnly')
            $store.Add($Certificate)
        } finally {
            $store.Close()
        }
    }
}

function Confirm-Firmware {
    $item = Get-Item -LiteralPath $firmwarePath -Force -ErrorAction SilentlyContinue
    if (!$item) {
        throw 'Missing sihp1005.img. Run /INSTALL or place the original firmware beside lj1005.inf.'
    }
    if ($item -isnot [IO.FileInfo] -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $item.Length -ne $firmwareBytes -or (Get-FileHash -LiteralPath $firmwarePath).Hash -ne $firmwareSha256) {
        throw 'Unexpected sihp1005.img. Move it aside, then supply the original image or run /INSTALL.'
    }
}

function Save-Firmware {
    if (Get-Item -LiteralPath $firmwarePath -Force -ErrorAction SilentlyContinue) {
        Confirm-Firmware
        return
    }

    $download = [IO.Path]::GetTempFileName()
    $staged = Join-Path $PSScriptRoot ([IO.Path]::GetRandomFileName())
    $streamCreated = $false
    try {
        Write-Host 'Downloading the original LaserJet 1005 package from HP...'
        try {
            $null = Invoke-WebRequest -Uri $hpUrl -UseBasicParsing -MaximumRedirection 0 -TimeoutSec 60 -OutFile $download
        } catch {
            throw "HP download failed. You can place the original sihp1005.img beside lj1005.inf. $($_.Exception.Message)"
        }

        # The HP self-extractor is a ZIP archive. Read only the firmware entry.
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::OpenRead($download)
        try {
            $entries = @($zip.Entries | Where-Object Name -eq $firmwareName)
            if ($entries.Count -ne 1 -or $entries[0].Length -ne $firmwareBytes) {
                throw 'The HP archive does not contain exactly one firmware image of the expected size.'
            }
            $reader = [IO.BinaryReader]::new($entries[0].Open())
            try {
                $bytes = $reader.ReadBytes($firmwareBytes + 1)
            } finally {
                $reader.Dispose()
            }
        } finally {
            $zip.Dispose()
        }
        if ($bytes.Length -ne $firmwareBytes -or (Get-Sha256 $bytes) -ne $firmwareSha256) {
            throw 'The downloaded firmware does not match the image required by the driver catalog.'
        }

        # Move without overwrite so a concurrent copy cannot be replaced.
        $stream = [IO.File]::Open($staged, 'CreateNew', 'Write', 'None')
        $streamCreated = $true
        try {
            $stream.Write($bytes, 0, $bytes.Length)
        } finally {
            $stream.Dispose()
        }
        [IO.File]::Move($staged, $firmwarePath)
        Write-Host 'Firmware saved beside lj1005.inf.'
    } finally {
        [IO.File]::Delete($download)
        if ($streamCreated) { [IO.File]::Delete($staged) }
    }
}

function Get-LaserJetPort {
    $devices = @(Get-PnpDevice -PresentOnly -Status OK |
        Where-Object InstanceId -Match '^USB\\VID_03F0&PID_1317\\[^\\]+$')
    if (!$devices.Count) {
        throw 'Connect and power on the original HP LaserJet 1005, then run /CHECK again.'
    }
    if ($devices.Count -gt 1) {
        throw 'More than one original LaserJet 1005 is connected. Leave only the intended printer connected and run /CHECK again.'
    }
    if (!(Get-Service -Name Spooler | Where-Object Status -eq Running)) {
        throw 'The Windows Print Spooler is stopped. Start it before running /CHECK again.'
    }
    $usbPortKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\USB Monitor\Ports'
    $spoolerPorts = @(Get-PrinterPort | Select-Object -ExpandProperty Name)
    $ports = @()
    if (Test-Path -LiteralPath $usbPortKey) {
        $ports = @(Get-ChildItem -LiteralPath $usbPortKey | Where-Object {
            $_.PSChildName -match '^USB[0-9]+$' -and $_.PSChildName -in $spoolerPorts -and
                (Get-ItemPropertyValue -LiteralPath $_.PSPath -Name 'Device Id' -ErrorAction SilentlyContinue) -eq $devices[0].InstanceId
        })
    }
    if ($ports.Count -gt 1) {
        throw "More than one USB port matches this printer ($($ports.PSChildName -join ', ')). Reconnect the printer and try /CHECK again."
    }
    if (!$ports.Count) {
        throw 'Windows sees the printer but has no matching USB print port. Reconnect it, wait for Windows to finish detection, then run /CHECK again.'
    }
    $ports[0].PSChildName
}

if ($install) {
    try {
        $verified = Get-VerifiedCertificate
        Save-Firmware
        Install-TesterCertificate $verified
    } catch {
        Write-Host "Preparation: $($_.Exception.Message)"
    }
}

$ready = $true
try {
    $verified = Get-VerifiedCertificate
    $missing = @(Get-MissingCertificateStores $verified)
    if ($missing.Count) {
        throw "Missing tester trust in $($missing -join ', '). Run /INSTALL in an administrator terminal."
    }
    Write-CheckResult 'Certificate: verified in both local computer stores.' -Passed
} catch {
    $ready = $false
    Write-CheckResult "Certificate: $($_.Exception.Message)"
}

try {
    Confirm-Firmware
    Write-CheckResult 'Firmware: sihp1005.img verified.' -Passed
} catch {
    $ready = $false
    Write-CheckResult "Firmware: $($_.Exception.Message)"
}

try {
    $port = Get-LaserJetPort
    Write-CheckResult "USB: Original LaserJet 1005 detected. Select $port in Have Disk." -Passed
} catch {
    $ready = $false
    Write-CheckResult "USB: $($_.Exception.Message)"
}

Write-Host ''
if ($ready) {
    Write-Host "Ready for Have Disk. Select existing port $port, then browse to lj1005.inf."
    exit 0
}
Write-Host 'Prerequisites incomplete. Resolve the items above and run /CHECK again.'
exit 1
