#!/bin/bash
# AI agent (set 20, item 4): after 61b's highway runs, 61m (steering lock matched to the sim) and 61y (= s20, split metric)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
T=$SP/ts
while [ ! -f $T/hwy20_61b.done ]; do sleep 20; done
for v in "$@"; do bash $SP/aiwork/ts_hwy20.sh $v; done
