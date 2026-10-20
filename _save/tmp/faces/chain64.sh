#!/bin/sh
# after chain63 (perf pair 2): ear shots (30: 1 m profiles and 60 degrees by day and night, 0.5 m profiles by day)
# with the batch 10 exe (ears are as on main)
while kill -0 $(cat /tmp/faces/chain63.pid 2>/dev/null) 2>/dev/null; do sleep 20; done
echo "start $(date +%T)"
EXE=/tmp/faces/nt_b10_o2.exe sh /tmp/faces/shoot2.sh ear_b10 $(cat /tmp/faces/ears2.args)
echo "ear_b10 done $(date +%T)"
rm -f /tmp/faces/chain64.pid
