#!/bin/sh
# detached: the clean stair runs (before_st2, after_st2) once memory frees up (after 08:30 UTC). Progress in orch6.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
until [ "$(date -u +%H%M)" -ge 0830 ]; do sleep 60; done
echo "go $(date +%T)"
for t in before_st2 after_st2; do
  for i in 1 2 3; do
    sh $T/run_st.sh $t
    grep -q "locostairs trips\|no stair found" $T/log_$t.txt 2>/dev/null && break
    echo "run $t incomplete, retrying"
  done
  rm -f $T/shots_$t/*.bmp
  echo "ran $t $(date +%T)"
done
for t in before_st2 after_st2; do
  echo "== $t"
  grep "locostairs" $T/log_$t.txt | grep -v "trace" | tail -8
done
echo "orch6 done $(date +%T)"
