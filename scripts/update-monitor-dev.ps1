param([int[]]$CompletedTestJobIds = @())
$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this monitor maintenance operation as administrator.'
}
$projectRoot = Split-Path $PSScriptRoot -Parent
$source = Join-Path $projectRoot 'build\Release\lj1005monitor.dll'
$destination = Join-Path ([Environment]::SystemDirectory) 'lj1005monitor.dll'
$queue = 'HP LaserJet 1005 Open ZJS (development)'
$monitorKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\HP LaserJet 1005 Firmware Monitor (development)'
if ((Get-ItemProperty -LiteralPath $monitorKey).Driver -ne 'lj1005monitor.dll') {
    throw 'The expected development monitor is not registered.'
}
$expected = (Get-FileHash -LiteralPath $source).Hash
if ($expected -eq (Get-FileHash -LiteralPath $destination).Hash) { Write-Output 'The installed monitor already matches this build.'; exit 0 }
# Only explicitly named, fully transmitted integration-test entries may be removed.
# Physical print confirmation is tracked separately in docs/TEST_STATUS.md.
$cleanup = @()
foreach ($printer in Get-Printer) {
    foreach ($job in @(Get-PrintJob -PrinterName $printer.Name)) {
        if ($printer.Name -ne $queue -or $job.ID -notin $CompletedTestJobIds -or
            $job.DocumentName -ne 'LaserJet 1005 GDI integration test' -or
            ([int]$job.JobStatus -band 0x1000) -eq 0) {
            throw 'A print job outside the explicitly selected completed tests exists. Spooler maintenance was not started.'
        }
        $cleanup += $job
    }
}
$service = Get-Service Spooler
if ($service.Status -ne 'Running') { throw 'The spooler must be running before this maintenance operation.' }
if (@($service.DependentServices | Where-Object Status -eq 'Running').Count) {
    throw 'A dependent service is running. This script will not stop other services.'
}
$artifacts = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$backup = Join-Path $artifacts ('lj1005monitor-before-' + [guid]::NewGuid().ToString('N') + '.dll')
Copy-Item -LiteralPath $destination -Destination $backup
foreach ($job in $cleanup) { Remove-PrintJob -PrinterName $queue -ID $job.ID }
# Recheck immediately before maintenance; a newly submitted job cancels the operation.
foreach ($printer in Get-Printer) {
    if (@(Get-PrintJob -PrinterName $printer.Name).Count) { throw 'A new print job arrived. Spooler maintenance was not started.' }
}
$spoolerProcessId = (Get-CimInstance Win32_Service -Filter "Name='Spooler'").ProcessId
Stop-Service Spooler -ErrorAction Stop
try {
    # SERVICE_STOPPED can be reported before the service process releases its DLL mappings.
    $spoolerProcess = Get-Process -Id $spoolerProcessId -ErrorAction SilentlyContinue
    if ($spoolerProcess -and !$spoolerProcess.WaitForExit(10000)) { throw 'The stopped spooler process did not exit within 10 seconds.' }
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try { Copy-Item -LiteralPath $source -Destination $destination -Force; break }
        catch [System.IO.IOException] {
            $code = $_.Exception.HResult -band 0xffff
            if ($code -notin @(32,33) -or $attempt -eq 19) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
    if ((Get-FileHash -LiteralPath $destination).Hash -ne $expected) { throw 'Installed DLL hash mismatch.' }
} catch {
    if ((Get-FileHash -LiteralPath $backup).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
        Copy-Item -LiteralPath $backup -Destination $destination -Force
    }
    throw
} finally {
    Start-Service Spooler -ErrorAction Stop
}
Write-Output "Updated monitor SHA256: $expected"
Write-Output "Previous DLL retained at: $backup"
Get-Printer -Name $queue | Select-Object Name,PortName,DriverName | ConvertTo-Json -Compress
