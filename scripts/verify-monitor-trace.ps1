# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
# Captures only the simulated monitor; does not open a physical printer or submit a job.
$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'ETW session control requires an administrator shell.'
}
$projectRoot = Split-Path $PSScriptRoot -Parent
$session = 'HPLJ1005-Fixture-' + [guid]::NewGuid().ToString('N')
$tracePath = Join-Path $projectRoot "artifacts\$session.etl"
$xmlPath = [IO.Path]::ChangeExtension($tracePath, '.xml')
& logman.exe create trace $session -p '{e8d2763b-f16e-4b57-b14c-c637f7915011}' 3 5 -o $tracePath -f bincirc -max 16 -ets
if ($LASTEXITCODE) { throw 'Could not start the fixture ETW session.' }
try {
    & (Join-Path $projectRoot 'build\Release\monitor_tests.exe') (Join-Path $projectRoot 'build\Release\monitor_fixture.dll')
    if ($LASTEXITCODE) { throw 'Monitor fixture failed during tracing.' }
} finally {
    & logman.exe stop $session -ets
    if ($LASTEXITCODE) { throw "Failed to stop ETW session $session; stop it explicitly with logman." }
}
& tracerpt.exe $tracePath -o $xmlPath -of XML -y
if ($LASTEXITCODE) { throw 'Could not decode the fixture trace.' }
$xml = Get-Content -LiteralPath $xmlPath -Raw
foreach ($stage in @('FirmwareReady','AwaitCompletion','PrinterEnd','CompletionAcknowledged','CompletionFailure','QueueCancelled')) {
    if (!$xml.Contains($stage)) { throw "Decoded ETW trace is missing $stage." }
}
if (!$xml.Contains('PrinterStatus')) { throw 'Decoded ETW trace has no printer status bytes.' }
Write-Output "Verified completion, cancellation, transport failure and status events in $xmlPath"
