#!/bin/sh
# Cross-compiles NeonTide.exe on Linux with MinGW-w64 (static, no DLL dependencies).
# Usage: ./build.sh [debug]   (OUT=path/to.exe overrides the output file, EXTRA="-DNO_CHARACTERS" adds flags)
set -e
cd "$(dirname "$0")"
mkdir -p build/gen bin
CXX=${CXX:-x86_64-w64-mingw32-g++}
# (under a shared memory cap, other builds go before the lead's snapshot checks: see tools/run.sh)
if [ -z "$NT_LEAD_SLOT" ]; then echo 500 > /proc/self/oom_score_adj 2>/dev/null; fi
if [ ! -x build/embed_shaders ] || [ tools/embed_shaders.cpp -nt build/embed_shaders ]; then
  g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders
fi
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
# The hand-built world (docs/HANDBUILT_WORLD.md): tools/worldc compiles world/ into build/gen/world.bin and world.rc,
# and the resource object embeds it in the exe (src/world/handworld.cpp reads it with --world=hand)
WORLDC_SRC="tools/worldc.cpp src/world/ntw.h src/world/ntw.cpp src/world/ntwschema.h src/world/ntwschema.cpp src/world/gridcodec.h src/world/gridcodec.cpp src/world/lz.h src/world/lz.cpp src/world/worldbin.h src/world/worldbin.cpp"
# (several builds share build/: every output goes through a temp file and a rename, and nothing is redone while the
# resource is newer than worldc and everything under world/)
if [ ! -x build/worldc ] || [ -n "$(find $WORLDC_SRC -newer build/worldc 2>/dev/null)" ]; then
  g++ -O2 -std=c++17 -I. tools/worldc.cpp -o build/worldc.$$
  mv -f build/worldc.$$ build/worldc
fi
if [ ! -f build/gen/world_res.o ] || [ build/worldc -nt build/gen/world_res.o ] || [ -n "$(find world -newer build/gen/world_res.o 2>/dev/null)" ]; then
  ./build/worldc --world=world --out=build/gen/world.bin
  (cd build/gen && ${WINDRES:-x86_64-w64-mingw32-windres} world.rc -O coff -o world_res.o.$$)
  mv -f build/gen/world_res.o.$$ build/gen/world_res.o
fi
WORLD_OBJ=build/gen/world_res.o
OPT="-O2 -DNDEBUG"
# Keep the unity build's peak memory near 1.2 GB (several builds may run at once on small machines)
GCCMEM="--param ggc-min-expand=20 --param ggc-min-heapsize=32768"
STRIP="-s"
if [ "$1" = "debug" ]; then OPT="-O0 -g"; STRIP=""; fi
# QUICK=1: -O1 for iteration builds on a busy test machine (about a third less compile time; not for timings)
if [ "${QUICK:-0}" = 1 ] && [ "$1" != "debug" ]; then OPT="-O1 -DNDEBUG"; fi
# VERSION=0.02.7 names the version the exe shows (src/core/version.h)
VER=
if [ -n "$VERSION" ]; then
  case "$VERSION" in *[!0-9A-Za-z.+-]*) echo "VERSION may only use 0-9 A-Z a-z . + -" >&2; exit 1;; esac
  VER="-DNT_VERSION=\"$VERSION\""
fi
# The commit the exe is built from, which the updater compares with GitHub's main (src/core/version.h): NT_COMMIT when
# set, else git's HEAD when this folder is a git checkout itself (not inside another one), else unknown
COMMIT=${NT_COMMIT:-}
if [ -z "$COMMIT" ] && [ -e .git ] && command -v git >/dev/null 2>&1; then COMMIT=$(git rev-parse HEAD 2>/dev/null || true); fi
case "$COMMIT" in *[!0-9a-f]*) COMMIT=;; esac
if [ ${#COMMIT} -ne 40 ]; then COMMIT=unknown; fi
VER="$VER -DNT_COMMIT=\"$COMMIT\""
# Machines that run several builds at once (the automated test rig) serialize them: each compile peaks near 2.1 GB.
# Opt in by creating /tmp/neontide_build.lock; the lock is released when this script exits.
if [ -e /tmp/neontide_build.lock ] && command -v flock >/dev/null 2>&1; then
  # (the lead's snapshot checks go first: see tools/run.sh)
  if [ -z "$NT_LEAD_SLOT" ]; then
    n=0
    while [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ $n -lt 120 ]; do sleep 15; n=$((n+1)); done
  fi
  exec 9>>/tmp/neontide_build.lock
  flock 9
  # the wait for the lock can be long: start only once there is room for the compile (Wine runs come and go; the
  # room is counted under the container's memory cap, where hitting it kills the compile)
  if [ -r /proc/meminfo ]; then
    # (under the launch lock games take too, held 15 s past the check until the compile shows up in memfree's count)
    exec 7>>/tmp/neontide_launch.lock
    flock 7
    n=0
    while [ "$(sh tools/memfree.sh 2>/dev/null || echo 100000)" -lt 3000 ] && [ $n -lt 90 ]; do sleep 10; n=$((n+1)); done
    sleep 15 </dev/null >/dev/null 2>&1 9>&- &
    exec 7>&-
  fi
fi
$CXX -std=c++17 $OPT $VER $EXTRA $GCCMEM -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable \
  -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen \
  src/main.cpp -o ${OUT:-bin/NeonTide.exe} $STRIP -static -static-libgcc -static-libstdc++ -mwindows \
  $WORLD_OBJ \
  -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt -lwinhttp -lbcrypt
ls -la ${OUT:-bin/NeonTide.exe}
