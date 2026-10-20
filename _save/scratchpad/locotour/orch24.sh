#!/bin/sh
# detached: (2) stair stance in game. Build st9 (st8 + the stance patch + LOCO_STOPS option) and st8s (st8 + the option);
# then, one game at a time and not while $T/hold exists: st9 locostairs, st9 stops, st8s stops. Log orch24.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st9/bin/loco_st9.exe ] || sh $T/build.sh st9
[ -f $T/st9/bin/loco_st9.exe ] || { echo "no exe for st9"; exit 1; }
echo "built st9 $(date +%T)"
# (the second build runs while the first game does)
( [ -f $T/st8s/bin/loco_st8s.exe ] || sh $T/build.sh st8s; echo "built st8s $(date +%T)" ) &
run() {
  while [ -f $T/hold ] || [ "$(date +%H%M)" -lt 0545 ]; do sleep 30; done
  for i in 1 2; do
    echo "run $2 $(date +%T)"
    sh $T/run_st2.sh $1 $2 $3
    grep -q "locostairs trips\|no stair found" $T/log_$2.txt 2>/dev/null && break
  done
}
run st9 st9
run st9 st9s stops
rm -f $T/st9/bin/loco_st9.exe
wait
[ -f $T/st8s/bin/loco_st8s.exe ] && run st8s st8s stops
rm -f $T/st8s/bin/loco_st8s.exe
echo "orch24 done $(date +%T)"
