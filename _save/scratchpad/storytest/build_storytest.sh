#!/bin/sh
# Builds the fast mission-test runner (scratch tool): src/main.cpp without its WinMain + loop.inc.
set -e
cd /home/user/GTA-6-Claude-v0.5
mkdir -p build/gen
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
ST=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/storytest
rm -f $ST/nt_storytest.exe
sed '/^int WINAPI WinMain/,$d' src/main.cpp > $ST/main_gen.cpp
cat $ST/loop.inc >> $ST/main_gen.cpp
# machine-wide rules: wait for memory, then take the shared build lock (released when this script exits)
/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh 3200
if [ -e /tmp/neontide_build.lock ] && command -v flock >/dev/null 2>&1; then
  exec 9>>/tmp/neontide_build.lock
  flock 9
fi
x86_64-w64-mingw32-g++ -std=c++17 -O1 -DNDEBUG --param ggc-min-expand=20 --param ggc-min-heapsize=32768 -march=x86-64-v2 -mfpmath=sse \
  -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess \
  -Ibuild/gen -Isrc $ST/main_gen.cpp -o $ST/nt_storytest.exe -s -static -static-libgcc -static-libstdc++ -mwindows \
  -ld3d11 -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt > $ST/build_full.log 2>&1 || true
grep -v "^In file included" $ST/build_full.log | grep -E -B2 -A3 "error|undefined" | head -40
ls -la $ST/nt_storytest.exe
