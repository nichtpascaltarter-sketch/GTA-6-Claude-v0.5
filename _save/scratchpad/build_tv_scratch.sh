#!/bin/sh
# Scratch variant of tests/vehicle/build.sh that skips an in-progress world file (facadedetail.cpp).
set -e
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
OUT=${1:-$S/bin/tv}
GEN=$S/gen
grep -E '^#include "world/[A-Za-z0-9_]+\.cpp"' src/main.cpp | grep -v facadedetail | sed 's|#include "world/|#include "src/world/|' > "$GEN/vt_world_includes.h"
echo "#include \"$S/facade_noop.cpp\"" >> "$GEN/vt_world_includes.h"
nice -n 10 g++ -O2 -std=c++17 --param ggc-min-expand=20 --param ggc-min-heapsize=131072 -I src -I . -I "$GEN" tests/vehicle/test_vehicle.cpp -o "$OUT" -lpthread
echo "built $OUT"
