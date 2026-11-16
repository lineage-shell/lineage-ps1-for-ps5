#!/usr/bin/env bash
# SwanStationPS5 interface preview for Linux: native clang + SDL2 (offscreen driver), a fake core and library.
#   bash tools/ui-preview/build-linux.sh [script]   -> build/preview-linux/shots/*.png
# Run it inside the image of tools/ui-preview/Dockerfile with the repo at /work; make the fake library first
# with bash tools/ui-preview/make-root.sh. SKIP_BUILD=1 reuses the last binary.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd /work
out=build/preview-linux
mkdir -p "$out/shots"
sources=(src/cheats.c src/config.c src/covers.c src/disc.c src/i18n.c src/library.c src/main.c
    src/net.c src/stats.c src/tips.c src/controls.c src/bezels.c src/art.c src/profiles.c src/gamedb.c src/stb_impl.c src/util.c src/play.c src/remote.c src/update.c src/vendor.c src/platform/vk/vk_probe.c src/platform/plat_sdl.c src/platform/xbr.c src/platform/blit.c src/discimport.c src/cdscsi.c src/state_save.c src/core/swanstation_options.c src/core/bios_choices.c third_party/rcheevos/src/rhash/md5.c
    src/ui/*.c tools/ui-preview/stubs.c)
if [[ ${SKIP_BUILD:-0} != 1 ]]; then
clang-18 -std=gnu11 -O1 -g -w -D_GNU_SOURCE \
    -DSwanStationPS5_PREVIEW -DSDL_MAIN_HANDLED -include tools/ui-preview/compat.h \
    -Isrc -Ithird_party/stb -Ithird_party/swanstation/dep/libretro-common/include \
    -Ithird_party/rcheevos/include $(pkg-config --cflags sdl2) \
    "${sources[@]}" $(pkg-config --libs sdl2) -lm -lpthread -o "$out/preview"
echo "built $out/preview"
fi
script=${1:-tools/ui-preview/tour.txt}
rm -f "$out"/shots/*.bmp "$out"/shots/*.png
(
    cd "$out"
    SwanStationPS5_ROOT="/work/build/preview-root" SwanStationPS5_SCRIPT="/work/$script" SwanStationPS5_SHOTS="shots" \
        SwanStationPS5_ASSETS="/work/assets" SwanStationPS5_FAKE_SCREEN=${SwanStationPS5_FAKE_SCREEN:-} SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-offscreen} \
        timeout 120 ./preview || echo "preview exited $?"
)
python3 - "$out/shots" <<'PY'
import sys, pathlib
from PIL import Image
for bmp in pathlib.Path(sys.argv[1]).glob("*.bmp"):
    Image.open(bmp).save(bmp.with_suffix(".png"))
    bmp.unlink()
print("screenshots:", ", ".join(sorted(p.name for p in pathlib.Path(sys.argv[1]).glob("*.png"))))
PY
