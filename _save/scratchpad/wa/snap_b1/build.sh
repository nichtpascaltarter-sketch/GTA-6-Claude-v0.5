#!/bin/sh
# Cross-compiles NeonTide.exe on Linux with MinGW-w64 (static, no DLL dependencies).
# Usage: ./build.sh [debug]   (OUT=path/to.exe overrides the output file, EXTRA="-DNO_CHARACTERS" adds flags)
set -e
cd "$(dirname "$0")"
mkdir -p build/gen bin
CXX=${CXX:-x86_64-w64-mingw32-g++}
if [ ! -x build/embed_shaders ] || [ tools/embed_shaders.cpp -nt build/embed_shaders ]; then
  g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders
fi
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
OPT="-O2 -DNDEBUG"
# Keep the unity build's peak memory near 1.2 GB (several builds may run at once on small machines)
GCCMEM="--param ggc-min-expand=20 --param ggc-min-heapsize=32768"
STRIP="-s"
if [ "$1" = "debug" ]; then OPT="-O0 -g"; STRIP=""; fi
# Machines that run several builds at once (the automated test rig) serialize them: each compile peaks near 1.8 GB.
# Opt in by creating /tmp/neontide_build.lock; the lock is released when this script exits.
if [ -e /tmp/neontide_build.lock ] && command -v flock >/dev/null 2>&1; then
  exec 9>>/tmp/neontide_build.lock
  flock 9
fi
$CXX -std=c++17 $OPT $EXTRA $GCCMEM -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable \
  -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen \
  src/main.cpp -o ${OUT:-bin/NeonTide.exe} $STRIP -static -static-libgcc -static-libstdc++ -mwindows \
  -ld3d11 -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt
ls -la ${OUT:-bin/NeonTide.exe}
