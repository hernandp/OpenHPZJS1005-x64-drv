# Loaded only by the dedicated VM migration rehearsal.
if (!('MigrationLab' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.ComponentModel;
public static class MigrationLab {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct Driver3 { public uint version; public string name, environment, driver, data, config, help; public IntPtr dependents; public string monitor, datatype; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct Monitor2 { public string name, environment, dll; }
    [StructLayout(LayoutKind.Sequential)]
    struct Defaults { public IntPtr datatype, mode; public uint access; }
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool AddPrinterDriverEx(string server,uint level,ref Driver3 info,uint flags);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool AddMonitor(string server,uint level,ref Monitor2 info);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool DeletePrinterDriverEx(string server,string environment,string name,uint flags,uint version);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool DeleteMonitor(string server,string environment,string name);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool OpenPrinter(string name,out IntPtr printer,ref Defaults defaults);
    [DllImport("winspool.drv", SetLastError=true)] static extern bool ClosePrinter(IntPtr printer);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool GetPrinter(IntPtr printer,uint level,IntPtr data,uint size,out uint needed);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool SetPrinter(IntPtr printer,uint level,IntPtr data,uint command);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode)]
    static extern int DocumentProperties(IntPtr window,IntPtr printer,string name,IntPtr output,IntPtr input,uint flags);
    static void Check(bool ok) { if (!ok) throw new Win32Exception(Marshal.GetLastWin32Error()); }
    public static void InstallMonitor() { var info=new Monitor2 {name="HP LaserJet 1005 Firmware Monitor (development)",environment="Windows x64",dll="lj1005monitor.dll"}; Check(AddMonitor(null,2,ref info)); }
    public static void InstallDriver(string folder,string spool,bool diagnostic) {
        var deps=String.Join("\0",new[]{Path.Combine(folder,"lj1005render.dll"),Path.Combine(spool,"UNIRES.DLL"),Path.Combine(spool,"STDNAMES.GPD")});
        if (!diagnostic) deps += "\0"+Path.Combine(folder,"lj1005.ini");
        var info=new Driver3 {version=3,name=diagnostic?"HP LaserJet 1005 GPD diagnostic":"HP LaserJet 1005 Series (Open ZJS)",environment="Windows x64",driver=Path.Combine(spool,"UNIDRV.DLL"),config=Path.Combine(spool,"UNIDRVUI.DLL"),help=Path.Combine(spool,"UNIDRV.HLP"),data=Path.Combine(folder,diagnostic?"lj1005_config.gpd":"lj1005.gpd"),datatype="RAW",monitor=diagnostic?null:"HP LaserJet 1005 Firmware Monitor (development)"};
        info.dependents=Marshal.StringToHGlobalUni(deps+"\0\0");
        // Private files were removed first. COPY_NEW_FILES leaves identical
        // Windows core files shared by other registered drivers in place.
        try { Check(AddPrinterDriverEx(null,3,ref info,0x18)); } finally { Marshal.FreeHGlobal(info.dependents); }
    }
    static IntPtr Printer(string name) { var defaults=new Defaults {access=0x000F000C}; IntPtr printer; Check(OpenPrinter(name,out printer,ref defaults)); return printer; }
    public static void SaveMode(string name,string file) {
        var printer=Printer(name); IntPtr data=IntPtr.Zero;
        try { uint size; GetPrinter(printer,8,IntPtr.Zero,0,out size); if(size==0 || size>1048576) throw new Exception("Invalid printer data size"); data=Marshal.AllocHGlobal((int)size); Check(GetPrinter(printer,8,data,size,out size)); var mode=Marshal.ReadIntPtr(data); if(mode==IntPtr.Zero) throw new Exception("No default DEVMODE"); int length=(ushort)Marshal.ReadInt16(mode,68)+(ushort)Marshal.ReadInt16(mode,70); if(length<220 || length>1048576) throw new Exception("Invalid DEVMODE"); var bytes=new byte[length]; Marshal.Copy(mode,bytes,0,length); File.WriteAllBytes(file,bytes); }
        finally { if(data!=IntPtr.Zero) Marshal.FreeHGlobal(data); ClosePrinter(printer); }
    }
    public static void RestoreMode(string name,string file) {
        var bytes=File.ReadAllBytes(file); if(bytes.Length<220 || bytes.Length>1048576 || BitConverter.ToUInt16(bytes,68)+BitConverter.ToUInt16(bytes,70)!=bytes.Length) throw new Exception("Invalid saved DEVMODE");
        var printer=Printer(name); IntPtr input=IntPtr.Zero, output=IntPtr.Zero, info=IntPtr.Zero;
        try { input=Marshal.AllocHGlobal(bytes.Length); Marshal.Copy(bytes,0,input,bytes.Length); int size=DocumentProperties(IntPtr.Zero,printer,name,IntPtr.Zero,IntPtr.Zero,0); if(size<=0 || size>1048576) throw new Exception("Cannot size DEVMODE"); output=Marshal.AllocHGlobal(size); if(DocumentProperties(IntPtr.Zero,printer,name,output,input,10)!=1) throw new Exception("Cannot convert DEVMODE"); info=Marshal.AllocHGlobal(IntPtr.Size); Marshal.WriteIntPtr(info,output); Check(SetPrinter(printer,8,info,0)); }
        finally { if(input!=IntPtr.Zero) Marshal.FreeHGlobal(input); if(output!=IntPtr.Zero) Marshal.FreeHGlobal(output); if(info!=IntPtr.Zero) Marshal.FreeHGlobal(info); ClosePrinter(printer); }
    }
}
'@
}
