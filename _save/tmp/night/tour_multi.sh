#!/bin/sh
# Tour stops START..START+COUNT-1 with one of my builds on Wine slot 4: EXE, P (tag), START, COUNT, EXTRA.
# Shots: /tmp/fx/P_auto_tour_NN_name.png; log: /tmp/fx/log_P.txt; done marker /tmp/fx/P.done
export NT_D3D12_SLOT=1
cd /home/user/GTA-6-Claude-v0.5
rm -rf /tmp/fx/$P; mkdir -p /tmp/fx/$P
WINEPREFIX=/tmp/wine_fx EXE=$EXE TIMEOUT=${TIMEOUT:-5400} nice -n 10 tools/run.sh --width ${W:-960} --height ${H:-540} --play --autoplay tour --tourstart $START --tourcount $COUNT --renderevery 6 --quality 1 $EXTRA --shotdir "Z:\\tmp\\fx\\$P\\" > /tmp/fx/run_$P.log 2>&1
echo "exit $?" >> /tmp/fx/run_$P.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_$P.txt
for f in /tmp/fx/$P/*.bmp; do [ -f "$f" ] && convert "$f" "/tmp/fx/${P}_$(basename $f .bmp).png" && rm -f "$f"; done
echo done > /tmp/fx/$P.done
