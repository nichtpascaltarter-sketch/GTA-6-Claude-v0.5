#!/bin/sh
# detached: the clasp before / after - clasp0 (HEAD c1af87a + landing predictions + a close-up camera on the joined hands)
# and clasp1 (+ the crossed clasp: animator hold branch, peds.cpp point 5 cm past the middle); QUICK builds, then one
# --autoplay couple run each (one game at a time), exes deleted after. Log orch17.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
for t in clasp1 clasp0; do
  [ -f $T/$t/bin/$t.exe ] || sh $T/bld_cl.sh $t
  [ -f $T/$t/bin/$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date +%T)"
done
for t in clasp1 clasp0; do
  sh $T/run_cl.sh $t $t
  grep "autoplay couple" $T/log_$t.txt | tail -4
done
rm -f $T/clasp0/bin/clasp0.exe $T/clasp1/bin/clasp1.exe
echo "orch17 done $(date +%T)"
