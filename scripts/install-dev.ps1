param([string]$QueueName = 'HP LaserJet 1005 Open ZJS (development)', [string]$PortName = 'FILE:', [switch]$ConfigOnlyDiagnostic, [switch]$ConnectUsb, [switch]$WithFirmwareMonitor, [string]$FirmwarePath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$package = Join-Path $projectRoot 'build\Release'
$driverName = 'HP LaserJet 1005 Series (Open ZJS)'
$dataFile = Join-Path $package 'lj1005.gpd'
if ($WithFirmwareMonitor -and $ConfigOnlyDiagnostic) { throw 'The configuration diagnostic cannot use the firmware monitor.' }
if ($ConfigOnlyDiagnostic) {
    $driverName = 'HP LaserJet 1005 GPD diagnostic'
    $QueueName = 'HP LaserJet 1005 GPD diagnostic'
    $dataFile = Join-Path $package 'lj1005_config.gpd'
    Get-Content (Join-Path $package 'lj1005.gpd') | Where-Object { $_ -notmatch '^\*OEMDLL:' } | Set-Content -LiteralPath $dataFile -Encoding ascii
}
$inbox = Join-Path $env:windir 'System32\spool\drivers\x64\3'
if ($ConnectUsb) {
    $PortName = & (Join-Path $PSScriptRoot 'resolve-usb-port.ps1')
}
foreach ($file in @('lj1005.gpd','lj1005.ini','lj1005render.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $package $file))) { throw "Build the Release package first: missing $file" }
}
foreach ($file in @('UNIDRV.DLL','UNIDRVUI.DLL','UNIDRV.HLP','UNIRES.DLL','STDNAMES.GPD')) {
    if (!(Test-Path -LiteralPath (Join-Path $inbox $file))) { throw "Missing Windows inbox component: $file" }
}
# This is a local development installation through the documented spooler API.
# It does not alter signature enforcement or install the original HP binaries.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Lj1005Install {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct DRIVER_INFO_3 {
        public uint cVersion;
        public string pName, pEnvironment, pDriverPath, pDataFile, pConfigFile, pHelpFile;
        public IntPtr pDependentFiles;
        public string pMonitorName, pDefaultDataType;
    }
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool AddPrinterDriverEx(string server, uint level, ref DRIVER_INFO_3 info, uint flags);
    [StructLayout(LayoutKind.Sequential)]
    struct PRINTER_DEFAULTS { public IntPtr datatype, devmode; public uint access; }
    [StructLayout(LayoutKind.Sequential)]
    struct PRINTER_INFO_2 {
        public IntPtr server, name, share, port, driver, comment, location, devmode;
        public IntPtr separator, processor, datatype, parameters, security;
        public uint attributes, priority, defaultPriority, startTime, untilTime, status, jobs, averagePpm;
    }
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool OpenPrinter(string name, out IntPtr printer, ref PRINTER_DEFAULTS defaults);
    [DllImport("winspool.drv", SetLastError=true)]
    static extern bool ClosePrinter(IntPtr printer);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool GetPrinter(IntPtr printer, uint level, IntPtr data, uint size, out uint needed);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool SetPrinter(IntPtr printer, uint level, IntPtr data, uint command);
    public static void EnableBidi(string name) { Configure(name, true, false); }
    public static void SetOnline(string name) { Configure(name, false, true); }
    static void Configure(string name, bool bidi, bool online) {
        var defaults = new PRINTER_DEFAULTS { access = 0x000F000C };
        IntPtr printer;
        if (!OpenPrinter(name, out printer, ref defaults)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        IntPtr buffer = IntPtr.Zero;
        try {
            uint needed;
            GetPrinter(printer, 2, IntPtr.Zero, 0, out needed);
            if (needed == 0 || needed > 16 * 1024 * 1024) throw new InvalidOperationException("Cannot size printer information");
            buffer = Marshal.AllocHGlobal((int)needed);
            if (!GetPrinter(printer, 2, buffer, needed, out needed)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            int offset = Marshal.OffsetOf<PRINTER_INFO_2>("attributes").ToInt32();
            int attributes = Marshal.ReadInt32(buffer, offset);
            int updated = attributes;
            if (bidi) updated |= 0x800;
            if (online) updated &= ~0x400;
            if (updated != attributes) {
                Marshal.WriteInt32(buffer, offset, updated);
                // Do not ask SetPrinter to rewrite the queue's security descriptor.
                Marshal.WriteIntPtr(buffer, Marshal.OffsetOf<PRINTER_INFO_2>("security").ToInt32(), IntPtr.Zero);
                if (!SetPrinter(printer, 2, buffer, 0)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }
        } finally { if (buffer != IntPtr.Zero) Marshal.FreeHGlobal(buffer); ClosePrinter(printer); }
    }
}
'@
$info = [Lj1005Install+DRIVER_INFO_3]::new()
$info.cVersion = 3
$info.pName = $driverName
$info.pEnvironment = 'Windows x64'
$info.pDriverPath = Join-Path $inbox 'UNIDRV.DLL'
$info.pConfigFile = Join-Path $inbox 'UNIDRVUI.DLL'
$info.pHelpFile = Join-Path $inbox 'UNIDRV.HLP'
$info.pDataFile = $dataFile
$info.pDefaultDataType = 'RAW'
if ($WithFirmwareMonitor) {
    & (Join-Path $PSScriptRoot 'install-monitor-dev.ps1') -FirmwarePath $FirmwarePath
    $info.pMonitorName = 'HP LaserJet 1005 Firmware Monitor (development)'
}
$dependent = @((Join-Path $package 'lj1005render.dll'), (Join-Path $inbox 'UNIRES.DLL'), (Join-Path $inbox 'STDNAMES.GPD'))
if (!$ConfigOnlyDiagnostic) { $dependent += Join-Path $package 'lj1005.ini' }
$info.pDependentFiles = [Runtime.InteropServices.Marshal]::StringToHGlobalUni(($dependent -join "`0") + "`0`0")
try {
    # APD_COPY_FROM_DIRECTORY | APD_COPY_NEW_FILES
    if (![Lj1005Install]::AddPrinterDriverEx($null, 3, [ref]$info, 0x18)) {
        throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
} finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($info.pDependentFiles) }
$existing = Get-Printer -Name $QueueName -ErrorAction SilentlyContinue
if ($existing) {
    if ($existing.DriverName -ne $driverName) { throw 'Refusing to modify a queue that uses a different driver.' }
    Set-Printer -Name $QueueName -PortName $PortName
} else { Add-Printer -Name $QueueName -DriverName $driverName -PortName $PortName }
if ($ConnectUsb) {
    Set-PrintConfiguration -PrinterName $QueueName -PaperSize A4
    [Lj1005Install]::SetOnline($QueueName)
}
if ($WithFirmwareMonitor) { [Lj1005Install]::EnableBidi($QueueName) }
Get-Printer -Name $QueueName | Select-Object Name, DriverName, PortName
