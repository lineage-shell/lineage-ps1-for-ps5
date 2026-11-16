#!/usr/bin/env python3
"""Laptop preview: fills the preview data folder with things to look at on the game page and the memory card screen.

    populate.py ROOT

- Save states for a few games: <states>/<id>.state0..9 (empty files) with thumbnails (<state>.thumb, PSXT 192x144,
  made from the game's cover, cut to 4:3 and pixelated like a PS1 picture) and file times spread over the last weeks.
- Play time and last played for them in stats.txt.
- A memory card for each (<saves>/<serial>_1.mcd) with saves named as those games name theirs, their real block
  counts, and 16x16 icons made from the cover (16 colours, as the console's).
Everything is made here from the local covers; nothing is downloaded and nothing goes in git. Run again: it is made
again (a stamp keeps it to once per version of this script).
SPDX-License-Identifier: GPL-3.0-or-later
"""
import hashlib, os, struct, sys, time
from PIL import Image

root = sys.argv[1]
covers = os.path.join(root, "covers", "default")
states = os.path.join(root, "states")
saves = os.path.join(root, "saves")
STAMP = hashlib.sha1(open(__file__, "rb").read()).hexdigest()

# serial: (slots used, hours played, days since last played, [(save name, blocks), ...])
GAMES = {
    "SCES-00344": ([0, 1, 4], 6.5, 0, [("CRASH BANDICOOT 23 PERCENT", 1)]),
    "SCES-01438": ([0, 2, 3, 7], 11.2, 1, [("SPYRO THE DRAGON 46 PERCENT", 1), ("SPYRO THE DRAGON 81 PERCENT", 1)]),
    "SCES-00867": ([0, 1, 2, 5, 8], 38.0, 2, [("FF7 CLOUD LV23 KALM", 1), ("FF7 CLOUD LV31 COSTA DEL SOL", 1),
                                             ("FF7 CLOUD LV44 GOLD SAUCER", 1)]),
    "SLES-01370": ([0, 3], 9.4, 5, [("METAL GEAR SOLID", 1), ("SHADOW MOSES 2", 1)]),
    "SCES-00984": ([0], 21.7, 9, [("GRAN TURISMO GAME DATA", 3)]),
    "SCES-01237": ([0, 1], 4.1, 14, [("TEKKEN 3 SYSTEM DATA", 1)]),
    "SLES-02529": ([0, 6], 7.9, 20, [("RE3 JILL 01 UPTOWN", 1), ("RE3 JILL 02 CITY HALL", 1)]),
    "SCES-02105": ([2], 3.3, 30, [("CTR GAME DATA", 1)]),
}


def cover(serial):
    p = os.path.join(covers, serial + ".jpg")
    return Image.open(p).convert("RGB") if os.path.exists(p) else Image.new("RGB", (500, 500), (40, 40, 90))


def thumb(serial, slot):
    """A PS1-looking picture for a save state: a 4:3 piece of the cover, a different one per slot, pixelated."""
    im = cover(serial)
    w, h = im.size
    cw, ch = int(w * 0.62), int(w * 0.62 * 3 / 4)
    x = int((w - cw) * ((slot * 37) % 10) / 9)
    y = int((h - ch) * ((slot * 53 + 3) % 10) / 9)
    piece = im.crop((x, y, x + cw, y + ch)).resize((96, 72), Image.BILINEAR).resize((192, 144), Image.NEAREST)
    return b"PSXT" + struct.pack("<HH", 192, 144) + piece.convert("RGBA").tobytes()


def sjis(text):
    out = b""
    for ch in text:
        if ch == " ":
            out += b"\x81\x40"
        elif ch.isdigit():
            out += struct.pack(">H", 0x824F + ord(ch) - 48)
        elif "A" <= ch <= "Z":
            out += struct.pack(">H", 0x8260 + ord(ch) - 65)
        elif "a" <= ch <= "z":
            out += struct.pack(">H", 0x8281 + ord(ch) - 97)
    return out


