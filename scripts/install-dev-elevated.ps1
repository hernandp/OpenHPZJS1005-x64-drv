param([switch]$ConfigOnlyDiagnostic, [switch]$ConnectUsb, [switch]$WithFirmwareMonitor, [string]$FirmwarePath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot 'artifacts') | Out-Null
Start-Transcript -Path (Join-Path $projectRoot 'artifacts\install-dev.log') -Force
try {
    & (Join-Path $PSScriptRoot 'install-dev.ps1') -ConfigOnlyDiagnostic:$ConfigOnlyDiagnostic -ConnectUsb:$ConnectUsb -WithFirmwareMonitor:$WithFirmwareMonitor -FirmwarePath $FirmwarePath
    if (!$?) { exit 1 }
} catch {
    $_ | Format-List * -Force
    $_.Exception | Format-List * -Force
    exit 1
} finally { Stop-Transcript }
