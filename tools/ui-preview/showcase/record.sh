#!/usr/bin/env bash
# Records one showcase clip: record.sh NAME SCRIPT [ini key=value ...]; env passes through (SwanStationPS5_STUB_MOVIE..).
# Writes $OUT/NAME/video.mp4 + audio.wav (1080p60, the preview's fixed clock) and $OUT/NAME.mp4 (muxed).
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
out=${OUT:-$HOME/ps5-work/showcase/ps1/raw}
name=$1 script=$2; shift 2
ini="$repo/build/laptop-preview/root/SwanStationPS5.ini"
for kv in "$@"; do sed -i "s/^${kv%%=*}=.*/$kv/" "$ini"; done
mkdir -p "$out/$name"; rm -f "$out/$name/video.mp4" "$out/$name/audio.wav"
PREVIEW_WINDOW=0 SwanStationPS5_RECORD="$out/$name" \
    systemd-run --user --scope -q -p MemoryMax=2500M -p MemorySwapMax=0 --setenv=PREVIEW_WINDOW=0 \
    --setenv=SwanStationPS5_RECORD="$out/$name" ${SwanStationPS5_STUB_MOVIE:+--setenv=SwanStationPS5_STUB_MOVIE=$SwanStationPS5_STUB_MOVIE} \
    ${SwanStationPS5_STUB_FIRST:+--setenv=SwanStationPS5_STUB_FIRST=$SwanStationPS5_STUB_FIRST} \
    bash "$repo/tools/ui-preview/laptop/preview.sh" local --script "$script" > "$out/$name/run.log" 2>&1
ffmpeg -loglevel error -y -i "$out/$name/video.mp4" -i "$out/$name/audio.wav" -c:v copy -c:a aac -b:a 192k -shortest "$out/$name.mp4"
echo "$name: $(ffprobe -v error -show_entries format=duration -of csv=p=0 "$out/$name.mp4") s"
