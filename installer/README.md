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
- `HKEY_CURRENT_USER\Software\SplatThumb`, which records what the MSI installed and, under
  `Previous`, what the `.ply` thumbnail registration looked like before

The `.ply\ShellEx` value is written by a small custom action (`customaction/`) instead of
the MSI's registry table. Windows Installer deletes every key it leaves empty, up through
the parents, so on its own it would also remove a `.ply` key that existed before. The
custom action saves what was there, including another application's thumbnail handler,
and on uninstall puts exactly that back. If another application has taken over the handler
since, it is left alone. Both steps undo themselves if the install or uninstall fails, and
both tell Windows that file associations changed.

No administrator rights, no `DisableProcessIsolation`, no `DllRegisterServer`. Uninstalling
from Settings > Apps removes only what the MSI wrote. CI installs and uninstalls the MSI on
every change and checks that the registry and folders are exactly as before
(`.github/scripts/Test-Msi.ps1`). Thumbnails that Windows already cached stay in its
thumbnail cache after uninstalling, until the cache is cleared (Disk Cleanup, "Thumbnails").

Do not install the MSI while a developer registration from `tools/dev/` is active:
unregister that first.

## Releasing

`.github/workflows/release.yml` publishes a release when a `v<major>.<minor>.<patch>` tag is
pushed:

1. Set the version in `project(SplatThumb VERSION ...)` in the top-level `CMakeLists.txt` and
   merge that to `main`.
2. Tag the merged commit on `main` and push the tag:
   `git tag v0.1.0 origin/main` then `git push origin v0.1.0`.

The workflow stops if the tag does not match the CMake version or the commit is not on
`main`. It builds and tests the Release configuration, builds the MSI, runs the same
install/upgrade/uninstall checks as CI, and only then creates the release with the MSI and
`SHA256SUMS.txt`. 0.x versions are marked as pre-releases. Only the final publishing job
has write access to the repository.

To try everything except publishing, run the workflow by hand from the Actions tab
("Run workflow") with the version to check.
