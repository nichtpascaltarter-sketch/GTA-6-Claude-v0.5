#!/bin/sh
# Phase C sign-off runs, one game per view (OOM-robust): nc (nt_cF) missing views, nm (nt_m59), dc/dmm day, tours
. /tmp/night/views.sh
NC3="-1283.07,-946.51,54.28,20.00,-45.00,21.50,westbrook_pools_night 1771.00,357.10,5.47,-65.00,3.00,20.50,calle_luna_street_evening -1584.69,-591.72,8.14,-96.93,-1.88,20.50,mimo_strip_mall_evening"
rm -f /tmp/night/shots/nc.done
sh /tmp/night/shoot1.sh nc bin/nt_cF.exe "$NC3" --exposurelog
sh /tmp/night/shoot1.sh nm bin/nt_m59.exe "$NIGHT" --exposurelog
sh /tmp/night/shoot1.sh dc bin/nt_cF.exe "$DAY"
sh /tmp/night/shoot1.sh dmm bin/nt_m59.exe "$DAY"
sh /tmp/night/tour1.sh tm59 bin/nt_m59.exe 4 5
sh /tmp/night/tour1.sh tcF bin/nt_cF.exe 4 5
echo done > /tmp/night/chain_signC2.done
