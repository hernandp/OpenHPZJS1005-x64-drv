#requires -Version 7.0
#requires -RunAsAdministrator
# Private, read-only recovery capture. This script never restores or removes a printer.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$VcRuntimeInstaller,
    [string]$QueueName = 'HP LaserJet 1005 Open ZJS (development)'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw 'Use a new backup directory.' }
$driverName = 'HP LaserJet 1005 Series (Open ZJS)'
$diagnosticName = 'HP LaserJet 1005 GPD diagnostic'
$monitorName = 'HP LaserJet 1005 Firmware Monitor (development)'
$queue = Get-Printer -Name $QueueName -Full
if ($queue.DriverName -ne $driverName) { throw 'The selected queue does not use the expected development driver.' }
if (Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005') { throw 'Owned package state exists; this capture is for the legacy development installation.' }
$driver = Get-PrinterDriver -Name $driverName
$diagnostic = Get-PrinterDriver -Name $diagnosticName
if ($driver.InfPath -or $diagnostic.InfPath) { throw 'Unexpected packaged driver registration.' }
$printers = @(Get-Printer -Full)
$related = @($printers | Where-Object { $_.DriverName -in @($driverName,$diagnosticName) })
if ($related.Count -ne 1 -or $related[0].Name -ne $QueueName) { throw 'Unexpected dependent queue; review migration scope.' }
if (@(Get-PrintJob -PrinterName $QueueName).Count) { throw 'Let jobs finish before taking a consistent recovery backup.' }
$signature = Get-AuthenticodeSignature -LiteralPath $VcRuntimeInstaller
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'A valid Microsoft-signed x64 runtime installer is required.' }
$spool = Join-Path $env:windir 'System32\spool\drivers\x64\3'
if ([IO.Path]::GetFullPath($driver.DataFile) -ne (Join-Path $spool 'lj1005.gpd') -or
    [IO.Path]::GetFullPath($diagnostic.DataFile) -ne (Join-Path $spool 'lj1005_config.gpd')) { throw 'Unexpected live data-file paths.' }
$monitorKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\' + $monitorName
if ((Get-ItemProperty -LiteralPath $monitorKey).Driver -ne 'lj1005monitor.dll') { throw 'Unexpected monitor registration.' }
$paths = @('lj1005.gpd','lj1005.ini','lj1005render.dll','lj1005_config.gpd') | ForEach-Object { Join-Path $spool $_ }
$paths += @('lj1005monitor.dll','lj1005-firmware.img') | ForEach-Object { Join-Path $env:windir ('System32\' + $_) }
foreach ($path in $paths) {
    $file = Get-Item -LiteralPath $path
    if ($file.LinkType) { throw 'Refusing a linked private file.' }
}
New-Item -ItemType Directory -Path $destination | Out-Null
$legacy = New-Item -ItemType Directory -Path (Join-Path $destination 'legacy-files')
$recovery = New-Item -ItemType Directory -Path (Join-Path $destination 'recovery-tools')
$initial = $printers | Select-Object Name,DriverName,PortName,Comment,Location,Priority,Shared,PermissionSDDL
$initial | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $destination 'printers-before.json')
$queue | Select-Object Name,DriverName,PortName,Comment,Location,Priority,Shared,PermissionSDDL,KeepPrintedJobs,EnableBidi | ConvertTo-Json | Set-Content (Join-Path $destination 'queue.json')
Get-PrintConfiguration -PrinterName $QueueName | Export-Clixml (Join-Path $destination 'configuration.clixml')
Get-PrinterPort -Name $queue.PortName | Export-Clixml (Join-Path $destination 'port.clixml')
Get-CimInstance Win32_Printer | Where-Object Default | Select-Object Name | ConvertTo-Json | Set-Content (Join-Path $destination 'current-user-default-printer.json')
$driver,$diagnostic | Export-Clixml (Join-Path $destination 'drivers.clixml')
$manifest = foreach ($path in $paths) {
    $file = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path).Hash
    Copy-Item -LiteralPath $path -Destination $legacy.FullName
    if ((Get-FileHash -LiteralPath (Join-Path $legacy.FullName $file.Name)).Hash -ne $hash) { throw 'Backup copy hash mismatch.' }
    [ordered]@{file=$file.Name;sha256=$hash;bytes=$file.Length;originalPath=$path;attributes=[string]$file.Attributes}
}
$manifest | ConvertTo-Json | Set-Content (Join-Path $legacy.FullName 'manifest.json')
# GetPrinter level 8 returns the global DEVMODE. No SetPrinter entry point is used.
if (!('OpenZjsBackup' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.ComponentModel;
public static class OpenZjsBackup {
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool OpenPrinter(string name,out IntPtr printer,IntPtr defaults);
    [DllImport("winspool.drv", SetLastError=true)] static extern bool ClosePrinter(IntPtr printer);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool GetPrinter(IntPtr printer,uint level,IntPtr data,uint size,out uint needed);
    static void Check(bool ok) { if(!ok) throw new Win32Exception(Marshal.GetLastWin32Error()); }
    public static void SaveGlobalMode(string name,string file) {
        IntPtr printer; Check(OpenPrinter(name,out printer,IntPtr.Zero)); IntPtr data=IntPtr.Zero;
        try {
            uint size; GetPrinter(printer,8,IntPtr.Zero,0,out size);
            if(size<72 || size>1048576) throw new Exception("Invalid printer data size");
            data=Marshal.AllocHGlobal((int)size); Check(GetPrinter(printer,8,data,size,out size));
            var mode=Marshal.ReadIntPtr(data); long offset=mode.ToInt64()-data.ToInt64();
            if(mode==IntPtr.Zero || offset<0 || offset>size-72) throw new Exception("Invalid DEVMODE pointer");
            int length=(ushort)Marshal.ReadInt16(mode,68)+(ushort)Marshal.ReadInt16(mode,70);
            if(length<220 || length>size-offset) throw new Exception("Invalid DEVMODE length");
            var bytes=new byte[length]; Marshal.Copy(mode,bytes,0,length); File.WriteAllBytes(file,bytes);
        } finally { if(data!=IntPtr.Zero) Marshal.FreeHGlobal(data); ClosePrinter(printer); }
    }
}
'@
}
[OpenZjsBackup]::SaveGlobalMode($QueueName,(Join-Path $destination 'global-devmode.bin'))
$registryBase = 'HKLM\SYSTEM\CurrentControlSet\Control\Print\'
$registryEntries = @{
    'driver.reg' = $registryBase + 'Environments\Windows x64\Drivers\Version-3\' + $driverName
    'diagnostic.reg' = $registryBase + 'Environments\Windows x64\Drivers\Version-3\' + $diagnosticName
    'monitor.reg' = $registryBase + 'Monitors\' + $monitorName
    'queue.reg' = $registryBase + 'Printers\' + $QueueName
}
foreach ($entry in $registryEntries.GetEnumerator()) {
    & reg.exe export $entry.Value (Join-Path $destination $entry.Key) | Out-Null
    if ($LASTEXITCODE) { throw 'Registration export failed.' }
}
foreach ($relative in @('scripts/install-dev.ps1','scripts/install-monitor-dev.ps1','scripts/resolve-usb-port.ps1','tests/vm/migration-native.ps1','tests/vm/migration-fixture.ps1','docs/DEVELOPMENT_MIGRATION.md')) {
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $recovery.FullName
}
Copy-Item -LiteralPath $VcRuntimeInstaller -Destination (Join-Path $recovery.FullName 'vc_redist.x64.exe')
if ((Get-FileHash $VcRuntimeInstaller).Hash -ne (Get-FileHash (Join-Path $recovery.FullName 'vc_redist.x64.exe')).Hash) { throw 'Runtime copy hash mismatch.' }
foreach ($entry in $manifest) {
    if ((Get-FileHash -LiteralPath $entry.originalPath).Hash -ne $entry.sha256) { throw 'A live file changed during capture.' }
}
$after = Get-Printer -Full | Select-Object Name,DriverName,PortName,Comment,Location,Priority,Shared,PermissionSDDL
if (($initial | Sort-Object Name | ConvertTo-Json -Compress) -ne ($after | Sort-Object Name | ConvertTo-Json -Compress) -or @(Get-PrintJob -PrinterName $QueueName).Count) { throw 'Printer state changed during capture; repeat after activity finishes.' }
[OpenZjsBackup]::SaveGlobalMode($QueueName,(Join-Path $destination 'global-devmode-after.bin'))
if ((Get-FileHash (Join-Path $destination 'global-devmode.bin')).Hash -ne (Get-FileHash (Join-Path $destination 'global-devmode-after.bin')).Hash) { throw 'Global settings changed during capture.' }
@'
PRIVATE RECOVERY DATA - DO NOT ATTACH TO ISSUES OR RELEASES.
Contains firmware, machine/account identifiers, printer permissions and registry exports.
This is a capture, not a validated automatic restore tool. Do not import .reg files blindly.
The saved development scripts expect their original build/layout; do not run them in this folder.
Use DEVELOPMENT_MIGRATION.md and a reviewed recovery procedure; VM fixtures are VM-only.
Copy this whole folder to an independent private backup location before migration.
Per-user defaults for other accounts, queued jobs, documents and application settings are not captured.
Recheck file hashes, no-job condition, current settings and candidate trust immediately before maintenance.
'@ | Set-Content (Join-Path $destination 'PRIVATE-READ-ME.txt')
$version = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
[ordered]@{status='captured';capturedUtc=[DateTime]::UtcNow.ToString('o');osBuild=$version.CurrentBuildNumber;osRevision=$version.UBR;globalDevModeSaved=$true;privateFiles=$manifest.Count;unchangedDuringCapture=$true;independentBackupConfirmed=$false;freshBundleRecoveryRehearsed=$false;physicalInstallationAuthorized=$false} | ConvertTo-Json | Set-Content (Join-Path $destination 'result.json')
Get-ChildItem -LiteralPath $destination -Recurse -File | ForEach-Object {
    [ordered]@{path=[IO.Path]::GetRelativePath($destination,$_.FullName);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
} | ConvertTo-Json | Set-Content (Join-Path $destination 'SHA256-MANIFEST.json')
Write-Output $destination
