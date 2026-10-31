# How SplatThumb draws a thumbnail

The renderer is a small CPU rasterizer in `core/src/render.cpp`. It only runs on files that
[detection](detection.md) classified as standard splats. All limits are in
`core/include/splatthumb/limits.h`, and the whole process stops after 3 seconds.

## 1. Load a sample (`loader.cpp`)

- At most 200,000 splats are read. Smaller files are read completely.
- Larger files are sampled in evenly spaced runs of 100 consecutive records (the default), or
  one record at every `N / 200,000` (uniform). Reads are at most 1 MiB each.

## 2. Check every value (`splat.cpp`)

A splat is skipped if any value is NaN or infinite, any coordinate is beyond ±1,000,000, its
rotation quaternion has (almost) zero length, or its opacity after the sigmoid is below 0.02.
Log scales are clamped to [-20, 10] before `exp()`. Color is the DC term only:
`clamp(0.5 + 0.28209479 * f_dc, 0, 1)`.

## 3. Choose a view (`camera.cpp`)

The target is the per-axis median of the positions. The 1st to 99th percentile box around it
sets the framing radius, which ignores floaters. The camera looks down from 45° azimuth and
20° elevation with a 45° field of view. Up is -Y, as in COLMAP-based training.

## 4. Project and blend (`render.cpp`)

Each splat's 3D covariance is projected to an ellipse on screen (EWA splatting, with a 0.3 px²
low-pass). Splats behind the camera or off screen are dropped, and an ellipse's radius is at
most half the image. Splats are sorted far to near and alpha-blended. Thumbnails up to 512 px
are drawn at twice the size and downsampled. The largest thumbnail is 1024 px.

The result is RGBA with premultiplied alpha. The background is transparent, or optionally
dark gray.

## Trying it

```powershell
splatthumb-cli render scene.ply thumb.png --size 256 --background transparent
```

The command prints how long each step took.
