#!/bin/sh
# The ordinary automated drive smoke on the commit-A build: must run as before; the log shows the skipped update check
sh /tmp/tools/gate.sh 3300
D=/tmp/upd2/smoke
L=/tmp/upd2/wine/drive_c/users/root/AppData/Local/NeonTide/log.txt
rm -rf $D && mkdir -p $D/bin && cp /tmp/upd2/tree/bin/nt_upd2.exe $D/bin/NeonTide.exe
EXE=$D/bin/NeonTide.exe WINEPREFIX=/tmp/upd2/wine TIMEOUT=1800 /tmp/upd2/tree/tools/run.sh --width 960 --height 540 --quality 1 \
  --play --autoplay drive --autoduration 30 --autoevery 10 --renderevery 6 --shotdir 'Z:\tmp\upd2\smoke\' > $D/stdout.txt 2>&1
echo "run.sh exit=$?"
cp $L $D/log.txt
echo "files next to the exe: $(ls $D/bin | tr '\n' ' ')"
grep -n "starting\|Updater\|Init done\|Unhandled\|FATAL\|autoplay t=" $D/log.txt | cut -c1-200 | head -30
ls $UPD 2>/dev/null; ls /tmp/upd2/wine/drive_c/users/root/AppData/Local/NeonTide/
