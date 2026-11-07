# macOS Quick Look thumbnails (M1 spike)

Prototype only: a host app (`SplatThumb.app`, `com.voxelkei.splatthumb`) that carries a
Quick Look thumbnail extension (`SplatThumbThumbnail.appex`,
`com.voxelkei.splatthumb.thumbnail`). The extension draws 3D Gaussian Splatting PLY files
with the shared `core/` library and shows the preview image embedded in `.spgm` projects.
Anything else (normal meshes, broken files, timeouts) is declined so macOS can show what it
would show otherwise.

## Build

Needs Xcode and CMake 3.25+. The project uses the Xcode generator because app extensions
and signing need it.

```bash
cmake -S macos -B build/macos-xcode -G Xcode -DSPLATTHUMB_TEAM_ID=<your team ID>
cmake --build build/macos-xcode --config Release
```

Leave out `SPLATTHUMB_TEAM_ID` to sign ad hoc ("Sign to Run Locally"). The result is
`build/macos-xcode/Release/SplatThumb.app`.

## Try it

1. Copy `SplatThumb.app` to `~/Applications` and open it once (this registers the extension).
2. `pluginkit -m -v -i com.voxelkei.splatthumb.thumbnail` should list it.
3. `qlmanage -r cache` and look at a `.ply` in Finder, or
   `qlmanage -t -s 256 -o /tmp/out file.ply`.
4. Logs: `log stream --predicate 'subsystem == "com.voxelkei.splatthumb"'`.

## Remove

Quit the app, delete `~/Applications/SplatThumb.app`, then `qlmanage -r` and
`qlmanage -r cache`. The extension disappears from `pluginkit` right away. A build folder
copy registers too (Xcode registers what it builds); remove it with
`lsregister -u build/macos-xcode/Release/SplatThumb.app`.

What macOS leaves behind: the empty sandbox folders
`~/Library/Containers/com.voxelkei.splatthumb{,.thumbnail}` and
`~/Library/Application Scripts/com.voxelkei.splatthumb{,.thumbnail}` (a few KB).

## What the spike found

- Finder never asks a third-party extension for `.ply`: Apple's
  `com.apple.HydraQLThumbnailExtension` claims `public.polygon-file-format` and wins, even
  after `pluginkit -e use`. It draws 3DGS files as square points, often upside down.
- For `.spgm` (a type only this app declares) the extension is used and works.

## Option B: custom Finder icons

Because Quick Look cannot take over `.ply`, the app (and the `splatthumb-icon` command) can
instead put the SplatThumb picture on each 3DGS PLY as a custom Finder icon:

```bash
build/macos-xcode/Release/splatthumb-icon set <file-or-folder>...
build/macos-xcode/Release/splatthumb-icon remove <file-or-folder>...
build/macos-xcode/Release/splatthumb-icon watch <folder>
```

In the app: a list of registered folders (remembered as security-scoped bookmarks; icons
are set when a folder is added), "remove all icons in the registered folders", and a
checkbox to watch them. Watching runs either only while the app is open, or resident: the
app registers itself as a login item (`SMAppService.mainApp`), keeps running in the menu
bar after its window is closed, and on the next launch catches up and resumes watching.
Switching back unregisters the login item. Folders inside a registered folder are already
covered and are not added twice.

- The icon lives in the file's extended attributes (`com.apple.ResourceFork`,
  `com.apple.FinderInfo`); the PLY data is unchanged. A fingerprint is kept in
  `com.voxelkei.splatthumb.icon` so unchanged files are skipped and only icons SplatThumb
  set are removed.
- `setIcon` bumps the modification date; it is put back. Removal also drops the empty
  resource fork `setIcon:nil` leaves, so the file ends up as before.
- Shown in icon and list view. The column/gallery preview and Space-bar Quick Look still
  come from Apple's Hydra extension.
- The icon survives Finder copies and `cp`; it is lost on filesystems and transfers that
  drop extended attributes (FAT/exFAT, many uploads, Git).
- The watcher waits until a file has not changed for 3 s, so files still being copied are
  not read half-written. Removing icons stops watching first.
- About 1 s per file at 512 px (188k splats).

## Thumbnail angle

"PLY を開いてアングルを決める…" (or dropping a `.ply` on the window or Dock icon, or Finder's
Open With → SplatThumb; the app is only an alternate handler, never the default) opens a
viewer like the Windows one: drag turns, right-drag or Shift-drag moves, scroll or pinch
zooms, and the axes show while turning. "この視点をサムネイルにする" saves the view and remakes
that file's icon; "リセット" removes the saved view and goes back to the automatic one.

Views use the shared core (`view_store`), so the files are the same as on Windows
(`<fingerprint>.view`, keyed by the file's contents, so identical copies share a view).
They live in the app's container:
`~/Library/Containers/com.voxelkei.splatthumb/Data/Library/Application Support/SplatThumb/views`.
`splatthumb-icon` reads the same folder. Deleting the app's container removes all saved
views; icons made from them stay until removed with "すべて外す".
