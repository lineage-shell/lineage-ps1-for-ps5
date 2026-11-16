#!/usr/bin/env bash
# Fills build/preview-root with a fake library (empty disc files named like real dumps) for the preview.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
games="$root/build/preview-root/games"
rm -rf "$games"
mkdir -p "$games"
cd "$games"
while IFS='|' read -r name serial discs; do
    d="$name [$serial]"
    mkdir -p "$d"
    if [ "$discs" = 1 ]; then
        : > "$d/$name [$serial].chd"
    else
        for i in $(seq 1 "$discs"); do : > "$d/$name [$serial] (Disc $i).chd"; done
        for i in $(seq 1 "$discs"); do echo "$name [$serial] (Disc $i).chd"; done > "$d/$name [$serial].m3u"
    fi
done <<'LIST'
Crash Bandicoot|SCUS-94900|1
Spyro the Dragon|SCUS-94228|1
Tekken 3|SCUS-94388|1
Gran Turismo|SCUS-94194|1
Final Fantasy VII|SCUS-94163|3
Metal Gear Solid|SLUS-00594|2
MediEvil|SCES-00311|1
Castlevania Symphony of the Night|SLUS-00067|1
PaRappa the Rapper|SCUS-94127|1
Resident Evil 2|SLUS-00421|2
Tony Hawk's Pro Skater|SLUS-00860|1
Silent Hill|SLUS-00707|1
WipEout|SCES-00010|1
Ape Escape|SCUS-94423|1
Vagrant Story|SLUS-01040|1
Chrono Cross|SLUS-01041|2
Dino Crisis|SLUS-00922|1
Driver|SLUS-00811|1
Final Fantasy Tactics|SCUS-94221|1
Jet Moto|SCUS-94167|1
Legend of Dragoon|SCUS-94491|4
LIST
echo "fake library: $(ls | wc -l) games in $games"

# a memory card with two saves (made-up names, icons drawn here) for the memory card screen
python3 - "$root/build/preview-root/saves" <<'PY'
import sys, os, struct
os.makedirs(sys.argv[1], exist_ok=True)
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
    card[frame + 0x0a:frame + 0x0a + 20] = b"BASLUS-00594GAME".ljust(20, b"\0")
    for k in range(1, nblocks):
        card[0x80 * (block + k)] = 0x52 if k < nblocks - 1 else 0x53
    d = 0x2000 * block
    card[d:d + 3] = b"SC\x11"
    t = sjis(name)[:64]
    card[d + 4:d + 4 + len(t)] = t
    pal = [0, colour, 0x7fff, 0x0421]
    for i, c in enumerate(pal):
        card[d + 0x60 + i * 2:d + 0x62 + i * 2] = struct.pack("<H", c)
    for y in range(16):
        for x in range(16):
            r = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
            idx = 0 if r > 7.5 else (3 if r < 2 else (2 if r < 3 else 1))
            i = y * 16 + x
            card[d + 0x80 + i // 2] |= idx << ((i & 1) * 4)
save(1, 1, "Metal Gear Solid", 0x001f)
save(2, 3, "Shadow Moses 2", 0x03e0)
open(os.path.join(sys.argv[1], "SLUS-00594_1.mcd"), "wb").write(card)
PY
