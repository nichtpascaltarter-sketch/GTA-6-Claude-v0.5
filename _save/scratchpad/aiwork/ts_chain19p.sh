#!/bin/bash
# AI agent (set 19): after chain o - the I-38 barrier hits by the Bayshore Boulevard Exit (base and s19 runs alike: cars on
# lane 68 at 26-33 m/s, 0.4-1.0 m left of it, into colliders 3469 / 3472): the colliders and the lanes there
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19p.done
while [ ! -f $T/chain19o.done ]; do sleep 15; done
TS_BIN=$T/traffic_sim $R $T/i38probe.txt --colliders 2481 3676 7 --nearlane 2480.1 3681.0 0 -1 --nearlane 2480.1 3681.0 0 1 --lane 68 55 100 --lanes 2482 3690 60 $T/i38_gore.ppm
echo done > $T/chain19p.done
