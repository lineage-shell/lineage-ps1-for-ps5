#!/usr/bin/env bash
# Laptop preview: the PS1 shelf in a real window with keyboard/controller input and the console sounds.
#
#   preview.sh [VERSION] [--script FILE] [--rebuild] [--build-only] [--list]
#
# VERSION:
#   current            branch ps1-console-shelf
#   v2                 branch ps1-shelf-v2 (all P1 changes)             (the default)
#   --with P1-01,P1-03 ps1-shelf-v2's preview tooling commit plus only these P1 commits, cherry-picked in order
#   local              this checkout's working tree as it is now (for trying a design)
#   --pick             a dialog to choose (zenity), used by the desktop launcher
# Recording: PREVIEW_WINDOW=0 SwanStationPS5_RECORD=<dir> preview.sh local --script FILE writes <dir>/video.mp4 (1080p60,
# every frame on the fixed clock) and <dir>/audio.wav (record.inc).
#
# Each build is cached in build/laptop-preview/bin/<key>/ (key = the commits + this tooling), so a second run starts
# at once. Builds use the host's gcc and SDL2 (no Docker), in a temporary git worktree that is removed afterwards.
# In the window F2 / F3 switch between every cached build. --script FILE plays a ui-preview script in the window.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here=$(cd -- "$(dirname -- "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)
repo=${PREVIEW_REPO:-$(cd "$here/../../.." && pwd)}
out="$repo/build/laptop-preview"
sounds=${PS1_SOUNDS:-$HOME/ps5-work/sounds/ps1}
CURRENT=ps1-console-shelf V2=ps1-shelf-v2 TOOLING=laptop-preview
mkdir -p "$out/bin"

version=v2 with="" script="" rebuild=0 build_only=0 pick=0
while (($#)); do
    case $1 in
        current|v2|local) version=$1 ;;
        --with) version=with; with=$2; shift ;;
        --with=*) version=with; with=${1#--with=} ;;
        --script) script=$(readlink -f "$2"); shift ;;
        --rebuild) rebuild=1 ;;
        --build-only) build_only=1 ;;
        --pick) pick=1 ;;
        --list)
            git -C "$repo" log --reverse --format='%s' "$CURRENT..$V2" | grep '^P1-' | cut -c1-90
            exit 0 ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

say() { echo "$*"; [[ -n ${PREVIEW_GUI:-} ]] && notify-send -a "PS1 Shelf Preview" "PS1 Shelf Preview" "$*" 2>/dev/null || true; }
fail() { echo "error: $*" >&2; [[ -n ${PREVIEW_GUI:-} ]] && zenity --error --title "PS1 Shelf Preview" --text "$*" 2>/dev/null; exit 1; }

p1_commits() { git -C "$repo" log --reverse --format='%h %s' "$CURRENT..$V2" | grep ' P1-'; }

if ((pick)); then
    export PREVIEW_GUI=1
    choice=$(zenity --list --radiolist --title "PS1 Shelf Preview" --text "Which version?" --width 560 --height 420 \
        --column "" --column "Version" --column "What" \
        FALSE current "ps1-console-shelf (as shipped)" TRUE v2 "ps1-shelf-v2: all P1 changes" \
        FALSE pick "choose P1 changes..." FALSE local "this checkout's working tree") || exit 0
    case $choice in
        pick)
            rows=()
            while read -r h subject; do rows+=(FALSE "${subject%%:*}" "${subject#*: }"); done < <(p1_commits)
            sel=$(zenity --list --checklist --title "PS1 Shelf Preview" --text "P1 changes to include" --width 900 \
                --height 520 --separator , --column "" --column "Change" --column "What" "${rows[@]}") || exit 0
            version=with with=$sel ;;
        *) version=$choice ;;
    esac
fi

