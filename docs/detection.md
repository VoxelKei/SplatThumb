# How SplatThumb detects Gaussian splat PLY files

SplatThumb renders a thumbnail only for PLY files that are 3D Gaussian Splatting scenes.
Everything else (meshes, point clouds, malformed files) is left to Windows. Detection reads
only the header; vertex data is never read for this step.

The code is in `core/src/ply_header.cpp` and `core/src/classify.cpp`. All limits are in
`core/include/splatthumb/limits.h`.

## Header rules

- At most the first 64 KiB of the file are read. The header must end with `end_header`
  within that range.
- The first line is exactly `ply`. Lines end with `\n` or `\r\n`, are at most 1 KiB, and
  contain only printable ASCII and tabs.
- `format binary_little_endian 1.0` must appear before any element. ASCII and big-endian
  files are not handled.
- `comment` and `obj_info` lines are skipped. Any other unknown keyword rejects the file.
- `element <name> <count>`: the count is decimal digits only and must fit in 64 bits.
  At most 16 elements, with unique names of at most 64 characters.
- `property <type> <name>` and `property list <count type> <item type> <name>`: types are
  `char/uchar/short/ushort/int/uint/float/double` or `int8 ... float64`. At most 256
  properties per element, with unique names.

## Classification

A file is a **standard splat** (INRIA layout) when all of these hold:

- There is no `face` element.
- A `vertex` element has `x y z f_dc_0 f_dc_1 f_dc_2 opacity scale_0 scale_1 scale_2
  rot_0 rot_1 rot_2 rot_3`, all as `float`. Other properties such as `nx ny nz` and
  `f_rest_*` may also be present.

A file is a **compressed splat** (PlayCanvas layout) when there is a `chunk` element and the
`vertex` element has `packed_position packed_rotation packed_scale packed_color` as `uint`.

For both kinds:

- No element may have a list property, so every record has a fixed size.
- The vertex count is between 1 and 50,000,000.
- The header size plus every element's `count x record size` is computed with overflow
  checks and must not exceed the file size. Extra bytes after the data are allowed.

For a compressed splat, also:

- The `chunk` element has the twelve `float` properties `min_x min_y min_z max_x max_y max_z
  min_scale_x min_scale_y min_scale_z max_scale_x max_scale_y max_scale_z`. The six color
  ranges `min_r ... max_b` are optional (older files store colors directly).
- Each chunk covers 256 vertices, so there must be at least `ceil(vertex count / 256)` chunks.
- When drawing, a chunk whose ranges are not finite, or a vertex whose position falls outside the
  coordinate limit, is skipped. Unpacked values always have a fixed size, so a small file cannot
  expand into a large amount of data.

## Spatialograph Maker projects (.spgm)

A `.spgm` file is a Spatialograph Maker project. From format version 3 the app saves a small
preview image near the start of the file, and SplatThumb shows that image as it is. It does not
read the project's settings or splat data, so the view and quality settings do not apply.

The file starts with:

| Bytes | Content |
| --- | --- |
| 0-3 | `SGP` followed by a zero byte |
| 4-7 | Format version, 32-bit little-endian |
| 8-11 | Image size in bytes, 32-bit little-endian (version 3) |
| 12- | The image, PNG or JPEG |

Checks, in order. Any failure means no thumbnail (Windows shows its usual icon):

- The first 4 bytes are `SGP\0`. If not, the file is treated like any other file (a PLY check).
- The version is exactly 3. Older projects carry no image; newer ones may store it differently.
- The image size is not 0, at most 4 MiB, and the image ends within the file.
- The image starts with the PNG signature (`89 50 4E 47 0D 0A 1A 0A`) or the JPEG start
  (`FF D8 FF`).
- Only those bytes are read. They are decoded by Windows' own PNG or JPEG decoder (WIC), the one
  that matches the signature and no other installed codec.
- Images wider or taller than 2048 pixels are refused before their pixels are decoded.

The image is scaled down to the thumbnail size, keeping its shape (it is never enlarged), and
any transparent parts are laid over the same dark gray as the PLY thumbnails.
