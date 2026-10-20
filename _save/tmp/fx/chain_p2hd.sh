#!/bin/sh
# phase 2 sharp proof: tour stops 11-13 at 1920x1080 with phase 1 (gd1) and with the decor plants (gd2)
cd /tmp/fx
until [ -f /tmp/fx/chain_p2test.done ]; do sleep 15; done
W=1920 H=1080 EXE=bin/nt_gd2.exe P=gd2h START=11 COUNT=3 sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_gd1.exe P=gd1h START=11 COUNT=3 sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_p2hd.done
