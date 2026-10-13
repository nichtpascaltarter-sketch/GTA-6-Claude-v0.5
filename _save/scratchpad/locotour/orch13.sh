#!/bin/sh
# detached: change 1 without the player's pairs (sep_vel2): push2 = push0 + it (locopush + melee: should match push0),
# sepclean3 = 61a3b7b + it (the clean exe for the AI agent). Log orch13.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh
echo "start $(date +%T)"
for t in push2 sepclean3; do
  sh $G sh $T/check.sh $T/$t 2>&1 | tail -8
  [ -f $T/$t/bin/loco_$t.exe ] || sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date +%T)"
done
sh $T/run_ap.sh push2 locopush push2_push
grep "locopush" $T/log_push2_push.txt | tail -7
sh $T/run_ap.sh push2 melee push2_melee --autoduration 20
grep "autoplay melee" $T/log_push2_melee.txt | tail -12
rm -f $T/push2/bin/loco_push2.exe
echo "orch13 done $(date +%T)"
