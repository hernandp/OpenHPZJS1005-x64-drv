#requires -Version 7.5
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CandidatePath,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{40}$')][string]$CertificateThumbprint,
    [string]$FirmwarePath,
    [string]$PythonExe = 'python',
    [string]$WdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10",
    [string]$SignTool = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe",
    [string]$OutputPathFile
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = Split-Path $PSScriptRoot -Parent
if (!$FirmwarePath) { $FirmwarePath = Read-Host 'Path to your original sihp1005.img (used locally; omitted from ZIP)' }
$firmware = Get-Item -LiteralPath $FirmwarePath
$expectedHash = '638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1'
if ($firmware.Length -ne 116892 -or (Get-FileHash -LiteralPath $firmware.FullName).Hash -ne $expectedHash) {
    throw 'Firmware must be the original 116892-byte sihp1005.img with the documented SHA-256.'
}
$candidate = (Resolve-Path -LiteralPath $CandidatePath).Path
$out = Join-Path $project ('artifacts\mini-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $out | Out-Null
# Reuse the established source, binary and dedicated tester-signing checks.
$pointer = Join-Path $out 'signed-input-path.txt'
& (Join-Path $PSScriptRoot 'package-test-signed.ps1') -CandidatePath $candidate `
    -CertificateThumbprint $CertificateThumbprint -PythonExe $PythonExe -WdkRoot $WdkRoot -SignTool $SignTool -OutputPathFile $pointer
$signedInput = (Get-Content -LiteralPath $pointer -Raw).Trim()
$inputBundle = Join-Path $signedInput 'HPLJ1005x64'
$info = Get-Content -LiteralPath (Join-Path $inputBundle 'BUILD-INFO.json') -Raw | ConvertFrom-Json -DateKind String
$bundle = Join-Path $out 'HPLJ1005-mini'
$logs = Join-Path $out 'validation'
New-Item -ItemType Directory -Path $bundle, $logs | Out-Null
foreach ($name in @('lj1005.inf','lj1005.gpd','lj1005.ini','lj1005render.dll','lj1005monitor.dll')) {
    Copy-Item -LiteralPath (Join-Path $inputBundle "driver\$name") -Destination $bundle
}
foreach ($name in @('LICENSE','THIRD_PARTY.md','TEST-CERTIFICATE.cer')) {
    Copy-Item -LiteralPath (Join-Path $inputBundle $name) -Destination $bundle
}
Copy-Item -LiteralPath (Join-Path $signedInput 'snapshot\source\docs\MINI_INSTALL.md') `
    -Destination (Join-Path $bundle 'README.md')
$infPath = Join-Path $bundle 'lj1005.inf'
$inf = Get-Content -LiteralPath $infPath -Raw
if ([regex]::Matches($inf, '(?m)^CopyFiles=DriverFiles,MonitorFiles\r?$').Count -ne 1) {
    throw 'Unexpected source INF layout.'
}
$inf = $inf.Replace('CopyFiles=DriverFiles,MonitorFiles', 'CopyFiles=DriverFiles,MonitorFiles,FirmwareFiles')
$inf = $inf.Replace('[DestinationDirs]', "[FirmwareFiles]`r`nlj1005-firmware.img,sihp1005.img`r`n`r`n[DestinationDirs]`r`nFirmwareFiles=11")
$inf = $inf.Replace('[SourceDisksFiles]', "[SourceDisksFiles]`r`nsihp1005.img=1")
[IO.File]::WriteAllText($infPath, $inf, [Text.Encoding]::ASCII)
function Run-Tool([string]$Exe, [string[]]$Arguments, [string]$Log) {
    & $Exe @Arguments *> (Join-Path $logs $Log)
    if ($LASTEXITCODE) { throw "$Log failed; see $logs" }
}
$temporaryFirmware = Join-Path $bundle 'sihp1005.img'
try {
    Copy-Item -LiteralPath $firmware.FullName -Destination $temporaryFirmware
    Run-Tool (Join-Path $WdkRoot "tools\$($info.wdkVersion)\x64\infverif.exe") @('/v', $infPath) 'infverif.log'
    Run-Tool (Join-Path $WdkRoot "bin\$($info.wdkVersion)\x86\Inf2Cat.exe") `
        @("/driver:$bundle", "/os:$($info.catalogOS)", '/verbose') 'inf2cat.log'
    Run-Tool $signTool @('sign','/s','My','/sha1',$CertificateThumbprint,'/fd','SHA256',
        (Join-Path $bundle 'lj1005.cat')) 'catalog-sign.log'
    Add-Type -AssemblyName System.Security.Cryptography.Pkcs
    $cms = [Security.Cryptography.Pkcs.SignedCms]::new()
    $cms.Decode([IO.File]::ReadAllBytes((Join-Path $bundle 'lj1005.cat')))
    $cms.CheckSignature($true)
    if ($cms.SignerInfos.Count -ne 1 -or $cms.SignerInfos[0].Certificate.Thumbprint -ne $CertificateThumbprint) {
        throw 'Unexpected mini catalog signer.'
    }
} finally {
    # Only this newly created, exact staging file; never delete the supplied image.
    if (Test-Path -LiteralPath $temporaryFirmware) { Remove-Item -LiteralPath $temporaryFirmware -Force }
}
$info.packageState = 'mini-self-signed-test-rehearsal'
$info | Add-Member -NotePropertyName requiredFirmware -NotePropertyValue ([ordered]@{
    file = 'sihp1005.img'; bytes = 116892; sha256 = $expectedHash; included = $false
    installedFile = '%SystemRoot%\System32\lj1005-firmware.img'
})
$info | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $bundle 'BUILD-INFO.json') -Encoding utf8
$entries = Get-ChildItem -LiteralPath $bundle -File | ForEach-Object {
    [ordered]@{ path = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() }
}
$entries | Sort-Object { $_.path } | ConvertTo-Json | Set-Content (Join-Path $bundle 'SHA256-MANIFEST.json') -Encoding utf8
$archive = Join-Path $out "HPLJ1005x64-$($info.version)-mini-self-signed-test.zip"
Compress-Archive -LiteralPath $bundle -DestinationPath $archive
Copy-Item -LiteralPath (Join-Path $signedInput $info.sourceArchive) -Destination $out
Get-FileHash -LiteralPath $archive, (Join-Path $out $info.sourceArchive) | ForEach-Object {
    $_.Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_.Path)
} | Set-Content (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
& $PythonExe (Join-Path $PSScriptRoot 'verify-mini-package.py') $out $signedInput $candidate
if ($LASTEXITCODE) { throw 'Mini ZIP verification failed.' }
Write-Host "Mini tester package: $out"
Write-Host 'Firmware excluded. Fresh-VM wizard acceptance pending. Not published.'
if ($OutputPathFile) { $out | Set-Content -LiteralPath $OutputPathFile -Encoding utf8 }
