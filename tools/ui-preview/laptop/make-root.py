#!/usr/bin/env python3
"""Laptop preview: a fake data folder with a big library and made-up covers, plus the on-screen key map.

    make-root.py ROOT REPO [SOUNDS_DIR]

Games are empty .chd files named like real dumps. Covers are drawn here, then replaced by real front covers from
xlenore/psx-covers where it has one (fetched once, cached outside ROOT, see real_covers). The console sounds are
copied from SOUNDS_DIR when it exists (they are never in git). An existing ROOT keeps its settings, saves and states;
the games and covers are made again only when this script changes (stamp file).
SPDX-License-Identifier: GPL-3.0-or-later
"""
import hashlib, os, random, shutil, struct, sys
from PIL import Image, ImageDraw, ImageFont

root, repo = sys.argv[1], sys.argv[2]
sounds = sys.argv[3] if len(sys.argv) > 3 else ""
fonts = os.path.join(repo, "assets", "fonts")

# name | serial | discs   (PAL releases, as kurolume's discs are; serials checked against DuckStation's gamedb, 2026-11-14)
GAMES = """Crash Bandicoot|SCES-00344|1
Crash Bandicoot 2 Cortex Strikes Back|SCES-00967|1
Crash Bandicoot 3 Warped|SCES-01420|1
Crash Team Racing|SCES-02105|1
Crash Bash|SCES-02834|1
Spyro the Dragon|SCES-01438|1
Spyro 2 Gateway to Glimmer|SCES-02104|1
Spyro Year of the Dragon|SCES-02835|1
Tekken 2|SCES-00255|1
Tekken 3|SCES-01237|1
Gran Turismo|SCES-00984|1
Gran Turismo 2|SCES-02380|2
Final Fantasy VII|SCES-00867|3
Final Fantasy VIII|SLES-02080|4
Final Fantasy IX|SLES-02965|4
Metal Gear Solid|SLES-01370|2
MediEvil|SCES-00311|1
MediEvil 2|SCES-02544|1
Castlevania Symphony of the Night|SLES-00524|1
PaRappa the Rapper|SCES-00743|1
Um Jammer Lammy|SCES-01753|1
Resident Evil|SLES-00200|1
Resident Evil 2|SLES-00972|2
Resident Evil 3 Nemesis|SLES-02529|1
Tony Hawk's Skateboarding|SLES-02055|1
Tony Hawk's Pro Skater 2|SLES-02908|1
Silent Hill|SLES-01514|1
WipEout|SCES-00010|1
WipEout 2097|SLES-00327|1
Wip3out|SCES-01909|1
Ape Escape|SCES-01564|1
Vagrant Story|SLES-02754|1
Tombi!|SCES-01330|1
Tombi! 2|SCES-02147|1
Dino Crisis|SLES-02207|1
Driver|SLES-01816|1
Driver 2 Back on the Streets|SLES-02993|2
Jet Rider|SCES-00566|1
Legend of Dragoon|SCES-03043|4
Abe's Oddysee|SLES-00664|1
Abe's Exoddus|SLES-01480|2
Alundra|SLES-01135|1
Alundra 2|SLES-02600|1
Breath of Fire III|SLES-01304|1
Breath of Fire IV|SLES-03552|1
Bushido Blade|SCES-00899|1
Colin McRae Rally|SLES-00477|1
Cool Boarders 2|SCES-00992|1
Destruction Derby|SCES-00008|1
Destruction Derby 2|SLES-00299|1
Diablo|SLES-01156|1
Time Crisis|SCES-00657|1
Fear Effect|SLES-02166|4
Grandia|SLES-02397|2
Hogs of War|SLES-01041|1
Kurushi|SCES-00866|1
Klonoa Door to Phantomile|SCES-00942|1
Kula World|SCES-01000|1
Legacy of Kain Soul Reaver|SLES-01301|1
Croc Legend of the Gobbos|SLES-00593|1
Mega Man Legends|SLES-01485|1
Mega Man X4|SLES-01176|1
Micro Machines V3|SLES-00016|1
Mr Driller|SCES-02771|1
Need for Speed III Hot Pursuit|SLES-01154|1
Medal of Honor|SLES-02470|1
Pac-Man World|SCES-00934|1
G-Police|SLES-00082|2
Parasite Eve II|SLES-02558|2
Rayman|SLES-00049|1
Ridge Racer|SCES-00001|1
Ridge Racer Type 4|SCES-01706|1
Rival Schools|SLES-01436|2
R-Type Delta|SCES-01762|1
Soul Blade|SCES-00577|1
Street Fighter Alpha 3|SLES-01863|1
Suikoden|SLES-00527|1
Suikoden II|SLES-02442|1
Syphon Filter|SCES-01910|1
Tenchu Stealth Assassins|SLES-01374|1
Tomb Raider|SLES-00024|1
Tomb Raider II|SLES-00718|1
Twisted Metal World Tour|SCES-00567|1
Vib-Ribbon|SCES-02873|1
Wild Arms|SCES-00321|1
Worms|SLES-00119|1"""