# ---- what to build: a base commit and the commits to cherry-pick on it
base="" picks=() label=""
case $version in
    current) base=$(git -C "$repo" rev-parse "$CURRENT"); label="current (ps1-console-shelf)" ;;
    v2) base=$(git -C "$repo" rev-parse "$V2"); label="v2 (all changes)" ;;
    with)
        base=$(git -C "$repo" log --reverse --format=%H "$CURRENT..$V2" | head -1) # the preview tooling commit
        for id in ${with//,/ }; do
            id=${id^^}
            [[ $id =~ ^P1-[0-9]$ ]] && id="P1-0${id#P1-}"
            h=$(p1_commits | awk -v id="$id:" '$2 == id {print $1}')
            [[ -n $h ]] || fail "no commit for $id on $V2 (try --list)"
            picks+=("$(git -C "$repo" rev-parse "$h")")
        done
        ((${#picks[@]})) || fail "--with needs a list such as P1-01,P1-03"
        label="with ${with^^}" ;;
    local) label="local working tree" ;;
esac

# the tooling laid over every build: plat_sdl.c's hooks (as a patch) and tools/ui-preview/laptop
tool_ref=$(git -C "$repo" rev-parse --verify -q "$TOOLING" || true)
[[ -n $tool_ref ]] || tool_ref=$(git -C "$repo" rev-parse --verify -q "origin/$TOOLING" || true)
[[ -n $tool_ref || $version == local ]] || fail "branch $TOOLING not found"
patch() { git -C "$repo" diff "$(git -C "$repo" merge-base "$V2" "$tool_ref")" "$tool_ref" -- src; }

if [[ $version == local ]]; then
    key=local
else
    key=$( { echo "$base ${picks[*]}"; patch; git -C "$repo" rev-parse "$tool_ref:tools/ui-preview/laptop"; } | sha1sum | cut -c1-12)
fi
bindir="$out/bin/$key"

compile() { # compile tree into $bindir/preview (parallel, -O2)
    local tree=$1 obj="$bindir/obj"
    mkdir -p "$obj"
    local sources
    sources=$(cd "$tree" && sed -n '/^sources=(/,/)$/p' tools/ui-preview/build-linux.sh | tr -d '()' | sed 's/sources=//')
    local cflags="-std=gnu11 -O2 -g0 -w -D_GNU_SOURCE -DSwanStationPS5_PREVIEW -DSDL_MAIN_HANDLED
        -include tools/ui-preview/compat.h -Isrc -Ithird_party/stb -Ithird_party/swanstation/dep/libretro-common/include
        -Ithird_party/rcheevos/include $(pkg-config --cflags sdl2)"
    (cd "$tree" && for f in $sources; do echo "$f"; done | \
        xargs -P "$(nproc)" -I{} sh -c 'gcc '"$(echo $cflags)"' -c "$1" -o "$2/$(echo "$1" | tr / _).o"' _ {} "$obj") \
        || return 1
    gcc "$obj"/*.o $(pkg-config --libs sdl2) -lm -lpthread -o "$bindir/preview" || return 1
    rm -rf "$obj"
}

overlay() { # put the window hooks and tools/ui-preview/laptop into the tree $1 (unless it has them already)
    grep -q laptop/live.inc "$1/src/platform/plat_sdl.c" || patch | git -C "$1" apply - ||
        fail "the laptop window hooks do not apply to this version"
    rm -rf "$1/tools/ui-preview/laptop"
    git -C "$repo" archive "$tool_ref" tools/ui-preview/laptop | tar -x -C "$1"
}

if [[ $version == local ]]; then
    # a copy of this checkout's src/ and tools/ (whatever branch, uncommitted changes included)
    say "building the local working tree..."
    rm -rf "$bindir"; mkdir -p "$bindir"
    lt="$out/local-tree"
    rm -rf "$lt"; mkdir -p "$lt/third_party"
    cp -r "$repo/src" "$repo/tools" "$lt/"
    for d in "$repo"/third_party/*; do ln -s "$d" "$lt/third_party/"; done
    git -C "$lt" init -q 2>/dev/null
    if [[ -e $repo/tools/ui-preview/laptop/live.inc ]] && grep -q laptop/live.inc "$lt/src/platform/plat_sdl.c"; then
        :   # the checkout is the laptop-preview branch (or has it merged): use its tooling as it is
    else
        [[ -n $tool_ref ]] || fail "branch $TOOLING not found"
        overlay "$lt"
    fi
    compile "$lt" || fail "the build failed"
    rm -rf "$lt"
elif [[ $rebuild == 1 || ! -x $bindir/preview ]]; then
    say "building $label (about 15 s, cached afterwards)..."
    rm -rf "$bindir"; mkdir -p "$bindir"
    wt="$out/wt-$key"
    git -C "$repo" worktree remove --force "$wt" 2>/dev/null || rm -rf "$wt"
    git -C "$repo" worktree prune
    git -C "$repo" worktree add -q --detach "$wt" "$base"
    cleanup() { git -C "$repo" worktree remove --force "$wt" 2>/dev/null || true; }
    trap cleanup EXIT
    for c in "${picks[@]}"; do
        git -C "$wt" -c user.name=preview -c user.email=preview@localhost cherry-pick -x "$c" >/dev/null 2>&1 || {
            git -C "$wt" cherry-pick --abort 2>/dev/null || true
            rm -rf "$bindir"
            fail "$(git -C "$repo" log -1 --format=%s "$c" | cut -c1-6) does not apply on top of the ones before it (it needs an earlier P1 change)"
        }
    done
    # submodules are not checked out in a worktree: use the main checkout's (headers only)
    for sm in third_party/swanstation third_party/rcheevos; do rm -rf "$wt/$sm"; ln -s "$repo/$sm" "$wt/$sm"; done
    overlay "$wt"
    compile "$wt" || { rm -rf "$bindir"; fail "the build failed"; }
    cleanup; trap - EXIT
fi
echo "$label" > "$bindir/label"
# builds made with older tooling are dropped (they would show up twice in the F2 list)
tool_id=$( [[ -n $tool_ref ]] && git -C "$repo" rev-parse "$tool_ref:tools/ui-preview/laptop" || echo none)
[[ $version == local ]] || echo "$tool_id" > "$bindir/tool"
for d in "$out"/bin/*; do
    [[ -d $d && ${d##*/} != local && $(cat "$d/tool" 2>/dev/null) != "$tool_id" ]] && rm -rf -- "$out/bin/${d##*/}"
done
((build_only)) && { echo "built $bindir/preview"; exit 0; }

# ---- the data folder and the version list for F2 / F3 (every cached build, newest first after this one)
python3 -I "$here/make-root.py" "$out/root" "$repo" "$sounds" >/dev/null
versions="$label|$bindir/preview"
while read -r d; do
    [[ $d == "$bindir" || ! -x $d/preview ]] && continue
    versions+=";$(cat "$d/label")|$d/preview"
done < <(ls -1dt "$out"/bin/*/ 2>/dev/null | sed 's:/$::')

cd "$out"
mkdir -p shots
# the version list's labels are kept per binary, so whichever one runs knows its name: VERSION_NAME is set from it
export SwanStationPS5_ROOT="$out/root" SwanStationPS5_ASSETS="$repo/assets" SwanStationPS5_SHOTS="$out/shots" \
    SwanStationPS5_WINDOW=${PREVIEW_WINDOW:-1} SwanStationPS5_KEYMAP="$out/root/keymap.png" SwanStationPS5_VERSIONS="$versions" \
    SwanStationPS5_VERSION_INDEX=0 SwanStationPS5_VERSION_NAME="$label"
[[ -n $script ]] && export SwanStationPS5_SCRIPT="$script" || unset SwanStationPS5_SCRIPT
unset SDL_VIDEODRIVER SDL_AUDIODRIVER
export SDL_APP_NAME="PS1 Shelf Preview" SDL_VIDEO_WAYLAND_WMCLASS=ps1-shelf-preview SDL_VIDEO_X11_WMCLASS=ps1-shelf-preview
echo "running $label  (log: $out/preview.log)"
exec "$bindir/preview" > >(tee "$out/preview.log") 2>&1
