#!/bin/sh
# usage: bld.sh <tree> <out>: sl/mk.sh, retried while the memory gate refuses (up to ~1 h)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
for i in 1 2 3 4 5 6; do
  sh $L/sl/mk.sh $1 $2 && exit 0
  echo "retry $i $(date +%T)"
done
exit 1
