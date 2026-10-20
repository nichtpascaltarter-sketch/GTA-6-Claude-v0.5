#!/bin/sh
# transit agent build: waits for memory, builds bin/nt_transit.exe (O1 to keep the compiler's footprint down)
cd /home/user/GTA-6-Claude-v0.5
LOG=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/tr_build.txt
while [ "$(free -m | awk '/Mem:/ {print $7}')" -lt 3000 ] || [ "$(ps aux | grep -c '[c]c1plus')" -gt 3 ]; do sleep 15; done
date > $LOG
./build/embed_shaders src/shaders build/gen/shaders_embedded.h >> $LOG 2>&1
( nice -n 5 x86_64-w64-mingw32-g++ -std=c++17 ${OPT:--O1} -DNDEBUG --param ggc-min-expand=10 --param ggc-min-heapsize=16384 -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable \
  -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen \
  src/main.cpp -o ${EXE_OUT:-bin/nt_transit.exe} -s -static -static-libgcc -static-libstdc++ -mwindows \
  -ld3d11 -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt ) >> $LOG 2>&1
echo "exit $?" >> $LOG; date >> $LOG
grep -E "error|^exit|real" $LOG | head -40
