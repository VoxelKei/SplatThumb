# SplatThumb

[日本語](README.ja.md)

Windows Explorer thumbnails for 3D Gaussian Splatting `.ply` files.

> **Status: early development.** There is no release yet. You can build the installer from
> source (see below).

SplatThumb only renders PLY files that are 3D Gaussian Splatting scenes, detected from the file
header. Meshes and point clouds are left to Windows. It reads files and never modifies them, makes
no network connections, and collects no telemetry.

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
