# Makes a thumbnail of one file with the registered .ply thumbnail handler, in two ways:
#   com:   create the class from its registration in this process, then Initialize with a
#          stream and GetThumbnail, the calls Explorer's thumbnail host makes.
#   shell: ask the shell (IShellItemImageFactory, thumbnails only), the way Explorer does.
# Exits 0 when the com way returned a bitmap, 1 otherwise. The shell way is only reported:
# on CI runners its isolation process does not see per-user registrations (it returns
# REGDB_E_CLASSNOTREG even though the class is registered), while on a normal desktop it
# does. Run this in its own single-threaded (-STA) process, as it loads the handler DLL. With -ResultPath, also
# writes the result there (for callers that cannot see this process's output).
param([Parameter(Mandatory)] [string] $Path, [int] $Size = 256, [string] $ResultPath)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

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

    [ComImport, Guid("b824b49d-22ac-4161-ac8a-9916e8fa3f7f"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IInitializeWithStream {
        [PreserveSig] int Initialize(IStream stream, int mode);
    }

    [ComImport, Guid("e357fccd-a995-4576-b01f-234630154e96"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IThumbnailProvider {
        [PreserveSig] int GetThumbnail(int cx, out IntPtr bitmap, out int alpha);
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    static extern void SHCreateItemFromParsingName(string path, IntPtr bindContext, ref Guid iid,
        [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory item);

    [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
    static extern int SHCreateStreamOnFileEx(string file, int mode, int attributes, bool create,
        IntPtr template, out IStream stream);

    [DllImport("ole32.dll")]
    static extern int CoCreateInstance(ref Guid clsid, IntPtr outer, int context, ref Guid iid,
        [MarshalAs(UnmanagedType.IUnknown)] out object obj);

    [DllImport("gdi32.dll")] static extern int GetObject(IntPtr obj, int size, out BITMAP bitmap);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr obj);

    // "hr width height"
    static string Describe(int hr, IntPtr bitmap) {
        if (hr < 0 || bitmap == IntPtr.Zero) return string.Format("0x{0:X8} 0 0", hr);
        BITMAP info;
        GetObject(bitmap, Marshal.SizeOf(typeof(BITMAP)), out info);
        DeleteObject(bitmap);
        return string.Format("0x{0:X8} {1} {2}", hr, info.bmWidth, info.bmHeight);
    }

    public static string FromShell(string path, int size) {
        Guid iid = typeof(IShellItemImageFactory).GUID;
        IShellItemImageFactory item;
        SHCreateItemFromParsingName(path, IntPtr.Zero, ref iid, out item);
        IntPtr bitmap;
        SIZE s; s.cx = size; s.cy = size;
        const int SIIGBF_THUMBNAILONLY = 0x8;
        int hr = item.GetImage(s, SIIGBF_THUMBNAILONLY, out bitmap);
        return Describe(hr, bitmap);
    }

    public static string FromCom(string clsid, string path, int size) {
        Guid c = new Guid(clsid);
        Guid unknown = new Guid("00000000-0000-0000-C000-000000000046");
        object obj;
        int hr = CoCreateInstance(ref c, IntPtr.Zero, 1 /* CLSCTX_INPROC_SERVER */, ref unknown, out obj);
        if (hr < 0) return Describe(hr, IntPtr.Zero);
        IStream stream;
        hr = SHCreateStreamOnFileEx(path, 0x20 /* STGM_READ | STGM_SHARE_DENY_WRITE */, 0, false,
            IntPtr.Zero, out stream);
        if (hr < 0) return Describe(hr, IntPtr.Zero);
        hr = ((IInitializeWithStream) obj).Initialize(stream, 0);
        if (hr < 0) return Describe(hr, IntPtr.Zero);
        IntPtr bitmap;
        int alpha;
        hr = ((IThumbnailProvider) obj).GetThumbnail(size, out bitmap, out alpha);
        return Describe(hr, bitmap);
    }
}
'@

$full = (Resolve-Path -LiteralPath $Path).ProviderPath
function Invoke-Safely([scriptblock] $Block) {
    try { & $Block } catch { "error 0 0 ($($_.Exception.Message))" }
}
$com = Invoke-Safely { [SplatThumbCiShell]::FromCom('{09C68D16-D21F-4B95-97A1-29B45AEBF4CC}', $full, $Size) }
$shell = Invoke-Safely { [SplatThumbCiShell]::FromShell($full, $Size) }
$result = "$com | shell: $shell"
Write-Host "Thumbnail of ${Path}: com: $result"
if ($ResultPath) { Set-Content -LiteralPath $ResultPath -Value $result }
$null, $width, $height = $com -split ' '
if ([int] $width -gt 0 -and [int] $height -gt 0) { exit 0 }
exit 1
