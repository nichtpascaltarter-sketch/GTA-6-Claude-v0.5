#!/bin/sh
# Finish a snapshot whose exe is already built and shader-checked ($SP/snap_new.exe): the first-person smoke drive in
# the lead's Wine slot, then commit (commit_snap.sh, with its merged-main guard) and push. Args: tree hash, message file.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
TREE=$1; MSGF=$2
LOGF=/tmp/wine_lead/drive_c/users/root/AppData/Local/NeonTide/log.txt
S=/tmp/snap_smoke; rm -rf $S; mkdir -p $S
rm -f $LOGF   # never judge a run by the previous run's log
cd $REPO
$SP/memgate.sh 2400
NT_LEAD_SLOT=1 EXE=$SP/snap_new.exe WINEPREFIX=/tmp/wine_lead TIMEOUT=3000 nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\snap_smoke\\" > $S/run.txt 2>&1
rc=$?
cp $LOGF $S/log.txt 2>/dev/null
if [ $rc -ne 0 ] || [ ! -s $S/log.txt ] || grep -q "Unhandled" $S/log.txt; then
  echo "SMOKE FAILED rc=$rc $TREE"; grep -E "Unhandled|autoplay t=" $S/log.txt | tail -5; exit 1
fi
grep -E "autoplay t=" $S/log.txt | tail -2 | cut -c1-160
echo "$TREE" > $SP/snap_tree_ok
cp $SP/snap_new.exe $REPO/bin/nt_snap.exe
cd $REPO && sh $SP/commit_snap.sh "$(cat $MSGF)" || { echo "COMMIT FAILED"; exit 1; }
for d in 0 2 4 8 16; do
  sleep $d
  if git push -u origin claude/neon-tide > $SP/snap_push.log 2>&1; then tail -1 $SP/snap_push.log; echo "COMMITTED $TREE"; exit 0; fi
  grep -qE "403|407|denied|rejected" $SP/snap_push.log && break
done
echo "PUSH FAILED (committed locally)"; tail -3 $SP/snap_push.log
