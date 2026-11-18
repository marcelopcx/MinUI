#!/usr/bin/env python3
"""Genera background.png y preview.png para los temas en skeleton/Temas/."""

from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    raise SystemExit("Instala Pillow: pip install pillow")

ROOT = Path(__file__).resolve().parent.parent / "skeleton" / "Temas"
FULL = (640, 480)
PREV = (320, 240)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def gradient(img, top, bottom):
    draw = ImageDraw.Draw(img)
    for y in range(img.height):
        c = lerp(top, bottom, y / max(img.height - 1, 1))
        draw.line([(0, y), (img.width, y)], fill=c)


def save_pair(folder: Path, draw_full, draw_prev):
    folder.mkdir(parents=True, exist_ok=True)
    full = Image.new("RGB", FULL)
    prev = Image.new("RGB", PREV)
    draw_full(full)
    draw_prev(prev)
    full.save(folder / "background.png")
    prev.save(folder / "preview.png")
    print("OK", folder.name)


def ps1_full(full):
    gradient(full, (0x1e, 0x27, 0x46), (0x3a, 0x52, 0x82))
    d = ImageDraw.Draw(full)
    for i in range(6):
        x = 80 + i * 90
        d.polygon([(x, 400), (x + 40, 340), (x + 80, 400)], fill=(90, 120, 180))
    d.rectangle([0, 0, full.width, 36], fill=(20, 30, 60))
    d.text((12, 8), "PlayStation", fill=(200, 210, 230))


def ps1_prev(prev):
    gradient(prev, (0x1e, 0x27, 0x46), (0x3a, 0x52, 0x82))
    ImageDraw.Draw(prev).text((8, 8), "PS1", fill=(200, 210, 230))


def gameboy_full(full):
    gradient(full, (0x9b, 0xbc, 0x0f), (0x8b, 0xac, 0x0f))
    d = ImageDraw.Draw(full)
    d.rectangle([40, 60, 220, 200], fill=(0x30, 0x62, 0x30), outline=(0x0f, 0x38, 0x0f), width=3)
    d.rectangle([50, 70, 210, 120], fill=(0x8b, 0xac, 0x0f))
    d.ellipse([480, 320, 600, 440], outline=(0x0f, 0x38, 0x0f), width=4)
    d.text((44, 210), "DOT MATRIX", fill=(15, 56, 15))


def gameboy_prev(prev):
    gradient(prev, (0x9b, 0xbc, 0x0f), (0x30, 0x62, 0x30))
    ImageDraw.Draw(prev).text((8, 8), "GB", fill=(15, 56, 15))


def snes_full(full):
    gradient(full, (0xd8, 0xc8, 0xe8), (0x9b, 0x7a, 0xb8))
    d = ImageDraw.Draw(full)
    for i, x in enumerate(range(60, 580, 100)):
        color = (0xe8, 0xd4, 0xf8) if i % 2 == 0 else (0x8b, 0x6c, 0xb0)
        d.rounded_rectangle([x, 360, x + 70, 420], radius=8, fill=color, outline=(0x4a, 0x2d, 0x6e))
    d.rectangle([0, 0, full.width, 40], fill=(0x6e, 0x4f, 0x94))
    d.text((12, 10), "SUPER NINTENDO", fill=(240, 230, 255))


def snes_prev(prev):
    gradient(prev, (0xd8, 0xc8, 0xe8), (0x9b, 0x7a, 0xb8))
    ImageDraw.Draw(prev).text((8, 8), "SNES", fill=(45, 27, 78))


if __name__ == "__main__":
    save_pair(ROOT / "PlayStation", ps1_full, ps1_prev)
