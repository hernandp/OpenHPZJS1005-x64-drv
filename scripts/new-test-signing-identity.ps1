#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$subject = 'CN=OpenZJS Technical Tester Signing'
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new public-certificate output directory.' }
if (@(Get-ChildItem Cert:\CurrentUser\My | Where-Object Subject -eq $subject).Count) {
    throw 'A tester signing identity already exists. Review and reuse its thumbprint instead of creating another.'
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
# This personal store retains the private key; it does NOT establish trust.
# Never export the private key or add this certificate to Root/TrustedPublisher here.
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
    -FriendlyName 'OpenZJS technical testers only - not public trust' `
    -CertStoreLocation Cert:\CurrentUser\My -Provider 'Microsoft Software Key Storage Provider' `
    -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -KeyExportPolicy NonExportable `
    -NotBefore (Get-Date).AddMinutes(-5) -NotAfter (Get-Date).AddYears(1)
$publicPath = Join-Path $OutputDirectory 'TEST-CERTIFICATE.cer'
Export-Certificate -Cert $cert -FilePath $publicPath -Type CERT | Out-Null
[ordered]@{
    subject = $cert.Subject
    thumbprintSha1 = $cert.Thumbprint
    certificateFileSha256 = (Get-FileHash -LiteralPath $publicPath -Algorithm SHA256).Hash.ToLowerInvariant()
    notBeforeUtc = $cert.NotBefore.ToUniversalTime().ToString('o')
    notAfterUtc = $cert.NotAfter.ToUniversalTime().ToString('o')
    privateKeyStore = 'CurrentUser/My'
    privateKeyExportPolicy = 'NonExportable'
    hostTrustAdded = $false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'IDENTITY.json') -Encoding utf8
Write-Host "Created test-only signing identity: $($cert.Thumbprint)"
Write-Host "Public certificate: $publicPath"
Write-Host 'Private key remains in CurrentUser/My. No trust store was changed.'
