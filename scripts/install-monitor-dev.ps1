param([string]$FirmwarePath)
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run the development monitor installer as administrator.'
}
$projectRoot = Split-Path $PSScriptRoot -Parent
$sourceDll = Join-Path $projectRoot 'build\Release\lj1005monitor.dll'
$systemDirectory = [Environment]::SystemDirectory
$destinationDll = Join-Path $systemDirectory 'lj1005monitor.dll'
$destinationFirmware = Join-Path $systemDirectory 'lj1005-firmware.img'
$monitorName = 'HP LaserJet 1005 Firmware Monitor (development)'
$monitorKey = "HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\$monitorName"
$expectedFirmwareHash = '638B9AD4A2CF6888208926EF7C7DE073DA8BA4CDED25D31000A585C4526A9CA1'
if (!(Test-Path -LiteralPath $sourceDll -PathType Leaf)) { throw 'Build the monitor DLL first.' }
if (!$FirmwarePath) { $FirmwarePath = $destinationFirmware }
if ((Get-FileHash -LiteralPath $FirmwarePath -Algorithm SHA256).Hash -ne $expectedFirmwareHash) {
    throw 'Firmware is not the verified original sihp1005.img. No system files were changed.'
}
if (Test-Path -LiteralPath $monitorKey) {
    $registeredDll = (Get-ItemProperty -LiteralPath $monitorKey).Driver
    if ($registeredDll -ne 'lj1005monitor.dll') { throw 'Existing monitor registration points to a different DLL.' }
    if ((Get-FileHash -LiteralPath $sourceDll).Hash -ne (Get-FileHash -LiteralPath $destinationDll).Hash) {
        throw 'The installed monitor differs from this build. Stop and update it through a planned maintenance operation; this script does not restart the shared print spooler.'
    }
} elseif (Test-Path -LiteralPath $destinationDll) {
    if ((Get-FileHash -LiteralPath $sourceDll).Hash -ne (Get-FileHash -LiteralPath $destinationDll).Hash) {
        throw 'An unregistered DLL already occupies the destination. Refusing to overwrite it.'
    }
}
if (([IO.Path]::GetFullPath($FirmwarePath)) -ne $destinationFirmware) {
    Copy-Item -LiteralPath $FirmwarePath -Destination $destinationFirmware -Force
}
if (!(Test-Path -LiteralPath $monitorKey)) {
    Copy-Item -LiteralPath $sourceDll -Destination $destinationDll -Force
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Lj1005MonitorInstall {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct MONITOR_INFO_2 { public string pName, pEnvironment, pDLLName; }
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool AddMonitor(string server, uint level, ref MONITOR_INFO_2 info);
}
'@
    $info = [Lj1005MonitorInstall+MONITOR_INFO_2]::new()
    $info.pName = $monitorName
    $info.pEnvironment = 'Windows x64'
    $info.pDLLName = 'lj1005monitor.dll'
    if (![Lj1005MonitorInstall]::AddMonitor($null, 2, [ref]$info)) {
        throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
}
Write-Output "Installed monitor: $monitorName"
Write-Output "Verified private firmware: $destinationFirmware"
