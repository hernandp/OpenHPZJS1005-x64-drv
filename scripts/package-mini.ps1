# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
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
# The existing verifier checks both ZIP manifests, source provenance, payload
# allowlists and production-only binaries. Extract these verified ZIPs, not loose files.
& $PythonExe (Join-Path $PSScriptRoot 'verify-candidate.py') $candidate
if ($LASTEXITCODE) { throw 'Unsigned input candidate failed verification.' }
$cert = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
if (!$cert.HasPrivateKey -or $cert.Subject -ne 'CN=OpenZJS Technical Tester Signing' -or
    $cert.Subject -ne $cert.Issuer -or $cert.NotBefore -gt (Get-Date) -or $cert.NotAfter -le (Get-Date)) {
    throw 'Expected the valid, dedicated self-signed tester identity with its private key.'
}
$usages = @($cert.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' } |
    ForEach-Object { $_.EnhancedKeyUsages } | ForEach-Object { $_.Value })
if ('1.3.6.1.5.5.7.3.3' -notin $usages) {
    throw 'Certificate must have code-signing usage.'
}
if (!(Test-Path -LiteralPath $SignTool -PathType Leaf)) { throw 'SignTool is missing.' }
$unsignedZip = @(Get-ChildItem -LiteralPath $candidate -Filter '*-unsigned-candidate.zip')
$sourceZip = @(Get-ChildItem -LiteralPath $candidate -Filter '*-source.zip')
if ($unsignedZip.Count -ne 1 -or $sourceZip.Count -ne 1) { throw 'Expected one candidate/source pair.' }
$out = Join-Path $project ('artifacts\mini-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $out | Out-Null
Expand-Archive -LiteralPath $unsignedZip[0].FullName -DestinationPath $out
$inputBundle = Join-Path $out 'HPLJ1005x64'
$info = Get-Content -LiteralPath (Join-Path $inputBundle 'BUILD-INFO.json') -Raw | ConvertFrom-Json -DateKind String
$bundle = Join-Path $out 'HPLJ1005-mini'
$logs = Join-Path $out 'validation'
New-Item -ItemType Directory -Path $bundle, $logs | Out-Null
$plugins = @('lj1005render.dll','lj1005monitor.dll')
if (Test-Path -LiteralPath (Join-Path $inputBundle 'driver\lj1005ui.dll')) { $plugins += 'lj1005ui.dll' }
foreach ($name in @('lj1005.inf','lj1005.gpd','lj1005.ini') + $plugins) {
    Copy-Item -LiteralPath (Join-Path $inputBundle "driver\$name") -Destination $bundle
}
foreach ($name in @('LICENSE','THIRD_PARTY.md','README.md')) {
    Copy-Item -LiteralPath (Join-Path $inputBundle $name) -Destination $bundle
}
Export-Certificate -Cert $cert -FilePath (Join-Path $bundle 'TEST-CERTIFICATE.cer') -Type CERT | Out-Null
$infPath = Join-Path $bundle 'lj1005.inf'
function Run-Tool([string]$Exe, [string[]]$Arguments, [string]$Log) {
    & $Exe @Arguments *> (Join-Path $logs $Log)
    if ($LASTEXITCODE) { throw "$Log failed; see $logs" }
}
function Sign-File([string]$Path, [string]$Log) {
    # No timestamp for this rehearsal build; expiry is explicitly disclosed.
    Run-Tool $SignTool @('sign', '/s', 'My', '/sha1', $CertificateThumbprint,
        '/fd', 'SHA256', '/v', $Path) $Log
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if (!$signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) {
        throw "Unexpected signer: $Path"
    }
    if ($signature.Status -notin @('Valid', 'NotTrusted', 'UnknownError')) {
        throw "Signature creation failed: $Path ($($signature.Status))"
    }
    # Host trust is deliberately not added. Full Windows trust verification is
    # a separate gate performed by the user in the disposable VM.
    [ordered]@{ file = [IO.Path]::GetRelativePath($bundle, $Path).Replace('\', '/')
        status = [string]$signature.Status; statusMessage = $signature.StatusMessage
        signerThumbprintSha1 = $signature.SignerCertificate.Thumbprint }
}
$signatureResults = foreach ($name in $plugins) {
    Sign-File (Join-Path $bundle $name) "$name-sign.log"
}
$signatureResults | ConvertTo-Json | Set-Content (Join-Path $logs 'signed-dlls.json')
$temporaryFirmware = Join-Path $bundle 'sihp1005.img'
try {
    Copy-Item -LiteralPath $firmware.FullName -Destination $temporaryFirmware
    if ((Get-FileHash -LiteralPath $temporaryFirmware).Hash -ne $expectedHash) {
        throw 'The staged firmware differs from the validated image.'
    }
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
$signing = [ordered]@{
    subject = $cert.Subject
    thumbprintSha1 = $cert.Thumbprint
    certificateFileSha256 = (Get-FileHash -LiteralPath (Join-Path $bundle 'TEST-CERTIFICATE.cer')).Hash.ToLowerInvariant()
    notBeforeUtc = $cert.NotBefore.ToUniversalTime().ToString('o')
    notAfterUtc = $cert.NotAfter.ToUniversalTime().ToString('o')
    fileDigestAlgorithm = 'SHA256'
    timestamped = $false
    catalogCmsSignatureVerified = $true
    windowsTrustAndCatalogMembershipRehearsed = $false
    hostTrustAdded = $false
    vmRehearsal = 'pending'
    publication = 'not-published'
}
$info | Add-Member -NotePropertyName signing -NotePropertyValue $signing
$info | Add-Member -NotePropertyName requiredFirmware -NotePropertyValue ([ordered]@{
    file = 'sihp1005.img'; bytes = 116892; sha256 = $expectedHash; included = $false
    installedFile = '%SystemRoot%\System32\lj1005-firmware.img'
})
$info | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $bundle 'BUILD-INFO.json') -Encoding utf8
$entries = Get-ChildItem -LiteralPath $bundle -File | ForEach-Object {
    [ordered]@{ path = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() }
}
$entries | Sort-Object { $_.path } | ConvertTo-Json | Set-Content (Join-Path $bundle 'SHA256-MANIFEST.json') -Encoding utf8
$archive = Join-Path $out "Benteveo-HPLJ1005-x64-$($info.version)-mini-self-signed-test.zip"
Compress-Archive -LiteralPath $bundle -DestinationPath $archive
Copy-Item -LiteralPath $sourceZip[0].FullName -Destination $out
Get-FileHash -LiteralPath $archive, (Join-Path $out $info.sourceArchive) | ForEach-Object {
    $_.Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_.Path)
} | Set-Content (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
& $PythonExe (Join-Path $PSScriptRoot 'verify-mini-package.py') $out $candidate
if ($LASTEXITCODE) { throw 'Mini ZIP verification failed.' }
Write-Host "Mini tester package: $out"
Write-Host 'Firmware excluded. Fresh-VM wizard acceptance pending. Not published.'
if ($OutputPathFile) { $out | Set-Content -LiteralPath $OutputPathFile -Encoding utf8 }
