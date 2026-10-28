#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CandidatePath,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{40}$')][string]$CertificateThumbprint,
    [string]$PythonExe = 'python',
    [string]$WdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10",
    [string]$SignTool = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe",
    [string]$OutputPathFile
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = Split-Path $PSScriptRoot -Parent
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
$out = Join-Path $project ('artifacts\tester-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $out | Out-Null
$logs = New-Item -ItemType Directory -Path (Join-Path $out 'validation')
Expand-Archive -LiteralPath $unsignedZip[0].FullName -DestinationPath $out
Expand-Archive -LiteralPath $sourceZip[0].FullName -DestinationPath (Join-Path $out 'snapshot')
$bundle = Join-Path $out 'HPLJ1005x64'
$source = Join-Path $out 'snapshot\source'
$info = Get-Content -LiteralPath (Join-Path $bundle 'BUILD-INFO.json') -Raw | ConvertFrom-Json
$inf2cat = Join-Path $WdkRoot "bin\$($info.wdkVersion)\x86\Inf2Cat.exe"
if (!(Test-Path -LiteralPath $inf2cat -PathType Leaf)) { throw 'Matching Inf2Cat is missing.' }

function Run-Tool([string]$Exe, [string[]]$ToolArgs, [string]$Log) {
    & $Exe @ToolArgs *> (Join-Path $logs.FullName $Log)
    if ($LASTEXITCODE) { throw "$Log failed; see $($logs.FullName)." }
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
$signatureResults = @()
foreach ($relative in @('Setup.exe','tools\lj1005.exe','driver\lj1005render.dll','driver\lj1005monitor.dll')) {
    $signatureResults += Sign-File (Join-Path $bundle $relative) ((Split-Path $relative -Leaf) + '-sign.log')
}
Run-Tool $inf2cat @("/driver:$bundle\driver", "/os:$($info.catalogOS)", '/verbose') 'signed-inf2cat.log'
$signatureResults += Sign-File (Join-Path $bundle 'driver\lj1005.cat') 'catalog-sign.log'
$signatureResults | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $logs.FullName 'host-signatures.json')

# Verify the catalog CMS signature cryptographically without granting host trust.
Add-Type -AssemblyName System.Security.Cryptography.Pkcs
$cms = [Security.Cryptography.Pkcs.SignedCms]::new()
$cms.Decode([IO.File]::ReadAllBytes((Join-Path $bundle 'driver\lj1005.cat')))
$cms.CheckSignature($true)
if ($cms.SignerInfos.Count -ne 1 -or $cms.SignerInfos[0].Certificate.Thumbprint -ne $CertificateThumbprint) {
    throw 'Unexpected catalog CMS signer.'
}
$publicPath = Join-Path $bundle 'TEST-CERTIFICATE.cer'
Export-Certificate -Cert $cert -FilePath $publicPath -Type CERT | Out-Null
$signing = [ordered]@{
    subject = $cert.Subject
    thumbprintSha1 = $cert.Thumbprint
    certificateFileSha256 = (Get-FileHash -LiteralPath $publicPath).Hash.ToLowerInvariant()
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
$signing | ConvertTo-Json | Set-Content (Join-Path $bundle 'SIGNING-INFO.json') -Encoding utf8
$docs = New-Item -ItemType Directory -Path (Join-Path $bundle 'docs')
foreach ($name in @('TESTER_RELEASE.md','VM_TESTER_REHEARSAL.md','USER_GUIDE.md',
        'RELEASE_ACCEPTANCE.md','DEVELOPMENT_MIGRATION.md','UPGRADE_ROLLBACK.md','TEST_STATUS.md')) {
    Copy-Item -LiteralPath (Join-Path $source "docs\$name") -Destination $docs.FullName
}
@"
# SELF-SIGNED TEST BUILD - NOT A PUBLICLY TRUSTED RELEASE

Version: $($info.version). This local rehearsal package is NOT approved for publication.
Read docs/TESTER_RELEASE.md before deciding to trust its certificate.
For the manual VM rehearsal use docs/VM_TESTER_REHEARSAL.md.

Certificate subject: $($cert.Subject)
SHA-1 certificate thumbprint (identifier, not the signature hash): $($cert.Thumbprint)
Public .cer SHA-256: $($signing.certificateFileSha256)
Valid until (UTC): $($signing.notAfterUtc)
File signatures use SHA-256. No timestamp was applied. Do not assume validity after expiry.
Setup, diagnostic EXE, renderer, monitor and catalog carry this test signature.
Full Windows trust and catalog membership acceptance in a fresh VM is PENDING.

You must explicitly import the public certificate on the test computer.
Setup does not add trust, disable signature enforcement or change Secure Boot.
The private key is never included. This is not Microsoft certification or public trust.
Firmware is user-supplied; no HP firmware or Windows binaries are included.

The current scope is one original LaserJet 1005 USB printer, Windows 11 Pro 25H2 x64,
A4 monochrome simplex. Physical packaged-driver acceptance remains pending.
Edge precise-size output is a known limitation; Office is not accepted.
Source revision: $($info.baseCommit); modified source snapshot: $($info.workingTreeModified).
Preserve the matching source ZIP and this complete package for recovery.
Keep the release's separately published certificate identity and archive checksums.
"@ | Set-Content -LiteralPath (Join-Path $bundle 'README.md') -Encoding utf8
$info.packageState = 'self-signed-test-rehearsal'
$info | Add-Member -NotePropertyName signing -NotePropertyValue $signing
$info | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $bundle 'BUILD-INFO.json') -Encoding utf8
$entries = Get-ChildItem -LiteralPath $bundle -Recurse -File |
    Where-Object Name -ne 'SHA256-MANIFEST.json' | ForEach-Object {
        [ordered]@{ path = [IO.Path]::GetRelativePath($bundle, $_.FullName).Replace('\', '/')
            sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant(); bytes = $_.Length }
    }
$entries | Sort-Object { $_.path } | ConvertTo-Json -Depth 3 |
    Set-Content (Join-Path $bundle 'SHA256-MANIFEST.json') -Encoding utf8
$archive = Join-Path $out "HPLJ1005x64-$($info.version)-self-signed-test.zip"
Compress-Archive -LiteralPath $bundle -DestinationPath $archive
Copy-Item -LiteralPath $sourceZip[0].FullName -Destination $out
Copy-Item -LiteralPath $publicPath -Destination $out
Copy-Item -LiteralPath (Join-Path $bundle 'SIGNING-INFO.json') -Destination $out
Get-FileHash -LiteralPath $archive, (Join-Path $out $sourceZip[0].Name), (Join-Path $out 'TEST-CERTIFICATE.cer') |
    ForEach-Object { $_.Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_.Path) } |
    Set-Content (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
& $PythonExe (Join-Path $PSScriptRoot 'verify-test-package.py') $out $candidate
if ($LASTEXITCODE) { throw 'Signed rehearsal archive failed independent verification.' }
Write-Host "Test rehearsal package: $out"
Write-Host 'Not published. VM trust, installation and cleanup rehearsal remain pending.'
if ($OutputPathFile) { $out | Set-Content -LiteralPath $OutputPathFile -Encoding utf8 }
