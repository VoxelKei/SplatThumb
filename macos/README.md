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
