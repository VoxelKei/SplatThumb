# <img src="app/icon/splatthumb.svg" width="40" height="40" alt=""> SplatThumb

See your Gaussian splats in Explorer and Finder before you open them.

[![MIT License](https://img.shields.io/badge/license-MIT-0f766e?style=flat-square)](LICENSE)
![Windows 10 / 11 x64](https://img.shields.io/badge/Windows_10_%2F_11-x64-0f766e?style=flat-square)
![macOS 13+](https://img.shields.io/badge/macOS-13%2B-0f766e?style=flat-square)
![Preview v0.5](https://img.shields.io/badge/preview-v0.5-64748b?style=flat-square)

[Download](../../releases) | [Install (Windows)](#install-and-uninstall-windows) | [Install (Mac)](#install-and-remove-mac) |
[Choosing the view](#choosing-the-view) | [Safety](#safety) | [Build](#building-from-source) | [日本語](README.ja.md)

<img src="docs/media/demo.webp" width="960" alt="Splat thumbnails appearing in Explorer, then choosing a file's view by turning it in the SplatThumb window">

SplatThumb shows 3D Gaussian Splatting `.ply` files as thumbnails in Windows Explorer and as
Finder icons on the Mac. It recognises splat scenes from the file header and leaves meshes and
point clouds alone. On Windows, files are only read, never modified; on the Mac, the icon is
added to the file's extended attributes but the splat data is never changed. There is no
network access and no telemetry.

- Previews standard (INRIA) and PlayCanvas / SuperSplat compressed PLY (on Windows, at every
  icon size).
- Lets you pick the view for each file by turning it like a turntable. The view is stored in
  your profile, keyed by the file's contents, and never written into the file.
- Keeps big scenes quick by drawing 50,000 to 400,000 splats, chosen globally or per file. The largest and most opaque splats are kept, so big scenes do not look sparse.
- Treats every file as untrusted: fixed limits, a fuzz-tested parser and renderer, Windows'
  separate thumbnail process, and the macOS app sandbox.
- Removes cleanly: the Windows uninstaller restores any thumbnail handler that was there
  before, and the Mac app can take all its icons off again.
- Also shows the preview saved in [Spatialograph Maker](https://www.spatialograph.app/)
  projects (`.spgm`).

## Windows and Mac versions

A Mac version is available from v0.5.0 (see [Install and remove (Mac)](#install-and-remove-mac)). macOS does not let other apps replace Finder's thumbnails for `.ply`, so it works
differently:

| | Windows | Mac |
|---|---|---|
| How pictures appear | Explorer thumbnail, for every `.ply` | Custom Finder icon, set by the app on `.ply` files in folders you register |
| When | Automatically | When a folder is added, and while watching (only while the app is open, or always via a login item) |
| The file | Only read, never modified | Data unchanged, but the icon is stored in the file's extended attributes (modified date kept) |
| Where it shows | Every Explorer view | Icon and list views; Finder's preview pane and Quick Look still show Apple's own preview |
| Copies | Thumbnail is made wherever the file is | Icon goes with Finder copies; lost on exFAT/FAT drives, zip, uploads and Git |
| Choosing the view | Right-click → "Set view with SplatThumb" | Right-click → Open With → SplatThumb |
| Saved views | `.view` files, keyed by the file's contents | The same `.view` files, so a view can be copied between the two |
| `.spgm` projects | Thumbnail | Thumbnail (Quick Look) |
| Removing it | Uninstall from Settings; everything is put back | Click "Remove All Icons in These Folders" in the app, then delete the app |
| Requirements | Windows 10/11 x64 | macOS 13 or later (Apple silicon or Intel) |

## Install and uninstall (Windows)

Requirements: Windows 10/11 x64. SplatThumb installs for your user account only and needs no
administrator rights.

1. Run `SplatThumb-<version>-x64.msi`. The installer is not code-signed yet, so Windows
   SmartScreen may warn that it is from an unknown publisher. Choose "More info", then
   "Run anyway", only if you got the file from this project.
2. Open a folder with 3D Gaussian Splatting `.ply` files in Explorer and choose large icons.

To uninstall, open Settings > Apps > Installed apps (on Windows 10, Settings > Apps > Apps &
features), and choose Uninstall next to SplatThumb.
Uninstalling removes the files and the registry entries the installer wrote, your saved views
and settings, and puts back the `.ply` and `.spgm` thumbnail handlers that were registered
before, if any.

Windows keeps thumbnails it has already made in its thumbnail cache, so they can still show
after uninstalling. To clear the cache, run Disk Cleanup, check only "Thumbnails", and choose
OK.

## Install and remove (Mac)

Requirements: macOS 13 or later, Apple silicon or Intel.

1. Download `SplatThumb-<version>-macos.dmg` from [Releases](../../releases), open it and drag
   SplatThumb to Applications. The app is notarized by Apple.
2. Open SplatThumb once and use "Add Folder…" to choose a folder with 3D Gaussian Splatting
   `.ply` files. The 3DGS files in it get SplatThumb's picture as their Finder icon (the
   splat data is not changed).
3. To give new or changed files icons too, turn on "Watch these folders" and choose "Only
   while the app is open" or "Always" (starts at login and keeps running in the menu bar).

To choose the angle, right-click a `.ply` in Finder and choose Open With → SplatThumb. Drag to
turn, right-drag or Shift-drag to move, and scroll or pinch to zoom. Click "Use this view" and
the icon is remade from that angle.

"Splats used for icons" in the main window sets how many splats are drawn at most (up to
50,000, 200,000 or 400,000). After changing it, click "Remake All Icons in These Folders" to
update the icons already set. The angle window has the same choice for one file, or "Same as
the app setting"; it is saved with the angle.

This also works one file at a time for a `.ply` outside the registered folders. Such a file is
not watched and is left out of "Remake All" and "Remove All". To take its icon off, select it
in Finder, press ⌘I, click the small icon at the top left and press Delete.

The app is shown in Japanese or English, following the macOS language order.

To remove SplatThumb:

1. In SplatThumb, click "Remove All Icons in These Folders". The icons come off and the files
   are as before.
   Icons you set one by one on files outside those folders come off with ⌘I as described above.
2. If you chose "Always", switch back to "Only while the app is open" (this removes the login
   item).
3. Quit SplatThumb and delete it from Applications.
4. If you like, also delete what is left of its settings and saved angles:
   `~/Library/Containers/com.voxelkei.splatthumb`, `~/Library/Containers/com.voxelkei.splatthumb.thumbnail`,
   `~/Library/Application Scripts/com.voxelkei.splatthumb` and
   `~/Library/Application Scripts/com.voxelkei.splatthumb.thumbnail` (saved angles are in the
   first one, under `Data/Library/Application Support/SplatThumb/views`).

The icon is kept in the file's extended attributes. Copied to an exFAT/FAT drive or a Windows
share, it can turn into a hidden file starting with `._` next to the `.ply`; on Windows you
can delete those.

## Update

On Windows, run the newer `SplatThumb-<version>-x64.msi` over the installed one. There is no need to
uninstall first: the new version replaces the old one, and your saved views and settings are
kept. Close the viewpoint and settings windows before installing.

Thumbnails Windows already made stay as they were until they are made again. To bring a
folder up to date with the new version, open "SplatThumb" from the Start menu and use "Remake
all existing thumbnails...".

On the Mac, quit SplatThumb and drag the new SplatThumb from the newer `.dmg` to Applications,
replacing the old one. Your folders and saved angles are kept. To redraw the icons with the
new version, click "Remake All Icons in These Folders".

## Choosing the view

This section is about Windows; for the Mac, see [Install and remove (Mac)](#install-and-remove-mac).

When the automatic view frames a file badly, you can pick the view for that file. To do so, right-click it in Explorer and choose "Set view with SplatThumb" (on
Windows 11, it is under "Show more options").

- Drag to turn the subject around its vertical (Y) axis or look from higher or lower,
  right-drag to move it, and use the wheel to zoom.
- "Splats used for the thumbnail" picks how many splats this file's thumbnail draws; "Same as
  Settings" follows the settings window.
- "Use this view" saves the view (and the splat count), updates the thumbnail and closes the
  window. "Reset" goes back to the automatic view and the settings window's splat count.
  "Settings..." opens the settings window described below.

The view is remembered by the file's contents, so it stays when you move or rename the file on
the same PC. It does not travel with a copy to another PC, and it is dropped if the file's
contents change.

Saved views are kept in `%LOCALAPPDATA%\SplatThumb\views`, one small text file per file.
Deleting that folder sends every thumbnail back to the automatic view the next time it is
made; uninstalling deletes it too. Details: [docs/views-and-settings.md](docs/views-and-settings.md).

## Settings

This section is about Windows. The settings and view windows are shown in Japanese or English, following the Windows display
language.

Open "SplatThumb" from the Start menu to:

- turn SplatThumb's thumbnails off or on,
- choose "Splats used for the thumbnail" (up to 50,000, 200,000 or 400,000),
- remake the existing thumbnails in a folder and all folders inside it after changing a
  setting.

The Mac app has the same splat-count choice; see [Install and remove (Mac)](#install-and-remove-mac).

## Which files get a thumbnail

| File | Thumbnail |
| --- | --- |
| 3D Gaussian Splatting PLY in the standard (INRIA) layout, binary little-endian, as written by most training tools | Yes |
| The same with extra properties such as normals or `f_rest_*` | Yes (only the base color is drawn) |
| PlayCanvas / SuperSplat compressed PLY (`.compressed.ply`) | Yes (only the base color is drawn) |
| Meshes, point clouds, ASCII or big-endian PLY | No, the usual icon stays |
| Other splat formats (`.spz`, `.splat`, `.sog`, ...) | No |

The Mac version gives icons to the same files.

Large files are thinned out: at most 200,000 splats are drawn (or 50,000 or 400,000 with the
"Splats used for the thumbnail" setting on Windows, "Splats used for icons" on the Mac). SplatThumb keeps the splats that add the most
to the picture (the largest and most opaque), reading the file for at most one extra second. If
a picture cannot be made within 3 seconds, the usual icon stays. The detection rules are in
[docs/detection.md](docs/detection.md).

## Troubleshooting

**No thumbnails at all.** In Explorer, open "..." > Options > View and make sure "Always show
icons, never thumbnails" is off. Use a view with large icons; "Details" and "List" show icons only.

**A thumbnail is old, or missing after installing.** Windows reuses thumbnails from its cache.
Use "Remake all existing thumbnails" in the settings, or clear the cache with Disk Cleanup
("Thumbnails" only).

**Another app's thumbnail shows instead.** Only one thumbnail handler can be registered for
`.ply` (or `.spgm`) per user. If another app registered one before SplatThumb was installed, SplatThumb
replaces it and puts it back when uninstalled. If an app registers its own handler after
SplatThumb was installed, that app's handler is used instead.

**A splat file shows the usual icon.** The file may use a layout SplatThumb does not read yet
(see the table above), or be too slow to draw within 3 seconds. If you think it should work,
please open an issue (without attaching the file if it is a real-world scan). If you build from
source, `splatthumb-cli.exe info <file>` shows why.

**Mac: a file gets no icon.** Check that it is in a folder registered in the app (or a folder
inside one). Files that are not 3DGS get no icon, and the app's log says "not3DGS" and why.
The column view's preview and Quick Look (Space) always show Apple's own preview, even for files
with an icon.

## Safety

- On Windows, files are opened read-only and never modified. On the Mac, the only thing added
  to a file is the icon (in its extended attributes); the splat data is never changed. Saved
  views and settings are kept in your user profile, not in or next to the files.
- Every size and count in a file is checked against fixed limits before it is used, so a broken
  or malicious file is rejected instead of crashing anything. The parser and renderer are
  fuzz-tested on every change.
- On Windows, the handler runs in Windows' separate thumbnail process, not inside Explorer
  itself. On the Mac, the app and its extension run in the macOS app sandbox and can only reach
  the folders and files you choose.
- No network access, no telemetry, no administrator rights.
- Uninstalling on Windows removes everything the installer wrote, along with your saved views and settings,
  and restores the previous thumbnail handlers, if there were any.

Please report security problems privately; see [SECURITY.md](SECURITY.md).

## Spatialograph Maker projects (.spgm)

<a href="https://www.spatialograph.app/"><img src="docs/media/spatialograph-maker.jpg" width="400" alt="Spatialograph Maker editing a Gaussian splat scene"></a>

SplatThumb also shows thumbnails for projects of [Spatialograph Maker](https://www.spatialograph.app/), a 3D
Gaussian Splatting editor by the same author. It does not draw the splats: it shows the preview image that the app
saves in the project file, as it is. The view and quality settings
do not apply. Projects saved by older versions have no preview image and keep the usual icon.

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
[installer/README.md](installer/README.md). To build the Mac app and its `.dmg`, see
[macos/README.md](macos/README.md).

## Changes

See [CHANGELOG.md](CHANGELOG.md).

## Contributing

Bug reports and ideas are welcome as [issues](../../issues). Pull requests are not accepted for
now; see [CONTRIBUTING.md](CONTRIBUTING.md).

## Author

VoxelKei ([@VoxelKei on X](https://x.com/VoxelKei)). Please send bug reports and requests to
[issues](../../issues), not to X.

## License

[MIT](LICENSE)
