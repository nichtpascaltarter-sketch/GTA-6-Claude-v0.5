#!/bin/sh
# Pull the shared tree's sources into the private dev copy, keeping my own world files (the city blocks).
R=/home/user/GTA-6-Claude-v0.5
D=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/dev
MINE=" buildings.cpp buildings.h buildmesh.cpp facadedetail.cpp cellgen.cpp sitecell.cpp sitegeo.cpp outskirts.cpp rural.cpp blockstyle.cpp massing.cpp lotfill.cpp "
n=0
cd $R/src && find . -type f | while read f; do
  base=$(basename "$f"); dir=$(dirname "$f")
  if [ "$dir" = "./world" ] && echo "$MINE" | grep -q " $base "; then
    if ! cmp -s "$R/src/$f" "$D/src/$f" && [ -e "$D/src/$f" ]; then echo "NOTE: shared $f differs from my copy (kept mine)"; fi
    continue
  fi
  if ! cmp -s "$R/src/$f" "$D/src/$f"; then mkdir -p "$D/src/$dir"; cp "$R/src/$f" "$D/src/$f"; echo "synced $f"; fi
done
for f in build.sh tools/memfree.sh tools/run.sh tools/native_stubs.cpp tools/embed_shaders.cpp tools/worldcheck.cpp; do
  cmp -s "$R/$f" "$D/$f" || { cp "$R/$f" "$D/$f"; echo "synced $f"; }
done
