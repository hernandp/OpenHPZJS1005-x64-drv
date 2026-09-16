param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
if (!$OutputPath) { $OutputPath = Join-Path $artifacts 'HPLJ1005x64-0.1.0-development.zip' }
$staging = Join-Path $artifacts ('package-' + [guid]::NewGuid().ToString('N'))
$bundle = Join-Path $staging 'HPLJ1005x64'
New-Item -ItemType Directory -Force -Path (Join-Path $bundle 'build\Release') | Out-Null
# An explicit allowlist prevents bundling private HP firmware, old driver binaries,
# downloaded Windows/WDK components, local captures, or the optional upstream checkout.
foreach ($directory in @('driver','include','src','scripts','tests','vendor','docs')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $directory) -Destination $bundle -Recurse
}
foreach ($file in @('CMakeLists.txt','README.md','LICENSE','THIRD_PARTY.md','.gitignore','.gitattributes','.clang-format')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination $bundle
}
$binaries = @('lj1005.exe','lj1005render.dll','lj1005monitor.dll','queue_test.exe',
              'protocol_tests.exe','firmware_tests.exe','pjl_tests.exe','render_smoke.exe',
              'monitor_tests.exe','monitor_fixture.dll')
foreach ($file in $binaries) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "build\Release\$file") -Destination (Join-Path $bundle 'build\Release')
}
foreach ($file in @('lj1005.inf','lj1005.gpd','lj1005.ini')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "driver\$file") -Destination (Join-Path $bundle 'build\Release')
}
$manifest = Get-ChildItem -LiteralPath $bundle -File -Recurse | ForEach-Object {
    [pscustomobject]@{
        path = [IO.Path]::GetRelativePath($bundle, $_.FullName).Replace('\','/')
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        bytes = $_.Length
    }
}
$manifest | Sort-Object path | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $bundle 'SHA256-MANIFEST.json') -Encoding utf8
Compress-Archive -LiteralPath $bundle -DestinationPath $OutputPath -Force
Get-Item -LiteralPath $OutputPath | Select-Object FullName, Length
Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256
