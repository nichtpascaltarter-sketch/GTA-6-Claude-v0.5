#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
n=0; while [ "$(sh tools/memfree.sh)" -lt 2000 ] && [ $n -lt 240 ]; do sleep 15; n=$((n+1)); done
echo "memfree $(sh tools/memfree.sh) at $(date +%T)"
cd /tmp/faces/bz && nice -n 5 g++ -O1 -std=c++17 -w -I src -I . /tmp/faces/tools/hairlist.cpp -o /tmp/faces/hairlist 2>&1 | tail -5
/tmp/faces/hairlist 80 > /tmp/faces/hairlist.txt
echo "done $(date +%T)"
