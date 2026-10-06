#!/bin/sh
# After lead_v3 finishes: first-person knife and bat (snapshot 431ef7f exe)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -q "ui exit" $SP/lead_v3.log 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
LOGF=/tmp/wine_fp2/drive_c/users/root/AppData/Local/NeonTide/log.txt
for w in knife bat; do
  D=/tmp/v4_$w; rm -rf $D; mkdir -p $D
  sh $SP/memgate.sh 2400 >/dev/null 2>&1
  EXE=$SP/snap_431ef7f.exe WINEPREFIX=/tmp/wine_fp2 TIMEOUT=4000 nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay melee --meleeweapon $w --autoduration 9 --autoevery 0.9 --renderevery 3 --quality 1 --shotdir "Z:\\tmp\\v4_$w\\" > $D/run.txt 2>&1
  echo "$w exit $?"
  cp $LOGF $D/log.txt 2>/dev/null
  grep -E "autoplay melee|Unhandled" $D/log.txt | tail -6 | cut -c1-200
done
