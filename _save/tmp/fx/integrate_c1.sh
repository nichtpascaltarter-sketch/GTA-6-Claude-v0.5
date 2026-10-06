#!/bin/sh
# Character-shading backlog (lamp bounce fill, per-card dither, face-hair feature shadows, beard shell wrap) into the
# main tree from /tmp/fx/gd4, after phase 3 is in: refuses lighting.hlsl / dynamic.hlsl if they changed in main since
# 06580c3, and PROGRESS.md unless it is phase 3's version (gd3). DRY=1 only checks.
cd /home/user/GTA-6-Claude-v0.5
FILES="src/shaders/lighting.hlsl src/shaders/dynamic.hlsl"
bad=0
for f in $FILES; do git diff --quiet 06580c3 -- $f || { echo "CHANGED IN MAIN: $f"; bad=1; }; done
[ "$(git hash-object PROGRESS.md)" = "$(git hash-object /tmp/fx/gd3/PROGRESS.md)" ] || { echo "PROGRESS.md is not phase 3's version"; bad=1; }
[ $bad = 1 ] && exit 1
[ -n "$DRY" ] && { echo "dry run: all clean"; exit 0; }
for f in $FILES PROGRESS.md; do cp /tmp/fx/gd4/$f $f; done
echo copied; for f in $FILES PROGRESS.md; do echo "$(git hash-object -w $f) $f"; done
