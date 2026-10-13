#!/bin/sh
# detached: the variant's melee smoke again, alone (the first one shared the Wine log with st5's run and lost its first
# 9 s): rebuild push2, wait for orch11's game to end, run melee. Log orch14.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/push2/bin/loco_push2.exe ] || sh $T/build.sh push2
[ -f $T/push2/bin/loco_push2.exe ] || { echo "no exe for push2"; exit 1; }
echo "built push2 $(date +%T)"
until grep -q "orch11 done\|no exe" $T/orch11.log 2>/dev/null; do sleep 30; done
sh $T/run_ap.sh push2 melee push2_melee --autoduration 20
grep "autoplay melee" $T/log_push2_melee.txt | tail -20
rm -f $T/push2/bin/loco_push2.exe
echo "orch14 done $(date +%T)"
