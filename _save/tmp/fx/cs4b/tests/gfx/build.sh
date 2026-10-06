#!/bin/sh
# Builds the graphics layer self-test (tests/gfx/gfx_selftest.exe) with MinGW-w64.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/gen
if [ ! -x build/embed_shaders ] || [ tools/embed_shaders.cpp -nt build/embed_shaders ]; then
  g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders
fi
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
x86_64-w64-mingw32-g++ -std=c++17 -O1 -g0 -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-class-memaccess \
  -march=x86-64-v2 -fno-strict-aliasing -Ibuild/gen tests/gfx/gfx_selftest.cpp -o tests/gfx/gfx_selftest.exe -s -static -static-libgcc \
  -static-libstdc++ -mwindows -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32
ls -la tests/gfx/gfx_selftest.exe
