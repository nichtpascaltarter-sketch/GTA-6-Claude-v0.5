#!/bin/sh
# Resume a snapshot whose syntax check, build, shader check and drive smoke passed, after the melee smoke was killed by
# the memory cgroup's OOM killer (kernel log evidence, not a game failure): re-run only the melee smoke with the same
# exe ($SP/snap_new.exe, checked against the tree beforehand), then commit and push exactly as snap_smoke.sh does.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
TREE=$1; MSGF=$2
S=/tmp/snap_smoke; mkdir -p $S
cd $REPO
rm -f /tmp/wine_lead/drive_c/users/root/AppData/Local/NeonTide/log.txt
$SP/memgate.sh 3200
NT_LEAD_SLOT=1 EXE=$SP/snap_new.exe WINEPREFIX=/tmp/wine_lead TIMEOUT=1500 nice -n 6 tools/run.sh --width 640 --height 360 --play --autoplay melee --meleeweapon bat --autoduration 18 --autoevery 6 --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\snap_smoke\\" > $S/run_melee.txt 2>&1
rc=$?
cp /tmp/wine_lead/drive_c/users/root/AppData/Local/NeonTide/log.txt $S/log_melee.txt 2>/dev/null
if [ $rc -ne 0 ] || [ ! -s $S/log_melee.txt ] || grep -qE "Unhandled|FATAL" $S/log_melee.txt; then
  echo "MELEE SMOKE FAILED rc=$rc $TREE"; grep -E "Unhandled|autoplay t=" $S/log_melee.txt | tail -5; exit 1
fi
grep -E "autoplay t=" $S/log_melee.txt | tail -1 | cut -c1-160
echo "$TREE" > $SP/snap_tree_ok
cp $SP/snap_new.exe $REPO/bin/nt_snap.exe
cd $REPO && sh $SP/commit_snap.sh "$(cat $MSGF)" || { echo "COMMIT FAILED"; exit 1; }
for d in 0 2 4 8 16; do
  sleep $d
  if git push -u origin claude/neon-tide > $SP/snap_push.log 2>&1; then tail -1 $SP/snap_push.log; echo "COMMITTED $TREE"; exit 0; fi
  grep -qE "403|407|denied|rejected" $SP/snap_push.log && break
done
echo "PUSH FAILED (committed locally)"; tail -3 $SP/snap_push.log
