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

Looking a view up is a single file open, however many are saved. The settings window has no
button to delete views; to drop them all, delete the `views` folder (thumbnails go back to the
automatic view when they are made again). Uninstalling deletes it as well.

```
splatthumb-view 1
eye 1.5 -2.25 -10
target 0.125 0 3
up 0 -1 0
fov 40
radius 2.5
quality 2
```

`eye`, `target` and `up` are in the PLY's own coordinates. `fov` is the vertical field of view
in degrees, and `radius` is the size of the framed region, which also sets the near clip as for
the automatic view. Files larger than 1 KiB, missing or repeated keys, numbers that are not
finite or exceed the coordinate limit, and views that cannot make a camera are ignored, and
the thumbnail uses the automatic view. `quality` is optional: 0, 1 or 2 like the `Quality` setting below, used
for this file instead of the global setting. A number other than 0 to 2 is ignored (the view
is kept and the global quality applies); a `quality` that is not a plain number makes the file
invalid like any other bad line. Unknown keys are ignored. The parser is fuzzed by
`fuzz/fuzz_view.cpp`.

## The viewpoint window

`SplatThumb.exe --view <file.ply>` opens a window showing the file as its thumbnail would
look. The installer adds it to the right-click menu of `.ply` files as "Set view with
SplatThumb" ("SplatThumb で視点を設定"); on Windows 11 it is under "Show more options".

- Drag to turn the subject like a turntable (left/right about the file's Y axis, up/down to
  look from higher or lower, stopping 5° short of straight above or below), right-drag (or
  Shift+drag) to move it, and use the wheel to zoom. The picture is never tilted.
- "Quality of this file" chooses the splat limit for this file, or "Same as Settings". Changing
  it reloads the file at that quality; the choice is stored in the view file when saved.
- "Use this view" saves the view and asks Windows to make the thumbnail again.
  "Reset" deletes the saved view (so also the file's quality) and does the same. "Settings..." opens the settings window
  (or brings an open one to the front).

The window loads the file with the same limits and checks as the thumbnail handler. While
dragging it draws at most 30,000 splats at low resolution, then redraws at full quality.

## The settings window

Running `SplatThumb.exe` without arguments (the Start menu shortcut) opens the settings
window. It shows in Japanese or English, following the Windows display language.

- Changing a setting saves it right away; there is no Save button.
- "Remake all existing thumbnails..." asks for a folder, looks for `.ply` files in it and in
  every folder inside it, and asks Windows to make again the thumbnails of those that already
  have one in its thumbnail cache. Files without a cached thumbnail are skipped; Explorer
  makes theirs with the current settings when it shows them. Choosing a drive (for example
  `K:\`) covers the whole drive. The search never follows links or junctions, skips the
  Windows folder, the recycle bin and "System Volume Information", and stops after 100,000
  `.ply` files or 64 folder levels. Windows keeps no list of the files it has thumbnails for,
  so searching is the only way to find them. This runs in the background and can be stopped;
  closing the window stops it after the current file.

## Settings

Settings are DWORD values under `HKCU\Software\SplatThumb\Settings`. Missing or
out-of-range values use the default.

| Value | Meaning | Default |
| --- | --- | --- |
| `Enabled` | 0 = off (Windows shows its usual icon), anything else = on | on |
| `Quality` | Splats drawn at most: 0 = 50,000, 1 = 200,000, 2 = 400,000 | 1 |

The background is always dark gray, so a thumbnail looks the same in light and dark Explorer
themes. (A transparent background was tried during v0.2 and dropped.)

Windows caches thumbnails, so a changed setting or view shows only after the thumbnail is
made again. The app asks Windows for that through `IThumbnailCache` with
`WTS_FORCEEXTRACTION`.

## Uninstalling

From the v0.2 installer on, uninstalling removes `%LOCALAPPDATA%\SplatThumb` (saved views
included) and `HKCU\Software\SplatThumb`, so nothing is left behind. Installing a newer
version over an older one keeps both. See [the installer README](../installer/README.md).
