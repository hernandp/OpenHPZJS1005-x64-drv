#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Destination,
    [string]$PackagePath
)
$ErrorActionPreference = 'Stop'
# Match the WDK tools used by the accepted engineering candidates. Extracting
# this package does not install a driver, SDK, certificate or machine component.
$version = '10.0.26100.6584'
$expectedHash = 'C393D03DFB640B5C92F546B32F6770EF68CD3AAF691956E7D66D8E2C28A1B55E'
if (Test-Path -LiteralPath $Destination) { throw 'Use a new WDK extraction directory.' }
New-Item -ItemType Directory -Path $Destination | Out-Null
if (!$PackagePath) {
    $PackagePath = Join-Path $Destination 'wdk.nupkg'
    Invoke-WebRequest -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.x64/$version/microsoft.windows.wdk.x64.$version.nupkg" -OutFile $PackagePath
}
if ((Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'WDK package hash differs from the reviewed version.'
}
$extracted = Join-Path $Destination 'extracted'
[IO.Compression.ZipFile]::ExtractToDirectory((Resolve-Path -LiteralPath $PackagePath).Path, $extracted)
$wdk = Join-Path $extracted 'c'
foreach ($relative in @('tools\10.0.26100.0\x64\infverif.exe',
                        'tools\10.0.26100.0\x64\gpdcheck.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $wdk $relative) -PathType Leaf)) {
        throw "Missing WDK tool: $relative"
    }
}
Write-Output ([IO.Path]::GetFullPath($wdk))
