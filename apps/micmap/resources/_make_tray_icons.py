#!/usr/bin/env python3
"""
Phase 10 / HEALTH-08 / D-04 — generate 3 tray-state .ico files from installer/micmap.ico.

Tints the source icon's RGB channel toward a target color while preserving alpha,
then packs 16/32/48 px PNG-compressed entries into a single multi-size .ico per
RESEARCH §Pattern 1 LR_DEFAULTSIZE recommendation.

Outputs (relative to repo root):
  apps/micmap/resources/tray_armed.ico     — green   #2ecc71 (D-04 armed state)
  apps/micmap/resources/tray_triggered.ico — amber   #f1c40f (D-04 pulse glyph)
  apps/micmap/resources/tray_error.ico     — red     #e74c3c (D-04 error state)
"""

import sys
from pathlib import Path
from PIL import Image, ImageEnhance

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
SRC_ICO = REPO_ROOT / "installer" / "micmap.ico"
OUT_DIR = REPO_ROOT / "apps" / "micmap" / "resources"
SIZES = [(16, 16), (32, 32), (48, 48)]

# (output filename, tint RGB, tint strength 0..1, brightness multiplier)
VARIANTS = [
    ("tray_armed.ico",     (0x2e, 0xcc, 0x71), 0.65, 1.00),  # green dominant
    ("tray_triggered.ico", (0xf1, 0xc4, 0x0f), 0.70, 1.10),  # amber/yellow pulse — slightly brighter
    ("tray_error.ico",     (0xe7, 0x4c, 0x3c), 0.70, 1.00),  # red dominant
]


def tint_image(img: Image.Image, target_rgb: tuple, strength: float, brightness: float) -> Image.Image:
    """RGB-tint an RGBA image toward target_rgb by `strength`, preserving alpha."""
    img = img.convert("RGBA")
    r, g, b, a = img.split()

    tr, tg, tb = target_rgb
    s = strength
    inv = 1.0 - s

    def blend_band(band, target_value):
        # Blend each pixel: out = src*(1-s) + target*s
        return band.point(lambda v: int(v * inv + target_value * s))

    r = blend_band(r, tr)
    g = blend_band(g, tg)
    b = blend_band(b, tb)

    out = Image.merge("RGBA", (r, g, b, a))
    if abs(brightness - 1.0) > 1e-3:
        out = ImageEnhance.Brightness(out).enhance(brightness)
        # Brightness clamps RGB but does not touch alpha — restore original alpha to be safe.
        rr, gg, bb, _ = out.split()
        out = Image.merge("RGBA", (rr, gg, bb, a))
    return out


def main() -> int:
    if not SRC_ICO.exists():
        print(f"ERROR: source icon not found: {SRC_ICO}", file=sys.stderr)
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # Pillow can read .ico and pick the largest entry; we need a high-quality
    # source for downscaling, so explicitly pick the highest-resolution frame.
    src = Image.open(SRC_ICO)
    best_w = 0
    best_frame = None
    try:
        # Pillow IcoImagePlugin exposes .ico.sizes
        for size in src.ico.sizes():
            if size[0] > best_w:
                best_w = size[0]
                best_frame = src.ico.getimage(size)
    except AttributeError:
        # Fallback — use the open() result directly
        best_frame = src
        best_w = src.size[0]

    if best_frame is None:
        best_frame = src

    print(f"Source icon: {SRC_ICO} (best frame {best_w}x{best_w})")
    base = best_frame.convert("RGBA")

    for filename, rgb, strength, brightness in VARIANTS:
        tinted = tint_image(base, rgb, strength, brightness)
        out_path = OUT_DIR / filename
        # Pillow's .save with format='ICO' packs all `sizes` into one multi-size .ico,
        # PNG-compressed for the larger entries (default behavior >= Pillow 9).
        tinted.save(out_path, format="ICO", sizes=SIZES)
        size_bytes = out_path.stat().st_size
        # Sanity-verify magic
        with open(out_path, "rb") as fh:
            magic = fh.read(4)
        ok = magic == b"\x00\x00\x01\x00"
        print(f"  wrote {out_path.name}: {size_bytes} bytes, magic={magic.hex()} ok={ok}")
        if not ok:
            print(f"ERROR: bad ICO magic for {out_path}", file=sys.stderr)
            return 2
        if size_bytes > 100 * 1024:
            print(f"ERROR: {out_path} exceeds 100KB ({size_bytes} bytes)", file=sys.stderr)
            return 3

    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
