#!/bin/sh
# After the snapshot chain (cca50c8 + tour stop 8) ends: verify and commit snapshot 3 (snap3_tree.txt)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while pgrep -f "snap_chain.sh" >/dev/null; do sleep 30; done
T3=$(cat $SP/snap3_tree.txt)
echo "snapshot 3: $T3"
sh $SP/snap_smoke.sh $T3 $SP/snap_msg3.txt > $SP/snap_smoke3.log 2>&1
echo "snap3 exit $?"; tail -4 $SP/snap_smoke3.log
mkdir -p $SP/smoke3_png && for f in /tmp/snap_smoke/*.bmp; do [ -f "$f" ] && convert "$f" $SP/smoke3_png/$(basename "${f%.bmp}").png; done
