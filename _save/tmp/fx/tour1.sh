#!/bin/sh
# First tour stop (Calle Luna 9.5 h) with gameplay: reproduce the white-hot awning strips
cd /home/user/GTA-6-Claude-v0.5
EXE=${EXE:-bin/nt_fx6.exe}
P=${P:-t1}
rm -rf /tmp/fx/$P; mkdir -p /tmp/fx/$P
WINEPREFIX=/tmp/wine_fx EXE=$EXE TIMEOUT=2400 nice -n 10 tools/run.sh --width 640 --height 360 --autoplay tour --renderevery 3 $EXTRA --shotdir "Z:\\tmp\\fx\\$P\\" > /tmp/fx/run_$P.log 2>&1 &
sleep 20
while [ -z "$(ls /tmp/fx/$P/ 2>/dev/null)" ]; do
  sleep 5
  ps aux | grep -q "[a]utoplay tour" || break
done
sleep 4
for p in $(ps aux | grep "[a]utoplay tour" | awk '{print $2}'); do kill $p 2>/dev/null; done
sleep 2
for f in /tmp/fx/$P/*.bmp; do [ -f "$f" ] && convert "$f" "/tmp/fx/${P}_$(basename $f .bmp).png"; done
ls /tmp/fx/${P}_*.png
