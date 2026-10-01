#!/bin/sh
# phase 2 test: tour stops 4-13 with the decor plants (gd2), after the phase-1 self-test
cd /tmp/fx
until [ -f /tmp/fx/chain_p1final.done ]; do sleep 15; done
EXE=bin/nt_gd2.exe P=gd2a START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_p2test.done
