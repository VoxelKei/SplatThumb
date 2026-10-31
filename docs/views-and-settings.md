# Saved views and settings

From v0.2, a thumbnail can use a viewpoint the user picked for that file, and a few
settings. This page describes where they are stored and how the thumbnail handler finds
them. The handler only ever reads them; the SplatThumb app writes them.

The code is in `core/src/fingerprint.cpp`, `core/src/view.cpp`, `core/src/view_store.cpp`,
`core/src/settings.cpp` and `shellext/user_store.cpp`.

## Why not next to the file

Explorer gives the thumbnail handler the file's contents, not its path, and runs it in a
separate, isolated process. Reading anything next to the PLY (a sidecar file or an NTFS
alternate data stream) would need the path, which in turn needs process isolation turned
off. SplatThumb keeps isolation on and never writes to PLY files, so views are stored per
user and looked up by the file's contents instead.

## Fingerprints

A fingerprint is the first 16 bytes of a SHA-256 over:

- a fixed tag (`splatthumb-fingerprint-1`) and the file size,
- the first 64 KiB of the file,
- for larger files, 64 pieces of 4 KiB at evenly spaced offsets, the last ending at the
  end of the file.

At most 320 KiB are read, whatever the file size. On a test PC this took 12 to 19 ms for
files of 20 to 129 MB. So:

- moving or renaming a file on the same PC keeps its view;
- copying it to another PC does not carry the view along;
- changing its contents (for example retraining) usually drops back to the automatic view.

The handler computes a fingerprint only when the views folder exists, so nothing extra is
read until a view has been saved.

## View files

Views are kept in `%LOCALAPPDATA%\SplatThumb\views\`, one small text file per file:

```
views\<first two hex digits>\<fingerprint>.view
```

Looking a view up is a single file open, however many are saved.

```
splatthumb-view 1
eye 1.5 -2.25 -10
target 0.125 0 3
up 0 -1 0
fov 40
radius 2.5
```

`eye`, `target` and `up` are in the PLY's own coordinates. `fov` is the vertical field of view
in degrees, and `radius` is the size of the framed region, which also sets the near clip as for
the automatic view. Files larger than 1 KiB, missing or repeated keys, numbers that are not
finite or exceed the coordinate limit, and views that cannot make a camera are ignored, and
the thumbnail uses the automatic view. Unknown keys are ignored. The parser is fuzzed by
`fuzz/fuzz_view.cpp`.

## The viewpoint window

`SplatThumb.exe --view <file.ply>` opens a window showing the file as its thumbnail would
look. The installer adds it to the right-click menu of `.ply` files as "Set view with
SplatThumb" ("SplatThumb で視点を設定"); on Windows 11 it is under "Show more options".

- Drag to turn the subject, right-drag (or Shift+drag) to move it, Ctrl+drag to turn the
  picture, and use the wheel to zoom. The two 90° buttons fix files stored on their side.
- "Training camera" is available when a `cameras.json` from the original 3DGS training
  code is found in the PLY's folder or up to three folders above it. It shows the view from
  its first camera. Only the first camera is read, from at most 1 MiB of the file.
- "Use this view" saves the view and asks Windows to make the thumbnail again.
  "Automatic" deletes the saved view and does the same.

The window loads the file with the same limits and checks as the thumbnail handler. While
dragging it draws at most 30,000 splats at low resolution, then redraws at full quality.

## The settings window

Running `SplatThumb.exe` without arguments (the Start menu shortcut) opens the settings
window. It shows in Japanese or English, following the Windows display language.

- "Save settings" writes the values below.
- "Delete all saved views..." removes the whole views folder after asking.
- "Remake thumbnails in a folder..." asks Windows to make the thumbnails of every `.ply`
  file directly in the chosen folder again (subfolders are not included, at most 10,000
  files). With the training-camera box ticked, files that have no saved view but have a
  `cameras.json` nearby first get a saved view from its first camera. This runs in the
  background and can be stopped; closing the window stops it after the current file.

## Settings

Settings are DWORD values under `HKCU\Software\SplatThumb\Settings`. Missing or
out-of-range values use the default.

| Value | Meaning | Default |
| --- | --- | --- |
| `Enabled` | 0 = off (Windows shows its usual icon), anything else = on | on |
| `Background` | 0 dark gray, 1 transparent, 2 white, 3 black | 0 |
| `Quality` | Splats drawn at most: 0 = 50,000, 1 = 200,000, 2 = 400,000 | 1 |

Windows caches thumbnails, so a changed setting or view shows only after the thumbnail is
made again. The app asks Windows for that through `IThumbnailCache` with
`WTS_FORCEEXTRACTION`.

## Uninstalling

From the v0.2 installer on, uninstalling removes `%LOCALAPPDATA%\SplatThumb` (saved views
included) and `HKCU\Software\SplatThumb`, so nothing is left behind. Installing a newer
version over an older one keeps both. See [the installer README](../installer/README.md).
