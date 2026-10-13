#!/bin/sh
# syntax check of a locotour tree's unity build (generates the embedded shaders header first). usage: check.sh <tree> [EXTRA flags]
cd "$1" || exit 1
mkdir -p build/gen
[ -x build/embed_shaders ] || g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders || exit 1
./build/embed_shaders src/shaders build/gen/shaders_embedded.h >/dev/null || exit 1
shift
echo 500 > /proc/self/oom_score_adj 2>/dev/null
x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces \
  -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen -Isrc "$@" src/main.cpp 2>&1 | grep -v "^In file included" | head -60
echo "check done"
