"""Draws the macOS icons from the Windows icon's design (tools/dev/make_icon.py: an orange
rounded square with a white splat) and writes:

  macos/app/Resources/AppIcon.icns       app icon, on Apple's icon grid (needs iconutil)
  macos/app/Resources/StatusIcon.png     menu bar template image, 18 px
  macos/app/Resources/StatusIcon@2x.png  the same at 36 px

The Windows files are not touched. Requires Pillow and macOS (iconutil).
Run from the repository root: python3 macos/app/icon/make_icons.py
"""
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

# The design lives in the Windows script; reuse its colours and splat shape.
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "dev"))
from make_icon import ORANGE, SPLAT_A, SPLAT_ANGLE, SPLAT_B, WHITE  # noqa: E402

# Apple's grid: the shape is 824/1024 of the canvas, centred, with rounder corners than on
# Windows, so the icon is the same size as other apps in the Dock and Finder.
MAC_SHAPE = 824 / 1024
MAC_RADIUS = 0.225


def splat_points(c: float, side: float) -> list:
    """The splat's outline, centred at (c, c), for a square of the given side."""
    a, b = SPLAT_A * side, SPLAT_B * side
    t = math.radians(SPLAT_ANGLE)
    pts = []
    for i in range(1024):
        u = 2 * math.pi * i / 1024
        x, y = a * math.cos(u), b * math.sin(u)
        pts.append((c + x * math.cos(t) - y * math.sin(t), c + x * math.sin(t) + y * math.cos(t)))
    return pts


def draw_app(size: int) -> Image.Image:
    ss = max(4, 2048 // size)  # supersampling
    n = size * ss
    side = MAC_SHAPE * n
    lo = (n - side) / 2
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([lo, lo, lo + side - 1, lo + side - 1], radius=MAC_RADIUS * side,
                        fill=ORANGE + (255,))
    d.polygon(splat_points(n / 2, side), fill=WHITE + (255,))
    return img.resize((size, size), Image.LANCZOS)


def draw_status(size: int) -> Image.Image:
    """Menu bar template image: a black rounded square with the splat cut out. macOS tints
    template images to match the menu bar, so only the alpha matters."""
    ss = max(8, 2048 // size)
    n = size * ss
    margin = n / 18  # 1 pt at 18 pt
    side = n - 2 * margin
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([margin, margin, n - margin - 1, n - margin - 1], radius=0.2 * side,
                        fill=(0, 0, 0, 255))
    d.polygon(splat_points(n / 2, side), fill=(0, 0, 0, 0))
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    out = Path(__file__).resolve().parents[1] / "Resources"
    out.mkdir(parents=True, exist_ok=True)
    draw_status(18).save(out / "StatusIcon.png", optimize=True)
    draw_status(36).save(out / "StatusIcon@2x.png", optimize=True)
    iconutil = shutil.which("iconutil")
    if iconutil is None:
        sys.exit("iconutil not found: run this on macOS to write AppIcon.icns")
    with tempfile.TemporaryDirectory() as tmp:
        iconset = Path(tmp) / "AppIcon.iconset"
        iconset.mkdir()
        for pt in (16, 32, 128, 256, 512):
            draw_app(pt).save(iconset / f"icon_{pt}x{pt}.png")
            draw_app(pt * 2).save(iconset / f"icon_{pt}x{pt}@2x.png")
        subprocess.run([iconutil, "-c", "icns", str(iconset), "-o", str(out / "AppIcon.icns")],
                       check=True)


if __name__ == "__main__":
    main()
