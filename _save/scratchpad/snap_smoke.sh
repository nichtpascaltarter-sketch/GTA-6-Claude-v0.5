#!/bin/sh
# Build one exact tree (argument: tree hash), check every shader, run a short first-person drive under Wine, and only
# then commit it on top of HEAD with the message in $2 (a file) and push. Other agents keep editing the working tree
# meanwhile; only this tree is committed.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
TREE=$1; MSGF=$2
D=/tmp/snapfixed
rm -rf $D && mkdir -p $D
cd $REPO && git archive $TREE | tar -x -C $D
cd $D && mkdir -p build/gen bin
# snapshot builds share the priority build lane with the D3D12 port (one compile at a time there, beside the shared
# lane), so a verified commit does not queue behind every agent's build; build.sh's memory wait still applies
# (SNAP_LANE=shared: queue in the shared lane instead, leaving the priority lane to the D3D12 port's own builds)
if [ "${SNAP_LANE:-priority}" != shared ]; then
  touch /tmp/neontide_build_d3d12.lock
  sed -i 's#/tmp/neontide_build.lock#/tmp/neontide_build_d3d12.lock#g' build.sh
fi
# a quick syntax check of the whole unity build first: a half-finished edit caught in the capture fails here in
# about a minute instead of after a full build under the shared build lock
g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
if ! nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wno-unused-function -Wno-unused-variable \
    -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen -Isrc src/main.cpp > $SP/snap_syn.log 2>&1; then
  echo "SYNTAX FAILED $TREE"; grep -m5 -E "error" $SP/snap_syn.log; exit 1
fi
$SP/memgate.sh 3200
if ! NT_LEAD_SLOT=1 OUT=bin/snap.exe nice -n 5 ./build.sh > $SP/snap_fixed.log 2>&1; then
  echo "BUILD FAILED $TREE"; grep -m5 -E "error|undefined" $SP/snap_fixed.log; exit 1
fi
$SP/memgate.sh 3200
# (a Direct3D 12 tree compiles its shaders as *_5_1 with unbounded descriptor tables: the D3D12 port's checker)
if grep -qs "ID3D12" $D/src/gfx/*.cpp $D/src/gfx/*.h; then SHC="sh $SP/shc_d3d12/shc_d3d12.sh $D /tmp/wine_lead"
else SHC="sh $SP/shc_tree.sh $D"; fi
if ! $SHC > $SP/snap_shc.log 2>&1; then
  echo "SHADERS FAILED $TREE"; cat $SP/snap_shc.log; exit 1
fi
tail -1 $SP/snap_shc.log
cp bin/snap.exe $SP/snap_new.exe
# smoke: a new game driven in the first-person view (also proves the camera preferences survive the new game)
S=/tmp/snap_smoke; rm -rf $S; mkdir -p $S
rm -f /tmp/wine_lead/drive_c/users/root/AppData/Local/NeonTide/log.txt   # never judge a run by the previous log
cd $REPO
$SP/memgate.sh 3200
NT_LEAD_SLOT=1 EXE=$SP/snap_new.exe WINEPREFIX=/tmp/wine_lead TIMEOUT=3000 nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\snap_smoke\\" > $S/run.txt 2>&1
rc=$?
cp /tmp/wine_lead/drive_c/users/root/AppData/Local/NeonTide/log.txt $S/log.txt 2>/dev/null
if [ $rc -ne 0 ] || [ ! -s $S/log.txt ] || grep -qE "Unhandled|FATAL" $S/log.txt; then
  echo "SMOKE FAILED rc=$rc $TREE"; grep -E "Unhandled|autoplay t=" $S/log.txt | tail -5; exit 1
fi
grep -E "autoplay t=" $S/log.txt | tail -2 | cut -c1-160
# and a short fight with a bat (hits, flinches, staggers, braced knock-downs, the ragdoll and the get-up): combat paths
# the drive never touches
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
  grep -qE "403|407|denied|rejected" $SP/snap_push.log && break   # policy or history refusals are not retried
done
echo "PUSH FAILED (committed locally)"; tail -3 $SP/snap_push.log
