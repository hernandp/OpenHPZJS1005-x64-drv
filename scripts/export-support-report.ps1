# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
#requires -Version 5.1
# Read-only support inventory. Never serialize raw printer, job or registry objects.
[CmdletBinding()]
param([string]$OutputPath)

function ConvertTo-ReportNumber($Value) {
    $number = 0L
    if ([long]::TryParse([string]$Value, [ref]$number) -and $number -ge 0) { return $number }
    return $null
}
function ConvertTo-ReportToken($Value, [string]$Pattern) {
    if ([string]$Value -cmatch $Pattern) { return [string]$Value }
    return $null
}
function New-OpenZjsSupportReport($Data) {
    # An explicit projection keeps future properties, paths and error messages private.
    [ordered]@{
        schemaVersion = 2
        generatedUtc = [DateTime]::UtcNow.ToString('o')
        os = [ordered]@{
            build = ConvertTo-ReportNumber $Data.os.CurrentBuildNumber
            revision = ConvertTo-ReportNumber $Data.os.UBR
            displayVersion = ConvertTo-ReportToken $Data.os.DisplayVersion '^\d{2}H[12]$'
            edition = ConvertTo-ReportToken $Data.os.EditionID '^(Core|CoreSingleLanguage|Professional|ProfessionalN|Enterprise|EnterpriseS|Education|EducationN)$'
            is64Bit = [bool]$Data.is64Bit
        }
        spooler = ConvertTo-ReportToken $Data.spooler '^(Running|Stopped|Paused|StartPending|StopPending|ContinuePending|PausePending)$'
        secureBoot = $(if ($Data.secureBoot -is [bool]) { $Data.secureBoot } else { $null })
        protectedPrintMode = $(if ($Data.protectedPrintMode -is [bool]) { $Data.protectedPrintMode } else { $null })
        queues = @($Data.queues | ForEach-Object {
            [ordered]@{
                # Ordinals identify queues only within this report; custom names are excluded.
                ordinal = ConvertTo-ReportNumber $_.ordinal
                portKind = ConvertTo-ReportToken $_.portKind '^(USB|FILE|other)$'
                statusCode = ConvertTo-ReportNumber $_.statusCode
                jobCount = ConvertTo-ReportNumber $_.jobCount
                paperSizeCode = ConvertTo-ReportNumber $_.paperSizeCode
                duplexCode = ConvertTo-ReportNumber $_.duplexCode
                color = $(if ($_.color -is [bool]) { $_.color } else { $null })
            }
        })
        files = @($Data.files | ForEach-Object {
            [ordered]@{
                role = ConvertTo-ReportToken $_.role '^(live|firmware)$'
                name = ConvertTo-ReportToken $_.name '^(lj1005\.(gpd|ini)|lj1005(render|monitor)\.dll|lj1005-firmware\.img)$'
                present = [bool]$_.present
                bytes = ConvertTo-ReportNumber $_.bytes
                sha256 = ConvertTo-ReportToken $_.sha256 '^[A-Fa-f0-9]{64}$'
                fileVersion = ConvertTo-ReportToken $_.fileVersion '^\d+\.\d+\.\d+\.\d+$'
            }
        })
        collectionErrors = @($Data.errors | ForEach-Object {
            [ordered]@{
                section = ConvertTo-ReportToken $_.section '^(os|spooler|secureBoot|protectedPrintMode|queues|files)$'
                hresult = ConvertTo-ReportToken $_.hresult '^0x[A-Fa-f0-9]{8}$'
            }
        })
    }
}
function Get-OpenZjsSupportData {
    $data = @{os=@{};is64Bit=[Environment]::Is64BitOperatingSystem;spooler=$null;secureBoot=$null;protectedPrintMode=$null;queues=@();files=@();errors=@()}
    function Add-CollectionError([string]$Section, $Failure) {
        $data.errors += @{section=$Section;hresult=('0x{0:X8}' -f $Failure.Exception.HResult)}
    }
    try { $data.os = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop } catch { Add-CollectionError 'os' $_ }
    try { $data.spooler = [string](Get-Service Spooler -ErrorAction Stop).Status } catch { Add-CollectionError 'spooler' $_ }
    try { $data.secureBoot = [bool](Confirm-SecureBootUEFI -ErrorAction Stop) } catch { Add-CollectionError 'secureBoot' $_ }
    try {
        $key = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Printers\WPP'
        $data.protectedPrintMode = $false
        if (Test-Path $key -ErrorAction Stop) {
            $wpp = Get-ItemProperty $key -ErrorAction Stop
            $data.protectedPrintMode = [bool]($wpp.WindowsProtectedPrintMode -or $wpp.WindowsProtectedPrintGroupPolicyState)
        }
    } catch { $data.protectedPrintMode = $null; Add-CollectionError 'protectedPrintMode' $_ }
    try {
        $ordinal = 0
        $driverNames = @('HP Laserjet 1005 (Project Benteveo Driver)', 'HP Laserjet 1005 (Benteveo Driver)')
        foreach ($queue in @(Get-Printer -ErrorAction Stop | Where-Object { $_.DriverName -in $driverNames })) {
            $ordinal++
            $item = @{ordinal=$ordinal;portKind='other';statusCode=[int]$queue.PrinterStatus;jobCount=$null;paperSizeCode=$null;duplexCode=$null;color=$null}
            if ($queue.PortName -match '^USB\d+$') { $item.portKind = 'USB' }
            elseif ($queue.PortName -eq 'FILE:') { $item.portKind = 'FILE' }
            try {
                # Job objects are immediately reduced to a count; no titles or owners leave this scope.
                $item.jobCount = @(Get-PrintJob -PrinterName $queue.Name -ErrorAction Stop).Count
                $config = Get-PrintConfiguration -PrinterName $queue.Name -ErrorAction Stop
                $item.paperSizeCode = [int]$config.PaperSize
                $item.duplexCode = [int]$config.DuplexingMode
                $item.color = [bool]$config.Color
            } catch { Add-CollectionError 'queues' $_ }
            $data.queues += $item
        }
    } catch { Add-CollectionError 'queues' $_ }
    $spool = Join-Path $env:windir 'System32\spool\drivers\x64\3'
    $files = @(
        @{role='firmware';path=(Join-Path $env:windir 'System32\lj1005-firmware.img')},
        @{role='live';path=(Join-Path $env:windir 'System32\lj1005monitor.dll')}
    )
    foreach ($name in @('lj1005.gpd','lj1005.ini','lj1005render.dll')) { $files += @{role='live';path=(Join-Path $spool $name)} }
    foreach ($file in $files) {
        $item = @{role=$file.role;name=[IO.Path]::GetFileName($file.path);present=$false;bytes=$null;sha256=$null;fileVersion=$null}
        try {
            if (Test-Path -LiteralPath $file.path -ErrorAction Stop) {
                $info = Get-Item -LiteralPath $file.path -ErrorAction Stop
                $item.present = $true
                $item.bytes = $info.Length
                $item.sha256 = (Get-FileHash -LiteralPath $file.path -Algorithm SHA256 -ErrorAction Stop).Hash
                if ($info.Extension -eq '.dll') { $item.fileVersion = $info.VersionInfo.FileVersion }
            }
        } catch { Add-CollectionError 'files' $_ }
        $data.files += $item
    }
    return $data
}
if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    if (!$OutputPath) { throw 'Specify -OutputPath for a new JSON file.' }
    if (![Environment]::Is64BitProcess) { throw 'Run the 64-bit Windows PowerShell to inspect the x64 driver.' }
    $report = New-OpenZjsSupportReport (Get-OpenZjsSupportData)
    $json = $report | ConvertTo-Json -Depth 8
    # CreateNew refuses to overwrite an existing report, even if another process creates it meanwhile.
    $stream = [IO.File]::Open([IO.Path]::GetFullPath($OutputPath),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    try {
        $writer = New-Object IO.StreamWriter($stream, (New-Object Text.UTF8Encoding($false)))
        try { $writer.WriteLine($json) } finally { $writer.Dispose() }
    } finally { $stream.Dispose() }
    Write-Output ([IO.Path]::GetFullPath($OutputPath))
}
