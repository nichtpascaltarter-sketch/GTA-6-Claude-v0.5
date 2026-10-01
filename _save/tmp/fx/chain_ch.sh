#!/bin/sh
# character close-ups: shader check, before (nt_fx8), then after (nt_fx9)
while ! grep -q "e2 done\|failed" /tmp/fx/chain_e.log 2>/dev/null; do sleep 10; done
sh /tmp/fx/shc/run.sh > /tmp/fx/shc_ch.txt 2>&1
tail -3 /tmp/fx/shc_ch.txt
SH="-300,1519.3,6.15,0,-3,10.5,c0_morning -298.4,1519.3,6.1,0,-3,10.5,c1_morning -300,1519.3,6.15,0,-3,17.8,c0_evening"
W=640 H=360 EXE=bin/nt_fx8.exe SHOTS="$SH" TIMEOUT=1700 /tmp/fx/shoot.sh ch0 --viewer characters --count 3 --clip 0 --settle 12 --quality 2 --clouds 0.2 > /tmp/fx/ch0.out 2>&1
echo "ch0 done"
while ! grep -q "build exit" /tmp/fx/build_fx9.log 2>/dev/null; do sleep 10; done
grep -q "build exit 0" /tmp/fx/build_fx9.log || { echo "nt_fx9 build failed"; exit 1; }
grep -q " 0 failed" /tmp/fx/shc_ch.txt || { echo "shader check failed"; exit 1; }
W=640 H=360 EXE=bin/nt_fx9.exe SHOTS="$SH" TIMEOUT=1700 /tmp/fx/shoot.sh ch1 --viewer characters --count 3 --clip 0 --settle 12 --quality 2 --clouds 0.2 > /tmp/fx/ch1.out 2>&1
echo "ch1 done"
