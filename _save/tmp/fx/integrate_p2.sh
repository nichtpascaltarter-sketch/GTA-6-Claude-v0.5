#!/bin/sh
# Phase 2 (GPU-placed decor plants) into the main tree from /tmp/fx/gd2, refusing any file that changed in main since
# 06580c3. DRY=1 only checks.
cd /home/user/GTA-6-Claude-v0.5
FILES="src/render/props_render.cpp src/render/renderer.cpp src/shaders/propcull.hlsl src/shaders/overhead.hlsl src/shaders/grass.hlsl PROGRESS.md"
NEW="src/render/vegdecor.cpp"
bad=0
for f in $FILES; do git diff --quiet 06580c3 -- $f || { echo "CHANGED IN MAIN: $f"; bad=1; }; done
for f in $NEW; do [ -e $f ] && { echo "EXISTS IN MAIN: $f"; bad=1; }; done
[ $bad = 1 ] && exit 1
[ -n "$DRY" ] && { echo "dry run: all clean"; exit 0; }
for f in $FILES $NEW; do cp /tmp/fx/gd2/$f $f; done
echo copied; for f in $FILES $NEW; do echo "$(git hash-object -w $f) $f"; done
