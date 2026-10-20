#!/bin/sh
# relaunch.sh (after a container restart): set aside the logs of runs that were killed, clear stale pid / hold / build
# marker files and start the chains again - bld47 (loco_st21i.exe, v3i), bld48 (loco_st19j / st20j.exe, v3j) and orch49
# (the runs in jobs49.txt). Each skips what is done / built. (Done before 11:47: st19pl st19apl st19p st19ap st20p st20pl
# st19ch st19ach st19chg st19achg st19ng st20ng st21ng st19nh st20nh; st20sp / st21sp ran, their spiral walkers never got
# on - v3g - superseded by st20spi / st21spi.)
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
cd $T
k=$(date -u +%H%M)
grep -v '^#' jobs49.txt | while IFS='|' read -r J TR EX MK BN AP EV DG; do
  [ -n "$J" ] || continue
  if [ -f log_$J.txt ] && ! grep -q "$DG" log_$J.txt; then mv log_$J.txt log_$J.killed$k.txt; [ -f run_$J.txt ] && mv run_$J.txt run_$J.killed$k.txt; echo "set aside $J"; fi
done
for p in orch49 bld47 bld48 bld49; do
  if [ -f $p.pid ] && ! kill -0 "$(cat $p.pid)" 2>/dev/null; then mv $p.pid $p.pid.stale$k; echo "stale $p.pid set aside"; fi
done
[ -f hold ] && mv hold hold.stale$k
sleep 2
for f in "$T"/*.building; do [ -e "$f" ] && mv "$f" "$f.stale$k"; done
[ -f bld47.pid ] || grep -q "bld47 done" bld47*.log 2>/dev/null || { nohup sh $T/bld47.sh > $T/bld47_$k.log 2>&1 & echo "bld47 -> bld47_$k.log"; }
sleep 2
[ -f bld48.pid ] || grep -q "bld48 done" bld48*.log 2>/dev/null || { nohup sh $T/bld48.sh > $T/bld48_$k.log 2>&1 & echo "bld48 -> bld48_$k.log"; }
sleep 2
[ -f bld49.pid ] || grep -q "bld49 done" bld49*.log 2>/dev/null || { nohup sh $T/bld49.sh > $T/bld49_$k.log 2>&1 & echo "bld49 -> bld49_$k.log"; }
sleep 20
[ -f orch49.pid ] || grep -q "orch49 done" orch49*.log 2>/dev/null || { nohup sh $T/orch49.sh > $T/orch49_$k.log 2>&1 & echo "orch49 -> orch49_$k.log"; }
