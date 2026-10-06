#!/bin/bash
# cam.sh idx out.png ex ey ez tx ty tz fov [extra]
cd /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
i=$1; o=$2; shift 2
./preview $i out --cam $1 $2 $3 $4 $5 $6 $7 ${@:8} >/dev/null
f=$(printf "out/m%02dx.ppm" $i)
convert $f -resize 840x360 out/$o
