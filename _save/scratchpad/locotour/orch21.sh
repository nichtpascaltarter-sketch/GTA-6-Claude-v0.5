#!/bin/sh
# detached: rebuild st7b (trace now with stairSeen / rootRatio); the game runs wait for main's go (not launched here)
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
sh $T/build.sh st7b
[ -f $T/st7b/bin/loco_st7b.exe ] && echo "built st7b $(date +%T)" || echo "no exe for st7b"
