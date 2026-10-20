#!/bin/sh
# faces in context: the greet autoplay (airport curb + downtown sidewalk, side views at chest height) with the batch 7 build
cd /tmp/faces
while [ ! -f /tmp/faces/chain43.done ]; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/faces/greet_b7
WINEPREFIX=/tmp/wine_faces EXE=/tmp/faces/nt_b7_o2.exe TIMEOUT=1800 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay greet --shotdir 'Z:\tmp\faces\greet_b7\' > /tmp/faces/run_greet_b7.log 2>&1
echo "exit $?" >> /tmp/faces/run_greet_b7.log
cp /tmp/wine_faces/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/faces/log_greet_b7.txt 2>/dev/null
for f in /tmp/faces/greet_b7/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
echo done > /tmp/faces/chain44.done
