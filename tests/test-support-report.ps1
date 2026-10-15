#requires -Version 5.1
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/export-support-report.ps1')
$secret = 'PRIVATE-CANARY-ACCOUNT-DOCUMENT-PATH'
$raw = @{
    os=@{CurrentBuildNumber='26200';UBR=100;DisplayVersion='25H2';EditionID='Professional';RegisteredOwner=$secret}
    is64Bit=$true;spooler='Running';secureBoot=$true;protectedPrintMode=$false
    installation=@{Status='Complete';User=$secret};update=@{Phase='Available';BackupPath=$secret}
    queues=@(@{ordinal=1;portKind='USB';statusCode=0;jobCount=2;paperSizeCode=9;duplexCode=0;color=$false;Name=$secret;Jobs=@(@{Document=$secret;User=$secret})})
    files=@(@{role='live';name='lj1005render.dll';present=$true;bytes=123;sha256=('A'*64);fileVersion='0.1.1.0';path=$secret;contents=$secret})
    errors=@(@{section='queues';hresult='0x80070005';message=$secret})
}
$report = New-OpenZjsSupportReport $raw
$json = $report | ConvertTo-Json -Depth 8
if ($json.Contains($secret)) { throw 'Sensitive raw properties leaked.' }
if ($report.os.build -ne 26200 -or $report.queues[0].jobCount -ne 2 -or $report.files[0].sha256 -ne ('A'*64)) { throw 'Useful diagnostic fields were lost.' }
$raw.installation.Status = 'NeedsRemoval'
$raw.update.Phase = 'RollbackComplete'
$recovery = New-OpenZjsSupportReport $raw
if ($recovery.installation.status -ne 'NeedsRemoval' -or $recovery.installation.updatePhase -ne 'RollbackComplete') { throw 'Recovery states were lost.' }
foreach ($phase in @('UpgradeRestoringQueue','RollbackRestoringQueue')) {
    $raw.update.Phase = $phase
    if ((New-OpenZjsSupportReport $raw).installation.updatePhase -ne $phase) { throw 'Queue restoration phase was lost.' }
}
$raw.os.CurrentBuildNumber = $secret
$raw.installation.Status = $secret
$raw.files[0].fileVersion = $secret
$raw.files[0].name = $secret
$raw.queues[0].portKind = $secret
$raw.errors[0].hresult = $secret
$json = New-OpenZjsSupportReport $raw | ConvertTo-Json -Depth 8
if ($json.Contains($secret)) { throw 'Invalid values in permitted fields leaked.' }
$empty = New-OpenZjsSupportReport @{os=@{};installation=@{};update=@{};queues=@();files=@();errors=@()}
if ($empty.queues.Count -ne 0 -or $null -ne $empty.os.build -or $null -ne $empty.secureBoot) { throw 'Missing collection results were treated as success.' }
Write-Output 'Support report projection checks passed: useful fields, privacy canaries, invalid values, missing data.'
