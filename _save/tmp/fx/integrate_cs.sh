#!/bin/sh
# Copies the verified character shading from /tmp/fx/cs2 into the main tree, refusing any file that changed in main
# since 2de35c5 (the private tree's base). DRY=1 only checks.
cd /home/user/GTA-6-Claude-v0.5
FILES="src/render/renderer.cpp src/render/renderer.h src/shaders/common.hlsli src/shaders/decals.hlsl src/shaders/dynamic.hlsl src/shaders/lighting.hlsl"
bad=0
for f in $FILES; do
  git diff --quiet 2de35c5 -- $f || { echo "CHANGED IN MAIN: $f"; bad=1; }
done
[ -e src/render/skin.cpp ] && { echo "EXISTS IN MAIN: src/render/skin.cpp"; bad=1; }
[ $bad = 1 ] && exit 1
[ -n "$DRY" ] && { echo "dry run: all clean"; exit 0; }
for f in $FILES src/render/skin.cpp; do cp /tmp/fx/cs2/$f $f; done
echo "copied"; for f in $FILES src/render/skin.cpp; do echo "$(git hash-object -w $f | cut -c1-8) $f"; done
