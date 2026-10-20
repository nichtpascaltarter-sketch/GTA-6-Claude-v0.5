#!/bin/sh
# detached: the fast-turn hover in game. Build st12 (= st11 + patches/turn.py); after orch26 (one game at a time, not
# while $T/hold exists): locotour on st11's copied exe (baseline: HEAD + the P2 fix) -> log_st11t.txt, then locotour on
# st12 -> log_st12t.txt. Log orch27.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st12/bin/loco_st12.exe ] || sh $T/build.sh st12
[ -f $T/st12/bin/loco_st12.exe ] || { echo "no exe for st12"; exit 1; }
echo "built st12 $(date +%T)"
until grep -q 'orch26 done' $T/orch26.log 2>/dev/null; do sleep 30; done
run() {
  while [ -f $T/hold ]; do sleep 30; done
  for i in 1 2; do
    echo "run $4 $(date +%T)"
    sh $T/run_ap2.sh $1 $2 $3 $4
    grep -q "locotour steps all" $T/log_$4.txt 2>/dev/null && break
  done
}
[ -f $T/st11/bin/loco_st11t.exe ] && run st11 loco_st11t.exe locotour st11t
rm -f $T/st11/bin/loco_st11t.exe
run st12 loco_st12.exe locotour st12t
rm -f $T/st12/bin/loco_st12.exe
echo "orch27 done $(date +%T)"
