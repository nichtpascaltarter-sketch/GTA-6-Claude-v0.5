#!/bin/sh
# Cross-compiles NeonTide.exe on Linux with MinGW-w64 (static, no DLL dependencies).
# Usage: ./build.sh [debug]   (OUT=path/to.exe overrides the output file)
set -e
cd "$(dirname "$0")"
mkdir -p build/gen bin
CXX=${CXX:-x86_64-w64-mingw32-g++}
if [ ! -x build/embed_shaders ] || [ tools/embed_shaders.cpp -nt build/embed_shaders ]; then
  g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders
fi
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
OPT="-O2 -DNDEBUG"
STRIP="-s"
if [ "$1" = "debug" ]; then OPT="-O0 -g"; STRIP=""; fi
$CXX -std=c++17 $OPT -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable \
  -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen \
  src/main.cpp -o ${OUT:-bin/NeonTide.exe} $STRIP -static -static-libgcc -static-libstdc++ -mwindows \
  -ld3d11 -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt
ls -la ${OUT:-bin/NeonTide.exe}
