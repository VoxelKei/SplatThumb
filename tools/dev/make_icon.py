"""Draws the SplatThumb icon (an orange rounded square with a white splat) and writes
app/icon/splatthumb.ico and app/icon/splatthumb.svg.

Each size is drawn on its own so the square's edges fall on whole pixels at 16-48 px.
Requires Pillow. Run from the repository root: python tools/dev/make_icon.py
"""
import io
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw

ORANGE = (0xF0, 0x78, 0x00)
WHITE = (0xFF, 0xFF, 0xFF)
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
# In a 256 px box: margin, corner radius (fraction of the square), and the splat's
# half-axes (fraction of the square) and angle in degrees (y down).
MARGIN = 6
RADIUS = 0.08
SPLAT_A = 0.479
SPLAT_B = 0.18
SPLAT_ANGLE = -41.8


def draw(size: int) -> Image.Image:
    margin = round(size * MARGIN / 256)
    side = size - 2 * margin
    ss = max(8, 2048 // size)  # supersampling
    n = size * ss
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    lo, hi = margin * ss, (size - margin) * ss - 1
    radius = max(RADIUS * side, 1.5 if size <= 24 else 0) * ss
    d.rounded_rectangle([lo, lo, hi, hi], radius=radius, fill=ORANGE + (255,))
    c = size * ss / 2
    a, b = SPLAT_A * side * ss, SPLAT_B * side * ss
    t = math.radians(SPLAT_ANGLE)
    pts = []
    for i in range(1024):
        u = 2 * math.pi * i / 1024
        x, y = a * math.cos(u), b * math.sin(u)
        pts.append((c + x * math.cos(t) - y * math.sin(t), c + x * math.sin(t) + y * math.cos(t)))
    d.polygon(pts, fill=WHITE + (255,))
    return img.resize((size, size), Image.LANCZOS)


def bmp_entry(img: Image.Image) -> bytes:
    """A 32-bit DIB as stored in .ico files: bottom-up BGRA, then a 1-bit AND mask."""
    w, h = img.size
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    px = img.load()
    rows = []
    for y in range(h - 1, -1, -1):
        rows.append(b"".join(struct.pack("BBBB", px[x, y][2], px[x, y][1], px[x, y][0], px[x, y][3])
                             for x in range(w)))
    mask_stride = ((w + 31) // 32) * 4
    mask = bytes(mask_stride * h)  # alpha channel decides; mask all zero
    return header + b"".join(rows) + mask


def write_ico(path: Path) -> None:
    entries = []
    for s in SIZES:
        img = draw(s)
        if s >= 256:
            buf = io.BytesIO()
            img.save(buf, "PNG", optimize=True)
            data = buf.getvalue()
        else:
            data = bmp_entry(img)
        entries.append((s, data))
    out = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    for s, data in entries:
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    out += b"".join(data for _, data in entries)
    path.write_bytes(out)


def write_svg(path: Path) -> None:
    side = 256 - 2 * MARGIN
    path.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">\n'
        f'  <rect x="{MARGIN}" y="{MARGIN}" width="{side}" height="{side}" '
        f'rx="{RADIUS * side:.2f}" fill="#F07800"/>\n'
        f'  <ellipse cx="128" cy="128" rx="{SPLAT_A * side:.2f}" ry="{SPLAT_B * side:.2f}" '
        f'transform="rotate({SPLAT_ANGLE} 128 128)" fill="#FFFFFF"/>\n'
        "</svg>\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    out = Path("app/icon")
    out.mkdir(parents=True, exist_ok=True)
    write_ico(out / "splatthumb.ico")
    write_svg(out / "splatthumb.svg")
