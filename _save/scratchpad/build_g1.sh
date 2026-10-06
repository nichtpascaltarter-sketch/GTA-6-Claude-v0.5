#!/bin/sh
# Build the tree in the current directory with -O2 -g1 (line tables, not stripped) for crash symbolization.
set -e
mkdir -p build/gen bin
[ -x build/embed_shaders ] || g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
x86_64-w64-mingw32-g++ -std=c++17 -O2 -g1 -DNDEBUG $EXTRA --param ggc-min-expand=20 --param ggc-min-heapsize=32768 -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -w -Ibuild/gen \
  src/main.cpp -o ${OUT:-bin/g1.exe} -static -static-libgcc -static-libstdc++ -mwindows \
  -ld3d11 -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt
ls -la ${OUT:-bin/g1.exe}
