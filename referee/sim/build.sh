#!/bin/bash
# Builds a native (host) copy of the referee UI for fast visual iteration
# without flashing hardware. Links ui.cpp against the exact same LVGL
# library the firmware uses (vendored under referee/.pio/libdeps).
set -euo pipefail

cd "$(dirname "$0")"
REFEREE_DIR="$(cd .. && pwd)"
LVGL_DIR="$REFEREE_DIR/.pio/libdeps/referee/lvgl"
BLAHAJ_COMMON="$REFEREE_DIR/../lib/blahaj_common"
BUILD_DIR="$REFEREE_DIR/sim/build"
OBJ_DIR="$BUILD_DIR/obj"

mkdir -p "$OBJ_DIR"

INCLUDES=(
    -I"$REFEREE_DIR/include"
    -I"$LVGL_DIR"
    -I"$LVGL_DIR/src"
    -I"$BLAHAJ_COMMON"
    -I"$REFEREE_DIR/src"
)
DEFS=(-DLV_CONF_INCLUDE_SIMPLE=1)

# LVGL C sources — compiled once and cached; only recompiled if missing.
compiled_any=0
while IFS= read -r -d '' c_file; do
    obj="$OBJ_DIR/$(echo "$c_file" | sed 's|/|_|g').o"
    if [ ! -f "$obj" ] || [ "$c_file" -nt "$obj" ]; then
        gcc -c -O0 -g -std=gnu11 "${INCLUDES[@]}" "${DEFS[@]}" "$c_file" -o "$obj"
        compiled_any=1
    fi
done < <(find "$LVGL_DIR/src" -name '*.c' -print0)
if [ "$compiled_any" = 1 ]; then echo "Compiled LVGL C sources"; fi

# Our own sources — always recompiled (cheap, only a few files).
g++ -c -O0 -g -std=c++17 "${INCLUDES[@]}" "${DEFS[@]}" \
    "$REFEREE_DIR/src/ui.cpp" -o "$OBJ_DIR/ui.o"
g++ -c -O0 -g -std=c++17 "${INCLUDES[@]}" "${DEFS[@]}" \
    "$REFEREE_DIR/sim/sim_main.cpp" -o "$OBJ_DIR/sim_main.o"

g++ "$OBJ_DIR"/*.o -o "$BUILD_DIR/sim_referee" -lm

echo "Built $BUILD_DIR/sim_referee"
