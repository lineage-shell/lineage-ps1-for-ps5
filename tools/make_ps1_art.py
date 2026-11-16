#!/usr/bin/env python3
# SwanStation PS1 (the ps1-console-shelf fork) - draws its home-screen art in the PS1 theme's look.
# SPDX-License-Identifier: GPL-3.0-or-later
"""Home-screen art for the fork, so it is told apart from the official app on the PS5's home screen:

    python3 tools/make_ps1_art.py   -> sce_sys/icon0.png (512x512)
                                       sce_sys/background-source.png, sce_sys/launch-background-source.png
Then python3 tools/make_dds.py turns the two backgrounds into pic0.dds / pic1.dds.

Everything is drawn here: the console's indigo light, shaded spheres, a rainbow splatter plaque and an outlined
title plate (as ui/ps1.c draws them in the app). No logo or artwork is copied.
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT_BOLD = str(ROOT / "assets/fonts/MPLUSRounded1c-ExtraBold-latin.ttf")


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t + 0.5) for i in range(3))


def stops_eval(stops, t):
    t = max(stops[0][0], min(t, stops[-1][0]))
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if t <= b:
            return lerp(ca, cb, (t - a) / (b - a) if b > a else 1.0)
    return stops[-1][1]


def hexrgb(h):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255)


LIGHT = [(0.0, hexrgb(0x8282fa)), (0.15, hexrgb(0x6464d8)), (0.34, hexrgb(0x4848a8)),
         (0.56, hexrgb(0x2e2e78)), (0.77, hexrgb(0x16164c)), (1.0, hexrgb(0x000120))]
SPHERE = [(0, hexrgb(0xdadcf6)), (0.08, hexrgb(0x9094e8)), (0.28, hexrgb(0x5a5cbe)),
          (0.56, hexrgb(0x37388e)), (0.82, hexrgb(0x1e1e5c)), (1, hexrgb(0x11113c))]


def background(w, h, cx=0.55, cy=0.48):
    small = Image.new("RGB", (w // 4, h // 4))
    px = small.load()
    sw, sh = small.size
    for y in range(sh):
        for x in range(sw):
            dx = ((x + 0.5) / sw - cx) / 0.56
            dy = ((y + 0.5) / sh - cy) / 0.74
            px[x, y] = stops_eval(LIGHT, math.hypot(dx, dy))
    return small.resize((w, h), Image.BICUBIC)


def sphere(img, x, y, d):
    """A lit sphere (light from the upper left) with its soft shadow, top-left (x, y), diameter d."""
    d = int(d)
    shadow = Image.new("L", (d * 2, d * 2), 0)
    ImageDraw.Draw(shadow).ellipse((d // 2, d // 2, d // 2 + d, d // 2 + d), fill=128)
    shadow = shadow.filter(ImageFilter.GaussianBlur(d * 0.08))
    img.paste((0, 0, 24), (int(x - d / 2 + d * 0.043), int(y - d / 2 + d * 0.07)), shadow)
    ball = Image.new("RGB", (d, d))
    bp = ball.load()
    far = math.hypot(0.67, 0.72)
    for j in range(d):
        for i in range(d):
            u, v = (i + 0.5) / d, (j + 0.5) / d
            bp[i, j] = stops_eval(SPHERE, math.hypot(u - 0.33, v - 0.28) / far)
    mask = Image.new("L", (d * 4, d * 4), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, d * 4 - 1, d * 4 - 1), fill=255)
    img.paste(ball, (int(x), int(y)), mask.resize((d, d), Image.LANCZOS))


def splat(img, x, y, w, h, seed, label, size):
    """The concept's rainbow splatter, drawn at a fifth of its size and blown up without smoothing."""
    W, H = max(2, round(w / 5)), max(2, round(h / 5))
    rnd = random.Random(seed)
    blobs = [(W * (0.08 + 0.84 * i / 8) + (rnd.random() - 0.5) * W * 0.08, H * (0.5 + (rnd.random() - 0.5) * 0.35),
              W * (0.07 + rnd.random() * 0.07), H * (0.26 + rnd.random() * 0.2)) for i in range(9)]
    hue0 = rnd.random() * 60
    small = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sp = small.load()
    for py in range(H):
        for px_ in range(W):
            f = sum(math.exp(-(((px_ - bx) / rx) ** 2 + ((py - by) / ry) ** 2)) for bx, by, rx, ry in blobs)
            n = rnd.random()
            if not (f + (n - 0.5) * 0.7 > 0.55 or (f > 0.08 and n > 0.985)):
                continue
            hue = (200 - px_ / W * 300 + hue0 + (rnd.random() - 0.5) * 50 + 720) % 360
            light = 0.5 + (rnd.random() - 0.5) * 0.16
            if rnd.random() < 0.06:
                light = 0.18

            def k(nn):
                kk = (nn + hue / 30) % 12
                return light - min(light, 1 - light) * max(-1, min(kk - 3, 9 - kk, 1))
            sp[px_, py] = (int(k(0) * 255), int(k(8) * 255), int(k(4) * 255), 255)
    big = small.resize((int(w), int(h)), Image.NEAREST)
    img.paste(big, (int(x), int(y)), big)
    if label:
        font = ImageFont.truetype(FONT_BOLD, int(size))
        d = ImageDraw.Draw(img)
        tw = d.textlength(label, font=font)
        tx, ty = x + (w - tw) / 2, y + (h - size) / 2 - size * 0.18
        o = max(2, size * 0.045)
        for ox, oy in ((1.5 * o, 1.5 * o), (-o, -o), (o, -o), (-o, o), (0, 2 * o)):
            d.text((tx + ox, ty + oy), label, font=font, fill=(12, 12, 68))
        d.text((tx, ty), label, font=font, fill=(255, 255, 255))


