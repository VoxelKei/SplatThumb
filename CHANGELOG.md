# Changelog

All notable changes to SplatThumb. Versions follow [Semantic Versioning](https://semver.org/);
0.x versions are previews.

## 0.5.2 - 2026-11-08

- Big scenes no longer look sparse. When a file has more splats than the thumbnail draws,
  SplatThumb now keeps the ones that add the most to the picture (the largest and most opaque
  ones the camera can see) instead of an even sample. It reads the file for at most one more
  second to pick them; if that is not enough, it draws the even sample as before. Applies to
  Windows thumbnails, Mac Finder icons, and the CLI (`--importance off` turns it off). The
  window for setting the view still draws the even sample, so it stays quick to turn.
- The window for setting the view (Windows and Mac) draws its still picture at most 512 pixels
  across, without supersampling, and stretches it to the window, so it redraws faster.
- Mac: "Splats used for icons" (up to 50,000, 200,000 or 400,000) in the main window, and the
  same choice for one file in the angle window ("Same as the app setting" or a count of its
  own, saved with the angle), as on Windows. Icons whose count changed are redrawn when they
  are checked again; "Remake All Icons in These Folders" updates them all at once.

## 0.5.1 - 2026-11-08

- Mac: the "How to set the angle" box says that Open With also works for a `.ply` outside the
  registered folders, that such a file is not watched or covered by "Remake All" and "Remove
  All", and how to take its icon off in Finder (Get Info). The README says the same.
- No changes to Windows behaviour.

## 0.5.0 - 2026-11-08

- A Mac version (macOS 13 or later, Apple silicon and Intel), as a notarized `.dmg`. macOS does
  not let other apps replace Finder's thumbnails for `.ply`, so it works differently from
  Windows (see the README):
  - The app puts SplatThumb's picture on 3DGS `.ply` files as their custom Finder icon, in
    folders you register. The splat data and the modified date are unchanged; the icon is
    kept in the file's extended attributes and can be removed again.
  - Registered folders can be watched while the app is open, or always (a login item with a
    menu bar icon), so new and changed files get icons too. "Remake All Icons" and "Remove
    All Icons" act on all registered folders.
  - A viewer like the Windows one sets the angle, opened from Finder's Open With menu. Views
    are saved in the same `.view` format as on Windows.
  - Spatialograph Maker projects (`.spgm`) get Quick Look thumbnails.
  - English and Japanese, following the system language.
- On Apple platforms, saved views are read and written without `std::from_chars` for floats,
  so the core builds with Apple's compiler. Windows behaviour is unchanged.

## 0.4.0 - 2026-11-02

- SplatThumb has its own icon (an orange square with a white splat), shown on its windows, the
  taskbar, the Start menu, Settings > Apps and the .ply right-click item.
- The settings window shows the version, copyright and license, with links to the GitHub
  repository and to Spatialograph Maker. Links open in the browser only when clicked.
- The splat-count list in the viewpoint window is only as wide as its entries, followed by the
  file's total (for example "/ 2,000,000 in the file"), read from the PLY header.
- While you drag in the viewpoint window, the file's X (red), Y (green) and Z (blue) axes are
  drawn through the origin as a guide. Thumbnails never show them.
- The viewpoint window opens at the size it had when it was last closed.
- "Use this view" shows a slim progress bar while the thumbnail is remade (one step per
  thumbnail size), then closes the viewpoint window.
- The viewpoint window's controls are centred, and its buttons read Reset, Settings..., Use this
  view, Close from left to right.
- The viewpoint window opens next to the mouse pointer (on the same monitor) instead of at
  the default spot, and stays inside the screen.
- Quality can be set per file in the viewpoint window ("Splats used for the thumbnail"). It is saved
  with the view (optional `quality` line in the view file); older versions ignore it.
- The automatic view looks straight from the front (same X as the subject, -Z side) and a
  little from above, instead of 45° to the side. Saved views are unchanged.
- Dragging in the viewpoint window turns the subject like a turntable: left/right turns about
  the file's Y axis and up/down tilts the camera, so the picture no longer leans. The camera
  stops just short of straight above or below. Ctrl+drag (tilting the picture) is removed.
- The settings window opens in the centre of the screen (the monitor with the mouse pointer)
  instead of near the top-left corner.

## 0.3.0 - 2026-11-01

- Spatialograph Maker projects (`.spgm`) show the preview image saved in the project (project
  format version 3). Older projects without an image keep the usual icon.
- "Remake all existing thumbnails" also covers `.spgm` files.

## 0.2.0 - 2026-10-31

- Choose the thumbnail view for a file: right-click it and choose "Set view with SplatThumb".
  Views are remembered by the file's contents and kept in your user profile.
- Settings window (Start menu > SplatThumb): turn thumbnails off or on, choose the quality, and
  remake the existing thumbnails in a folder.
- PlayCanvas / SuperSplat compressed PLY (`.compressed.ply`) thumbnails.
- The settings and view windows follow the Windows display language (Japanese or English).
- Uninstalling also deletes saved views and settings.

## 0.1.0 - 2026-10-31

- First release: Explorer thumbnails for 3D Gaussian Splatting PLY files (standard INRIA
  layout, binary little-endian).
- Per-user MSI installer; no administrator rights. Uninstalling restores the previous `.ply`
  thumbnail handler, if there was one.
