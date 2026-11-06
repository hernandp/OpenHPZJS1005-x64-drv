param([string]$QueueName = 'HP LaserJet 1005 Series (Open ZJS)', [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$outputDirectory = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$test = Join-Path $projectRoot 'build\Release\queue_test.exe'
$cli = Join-Path $projectRoot 'build\Release\lj1005.exe'
foreach ($scenario in @('portrait','landscape','blank','multi','letter')) {
    $capture = Join-Path $outputDirectory "full-$scenario.zjs"
    $decoded = Join-Path $outputDirectory "full-$scenario.pbm"
    # An explicit output file is essential: never pass '-' in this offline test.
    & $test $QueueName $capture $scenario
    if ($LASTEXITCODE -ne 0) { throw "GDI capture failed: $scenario" }
    & $cli decode $capture $decoded
    if ($LASTEXITCODE -ne 0) { throw "Capture decode failed: $scenario" }
}
& $Python (Join-Path $projectRoot 'tests\verify_captures.py')
if ($LASTEXITCODE -ne 0) { throw 'Capture verification failed' }