def icon(serial, n):
    """16x16 in 16 colours from the middle of the cover (each save a slightly different piece); palette as 15-bit."""
    im = cover(serial)
    w, h = im.size
    s = int(min(w, h) * (0.55 - 0.08 * (n % 3)))
    x, y = (w - s) // 2 + (n * 17) % 30, (h - s) // 2 - (n * 11) % 20
    small = im.crop((x, y, x + s, y + s)).resize((16, 16), Image.LANCZOS).quantize(16)
    pal = small.getpalette()[:48] + [0] * max(0, 48 - len(small.getpalette()[:48]))
    palette = b""
    for i in range(16):
        r, g, b = pal[i * 3:i * 3 + 3]
        v = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
        palette += struct.pack("<H", v or 0x0421)  # never 0 (transparent)
    pix = small.tobytes()  # palette indices
    data = bytes((pix[i] & 15) | (pix[i + 1] & 15) << 4 for i in range(0, 256, 2))
    return palette, data


def checksum(frame):
    x = 0
    for b in frame[:0x7F]:
        x ^= b
    frame[0x7F] = x


def card(serial, entries):
    data = bytearray(128 * 1024)
    data[0:2] = b"MC"
    checksum(memoryview(data)[0:0x80])
    block = 1
    region = {"SC": "BE", "SL": "BE"}[serial[:2]]
    for n, (name, blocks) in enumerate(entries):
        if block + blocks > 16:
            break
        for k in range(blocks):
            f = 0x80 * (block + k)
            frame = memoryview(data)[f:f + 0x80]
            frame[0] = 0x51 if k == 0 else (0x53 if k == blocks - 1 else 0x52)
            if k == 0:
                frame[4:8] = struct.pack("<I", 0x2000 * blocks)
                frame[0x0A:0x0A + 20] = (region + serial.replace("-", "-") + "%02d" % n).encode().ljust(20, b"\0")[:20]
            nxt = (block + k) if k < blocks - 1 else 0xFFFF  # the next block's number (0-based), or none
            frame[8:10] = struct.pack("<H", nxt)
            checksum(frame)
        d = 0x2000 * block
        data[d:d + 4] = b"SC\x11" + bytes([blocks])
        t = sjis(name)[:64]
        data[d + 4:d + 4 + len(t)] = t
        palette, pixels = icon(serial, n)
        data[d + 0x60:d + 0x80] = palette
        data[d + 0x80:d + 0x100] = pixels
        block += blocks
    for b in range(block, 16):  # free blocks
        f = 0x80 * b
        frame = memoryview(data)[f:f + 0x80]
        frame[0] = 0xA0
        frame[8:10] = b"\xff\xff"
        checksum(frame)
    return bytes(data)


stamp_path = os.path.join(root, ".populate-stamp")
if os.path.exists(stamp_path) and open(stamp_path).read() == STAMP:
    sys.exit(0)
os.makedirs(states, exist_ok=True)
os.makedirs(saves, exist_ok=True)
now = time.time()
stats_path = os.path.join(root, "stats.txt")
stats = {}
if os.path.exists(stats_path):
    for line in open(stats_path):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        stats[f[0]] = f
for serial, (slots, hours, days, entries) in GAMES.items():
    for slot in range(10):
        for suffix in ("", ".thumb"):
            p = os.path.join(states, "%s.state%d%s" % (serial, slot, suffix))
            if os.path.exists(p):
                os.remove(p)
    for i, slot in enumerate(slots):
        p = os.path.join(states, "%s.state%d" % (serial, slot))
        open(p, "wb").close()
        open(p + ".thumb", "wb").write(thumb(serial, slot))
        when = now - days * 86400 - (len(slots) - 1 - i) * 3600 * (5 + slot)
        os.utime(p, (when, when))
    open(os.path.join(saves, serial + "_1.mcd"), "wb").write(card(serial, entries))
    old = stats.get(serial, [serial, "0", "0", "0", "-1", "0"])
    old[1], old[2] = str(int(hours * 3600)), str(int(now - days * 86400))
    stats[serial] = old
with open(stats_path, "w") as f:
    f.write("# SwanStationPS5 play records: id, seconds played, last played, favorite, achievements\n")
    for k, v in stats.items():
        f.write("\t".join(v) + "\n")
open(stamp_path, "w").write(STAMP)
print("preview: states, play time and memory cards for %d games" % len(GAMES))
