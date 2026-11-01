# Changelog

All notable changes to SplatThumb. Versions follow [Semantic Versioning](https://semver.org/);
0.x versions are previews.

## Unreleased

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
