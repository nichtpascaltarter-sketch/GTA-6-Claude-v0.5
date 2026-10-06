#!/bin/sh
# Private build: like build.sh but can disable in-progress modules of other agents (SKIP="vehicle_sim ui_all").
set -e
cd /home/user/GTA-6-Claude-v0.5
mkdir -p build/gen bin
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
cp src/main.cpp /tmp/fx/build/main_fx.cpp
for m in $SKIP; do
  sed -i "s#__has_include(\"sim/$m.cpp\")#0#; s#__has_include(\"ui/$m.cpp\")#0#; s#__has_include(\"anim/$m.cpp\")#0#" /tmp/fx/build/main_fx.cpp
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -DNDEBUG $EXTRA -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable \
  -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen -Isrc \
  /tmp/fx/build/main_fx.cpp -o ${OUT:-bin/nt_fx.exe} -s -static -static-libgcc -static-libstdc++ -mwindows \
  -ld3d11 -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt
ls -la ${OUT:-bin/nt_fx.exe}
