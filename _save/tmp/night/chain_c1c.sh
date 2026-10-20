#!/bin/sh
# phase C evaluation at 1280x720 Q2 (the 1080p Q3 runs keep getting OOM-killed on the crowded machine)
. /tmp/night/views.sh
V2="1771.00,357.10,5.47,-65.00,3.00,20.50,calle_luna_street_evening -1584.69,-591.72,8.14,-96.93,-1.88,20.50,mimo_strip_mall_evening"
W=1280 H=720 Q=2 SHOTS="$V2" EXE=bin/nt_hb.exe sh /tmp/night/shoot.sh e2b --exposurelog
W=1280 H=720 Q=2 SHOTS="$V2" EXE=bin/nt_c1.exe sh /tmp/night/shoot.sh e2c --exposurelog
W=1280 H=720 Q=2 sh /tmp/night/tourshot.sh vb bin/nt_hb.exe 7 2
W=1280 H=720 Q=2 sh /tmp/night/tourshot.sh vc bin/nt_c1.exe 7 2
echo done > /tmp/night/chain_c1c.done
