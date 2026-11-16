#!/usr/bin/env bash
# Installs the laptop preview's launchers for this user:
#   ~/.local/bin/ps1-shelf-preview                    (terminal: ps1-shelf-preview [current|v2|--with P1-01,..|local])
#   ~/.local/share/applications/ps1-shelf-preview.desktop  ("PS1 Shelf Preview": a version picker, and right-click
#                                                       actions for current / v2)
# The wrapper runs preview.sh from the laptop-preview branch even when another branch is checked out.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here=$(cd -- "$(dirname -- "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)
repo=$(cd "$here/../../.." && pwd)
bin="$HOME/.local/bin/ps1-shelf-preview"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/icons"

cat > "$bin" <<EOF
#!/usr/bin/env bash
# PS1 Shelf Preview launcher (made by $repo/tools/ui-preview/laptop/install.sh)
repo="$repo"
if [[ -x \$repo/tools/ui-preview/laptop/preview.sh ]]; then
    exec "\$repo/tools/ui-preview/laptop/preview.sh" "\$@"
fi
# another branch is checked out: run the tooling from the laptop-preview branch
d="\${XDG_CACHE_HOME:-\$HOME/.cache}/ps1-shelf-preview"
rm -rf "\$d"; mkdir -p "\$d"
git -C "\$repo" archive laptop-preview tools/ui-preview/laptop | tar -x -C "\$d"
PREVIEW_REPO="\$repo" exec "\$d/tools/ui-preview/laptop/preview.sh" "\$@"
EOF
chmod +x "$bin"

icon=ps1-shelf-preview
python3 - "$HOME/.local/share/icons/$icon.png" <<'PY'
import sys
from PIL import Image, ImageDraw
# a small grey console-style icon with the four face-button colours (drawn here)
img = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle([16, 56, 240, 200], 28, fill=(196, 196, 204, 255), outline=(90, 90, 100, 255), width=6)
d.ellipse([60, 92, 132, 164], fill=(120, 120, 130, 255))
for (x, y), c in zip([(176, 96), (206, 126), (176, 156), (146, 126)],
                     [(64, 200, 140), (230, 70, 90), (90, 140, 240), (230, 120, 200)]):
    d.ellipse([x - 13, y - 13, x + 13, y + 13], fill=c + (255,))
img.save(sys.argv[1])
PY

cat > "$HOME/.local/share/applications/ps1-shelf-preview.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=PS1 Shelf Preview
Comment=Try the SwanStation PS1 shelf designs in a window (fake library, console sounds)
Exec=$bin --pick
Icon=$HOME/.local/share/icons/$icon.png
Terminal=false
Categories=Game;
StartupWMClass=ps1-shelf-preview
Actions=current;v2;choose;

[Desktop Action current]
Name=Current (ps1-console-shelf)
Exec=env PREVIEW_GUI=1 $bin current

[Desktop Action v2]
Name=v2 (all changes)
Exec=env PREVIEW_GUI=1 $bin v2

[Desktop Action choose]
Name=Choose version...
Exec=$bin --pick
EOF
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
echo "installed: $bin and ~/.local/share/applications/ps1-shelf-preview.desktop"
