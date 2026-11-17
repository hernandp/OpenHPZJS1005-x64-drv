# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$DriverFolder = '.',
    [string]$HpPackage
)

$ErrorActionPreference = 'Stop'
$hpUrl = 'https://ftp.hp.com/pub/softlib/software2/COL2222/lj-10067-2/lj1005hostbased-en.exe'
$firmwareName = 'sihp1005.img'
$expectedBytes = 116892
$expectedSha256 = '638B9AD4A2CF6888208926EF7C7DE073DA8BA4CDED25D31000A585C4526A9CA1'

$folder = Get-Item -LiteralPath $DriverFolder
if ($folder -isnot [IO.DirectoryInfo] -or
    !(Test-Path -LiteralPath (Join-Path $folder.FullName 'lj1005.inf') -PathType Leaf)) {
    throw 'Choose the extracted mini ZIP folder that contains lj1005.inf.'
}
$destination = Join-Path $folder.FullName $firmwareName
if (Test-Path -LiteralPath $destination) {
    $existing = Get-Item -LiteralPath $destination
    if ($existing -is [IO.FileInfo] -and $existing.Length -eq $expectedBytes -and
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $expectedSha256) {
        Write-Output "Firmware is already verified: $destination"
        return
    }
    throw "An unexpected file or folder already exists at $destination. Move it aside and run the helper again."
}

$download = $null
$staged = $null
try {
    if ($HpPackage) {
        $package = (Get-Item -LiteralPath $HpPackage).FullName
    } else {
        $download = [IO.Path]::GetTempFileName()
        Write-Host 'Downloading the original LaserJet 1005 package from HP...'
        try {
            Invoke-WebRequest -Uri $hpUrl -UseBasicParsing -MaximumRedirection 0 -TimeoutSec 60 -OutFile $download
        } catch {
            throw "HP download failed. If you have the original HP package, use -HpPackage with its local path. $($_.Exception.Message)"
        }
        $package = $download
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    # Open the old self-extractor as a ZIP and read only its firmware entry.
    try {
        $zip = [IO.Compression.ZipFile]::OpenRead($package)
    } catch {
        throw "Could not open the HP package as a ZIP archive: $package"
    }
    try {
        $entries = @($zip.Entries | Where-Object Name -eq $firmwareName)
        if ($entries.Count -ne 1 -or $entries[0].Length -ne $expectedBytes) {
            throw 'The package does not contain one original LaserJet 1005 firmware image of the expected size.'
        }
        $reader = [IO.BinaryReader]::new($entries[0].Open())
        try {
            $bytes = $reader.ReadBytes($expectedBytes + 1)
        } finally {
            $reader.Dispose()
        }
    } finally {
        $zip.Dispose()
    }

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = [BitConverter]::ToString($sha256.ComputeHash($bytes)).Replace('-', '')
    } finally {
        $sha256.Dispose()
    }
    if ($bytes.Length -ne $expectedBytes -or $digest -ne $expectedSha256) {
        throw 'The extracted firmware does not match the SHA-256 required by the Benteveo catalog.'
    }

    $staged = Join-Path $folder.FullName ([IO.Path]::GetRandomFileName())
    [IO.File]::WriteAllBytes($staged, $bytes)
    [IO.File]::Move($staged, $destination)
    Write-Output "Firmware verified and ready for Have Disk: $destination"
} finally {
    if ($download) { [IO.File]::Delete($download) }
    if ($staged) { [IO.File]::Delete($staged) }
}
