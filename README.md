# SplatThumb

[日本語](README.ja.md)

Windows Explorer thumbnails for 3D Gaussian Splatting `.ply` files.

> **Status: early development (v0.1).** Download the installer from the
> [Releases](../../releases) page, or build it from source (see below).

SplatThumb only renders PLY files that are 3D Gaussian Splatting scenes, detected from the file
header. Meshes and point clouds are left to Windows. It reads files and never modifies them, makes
no network connections, and collects no telemetry.

## Which files get a thumbnail

| File | Thumbnail |
| --- | --- |
| 3D Gaussian Splatting PLY in the standard (INRIA) layout, binary little-endian, as written by most training tools | Yes |
| The same with extra properties such as normals or `f_rest_*` | Yes (only the base color is drawn) |
| PlayCanvas / SuperSplat compressed PLY (`.compressed.ply`) | Not yet (planned) |
| Meshes, point clouds, ASCII or big-endian PLY | No, Windows shows its usual icon |
| Other splat formats (`.spz`, `.splat`, `.sog`, ...) | No |

Large files are sampled: at most 200,000 splats are drawn. If a thumbnail cannot be made within
3 seconds, Windows shows its usual icon instead. The detection rules are in
[docs/detection.md](docs/detection.md).

## Safety

- Files are opened read-only and never modified.
- Every size and count in a file is checked against fixed limits before it is used, so a broken
  or malicious file is rejected instead of crashing anything. The parser and renderer are
  fuzz-tested on every change.
- The handler runs in Windows' separate thumbnail process, not inside Explorer itself.
- No network access, no telemetry, no administrator rights.
- Uninstalling removes everything the installer wrote and restores the previous `.ply` thumbnail
  handler, if there was one.

Please report security problems privately; see [SECURITY.md](SECURITY.md).

## Install and uninstall

Requirements: Windows 10/11 x64. SplatThumb installs for your user account only and needs no
administrator rights.

1. Run `SplatThumb-<version>-x64.msi`. The installer is not code-signed yet, so Windows
   SmartScreen may warn that it is from an unknown publisher. Choose "More info", then
   "Run anyway", only if you got the file from this project.
2. Open a folder with 3D Gaussian Splatting `.ply` files in Explorer and choose large icons.

To uninstall, open Settings > Apps > Installed apps, and choose Uninstall next to SplatThumb.
Uninstalling removes the files and the registry entries the installer wrote, and puts back the
`.ply` thumbnail handler that was registered before, if any.

Windows keeps thumbnails it has already made in its thumbnail cache, so they can still show
after uninstalling. To clear the cache, run Disk Cleanup, check only "Thumbnails", and choose
OK.

## Troubleshooting

**No thumbnails at all.** In Explorer, open "..." > Options > View and make sure "Always show
icons, never thumbnails" is off. Use a view with large icons; "Details" and "List" show icons only.

**A thumbnail is old, or missing after installing.** Windows reuses thumbnails from its cache.
Clear it with Disk Cleanup ("Thumbnails" only).

**Another app's thumbnail shows instead.** Only one thumbnail handler can be registered for
`.ply` per user. If another app registered one before SplatThumb was installed, SplatThumb
replaces it and puts it back when uninstalled. If an app registers its own handler after
SplatThumb was installed, that app's handler is used instead.

**A splat file shows the usual icon.** The file may use a layout SplatThumb does not read yet
(see the table above), or be too slow to draw within 3 seconds. To see why, run
`splatthumb-cli.exe info <file>` from a source build.

## Building from source

Requirements: Windows 10/11 x64, Visual Studio 2022 with "Desktop development with C++"
(includes CMake 3.25 or later).

From a Developer PowerShell for VS 2022, in the repository folder:

```powershell
cmake --workflow --preset msvc-debug
```

This configures, builds, and runs the tests. Use `msvc-release` for an optimized build.
Building does not register anything with Windows.

To check how SplatThumb classifies a file (read-only):

```powershell
build\msvc\bin\Debug\splatthumb-cli.exe info path\to\scene.ply
```

To render a thumbnail to a PNG file:

```powershell
build\msvc\bin\Release\splatthumb-cli.exe render path\to\scene.ply thumb.png --size 256
```

The background is dark gray. Add `--background transparent` for a transparent one.

To try the Explorer thumbnail handler from a local build, see
[docs/dev-registration.md](docs/dev-registration.md). To build the installer (MSI), see
[installer/README.md](installer/README.md).

## License

[MIT](LICENSE)
