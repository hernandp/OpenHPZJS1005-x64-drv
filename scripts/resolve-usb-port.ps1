# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$deviceOutput = @(& (Join-Path $projectRoot 'build\Release\lj1005.exe') usb-list)
if ($LASTEXITCODE -ne 0) { throw 'The original LaserJet 1005 must be connected.' }
$instances = @($deviceOutput | Where-Object { $_ -match '^USB\\VID_03F0&PID_1317\\' })
if ($instances.Count -ne 1) { throw 'Expected exactly one connected original LaserJet 1005.' }
# Windows retains USB port registrations when a printer moves between physical sockets.
# Match the currently present device instance, not all historical HP registrations.
$ports = @(Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\USB Monitor\Ports' | Where-Object {
    (Get-ItemProperty -LiteralPath $_.PSPath).'Device Id' -eq $instances[0]
})
if ($ports.Count -ne 1) { throw 'Expected exactly one Windows USB port for the connected LaserJet 1005 instance.' }
$ports[0].PSChildName
