#requires -Version 7.0
#requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$IsoPath,
    [ValidatePattern('^OpenZJS-[A-Za-z0-9-]+$')][string]$VMName = 'OpenZJS-Win11-25H2',
    [string]$LabRoot = "$env:ProgramData\OpenZJS-Lab"
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$evidence = Join-Path $projectRoot "artifacts\$VMName"
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$iso = (Get-Item -LiteralPath $IsoPath).FullName
if ([IO.Path]::GetExtension($iso) -ne '.iso') { throw 'Expected a Windows installation ISO.' }
$vmDirectory = [IO.Path]::GetFullPath((Join-Path $LabRoot $VMName))
if (Get-VM -Name $VMName -ErrorAction SilentlyContinue) { throw "VM already exists: $VMName" }
if (Test-Path -LiteralPath $vmDirectory) { throw "Refusing to reuse existing directory: $vmDirectory" }
$vhdPath = Join-Path $vmDirectory 'Windows.vhdx'
$mountedIsoHere = $false
$mountedVhd = $false
function Write-Phase([string]$Message) {
    $line = [DateTime]::UtcNow.ToString('o') + ' ' + $Message
    $line | Add-Content -LiteralPath (Join-Path $evidence 'provision.log')
    Write-Host $line
}
try {
    Write-Phase 'Inspecting installation media.'
    $isoHash = (Get-FileHash -LiteralPath $iso -Algorithm SHA256).Hash
    $image = Get-DiskImage -ImagePath $iso
    if (!$image.Attached) {
        $image = Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru
        $mountedIsoHere = $true
    }
    $volume = $image | Get-Volume
    if (!$volume.DriveLetter) { throw 'ISO has no accessible volume.' }
    $media = "$($volume.DriveLetter):\"
    $installImage = Join-Path $media 'sources\install.wim'
    if (!(Test-Path -LiteralPath $installImage)) { $installImage = Join-Path $media 'sources\install.esd' }
    $editions = @(Get-WindowsImage -ImagePath $installImage)
    $editions | Select-Object ImageIndex, ImageName, ImageDescription |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'media-editions.json')
    $edition = @($editions | Where-Object ImageName -eq 'Windows 11 Pro')
    if ($edition.Count -ne 1) { throw 'Media must contain exactly one Windows 11 Pro image.' }
    Write-Phase "Creating fresh 128 GB dynamic VHDX for image index $($edition[0].ImageIndex)."
    New-Item -ItemType Directory -Path $vmDirectory | Out-Null
    New-VHD -Path $vhdPath -Dynamic -SizeBytes 128GB | Out-Null
    $mounted = Mount-VHD -Path $vhdPath -PassThru
    $mountedVhd = $true
    $disk = $mounted | Get-Disk
    # Never operate on a guessed disk number: this must be the newly created,
    # empty VHDX returned by Mount-VHD, and never a host system/boot disk.
    if ($mounted.Path -ne $vhdPath -or $disk.IsBoot -or $disk.IsSystem -or
        $disk.PartitionStyle -ne 'RAW' -or $disk.Size -ne 128GB) {
        throw 'Mounted disk identity or empty-disk checks failed.'
    }
    $disk | Initialize-Disk -PartitionStyle GPT
    $efi = New-Partition -DiskNumber $disk.Number -Size 260MB -AssignDriveLetter -GptType '{C12A7328-F81F-11D2-BA4B-00A0C93EC93B}'
    $efi | Format-Volume -FileSystem FAT32 -NewFileSystemLabel 'OpenZJS EFI' -Confirm:$false | Out-Null
    # Initialize-Disk has already created the Microsoft Reserved partition.
    $windows = New-Partition -DiskNumber $disk.Number -UseMaximumSize -AssignDriveLetter
    $windows | Format-Volume -FileSystem NTFS -NewFileSystemLabel 'OpenZJS Windows' -Confirm:$false | Out-Null
    $windowsRoot = "$($windows.DriveLetter):\"
    $efiRoot = "$($efi.DriveLetter):"
    if (!$windows.DriveLetter -or !$efi.DriveLetter -or $windowsRoot -eq "$env:SystemDrive\" -or
        $efiRoot -eq $env:SystemDrive) { throw 'Unsafe drive-letter assignment.' }
    Write-Phase 'Applying Windows image to the new virtual disk.'
    & "$env:SystemRoot\System32\dism.exe" /Apply-Image "/ImageFile:$installImage" "/Index:$($edition[0].ImageIndex)" "/ApplyDir:$windowsRoot" /CheckIntegrity "/LogPath:$evidence\dism.log" > (Join-Path $evidence 'dism-output.log')
    if ($LASTEXITCODE -ne 0) { throw "DISM failed: $LASTEXITCODE" }

    # The lab credential is random and DPAPI-encrypted for this host user. It is
    # never printed, committed, or shared with a network. Setup consumes the
    # temporary answer file inside the guest; remove it after first boot.
    $password = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)) + 'aZ9!'
    $credential = [pscredential]::new('DriverLab', (ConvertTo-SecureString $password -AsPlainText -Force))
    $credential | Export-Clixml -LiteralPath (Join-Path $evidence 'credential.clixml')
    $answer = @"
<?xml version="1.0" encoding="utf-8"?>
<unattend xmlns="urn:schemas-microsoft-com:unattend" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
  <settings pass="specialize">
    <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <ComputerName>OPENZJS-LAB</ComputerName><TimeZone>UTC</TimeZone>
    </component>
  </settings>
  <settings pass="oobeSystem">
    <component name="Microsoft-Windows-International-Core" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <InputLocale>en-US</InputLocale><SystemLocale>en-US</SystemLocale><UILanguage>en-US</UILanguage><UserLocale>en-US</UserLocale>
    </component>
    <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <OOBE><HideEULAPage>true</HideEULAPage><HideOnlineAccountScreens>true</HideOnlineAccountScreens><HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE><ProtectYourPC>3</ProtectYourPC></OOBE>
      <UserAccounts><LocalAccounts><LocalAccount wcm:action="add"><Name>DriverLab</Name><DisplayName>Driver Lab</DisplayName><Group>Administrators</Group><Password><Value>$password</Value><PlainText>true</PlainText></Password></LocalAccount></LocalAccounts></UserAccounts>
      <AutoLogon><Enabled>true</Enabled><LogonCount>1</LogonCount><Username>DriverLab</Username><Password><Value>$password</Value><PlainText>true</PlainText></Password></AutoLogon>
    </component>
  </settings>
</unattend>
"@
    $panther = Join-Path $windowsRoot 'Windows\Panther'
    New-Item -ItemType Directory -Force -Path $panther | Out-Null
    $answer | Set-Content -LiteralPath (Join-Path $panther 'unattend.xml') -Encoding utf8
    $password = $null
    $answer = $null
    Dismount-VHD -Path $vhdPath
    $mountedVhd = $false
    Write-Phase 'Preparing boot files inside isolated Windows PE, then starting Windows setup.'
    & (Join-Path $PSScriptRoot 'initialize-test-vm-boot.ps1') -VMName $VMName -VMDirectory $vmDirectory `
        -EvidencePath $evidence -IsoPath $iso -ImageIndex $edition[0].ImageIndex -IsoSha256 $isoHash
    Write-Phase 'VM started. Wait for Windows setup and verify via PowerShell Direct before taking the clean checkpoint.'
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $evidence 'provision-error.txt')
    throw
} finally {
    if ($mountedVhd) { Dismount-VHD -Path $vhdPath }
    if ($mountedIsoHere) { Dismount-DiskImage -ImagePath $iso | Out-Null }
}
