#!/bin/bash
# batches 17 + 18 (world side) into the shared tree from $S/dev26: check each shared copy is the expected base (HEAD's,
# batch 16's propmesh.cpp, or the beacon-sync worldtypes.h already there), copy, write the blobs, MinGW in the shared tree
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
declare -A BASE=(
  [src/world/propmesh.cpp]=e521a68e115dbb2a88cc4a33fcbb55097ebf0136
  [src/world/worldtypes.h]=63a9e6e3f61166ae61a4c7e5a69e3402a5be6554
)
F="src/world/propmesh.cpp src/world/cellgen.cpp src/render/props_render.cpp src/world/landmarks.cpp src/world/worldtypes.h src/world/sitegeo.cpp src/world/leisure.cpp src/world/airport.cpp src/world/port.cpp src/world/rural.cpp src/world/buildmesh.cpp"
for f in $F; do
  want=${BASE[$f]:-$(git rev-parse HEAD:$f)}
  if [ "$(git hash-object $f)" != "$want" ]; then echo "shared $f is not the expected base ($want): stop"; exit 1; fi
done
echo "shared copies are the expected bases on HEAD $(git rev-parse --short HEAD)"
for f in $F; do cp $S/dev26/$f $f; done
for f in $F; do echo "$f $(git hash-object -w $f) (base $(echo ${BASE[$f]:-$(git rev-parse HEAD:$f)} | cut -c1-7))"; done | tee /tmp/city/b17_blobs.txt
while [ "$(sh tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_shared_b17.log 2>&1
echo "syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_shared_b17.log) warnings=$(grep -c 'warning' /tmp/city/syntax_shared_b17.log)"
