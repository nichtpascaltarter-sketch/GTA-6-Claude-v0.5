#!/bin/sh
# Copies the verified bindless work from /tmp/fx/bl into the main tree, refusing any file that changed in main since
# the private base (bl commit 0d35257). DRY=1 only checks.
cd /home/user/GTA-6-Claude-v0.5
FILES="src/gfx/gfx.h src/gfx/gfx.cpp src/gfx/gfx_context.cpp src/gfx/gfx_tools.cpp src/render/dynamic.cpp src/render/props_render.cpp
src/render/renderer.cpp src/render/renderer.h src/render/weather.cpp src/render/world_render.cpp src/shaders/bindless.hlsli
src/shaders/common.hlsli src/shaders/dynamic.hlsl src/shaders/facade.hlsli src/shaders/gfxtest.hlsl src/shaders/overhead.hlsl
src/shaders/props.hlsl src/shaders/world.hlsl src/world/transitmesh.cpp PROGRESS.md"
bad=0
for f in $FILES; do
  a=$(md5sum < $f | cut -c1-32); b=$(git -C /tmp/fx/bl show 0d35257:$f | md5sum | cut -c1-32)
  if [ "$a" != "$b" ]; then echo "CHANGED IN MAIN: $f"; bad=1; fi
done
[ -e src/shaders/materials.hlsli ] && { echo "EXISTS IN MAIN: src/shaders/materials.hlsli"; bad=1; }
[ $bad = 1 ] && exit 1
[ -n "$DRY" ] && { echo "dry run: all clean"; exit 0; }
for f in $FILES src/shaders/materials.hlsli; do cp /tmp/fx/bl/$f $f; done
echo "copied"; md5sum $FILES src/shaders/materials.hlsli | awk '{print substr($1,1,8), $2}'
