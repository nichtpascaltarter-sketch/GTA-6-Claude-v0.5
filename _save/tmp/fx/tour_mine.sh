#!/bin/sh
# Tour stop 0 with one of my builds: EXE, P, EXTRA
cd /home/user/GTA-6-Claude-v0.5
rm -rf /tmp/fx/$P; mkdir -p /tmp/fx/$P
/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh 2600
WINEPREFIX=/tmp/wine_fx EXE=$EXE TIMEOUT=2400 nice -n 10 tools/run.sh --width 960 --height 540 --play --autoplay tour --renderevery 6 --quality 1 $EXTRA --shotdir "Z:\\tmp\\fx\\$P\\" > /tmp/fx/run_$P.log 2>&1 &
sleep 15
/tmp/fx/tour_watch.sh $P
