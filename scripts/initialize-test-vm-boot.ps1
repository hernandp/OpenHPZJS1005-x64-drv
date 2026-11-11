# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
#requires -Version 7.0
#requires -RunAsAdministrator
# Internal provisioning stage: only use with the fresh VHDX prepared by new-test-vm.ps1.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^OpenZJS-[A-Za-z0-9-]+$')][string]$VMName,
    [Parameter(Mandatory)][string]$VMDirectory,
    [Parameter(Mandatory)][string]$EvidencePath,
    [Parameter(Mandatory)][string]$IsoPath,
    [Parameter(Mandatory)][int]$ImageIndex,
    [Parameter(Mandatory)][string]$IsoSha256
)
$ErrorActionPreference = 'Stop'
$lab = [IO.Path]::GetFullPath($VMDirectory)
$evidence = [IO.Path]::GetFullPath($EvidencePath)
$vhd = Join-Path $lab 'Windows.vhdx'
$peVhd = Join-Path $lab 'BootRepair.vhdx'
$mount = Join-Path $lab 'pe-mount'
$iso = (Get-Item -LiteralPath $IsoPath).FullName
$isoMounted = $false
$peMounted = $false
$wimMounted = $false
function Phase($message) { ([DateTime]::UtcNow.ToString('o') + ' ' + $message) | Add-Content "$evidence\winpe-provision.log" }
try {
    if (Get-VM -Name $vmName -ErrorAction SilentlyContinue) { throw 'VM already exists.' }
    if (Test-Path $peVhd) { throw 'Repair disk already exists; inspect before retry.' }
    Phase 'Preparing isolated Windows PE boot media.'
    $mainDisk = Mount-VHD -Path $vhd -PassThru | Get-Disk
    try {
        if ($mainDisk.IsBoot -or $mainDisk.IsSystem -or $mainDisk.Size -ne 128GB) { throw 'Wrong main virtual disk.' }
        $os = Get-Partition -DiskNumber $mainDisk.Number | Where-Object Size -gt 100GB
        $efi = Get-Partition -DiskNumber $mainDisk.Number | Where-Object GptType -eq '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
        if (!$efi -or $efi.PartitionNumber -lt 1 -or $efi.PartitionNumber -gt 128) { throw 'Unexpected EFI layout.' }
        $efiNumber = $efi.PartitionNumber
        'OpenZJS isolated VM boot target' | Set-Content "$($os.DriveLetter):\OpenZJS-VMBootTarget.txt"
    } finally { Dismount-VHD -Path $vhd }
    $image = Get-DiskImage -ImagePath $iso
    if (!$image.Attached) { $image = Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru; $isoMounted = $true }
    $media = "$(( $image | Get-Volume).DriveLetter):\"
    New-VHD -Path $peVhd -Dynamic -SizeBytes 3GB | Out-Null
    $disk = Mount-VHD -Path $peVhd -PassThru | Get-Disk
    $peMounted = $true
    if ($disk.IsBoot -or $disk.IsSystem -or $disk.PartitionStyle -ne 'RAW' -or $disk.Size -ne 3GB) { throw 'Wrong repair disk.' }
    $disk | Initialize-Disk -PartitionStyle GPT
    $partition = New-Partition -DiskNumber $disk.Number -UseMaximumSize -AssignDriveLetter -GptType '{C12A7328-F81F-11D2-BA4B-00A0C93EC93B}'
    $partition | Format-Volume -FileSystem FAT32 -NewFileSystemLabel 'OpenZJS PE' -Confirm:$false | Out-Null
    $peRoot = "$($partition.DriveLetter):\"
    foreach ($directory in @('boot', 'efi')) { Copy-Item -LiteralPath (Join-Path $media $directory) -Destination $peRoot -Recurse }
    foreach ($file in @('bootmgr', 'bootmgr.efi')) { Copy-Item -LiteralPath (Join-Path $media $file) -Destination $peRoot }
    New-Item -ItemType Directory -Path (Join-Path $peRoot 'sources'), $mount | Out-Null
    $wim = Join-Path $peRoot 'sources\boot.wim'
    Copy-Item -LiteralPath (Join-Path $media 'sources\boot.wim') -Destination $wim
    (Get-Item -LiteralPath $wim).IsReadOnly = $false
    Mount-WindowsImage -ImagePath $wim -Index 2 -Path $mount -LogPath "$evidence\pe-mount.log" | Out-Null
    $wimMounted = $true
    @'
[LaunchApps]
%SYSTEMROOT%\System32\cmd.exe, "/c X:\Windows\System32\labboot.cmd"
'@ | Set-Content (Join-Path $mount 'Windows\System32\winpeshl.ini') -Encoding ascii
    $bootScript = @'
@echo off
if /I not "%SystemDrive%"=="X:" exit /b 99
wpeinit
set TARGET=
for %%L in (C D E F G H I J K L M N O P Q R T U V W Y Z) do if exist %%L:\OpenZJS-VMBootTarget.txt set TARGET=%%L:
if not defined TARGET goto finish
if not exist %TARGET%\Windows\System32\config\BCD-Template goto finish
>X:\efi.txt echo select disk 0
>>X:\efi.txt echo select partition __EFI_PARTITION__
>>X:\efi.txt echo assign letter=S
>>X:\efi.txt echo exit
diskpart /s X:\efi.txt >%TARGET%\lab-diskpart.log 2>&1
if errorlevel 1 goto finish
if not exist S:\ goto finish
bcdboot %TARGET%\Windows /s S: /f UEFI /v >%TARGET%\lab-bcdboot.log 2>&1
if errorlevel 1 goto finish
echo success>%TARGET%\OpenZJS-BootReady.txt
:finish
wpeutil shutdown
'@
    $bootScript.Replace('__EFI_PARTITION__', [string]$efiNumber) |
        Set-Content (Join-Path $mount 'Windows\System32\labboot.cmd') -Encoding ascii
    Dismount-WindowsImage -Path $mount -Save -LogPath "$evidence\pe-commit.log" | Out-Null
    $wimMounted = $false
    Dismount-VHD -Path $peVhd
    $peMounted = $false
    Phase 'Creating VM with main disk at SCSI 0 and temporary PE disk at SCSI 1.'
    $vm = New-VM -Name $vmName -Generation 2 -MemoryStartupBytes 6GB -VHDPath $vhd -Path $lab
    Set-VM -VM $vm -ProcessorCount 4 -AutomaticCheckpointsEnabled $false -CheckpointType Standard -AutomaticStartAction Nothing
    Add-VMHardDiskDrive -VM $vm -Path $peVhd -ControllerType SCSI -ControllerNumber 0 -ControllerLocation 1
    Set-VMFirmware -VM $vm -EnableSecureBoot On -SecureBootTemplate MicrosoftWindows -FirstBootDevice (Get-VMHardDiskDrive -VM $vm | Where-Object Path -eq $peVhd)
    Set-VMKeyProtector -VM $vm -NewLocalKeyProtector
    Enable-VMTPM -VM $vm
    [ordered]@{
        vmName = $vmName; vmId = $vm.Id; vmDirectory = $lab; vhdPath = $vhd
        isoPath = $iso; isoSha256 = $IsoSha256
        imageIndex = $ImageIndex; imageName = 'Windows 11 Pro'; secureBoot = $true; tpm = $true
        networkConnected = $false; createdUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content "$evidence\vm.json"
    Start-VM -VM $vm
    Phase 'Windows PE started; waiting for repair and shutdown.'
    $deadline = (Get-Date).AddMinutes(5)
    while ((Get-VM -Name $vmName).State -ne 'Off' -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 3 }
    if ((Get-VM -Name $vmName).State -ne 'Off') { throw 'Windows PE did not shut down within five minutes.' }
    $mainDisk = Mount-VHD -Path $vhd -PassThru | Get-Disk
    try {
        $os = Get-Partition -DiskNumber $mainDisk.Number | Where-Object Size -gt 100GB
        $osRoot = "$($os.DriveLetter):\"
        foreach ($log in @('lab-diskpart.log', 'lab-bcdboot.log')) {
            if (Test-Path (Join-Path $osRoot $log)) { Copy-Item -LiteralPath (Join-Path $osRoot $log) -Destination $evidence }
        }
        if (!(Test-Path (Join-Path $osRoot 'OpenZJS-BootReady.txt'))) { throw 'Windows PE did not confirm boot setup success.' }
    } finally { Dismount-VHD -Path $vhd }
    Get-VMHardDiskDrive -VM $vm | Where-Object Path -eq $peVhd | Remove-VMHardDiskDrive
    Set-VMFirmware -VM $vm -FirstBootDevice (Get-VMHardDiskDrive -VM $vm | Where-Object Path -eq $vhd)
    Start-VM -VM $vm
    Phase 'Boot repair passed; temporary PE disk detached and Windows setup started.'
} catch {
    $_ | Out-String | Set-Content "$evidence\winpe-error.txt"
    throw
} finally {
    if ($wimMounted) { Dismount-WindowsImage -Path $mount -Discard | Out-Null }
    if ($peMounted) { Dismount-VHD -Path $peVhd }
    if ($isoMounted) { Dismount-DiskImage -ImagePath $iso | Out-Null }
}
