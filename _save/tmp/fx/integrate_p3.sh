#!/bin/sh
# Phase 3 (palms, leaf canopy, storefront glass) into the main tree from /tmp/fx/gd3, after phase 2 is in: refuses any
# phase 3 file that changed in main since 06580c3, and PROGRESS.md unless it is phase 2's version (gd2). DRY=1 only checks.
cd /home/user/GTA-6-Claude-v0.5
FILES="src/world/propmesh.cpp src/shaders/props.hlsl src/shaders/matgen.hlsl src/render/materials.cpp src/shaders/facade.hlsli"
bad=0
for f in $FILES; do git diff --quiet 06580c3 -- $f || { echo "CHANGED IN MAIN: $f"; bad=1; }; done
[ "$(git hash-object PROGRESS.md)" = "$(git hash-object /tmp/fx/gd2/PROGRESS.md)" ] || { echo "PROGRESS.md is not phase 2's version"; bad=1; }
[ $bad = 1 ] && exit 1
[ -n "$DRY" ] && { echo "dry run: all clean"; exit 0; }
for f in $FILES PROGRESS.md; do cp /tmp/fx/gd3/$f $f; done
echo copied; for f in $FILES PROGRESS.md; do echo "$(git hash-object -w $f) $f"; done
