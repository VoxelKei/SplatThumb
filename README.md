# SplatThumb

[日本語](README.ja.md)

Windows Explorer thumbnails for 3D Gaussian Splatting `.ply` files.

> **Status: early development.** Nothing is ready to install yet.

SplatThumb only renders PLY files that are 3D Gaussian Splatting scenes, detected from the file
header. Meshes and point clouds are left to Windows. It reads files and never modifies them, makes
no network connections, and collects no telemetry.

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

## License

[MIT](LICENSE)
