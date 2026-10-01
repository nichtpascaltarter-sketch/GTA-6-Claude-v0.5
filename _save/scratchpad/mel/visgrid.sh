#!/bin/bash
# visgrid.sh seed height view out [yaw]
cd /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
S=$1; Z=$2; V=$3; O=$4; Y=${5:-0}
for v in $(seq 0 14); do PREVIEW_VISEME=$v ./preview mel/v$v.ppm --view $V --w 300 --h 300 --seed $S --dist ${D:-0.22} --height $Z --yaw $Y >/dev/null 2>&1; convert mel/v$v.ppm -gravity South -fill white -pointsize 20 -annotate +0+5 "$v" mel/v$v.png; rm mel/v$v.ppm; done
convert mel/v0.png mel/v1.png mel/v2.png mel/v3.png mel/v4.png +append mel/vr1.png; convert mel/v5.png mel/v6.png mel/v7.png mel/v8.png mel/v9.png +append mel/vr2.png; convert mel/v10.png mel/v11.png mel/v12.png mel/v13.png mel/v14.png +append mel/vr3.png; convert mel/vr1.png mel/vr2.png mel/vr3.png -append mel/$O.png
