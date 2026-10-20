#!/bin/bash
# AI agent: harness runs over several districts in a row (one at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
R=$SP/aiwork/ts_run.sh
$R $SP/ts/sim_beach.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6
$R $SP/ts/sim_luna.txt --sim 2250 100 450 --cars 140 --peds 120 --minutes 6
$R $SP/ts/sim_hwy.txt --sim 2342 1143 600 --cars 160 --peds 40 --minutes 6
$R $SP/ts/sim_mid.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6
echo done > $SP/ts/multi.done
