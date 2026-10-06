#!/bin/sh
# Second integration of the character shading (feature shadows, eyes and teeth SM_EYE, --shaderdir): copies from
# /tmp/fx/cs2, refusing any file that changed in main since my first integration (or since 2de35c5 for gfx_shaders.cpp).
# DRY=1 only checks.
cd /home/user/GTA-6-Claude-v0.5
bad=0
chk() { [ "$(git hash-object $1 | cut -c1-8)" = "$2" ] || { echo "CHANGED IN MAIN: $1"; bad=1; }; }
chk src/shaders/lighting.hlsl 4b7758f4
chk src/shaders/dynamic.hlsl 1a8e642d
git diff --quiet 2de35c5 -- src/gfx/gfx_shaders.cpp || { echo "CHANGED IN MAIN: src/gfx/gfx_shaders.cpp"; bad=1; }
[ $bad = 1 ] && exit 1
[ -n "$DRY" ] && { echo "dry run: all clean"; exit 0; }
for f in src/shaders/lighting.hlsl src/shaders/dynamic.hlsl src/gfx/gfx_shaders.cpp; do cp /tmp/fx/cs2/$f $f; done
echo "copied"; for f in src/shaders/lighting.hlsl src/shaders/dynamic.hlsl src/gfx/gfx_shaders.cpp; do echo "$(git hash-object -w $f | cut -c1-8) $f"; done
