#!/bin/sh
# transit agent: run the transit autoplay tests one after another (one Wine instance at a time)
cd /home/user/GTA-6-Claude-v0.5
OUT=/tmp/transit
mkdir -p $OUT
LOGF=/tmp/wine_transit/drive_c/users/root/AppData/Local/NeonTide/log.txt
for mode in ${MODES:-metro bus ferry}; do
  rm -f $OUT/${mode}_*.bmp $OUT/auto_${mode}_*.bmp
  WINEPREFIX=/tmp/wine_transit EXE=bin/nt_transit.exe TIMEOUT=${TO:-1800} nice -n 10 tools/run.sh --width 640 --height 360 --play --autoplay $mode --autoduration ${DUR:-600} --autoevery 90 --renderevery ${RE:-3} ${TH:+--transithour $TH} --shotdir 'Z:\tmp\transit\' > $OUT/run_$mode.txt 2>&1
  echo "$mode exit $?" >> $OUT/run_$mode.txt
  cp $LOGF $OUT/log_$mode.txt 2>/dev/null
  grep -E "Transit test|Transit:.*(bus|ferry|Ferry|route|stop|skip|boarded|ashore)|error|Error|crash|Crash" $OUT/log_$mode.txt | head -80 > $OUT/summary_$mode.txt
  for f in $OUT/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "$f"; done
done
echo all done
