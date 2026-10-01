#!/bin/sh
# After lead_v4: first-person driving (cockpit view) by day and at night
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -q "bat exit" $SP/lead_v4.log 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
LOGF=/tmp/wine_fp2/drive_c/users/root/AppData/Local/NeonTide/log.txt
D=/tmp/v5_fpdrive; rm -rf $D; mkdir -p $D
sh $SP/memgate.sh 2400 >/dev/null 2>&1
EXE=$SP/snap_431ef7f.exe WINEPREFIX=/tmp/wine_fp2 TIMEOUT=4000 nice -n 6 tools/run.sh --width 960 --height 540 --play --firstperson --autoplay drive --autoduration 40 --autoevery 8 --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\v5_fpdrive\\" > $D/run.txt 2>&1
echo "fpdrive exit $?"
cp $LOGF $D/log.txt 2>/dev/null
grep -E "autoplay t=|Unhandled" $D/log.txt | tail -4 | cut -c1-200
