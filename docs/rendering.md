# How SplatThumb draws a thumbnail

The renderer is a small CPU rasterizer in `core/src/render.cpp`. It only runs on files that
[detection](detection.md) classified as standard splats. All limits are in
`core/include/splatthumb/limits.h`, and the whole process stops after 3 seconds.

## 1. Load a sample (`loader.cpp`)

- At most 200,000 splats are read. Smaller files are read completely.
- Larger files are sampled in evenly spaced runs of 100 consecutive records (the default), or
  one record at every `N / 200,000` (uniform). Reads are at most 1 MiB each.
- This even sample sets the view (step 3). For drawing, though, an even sample of a big scene
  looks sparse: with 2.5 million splats only 1 in 12 is left, and the gaps show.

## 2. Check every value (`splat.cpp`)

A splat is skipped if any value is NaN or infinite, any coordinate is beyond ±1,000,000, its
rotation quaternion has (almost) zero length, or its opacity after the sigmoid is below 0.02.
Log scales are clamped to [-20, 10] before `exp()`. Color is the DC term only:
`clamp(0.5 + 0.28209479 * f_dc, 0, 1)`.

## 3. Choose a view (`camera.cpp`)

Framing looks at the solid core of the file: splats that are at least half opaque and not
among the largest 10%. Large, faint splats are mostly floaters. (If fewer than 100 splats
qualify, all of them are used.) The target is the per-axis median of the core, and the 1st to
99th percentile box around it sets the framing radius. The radius is also capped at twice the
distance that 90% of the core lies within, so a scene that thins out far around its middle
is not shrunk to a speck.

The camera looks from the -Z side toward +Z, as COLMAP capture cameras face the subject,
straight from the front (0° azimuth: same X as the target) and 20° above (toward -Y) with a
45° field of view. Up is -Y, as in COLMAP-based
training. Splats more than 1.5 radii in front of the target are left out, which removes
foreground clutter in scenes and nothing on a single object.

## 3b. Keep the most important splats (`loader.cpp`, `load_important_splats`)

When the file was sampled, the whole file is read again in 1 MiB pieces and the 200,000 splats
that add the most to the picture are kept instead: those with the highest opacity × the area
of their two largest axes. Splats the camera cannot see (behind it, in front of the near clip,
or outside the view with their 3-sigma reach) are left out first. In a test scene of 2.5
million splats this looks almost the same as drawing every splat, even with only 50,000.

- The pieces are read in an order spread over the file (each step about N / 1.618 pieces), so
  if time runs out the splats read so far still come from all over the scene.
- Reading stops after 1 second (`kImportanceReadMs`) or at the overall deadline. The pick is
  used if the whole file was read or at least twice as many records as the sample; otherwise
  the even sample is drawn, as is the case after a read error or when memory runs out.
- At most twice the splat limit is held while picking (about 24 MB for 200,000).
- The viewer that sets the angle still draws the even sample. The important splats are
  larger, so drawing them took 3 to 6 times as long there (0.7 to 1.5 s for a still picture
  of a 2.5 million splat scene), which made turning the view feel slow.
- `splatthumb-cli render ... --importance off` draws the even sample, as before.

## 4. Project and blend (`render.cpp`)

Each splat's 3D covariance is projected to an ellipse on screen (EWA splatting, with a 0.3 px²
low-pass). Splats behind the camera or off screen are dropped, and an ellipse's radius is at
most half the image. Splats are sorted far to near and alpha-blended. Thumbnails up to 512 px
are drawn at twice the size and downsampled. The largest thumbnail is 1024 px.

The result is RGBA with premultiplied alpha. The background is dark gray by default, so a
thumbnail looks the same in light and dark Explorer themes; it can also be transparent.

## Trying it

```powershell
splatthumb-cli render scene.ply thumb.png --size 256 --background gray
```

The command prints how long each step took.
