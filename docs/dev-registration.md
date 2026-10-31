# Trying the thumbnail handler (developers)

The MSI is the supported way to install SplatThumb. To try a DLL you built yourself, the
scripts in `tools/dev/` register it for your user account only, and undo it exactly.

Do not use them while SplatThumb is installed from the MSI, and unregister before installing
the MSI: both use the same CLSID, so each would overwrite the other's registration.

## What they change

Only `HKEY_CURRENT_USER`, and no administrator rights are needed:

| Key | Value |
| --- | --- |
| `Software\Classes\CLSID\{09C68D16-D21F-4B95-97A1-29B45AEBF4CC}` | `SplatThumb Thumbnail Handler` |
| `...\InprocServer32` | path of your DLL, `ThreadingModel = Apartment` |
| `Software\Classes\.ply\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}` | `{09C68D16-D21F-4B95-97A1-29B45AEBF4CC}` |

`DisableProcessIsolation` is never set, so Explorer runs the handler in a separate
process. Before registering, the script saves every key it touches to
`%LOCALAPPDATA%\SplatThumb-dev\registration-state.json`, and it lists other `.ply`
thumbnail handlers it finds (in HKLM or from other applications) without changing them.

## Register

```powershell
cmake --workflow --preset msvc-release
.\tools\dev\Register-Dev.ps1 -DllPath build\msvc\bin\Release\splatthumb_shellext.dll -WhatIf
.\tools\dev\Register-Dev.ps1 -DllPath build\msvc\bin\Release\splatthumb_shellext.dll
```

`-WhatIf` only shows the changes. If PowerShell refuses to run scripts, start it with
`powershell -ExecutionPolicy Bypass -File .\tools\dev\Register-Dev.ps1 ...` for this one run.

Explorer may keep showing old icons from its thumbnail cache. Restart Explorer, or remove
"Thumbnails" with Disk Cleanup. Thumbnails are not shown at all when Folder Options has
"Always show icons, never thumbnails" turned on.

Windows keeps the DLL loaded in its thumbnail process (`dllhost.exe`) for a while after
drawing, so a rebuild may fail to overwrite it. Wait a minute, or sign out and back in.

## Unregister

```powershell
.\tools\dev\Unregister-Dev.ps1 -WhatIf
.\tools\dev\Unregister-Dev.ps1
```

Keys that did not exist before are removed and the values of the others are put back.
The script then compares the registry with the saved state and stops with an error if
they differ. The state file is kept, renamed `*.restored-<time>.json`, as a record.
