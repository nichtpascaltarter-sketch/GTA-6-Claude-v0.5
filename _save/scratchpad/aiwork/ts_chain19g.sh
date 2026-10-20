#!/bin/bash
# AI agent (set 19): after chain f - lane/walk maps of the walk links found on carriageways (not the overlap pairs)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
T=$SP/ts
rm -f $T/chain19g.done
while [ ! -f $T/chain19f.done ]; do sleep 15; done
$AW/ts_run.sh $T/walkmaps.txt --lanes 4900 8550 150 $T/wm_ne1.ppm --lanes 4600 7800 150 $T/wm_ne2.ppm --lanes 2984 -2049 70 $T/wm_keylime.ppm --lanes 3584 -1849 70 $T/wm_18th.ppm --lanes 2400 -4400 150 $T/wm_s1.ppm --lanes 3700 -330 90 $T/wm_bay11.ppm
echo done > $T/chain19g.done
