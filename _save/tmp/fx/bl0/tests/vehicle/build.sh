#!/bin/sh
# Builds the native (Linux) vehicle dynamics test harness.
# The world sources are taken from src/main.cpp (same files, same order) so the harness follows the game build.
# Usage: tests/vehicle/build.sh [output]      (default /tmp/test_vehicle)
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-/tmp/test_vehicle}
GEN=$(mktemp -d)
grep -E '^#include "world/[A-Za-z0-9_]+\.cpp"' src/main.cpp | sed 's|#include "world/|#include "src/world/|' > "$GEN/vt_world_includes.h"
g++ -O2 -std=c++17 --param ggc-min-expand=20 --param ggc-min-heapsize=131072 -I src -I . -I "$GEN" \
    tests/vehicle/test_vehicle.cpp -o "$OUT" -lpthread
rm -rf "$GEN"
echo "built $OUT"
