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

- `%LOCALAPPDATA%\Programs\SplatThumb\splatthumb_shellext.dll`, `SplatThumb.exe` and
  `LICENSE.txt`
- A Start menu shortcut, "SplatThumb", which opens the settings window
- The same four `HKEY_CURRENT_USER` values as [the developer scripts](../docs/dev-registration.md),
  with `InprocServer32` pointing at the installed DLL
- A right-click item for `.ply` files, under
  `HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\.ply\shell\SplatThumb.SetView`,
  which runs `SplatThumb.exe --view "<file>"`. Its text comes from `SplatThumb.exe`, so it is
  shown in Japanese or English. On Windows 11 it is under "Show more options".
- `HKEY_CURRENT_USER\Software\SplatThumb`, which records what the MSI installed and, under
  `Previous`, what the `.ply` registration looked like before

The `.ply\ShellEx` value and the right-click item are written by a small custom action
(`customaction/`) instead of the MSI's registry table. Windows Installer deletes every key it leaves empty, up through
the parents, so on its own it would also remove a `.ply` key that existed before. The
custom action saves what was there, including another application's thumbnail handler,
and on uninstall puts exactly that back. If another application has taken over the handler
since, it is left alone. Both steps undo themselves if the install or uninstall fails, and
both tell Windows that file associations changed. The custom action works through a list of
extensions (`kExtensions` in `customaction/ply_registration.h`, only `.ply` for now), and
saves the state of each one under its own key.

Uninstalling also deletes what `SplatThumb.exe` saved for the user: the settings
(`HKEY_CURRENT_USER\Software\SplatThumb\Settings`) and the saved views
(`%LOCALAPPDATA%\SplatThumb`). This runs only after the uninstall has succeeded, and not
when installing a newer version replaces an older one, so upgrading keeps them.

No administrator rights, no `DisableProcessIsolation`, no `DllRegisterServer`. Uninstalling
from Settings > Apps removes what the MSI wrote and what the app saved, and nothing else. CI installs and uninstalls the MSI on
every change and checks that the registry and folders are exactly as before
(`.github/scripts/Test-Msi.ps1`), including after upgrading from the previous release.

Windows keeps two caches of its own that uninstalling does not clear: thumbnails it already
made stay in its thumbnail cache until the cache is cleared (Disk Cleanup, "Thumbnails"),
and the right-click item's text and the program's name may stay in its "MuiCache" of
display names (`HKEY_CURRENT_USER\Software\Classes\Local Settings`), as for any program
that was run. Neither has any effect once SplatThumb is gone.

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