def plate(img, right, top, label, size):
    """The outlined navy title plate."""
    font = ImageFont.truetype(FONT_BOLD, int(size))
    d = ImageDraw.Draw(img)
    tw = d.textlength(label, font=font)
    pad = size * 0.75
    w, h = tw + pad * 2, size * 2.05
    x = right - w
    border = max(2, int(size / 10))
    d.rectangle((x, top, x + w, top + h), fill=(22, 28, 116), outline=(114, 114, 222), width=border)
    ty = top + (h - size) / 2 - size * 0.2
    d.text((x + pad, ty + size * 0.075), label, font=font, fill=(5, 8, 56))
    d.text((x + pad, ty), label, font=font, fill=(222, 223, 236))


def disc(img, cx, cy, r):
    """A PS1 disc, black side up."""
    d = ImageDraw.Draw(img)
    d.ellipse((cx - r * 1.06, cy - r * 1.06, cx + r * 1.06, cy + r * 1.06), fill=(42, 42, 58))
    d.ellipse((cx - r * 1.04, cy - r * 1.04, cx + r * 1.04, cy + r * 1.04), fill=(154, 160, 180))
    cols = [(12, 12, 16), (42, 16, 48), (10, 10, 14), (16, 42, 48), (11, 11, 15), (42, 36, 16), (10, 10, 14), (26, 16, 64)]
    for i in range(64):
        c = lerp(cols[(i // 8) % 8], cols[(i // 8 + 1) % 8], (i % 8) / 8)
        d.pieslice((cx - r, cy - r, cx + r, cy + r), i * 360 / 64 - 70, (i + 1) * 360 / 64 - 70 + 0.5, fill=c)
    d.ellipse((cx - r * 0.214, cy - r * 0.214, cx + r * 0.214, cy + r * 0.214), fill=(0, 0, 0))
    d.ellipse((cx - r * 0.156, cy - r * 0.156, cx + r * 0.156, cy + r * 0.156), fill=(108, 111, 124))
    d.ellipse((cx - r * 0.127, cy - r * 0.127, cx + r * 0.127, cy + r * 0.127), fill=(200, 204, 216))
    d.ellipse((cx - r * 0.06, cy - r * 0.06, cx + r * 0.06, cy + r * 0.06), fill=(30, 30, 40))


def icon():
    img = background(512, 512, 0.5, 0.45)
    sphere(img, -40, -40, 190)
    sphere(img, 132, 92, 70)
    sphere(img, 360, 330, 200)
    disc(img, 256, 230, 118)
    splat(img, 26, 356, 460, 120, 11, "SWANSTATION", 50)
    plate(img, 490, 22, "PS1", 44)
    return img


def backdrop(w, h):
    k = w / 1920
    img = background(w, h)
    for x, y, dd in ((40, -30, 230), (240, 150, 120), (1590, 700, 250), (1500, 920, 100)):
        sphere(img, x * k, y * k, dd * k)
    plate(img, 1824 * k, 64 * k, "SWANSTATION PS1", 40 * k)
    return img


def main():
    out = ROOT / "sce_sys"
    icon().save(out / "icon0.png")
    backdrop(3840, 2160).save(out / "background-source.png")
    launch = backdrop(3840, 2160)
    disc(launch, 1920, 1080, 420)
    launch.save(out / "launch-background-source.png")
    print("wrote", out / "icon0.png", out / "background-source.png", out / "launch-background-source.png")


if __name__ == "__main__":
    main()
