#!/bin/sh
# Scorecard 4: the 16-stop district tour on the Direct3D 12 build (snapshot 11's exe), on slot 4 (NT_D3D12_SLOT: free
# once the D3D12 port's leftover suite has ended; run.sh waits for the slot and for 3 GB under the memory cap).
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
rm -rf /tmp/tour4b; mkdir -p /tmp/tour4b
rm -f /tmp/wine_tour/drive_c/users/root/AppData/Local/NeonTide/log.txt
NT_D3D12_SLOT=1 EXE=$SP/nt_tour4b.exe WINEPREFIX=/tmp/wine_tour TIMEOUT=10800 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --quality 1 --renderevery 6 --shotdir 'Z:\tmp\tour4b\' > /tmp/tour4b/run.txt 2>&1
echo "tour4 exit $?"
cp /tmp/wine_tour/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/tour4b/log.txt 2>/dev/null
grep -E "tour (shot|done)|Unhandled|FATAL" /tmp/tour4b/log.txt | tail -24
