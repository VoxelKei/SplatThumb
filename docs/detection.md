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
