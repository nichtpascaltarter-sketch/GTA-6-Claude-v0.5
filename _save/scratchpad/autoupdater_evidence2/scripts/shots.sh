#!/bin/sh
# Update notice screenshots through the UI test harness (--viewer menu): the notice state comes from the shot name,
# _k<keys> presses keys (u) and menu actions go to the log
sh /tmp/tools/gate.sh 3300
D=/tmp/upd2/shots
rm -rf $D && mkdir -p $D
P="3168,-242.9,14,-70,4,18.4"
EXE=/tmp/upd2/tree/bin/nt_upd2.exe WINEPREFIX=/tmp/upd2/wine TIMEOUT=1800 /tmp/upd2/tree/tools/run.sh --viewer menu --width 1280 --height 720 \
  --uifast --quality 0 --settle 50 --shotdir 'Z:\tmp\upd2\shots\' \
  --shot $P,u01_menu_main_updcheck --shot $P,u02_menu_main_updget --shot $P,u03_menu_main_updunpack --shot $P,u04_menu_main_updbuild \
  --shot $P,u05_menu_main_updbuild_updlong --shot $P,u06_menu_main_updready --shot $P,u07_menu_main_updfail --shot $P,u08_menu_main_updtools \
  --shot $P,u09_menu_main_updready_ku --shot $P,u10_menu_main_updfail_ku --shot $P,u11_menu_stats_updbuild --shot $P,u12_menu_main \
  --shot $P,u13_menu_main_updready_updlong > $D/stdout.txt 2>&1
echo "run.sh exit=$?"
cp /tmp/upd2/wine/drive_c/users/root/AppData/Local/NeonTide/log.txt $D/log.txt
cd $D && for f in *.bmp; do convert "$f" "${f%.bmp}.png" && rm -f "$f"; done; ls
grep -n "menu action\|starting\|Unhandled\|FATAL" $D/log.txt | head -20
