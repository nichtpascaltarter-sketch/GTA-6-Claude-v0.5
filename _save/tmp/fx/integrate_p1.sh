#!/bin/sh
# Phase 1 (GPU-driven props) into the main tree from /tmp/fx/gd, refusing any file that changed in main since 4b6357f.
# DRY=1 only checks.
cd /home/user/GTA-6-Claude-v0.5
FILES="src/render/props_render.cpp src/render/renderer.cpp src/render/shadows.cpp src/gfx/gfx_tools.cpp src/shaders/gfxtest.hlsl PROGRESS.md"
bad=0
for f in $FILES; do git diff --quiet 4b6357f -- $f || { echo "CHANGED IN MAIN: $f"; bad=1; }; done
[ -e src/shaders/propcull.hlsl ] && { echo "EXISTS IN MAIN: src/shaders/propcull.hlsl"; bad=1; }
[ $bad = 1 ] && exit 1
[ -n "$DRY" ] && { echo "dry run: all clean"; exit 0; }
for f in $FILES src/shaders/propcull.hlsl; do cp /tmp/fx/gd/$f $f; done
echo copied; for f in $FILES src/shaders/propcull.hlsl; do echo "$(git hash-object -w $f | cut -c1-8) $f"; done
