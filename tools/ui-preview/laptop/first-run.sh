#!/usr/bin/env bash
# Laptop preview: the first start on an empty data folder (no BIOS, settings, covers, logo cache or USB folder).
#   first-run.sh BIOS_FILE [GAMES_DIR]
# Run 1 (no games): the setup screen, CHECK AGAIN with nothing, the BIOS copied in while it waits, CHECK AGAIN, the
# opening, the empty library, Settings. Run 2 (GAMES_DIR's games copied in, no covers): the next start's opening and
# the library. Shots in build/laptop-preview/first-run/shots{1,2}. Build first: preview.sh local --build-only.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here=$(cd -- "$(dirname -- "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)
repo=$(cd "$here/../../.." && pwd)
bios=$(readlink -f "$1"); games=${2:-}
out="$repo/build/laptop-preview/first-run"; bin="$repo/build/laptop-preview/bin/local/preview"
[[ -x $bin ]] || { echo "build first: preview.sh local --build-only" >&2; exit 1; }
rm -rf "${out:?}"; mkdir -p "$out/root" "$out/shots1" "$out/shots2"
run() { # run N SCRIPT
    SwanStationPS5_ROOT="$out/root" SwanStationPS5_ASSETS="$repo/assets" SwanStationPS5_SHOTS="$out/shots$1" \
        SwanStationPS5_WINDOW=0 SwanStationPS5_SCRIPT="$2" SDL_AUDIODRIVER=dummy "$bin" > "$out/run$1.log" 2>&1
}
# the BIOS goes in once the "nothing found" shot is taken
( for _ in $(seq 600); do [[ -e $out/shots1/f03-check-nothing.bmp ]] && break; sleep 0.1; done
  mkdir -p "$out/root/bios"; cp "$bios" "$out/root/bios/" ) &
run 1 "$here/../ps1-v3-first-run.txt"
wait
if [[ -n $games ]]; then
    mkdir -p "$out/root/games"; cp -r "$games"/. "$out/root/games/"
    run 2 "$here/../ps1-v3-first-run2.txt"
fi
echo "shots: $out/shots1 $out/shots2"
