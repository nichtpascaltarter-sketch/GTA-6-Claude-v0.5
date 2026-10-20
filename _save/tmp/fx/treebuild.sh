#!/bin/sh
# Builds my renderer work against the last committed tree (other agents' in-progress edits excluded).
# Mirrors build.sh (D3D12: links dxgi only). QUICK=1 -> -O1. REFRESH=1 re-exports HEAD. OVERLAY="paths" adds files.
set -e
# serialize compiles with the other agents' builds (build.sh uses the same lock) and wait for room
if [ -e /tmp/neontide_build.lock ] && command -v flock >/dev/null 2>&1; then exec 9>>/tmp/neontide_build.lock; flock 9; fi
REPO=/home/user/GTA-6-Claude-v0.5
n=0; while [ "$(sh $REPO/tools/memfree.sh 2>/dev/null || echo 100000)" -lt 3200 ] && [ $n -lt 120 ]; do sleep 10; n=$((n+1)); done
T=/tmp/fx/tree
if [ ! -d $T/src ] || [ "$REFRESH" = "1" ]; then
  rm -rf $T; mkdir -p $T
  (cd $REPO && git archive HEAD) | tar -x -C $T
fi
if [ -z "$PURE" ]; then
  rm -rf $T/src/render $T/src/shaders; cp -r $REPO/src/render $T/src/render
  cp -r $REPO/src/shaders $T/src/shaders
fi
for f in $OVERLAY; do cp $REPO/$f $T/$f; done
# REVERT="paths": take these files from HEAD instead of the working tree (a "before" build)
for f in $REVERT; do (cd $REPO && git show HEAD:$f) > $T/$f; done
cd $T
OPT="-O2 -DNDEBUG"; [ -n "$QUICK" ] && OPT="-O1 -DNDEBUG"
mkdir -p build/gen bin
[ -x build/embed_shaders ] || g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
x86_64-w64-mingw32-g++ -std=c++17 $OPT $EXTRA --param ggc-min-expand=20 --param ggc-min-heapsize=32768 -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable \
  -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen \
  src/main.cpp -o $REPO/${OUT:-bin/nt_fx.exe} -s -static -static-libgcc -static-libstdc++ -mwindows \
  -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt
ls -la $REPO/${OUT:-bin/nt_fx.exe}
