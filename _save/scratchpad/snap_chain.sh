#!/bin/sh
# After snapshot 970af15 lands: keep its verified -O2 D3D11 exe as the D3D12 port's baseline, then verify and commit
# the next tree (snap2_tree.txt), then tour stop 8 (umbrellas in the downtown rain at night) on the newest exe.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while pgrep -f "snap_smoke.sh 970af15" >/dev/null; do sleep 20; done
if [ "$(cat $SP/snap_tree_ok 2>/dev/null)" = "970af15df150e447ed35191478cf48d24541d784" ]; then
  cp $SP/snap_new.exe $SP/snap_970af15.exe && echo "baseline exe saved: snap_970af15.exe"
else
  echo "970af15 snapshot did not pass: no baseline exe"
fi
mkdir -p $SP/smoke1_shots && cp /tmp/snap_smoke/*.bmp $SP/smoke1_shots/ 2>/dev/null
T2=$(cat $SP/snap2_tree.txt)
echo "snapshot 2: $T2"
sh $SP/snap_smoke.sh $T2 $SP/snap_msg2.txt > $SP/snap_smoke2.log 2>&1
echo "snap2 exit $?"; tail -4 $SP/snap_smoke2.log
mkdir -p $SP/smoke2_shots && cp /tmp/snap_smoke/*.bmp $SP/smoke2_shots/ 2>/dev/null
cd /home/user/GTA-6-Claude-v0.5
LOGF=/tmp/wine_fp2/drive_c/users/root/AppData/Local/NeonTide/log.txt
D=/tmp/v8_stop8; rm -rf $D; mkdir -p $D
sh $SP/memgate.sh 2400 >/dev/null 2>&1
EXE=$SP/snap_new.exe WINEPREFIX=/tmp/wine_fp2 TIMEOUT=4000 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart 8 --tourcount 1 --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\v8_stop8\\" > $D/run.txt 2>&1
echo "stop8 exit $?"
cp $LOGF $D/log.txt 2>/dev/null
grep -E "tour shot|Unhandled" $D/log.txt | tail -2 | cut -c1-200
