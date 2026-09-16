param([string]$CompletedTestJobs = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
Start-Transcript -Path (Join-Path $artifacts 'update-monitor-dev.log') -Force
try {
    if ($CompletedTestJobs -and $CompletedTestJobs -notmatch '^\d+(,\d+)*$') { throw 'Expected comma-separated decimal job IDs.' }
    [int[]]$jobIds = @()
    if ($CompletedTestJobs) { $jobIds = @($CompletedTestJobs.Split(',') | ForEach-Object { [int]$_ }) }
    & (Join-Path $PSScriptRoot 'update-monitor-dev.ps1') -CompletedTestJobIds $jobIds
} catch {
    $_ | Format-List * -Force
    exit 1
} finally { Stop-Transcript }
