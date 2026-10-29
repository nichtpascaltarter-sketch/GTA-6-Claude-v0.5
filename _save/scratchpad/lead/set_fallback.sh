#!/bin/sh
# PC first, cloud fallback, for one showcase set. usage: set_fallback.sh <pkg id> <set N> <start-deadline-epoch> <tag>
# Waits on the PC package; if the PC hasn't started it by the deadline, takes it back and renders the set in the cloud,
# after any running snapshot chain (never alongside its smoke). A chain launched meanwhile waits for showcase.sh itself.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
id=$1; n=$2; dl=$3; tag=$4
sh $SP/lead/pcwait.sh $id $dl; rc=$?
echo "pcwait rc=$rc"
[ $rc = 3 ] || exit $rc
sh $SP/pc/queue_pkg.sh cancel $id
# (a chain is lead/sNN/launch.sh -> chain.sh, which execs snap_smoke.sh: look for all three)
while ps -eo args | grep -v grep | grep -qE "lead/s[0-9]+/(chain|launch)\.sh|snap_smoke\.sh"; do sleep 30; done
echo "cloud showcase set $n start $(date -u +%T)"
cd /home/user/GTA-6-Claude-v0.5 && sh $SP/showcase.sh $n > $SP/lead/showcase_set${n}_$tag.log 2>&1; echo "showcase exit $?"
tail -3 $SP/lead/showcase_set${n}_$tag.log
