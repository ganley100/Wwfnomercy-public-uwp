#!/usr/bin/env python3
"""Generate every app icon from one square source image (default: icons/boxart.png).

Writes:
  icons/app.png              512x512   launcher thumbnail (embedded via file_to_c)
  icons/app.ico              16-256    desktop exe icon (icons/app.rc)
  uwp/Assets/*.png                     MSIX tiles, store logo and splash screen (Package.appxmanifest)

Square tiles are a straight resize. Wide tiles and the splash screen center the square image on a
background sampled from the source's own edges, so the box art isn't stretched or cropped.

Usage: python uwp/make_icons.py [source.png]   (needs Pillow)
"""
import sys
from pathlib import Path

from PIL import Image, ImageStat

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "uwp" / "Assets"

SQUARE = {
    "Square44x44Logo.png": 44,
    "Square44x44Logo.targetsize-24_altform-unplated.png": 24,
    "Square44x44Logo.targetsize-48_altform-unplated.png": 48,
    "Square44x44Logo.targetsize-256_altform-unplated.png": 256,
    "Square71x71Logo.png": 71,
    "Square150x150Logo.png": 150,
    "Square310x310Logo.png": 310,
    "StoreLogo.png": 50,
    "LockScreenLogo.png": 24,
}
WIDE = {
    "Wide310x150Logo.png": (310, 150),
    "SplashScreen.png": (620, 300),
}


def edge_color(img):
    """Average color of the outermost 4% of the image, for letterboxing."""
    w, h = img.size
    m = max(1, w // 25)
    strips = [img.crop((0, 0, w, m)), img.crop((0, h - m, w, h)), img.crop((0, 0, m, h)), img.crop((w - m, 0, w, h))]
    means = [ImageStat.Stat(s.convert("RGB")).mean for s in strips]
    return tuple(int(sum(c[i] for c in means) / len(means)) for i in range(3)) + (255,)


def square(img, size):
    return img.resize((size, size), Image.LANCZOS)


def letterbox(img, size, bg):
    w, h = size
    canvas = Image.new("RGBA", size, bg)
    inner = square(img, h)
    canvas.paste(inner, ((w - h) // 2, 0), inner)
    return canvas


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "icons" / "boxart.png"
    img = Image.open(src).convert("RGBA")
    if img.width != img.height:
        side = min(img.size)
        left, top = (img.width - side) // 2, (img.height - side) // 2
        img = img.crop((left, top, left + side, top + side))
        print(f"note: {src.name} is not square; center-cropped to {side}x{side}")
    bg = edge_color(img)

    square(img, 512).save(ROOT / "icons" / "app.png")
    img.save(ROOT / "icons" / "app.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

    ASSETS.mkdir(parents=True, exist_ok=True)
    for name, size in SQUARE.items():
        square(img, size).save(ASSETS / name)
    for name, size in WIDE.items():
        letterbox(img, size, bg).save(ASSETS / name)
    print(f"icons written from {src} (letterbox color {bg[:3]})")


if __name__ == "__main__":
    main()