STAMP = hashlib.sha1(open(__file__, "rb").read()).hexdigest()


def font(name, size):
    try:
        return ImageFont.truetype(os.path.join(fonts, name), size)
    except OSError:
        return ImageFont.load_default()


def wrap(draw, text, f, width):
    lines, line = [], ""
    for word in text.split():
        test = (line + " " + word).strip()
        if draw.textlength(test, font=f) <= width or not line:
            line = test
        else:
            lines.append(line)
            line = word
    return lines + [line]


def cover(path, name, serial):
    """A made-up jewel-case front: a coloured field, shapes, the title and a black side band."""
    rnd = random.Random(serial)
    w, h = 500, 500
    hue = rnd.random()
    import colorsys
    def col(dh, s, v):
        r, g, b = colorsys.hsv_to_rgb((hue + dh) % 1, s, v)
        return int(r * 255), int(g * 255), int(b * 255)
    img = Image.new("RGB", (w, h), col(0, 0.7, 0.35))
    d = ImageDraw.Draw(img)
    for y in range(h):  # vertical gradient
        t = y / h
        d.line([(0, y), (w, y)], fill=col(0.08 * t, 0.75, 0.25 + 0.55 * (1 - t)))
    for _ in range(rnd.randint(4, 9)):
        x, y, r = rnd.randint(0, w), rnd.randint(80, h), rnd.randint(30, 160)
        shape = rnd.choice(("ellipse", "rect", "tri"))
        c = col(rnd.uniform(-0.3, 0.3), rnd.uniform(0.3, 0.9), rnd.uniform(0.4, 1.0))
        if shape == "ellipse":
            d.ellipse([x - r, y - r, x + r, y + r], fill=c)
        elif shape == "rect":
            d.rectangle([x - r, y - r // 2, x + r, y + r // 2], fill=c)
        else:
            d.polygon([(x, y - r), (x - r, y + r), (x + r, y + r)], fill=c)
    d.rectangle([0, 0, 46, h], fill=(12, 12, 12))  # the black spine band of a PS1 case
    d.text((23, h - 20), serial, font=font("ShareTechMono-400.ttf", 16), fill=(220, 220, 220), anchor="ms")
    f = font("ChakraPetch-700.ttf", 46)
    lines = wrap(d, name.upper(), f, w - 90)
    y = 40
    for line in lines[:4]:
        d.text((272, y), line, font=f, fill=(0, 0, 0), anchor="ma", stroke_width=0)
        d.text((270, y - 2), line, font=f, fill=(255, 255, 240), anchor="ma")
        y += 52
    img.save(path, quality=88)


def keymap(path):
    """The key map shown over the window at start and on F1 (drawn here so the app needs no text code)."""
    rows = [
        ("Arrows", "D-pad"), ("Enter / X", "Cross"), ("Backspace / Esc", "Circle"), ("S", "Square"),
        ("T", "Triangle"), ("Q / E", "L1 / R1"), ("1 / 3", "L2 / R2"), ("Tab", "Options"),
        ("Space / P", "Touchpad"), ("Z / C", "L3 / R3"), ("J / L", "Right stick"), ("", ""),
        ("F1", "this key map"), ("F11", "full screen"), ("F2 / F3", "next / previous version"),
    ]
    W, H = 860, 90 + 44 * len(rows)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W - 1, H - 1], fill=(10, 12, 30, 225), outline=(200, 200, 220, 255), width=3)
    d.text((W // 2, 26), "KEYBOARD", font=font("ChakraPetch-700.ttf", 38), fill=(255, 255, 255, 255), anchor="ma")
    f = font("IBMPlexMono-400.ttf", 28)
    y = 86
    for key, what in rows:
        d.text((W // 2 - 60, y), key, font=f, fill=(255, 220, 120, 255), anchor="ra")
        d.text((W // 2 - 20, y), what, font=f, fill=(235, 235, 235, 255), anchor="la")
        y += 44
    img.save(path)


os.makedirs(root, exist_ok=True)
stamp_path = os.path.join(root, ".laptop-stamp")
fresh = not os.path.exists(stamp_path) or open(stamp_path).read() != STAMP
if fresh:
    games = os.path.join(root, "games")
    shutil.rmtree(games, ignore_errors=True)
    covers = os.path.join(root, "covers", "default")
    shutil.rmtree(covers, ignore_errors=True)
    os.makedirs(covers)
    for row in GAMES.splitlines():
        name, serial, discs = row.split("|")
        folder = os.path.join(games, f"{name} [{serial}]")
        os.makedirs(folder)
        if discs == "1":
            open(os.path.join(folder, f"{name} [{serial}].chd"), "wb").close()
        else:
            names = [f"{name} [{serial}] (Disc {i}).chd" for i in range(1, int(discs) + 1)]
            for n in names:
                open(os.path.join(folder, n), "wb").close()
            open(os.path.join(folder, f"{name} [{serial}].m3u"), "w").write("\n".join(names) + "\n")
        cover(os.path.join(covers, serial + ".jpg"), name, serial)
    for sub in ("bios", "saves", "states", "cheats", "logs"):
        os.makedirs(os.path.join(root, sub), exist_ok=True)
    # a memory card with two saves (made-up names and icons), as tools/ui-preview/make-root.sh makes
    card = bytearray(128 * 1024)
    card[0:2] = b"MC"
    def sjis(text):
        out = b""
        for ch in text:
            if ch == " ": out += b"\x81\x40"
            elif ch.isdigit(): out += struct.pack(">H", 0x824f + ord(ch) - 48)
            elif ch.isupper(): out += struct.pack(">H", 0x8260 + ord(ch) - 65)
            else: out += struct.pack(">H", 0x8281 + ord(ch.lower()) - 97)
        return out
    def save(block, nblocks, name, colour):
        frame = 0x80 * block
        card[frame] = 0x51
        card[frame + 4:frame + 8] = struct.pack("<I", 0x2000 * nblocks)
        card[frame + 0x0a:frame + 0x0a + 20] = b"BESLES-01370GAME".ljust(20, b"\0")
        for k in range(1, nblocks):
            card[0x80 * (block + k)] = 0x52 if k < nblocks - 1 else 0x53
        dd = 0x2000 * block
        card[dd:dd + 3] = b"SC\x11"
        t = sjis(name)[:64]
        card[dd + 4:dd + 4 + len(t)] = t
        for i, c in enumerate([0, colour, 0x7fff, 0x0421]):
            card[dd + 0x60 + i * 2:dd + 0x62 + i * 2] = struct.pack("<H", c)
        for y in range(16):
            for x in range(16):
                r = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
                idx = 0 if r > 7.5 else (3 if r < 2 else (2 if r < 3 else 1))
                i = y * 16 + x
                card[dd + 0x80 + i // 2] |= idx << ((i & 1) * 4)
    save(1, 1, "Metal Gear Solid", 0x001f)
    save(2, 3, "Shadow Moses 2", 0x03e0)
    open(os.path.join(root, "saves", "SLES-01370_1.mcd"), "wb").write(card)
    # save states and an exit save for Crash Bandicoot, so the game page has slots to show
    for name in ("SCES-00344.resume", "SCES-00344.state0", "SCES-00344.state1", "SCES-00344.state4"):
        p = os.path.join(root, "states", name)
        if not os.path.exists(p):
            open(p, "wb").close()
    keymap(os.path.join(root, "keymap.png"))
    open(stamp_path, "w").write(STAMP)

# settings: kept between runs, but never download covers, and say this version wrote them (no "new version" question)
version = "1.6.2"
for line in open(os.path.join(repo, "src", "SwanStationPS5.h")):
    if line.startswith("#define SwanStationPS5_VERSION "):
        version = line.split('"')[1]
ini = os.path.join(root, "SwanStationPS5.ini")
if os.path.exists(os.path.join(root, "shelf.ini")):  # the fork's own settings file (it imports SwanStationPS5.ini once)
    ini = os.path.join(root, "shelf.ini")
lines = open(ini).read().splitlines() if os.path.exists(ini) else ["# SwanStationPS5 settings (laptop preview)"]
lines = [l for l in lines if not l.startswith(("cover_download=", "version="))]
open(ini, "w").write("\n".join(lines + ["cover_download=0", f"version={version}"]) + "\n")

def real_covers():
    """Real front covers from xlenore/psx-covers (the app's own download source), fetched once per serial into
    build/laptop-preview/covers-download (untrusted: each file is checked and re-encoded with PIL before it is used),
    then written over the drawn cover. A game the set has no cover for keeps its drawn one. Preview data only, never
    in git. PREVIEW_REAL_COVERS=0 keeps the drawn covers; PREVIEW_OFFLINE=1 uses only what is cached."""
    import urllib.error, urllib.request
    cache = os.path.join(os.path.dirname(os.path.abspath(root)), "covers-download")
    os.makedirs(cache, exist_ok=True)
    dst_dir = os.path.join(root, "covers", "default")
    done_path = os.path.join(root, ".real-covers")  # serials already written (cleared when the covers are drawn again)
    done = set(open(done_path).read().split()) if os.path.exists(done_path) else set()
    got = 0
    for row in GAMES.splitlines():
        serial = row.split("|")[1]
        raw, missing = os.path.join(cache, serial + ".jpg"), os.path.join(cache, serial + ".missing")
        if not os.path.exists(raw) and not os.path.exists(missing) and os.environ.get("PREVIEW_OFFLINE") != "1":
            url = f"https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/default/{serial}.jpg"
            try:
                with urllib.request.urlopen(url, timeout=15) as r:
                    data = r.read(4 << 20)
                open(raw + ".part", "wb").write(data)
                os.replace(raw + ".part", raw)
            except urllib.error.HTTPError as e:
                if e.code == 404:
                    open(missing, "w").close()
            except OSError:
                pass  # offline: tried again next time
        if not os.path.exists(raw):
            continue
        if serial in done:
            got += 1
            continue
        try:
            with Image.open(raw) as im:
                im.verify()
            with Image.open(raw) as im:
                if im.format not in ("JPEG", "PNG") or not (64 <= im.width <= 4096 and 64 <= im.height <= 4096):
                    raise ValueError("not a cover")
                im.convert("RGB").save(os.path.join(dst_dir, serial + ".jpg"), quality=90)
            done.add(serial)
            got += 1
        except Exception:
            os.replace(raw, raw + ".bad")
    open(done_path, "w").write("\n".join(sorted(done)) + "\n")
    return got


if fresh and os.path.exists(os.path.join(root, ".real-covers")):
    os.remove(os.path.join(root, ".real-covers"))
if os.environ.get("PREVIEW_REAL_COVERS", "1") == "1":
    print(f"real covers: {real_covers()} of {len(GAMES.splitlines())}")

# save states with pictures, play time and full memory cards for a few games (populate.py)
import subprocess
if fresh and os.path.exists(os.path.join(root, ".populate-stamp")):
    os.remove(os.path.join(root, ".populate-stamp"))  # the games were made again: their states and cards too
subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "populate.py"), root],
               check=False)

if sounds and os.path.isdir(sounds):
    dst = os.path.join(root, "console-sounds")
    os.makedirs(dst, exist_ok=True)
    for f in os.listdir(sounds):
        if f.lower().endswith(".wav") and not os.path.exists(os.path.join(dst, f)):
            shutil.copy(os.path.join(sounds, f), dst)
print(f"laptop preview root: {root} ({len(GAMES.splitlines())} games{', made again' if fresh else ''})")
