#!/bin/sh
# Final character shots with bin/char6.exe: protagonists at LOD0, then the same lineup at LOD2 (one Wine run at a time).
C=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/c4
cd /home/user/GTA-6-Claude-v0.5
mkdir -p $C/final
SD='Z:\tmp\claude-0\-home-user-GTA-6-Claude-v0-5\cc7ce613-fe8a-5fa0-8160-c9078e8be173\scratchpad\c4\final\'
sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh 2400
EXE=bin/char6.exe WINEPREFIX=/tmp/wine_char TIMEOUT=2400 nice -n 6 tools/run.sh --viewer characters --protagonists --count 6 --clip 0 --width 960 --height 540 --settle 12 --shotdir "$SD" \
 --shot "-298.4,1516.0,5.55,0,-3,11,lod0_lineup" \
 --shot "-299.9,1522.0,5.7,180,-6,11,mari_back" \
 --shot "-297.2,1518.3,5.7,35,-6,11,dex_34" \
 --shot "-300,1518.6,5.85,0,-8,11,mari_close" > $C/final/run_lod0.log 2>&1
cp /tmp/wine_char/drive_c/users/root/AppData/Local/NeonTide/log.txt $C/final/log_lod0.txt
sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh 2400
EXE=bin/char6.exe WINEPREFIX=/tmp/wine_char TIMEOUT=2400 nice -n 6 tools/run.sh --viewer characters --protagonists --count 6 --clip 0 --clod 2 --width 960 --height 540 --settle 12 --shotdir "$SD" \
 --shot "-298.4,1516.0,5.55,0,-3,11,lod2_lineup" \
 --shot "-298.4,1480.0,6.5,0,-1,11,lod2_40m" \
 --shot "-300,1518.6,5.85,0,-8,11,lod2_mari_close" \
 --shot "-298.4,1518.4,5.95,0,-8,11,lod2_dex_close" > $C/final/run_lod2.log 2>&1
cp /tmp/wine_char/drive_c/users/root/AppData/Local/NeonTide/log.txt $C/final/log_lod2.txt
cd $C/final && for f in *.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "$f"; done
ls $C/final
