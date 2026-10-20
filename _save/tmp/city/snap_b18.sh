#!/bin/bash
# the beacon-sync world side ("batch 17" in the lead's words) into the shared tree from $S/dev28: check the shared copies are
# HEAD's (worldtypes.h: the 63a9e6e3 the lead already put in), copy, write the blobs, MinGW in the shared tree
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
if [ "$(git hash-object src/world/worldtypes.h)" != "63a9e6e3f61166ae61a4c7e5a69e3402a5be6554" ]; then echo "shared worldtypes.h is not 63a9e6e3: stop"; exit 1; fi
if ! cmp -s src/world/worldtypes.h $S/dev28/src/world/worldtypes.h; then echo "dev28 worldtypes.h differs from the shared one: stop"; exit 1; fi
F="src/world/sitegeo.cpp src/world/landmarks.cpp src/world/leisure.cpp src/world/airport.cpp src/world/port.cpp src/world/rural.cpp src/world/buildmesh.cpp"
for f in $F; do
  if [ "$(git hash-object $f)" != "$(git rev-parse HEAD:$f)" ]; then echo "shared $f differs from HEAD: stop"; exit 1; fi
done
echo "shared copies equal HEAD $(git rev-parse --short HEAD) (worldtypes.h 63a9e6e3 already in)"
for f in $F; do cp $S/dev28/$f $f; done
for f in $F; do echo "$f $(git hash-object -w $f) (HEAD $(git rev-parse --short HEAD:$f))"; done | tee /tmp/city/b18_blobs.txt
echo "src/world/worldtypes.h $(git hash-object -w src/world/worldtypes.h) (HEAD $(git rev-parse --short HEAD:src/world/worldtypes.h)), already in the shared tree" | tee -a /tmp/city/b18_blobs.txt
while [ "$(sh tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_shared_b18.log 2>&1
echo "syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_shared_b18.log) warnings=$(grep -c 'warning' /tmp/city/syntax_shared_b18.log)"
