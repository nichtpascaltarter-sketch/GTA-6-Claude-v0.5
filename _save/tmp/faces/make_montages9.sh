#!/bin/sh
# batch 9 montages (eye region): <beforeDir> <afterDir> <labelBefore> <labelAfter>
B=$1; A=$2; LB=$3; LA=$4
OUT=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/faces
for f in $B/*.bmp $A/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
/tmp/faces/grid_eyes.sh $OUT/b9_eyes_1m_day.png 0,1,7,17,27,38 1m_day "$B:$LB" "$A:$LA" > /dev/null
/tmp/faces/grid_eyes.sh $OUT/b9_eyes_1m_night.png 0,1,7,17,27,38 1m_night "$B:$LB" "$A:$LA" > /dev/null
/tmp/faces/grid_eyes.sh $OUT/b9_eyes_0p5m_day.png 0,1,7,17,27,38 0p5m_day "$B:$LB" "$A:$LA" > /dev/null
/tmp/faces/grid_eyes.sh $OUT/b9_eyes_more_1m.png 13,20,14,18 1m_day,1m_night "$B:$LB" "$A:$LA" > /dev/null
/tmp/faces/grid_eyes.sh $OUT/b9_eyes_threequarter.png 0,1,7,38 0p5m_day_y35,0p5m_night_y35 "$B:$LB" "$A:$LA" > /dev/null
/tmp/faces/grid_eyes.sh $OUT/b9_profile_1m_day.png 0,1 1m_day_y90 "$B:$LB" "$A:$LA" > /dev/null
sh /tmp/faces/make_montages2.sh $B $A b9 "$LB" "$LA" > /dev/null
ls $OUT/b9_*.png
