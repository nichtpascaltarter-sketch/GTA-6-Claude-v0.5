#!/bin/sh
# Start the next showcase once the running snapshot check ends, so it renders the newest verified build and does not
# compete with the snapshot's smoke tests for memory. usage: showcase_after_snap.sh SNAPLOG [set if committed]
# (without a set: the regular rotation either way)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
LOG=$1
until grep -qE "COMMITTED|FAILED|HOLD" $LOG; do sleep 20; done
if [ -n "$2" ] && grep -q COMMITTED $LOG; then echo "snapshot committed: set $2"; exec sh $SP/showcase.sh $2; fi
echo "snapshot ended: next set in the rotation"; exec sh $SP/showcase.sh
