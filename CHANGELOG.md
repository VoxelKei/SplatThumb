# Changelog

All notable changes to SplatThumb. Versions follow [Semantic Versioning](https://semver.org/);
0.x versions are previews.

## Unreleased

- The viewpoint window opens next to the mouse pointer (on the same monitor) instead of at
  the default spot, and stays inside the screen.
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
