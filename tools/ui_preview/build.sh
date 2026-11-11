#!/usr/bin/env bash
# Render the base station screens on the PC, from the real UI code.
# Needs: g++, python3 + Pillow, and one `pio run -e base_cyd` first (for the LovyanGFX font files).
# Usage: tools/ui_preview/build.sh [output dir]   (default: docs/img/ui)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
fw="$repo/firmware"
out="${1:-$repo/docs/img/ui}"
lgfx="$fw/.pio/libdeps/base_cyd/LovyanGFX/src"
[ -d "$lgfx" ] || { echo "run 'pio run -e base_cyd' in firmware/ first (LovyanGFX fonts needed)"; exit 1; }
mkdir -p "$out"
tmp="$(mktemp -d)"
g++ -std=gnu++17 -O1 -Wall -Wno-unused-parameter -Wno-unused-variable \
    -D TM_RELAY_ACTIVE_HIGH=1 -D TM_LCD_ST7789=1 \
    -I "$here/fake" -I "$lgfx" -I "$fw/src/base" -I "$fw/lib/tm_control/src" -I "$fw/lib/tm_util/src" \
    "$here/preview_main.cpp" "$here/fake_env.cpp" "$here/fake_lgfx.cpp" \
    "$fw/src/base/ui.cpp" "$fw/src/base/ui_menu.cpp" \
    "$fw"/lib/tm_control/src/*.cpp \
    -o "$tmp/ui_preview"
"$tmp/ui_preview" "$tmp"
python3 - "$tmp" "$out" <<'EOF'
import sys, glob, os
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
for p in sorted(glob.glob(os.path.join(src, "*.ppm"))):
    im = Image.open(p).resize((640, 480), Image.NEAREST)
    im.save(os.path.join(dst, os.path.basename(p)[:-4] + ".png"))
print("PNG files in", dst)
EOF
rm -rf "$tmp"
