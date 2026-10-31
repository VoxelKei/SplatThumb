# Asks the Windows shell for a file's thumbnail the way Explorer does
# (IShellItemImageFactory, thumbnails only, no icon fallback). Prints the result and exits
# 0 when a thumbnail came back, 1 when none did. Run it in its own process: the shell may
# load the handler DLL into this process or into dllhost.exe. With -ResultPath, also writes
# the result there (for callers that cannot see this process's output).
param([Parameter(Mandatory)] [string] $Path, [int] $Size = 256, [string] $ResultPath)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class SplatThumbCiShell {
    [StructLayout(LayoutKind.Sequential)]
    struct SIZE { public int cx, cy; }

    [StructLayout(LayoutKind.Sequential)]
    struct BITMAP {
        public int bmType, bmWidth, bmHeight, bmWidthBytes;
        public ushort bmPlanes, bmBitsPixel;
        public IntPtr bmBits;
    }

    [ComImport, Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IShellItemImageFactory {
        [PreserveSig] int GetImage(SIZE size, int flags, out IntPtr bitmap);
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    static extern void SHCreateItemFromParsingName(string path, IntPtr bindContext, ref Guid iid,
        [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory item);

    [DllImport("ole32.dll")]
    static extern int CoCreateInstance(ref Guid clsid, IntPtr outer, int context, ref Guid iid, out IntPtr obj);

    // HRESULT of creating the class in this process, without the shell.
    public static string CreateInProc(string clsid) {
        Guid c = new Guid(clsid);
        Guid unknown = new Guid("00000000-0000-0000-C000-000000000046");
        IntPtr obj;
        int hr = CoCreateInstance(ref c, IntPtr.Zero, 1 /* CLSCTX_INPROC_SERVER */, ref unknown, out obj);
        if (hr >= 0) Marshal.Release(obj);
        return string.Format("0x{0:X8}", hr);
    }

    [DllImport("gdi32.dll")] static extern int GetObject(IntPtr obj, int size, out BITMAP bitmap);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr obj);

    // Returns "hr width height".
    public static string Get(string path, int size) {
        Guid iid = typeof(IShellItemImageFactory).GUID;
        IShellItemImageFactory item;
        SHCreateItemFromParsingName(path, IntPtr.Zero, ref iid, out item);
        IntPtr bitmap;
        SIZE s; s.cx = size; s.cy = size;
        const int SIIGBF_THUMBNAILONLY = 0x8;
        int hr = item.GetImage(s, SIIGBF_THUMBNAILONLY, out bitmap);
        if (hr < 0) return string.Format("0x{0:X8} 0 0", hr);
        BITMAP info;
        GetObject(bitmap, Marshal.SizeOf(typeof(BITMAP)), out info);
        DeleteObject(bitmap);
        return string.Format("0x{0:X8} {1} {2}", hr, info.bmWidth, info.bmHeight);
    }
}
'@

try {
    $result = [SplatThumbCiShell]::Get((Resolve-Path -LiteralPath $Path).ProviderPath, $Size)
} catch {
    $result = "error 0 0 ($($_.Exception.Message))"
}
Write-Host "Thumbnail of ${Path}: $result"
$hr, $width, $height = $result -split ' '
if ([int] $width -le 0) {
    # Why not: what this process sees of the registration.
    $clsid = '{09C68D16-D21F-4B95-97A1-29B45AEBF4CC}'
    $merged = $null -ne [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey("CLSID\$clsid\InprocServer32")
    $principal = [Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
    $admin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    $result += " | HKCR sees CLSID: $merged; in-process create: $([SplatThumbCiShell]::CreateInProc($clsid));" +
    " session $((Get-Process -Id $PID).SessionId); admin: $admin"
    Write-Host $result
}
if ($ResultPath) { Set-Content -LiteralPath $ResultPath -Value $result }
if ([int] $width -gt 0 -and [int] $height -gt 0) { exit 0 }
exit 1
