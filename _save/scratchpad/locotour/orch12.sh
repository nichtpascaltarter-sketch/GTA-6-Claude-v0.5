#!/bin/sh
# detached: the clean exe for the AI agent's crowd tests, rebuilt on the new main: sepclean2 = 61a3b7b + change 1
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh
echo "start $(date +%T)"
sh $G sh $T/check.sh $T/sepclean2 2>&1 | tail -8
[ -f $T/sepclean2/bin/loco_sepclean2.exe ] || sh $T/build.sh sepclean2
[ -f $T/sepclean2/bin/loco_sepclean2.exe ] || { echo "no exe for sepclean2"; exit 1; }
echo "built sepclean2 $(date +%T)"
echo "orch12 done $(date +%T)"
