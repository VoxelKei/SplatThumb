# installer

Per-user (HKCU) MSI built with the [WiX Toolset](https://wixtoolset.org/) v5. The version
is pinned in `.config/dotnet-tools.json`, so every build uses the same WiX.

## Build

Requires the .NET SDK (6 or later) in addition to the usual build tools. From the
repository folder:

```powershell
cmake --workflow --preset msvc-release
cmake --build --preset msvc-release --target msi
```

The first run downloads WiX through `dotnet tool restore`. The result is
`build\msvc\msi\Release\SplatThumb-<version>-x64.msi`. The version comes from
`project(VERSION)` in the top-level `CMakeLists.txt`.

## What it installs

- `%LOCALAPPDATA%\Programs\SplatThumb\splatthumb_shellext.dll` and `LICENSE.txt`
- The same four `HKEY_CURRENT_USER` values as [the developer scripts](../docs/dev-registration.md),
  with `InprocServer32` pointing at the installed DLL
- `HKEY_CURRENT_USER\Software\SplatThumb`, which records what the MSI installed

No administrator rights, no `DisableProcessIsolation`, no `DllRegisterServer`. Uninstalling
from Settings > Apps removes only what the MSI wrote. CI installs and uninstalls the MSI on
every change and checks that the registry and folders are exactly as before
(`.github/scripts/Test-Msi.ps1`).

Do not install the MSI while a developer registration from `tools/dev/` is active:
unregister that first.
