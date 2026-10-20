#!/bin/bash
# Part 1's blobs from $S/p62, with guards: every base must be 62's or the roads set's (rb62 on 62).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
R=/home/user/GTA-6-Claude-v0.5
ok=1
base() { git -C $R ls-tree 7b9146d -- $1 | awk '{print $3}' | cut -c1-8; }
# the roads set's blobs on 62 (rb_blobs.txt with main.cpp and build.sh from rb62_blobs.txt)
declare -A RS=( [src/world/handworld.cpp]=6b769ce8 [src/world/handworld.h]=76052426 [tools/worldc.cpp]=4ef1bdf8 [build.sh]=ff1e292c
               [docs/HANDBUILT_WORLD.md]=2803a921 [docs/NTW_FORMAT.md]=c1559061 [tests/world/worldmodel_test.cpp]=c9a77220 )
out=/tmp/city/p1_blobs.txt; : > $out
for f in world/world.ntw world/districts/cl/district.ntw world/districts/cl/roads.ntw world/lib/signals/cl_twophase.ntw \
         src/world/terrainops.cpp src/world/roadgrade.cpp src/world/worldmap.cpp src/world/roads.cpp src/world/roads.h \
         src/world/worldmodel_roads.cpp src/world/cellgen.cpp src/world/roadmesh.cpp src/world/handworld.cpp src/world/handworld.h \
         tools/worldc.cpp build.sh docs/HANDBUILT_WORLD.md docs/NTW_FORMAT.md tests/world/worldmodel_test.cpp src/game/app.cpp src/game/savegame.cpp; do
  if [ -n "${RS[$f]}" ]; then b=${RS[$f]}; src="the roads set";
    # the roads set's blob must be what part 1 was built on: rb's file (rb62's for build.sh)
    if [ "$f" = build.sh ]; then have=$(git -C $R hash-object $S/rb62/build.sh.merged | cut -c1-8); else have=$(git -C $R hash-object $S/rb/$f | cut -c1-8); fi
    [ "$have" = "$b" ] || { echo "GUARD $f: roads blob $have != $b"; ok=0; }
  else b=$(base $f); src="62"; [ -z "$b" ] && b=new; fi
  h=$(git -C $R hash-object -w $S/p62/$f)
  if [ "$b" = new ]; then echo "$f ${h:0:8} new" >> $out; else echo "$f ${h:0:8} on $b ($src)" >> $out; fi
done
[ $ok = 1 ] && cat $out || { echo "guards failed"; exit 1; }
