#!/bin/sh
# after-shots for milestone 5 (prison, speedway) and the review notes (campus density, moss, deco colour blocking) with d5
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
TIMEOUT=6000 sh $SP/pl/shots.sh $SP/pl/snap_d5/bin/places_agent.exe $SP/pl/after5 \
 "300,8750,120,0,-35,11,prison_air" "340,8890,8.6,0,5,10,prison_gate" "350,9020,8.6,0,3,15,prison_yard" "300,8750,120,0,-35,21.5,prison_night" \
 "300,5450,120,0,-35,11,speedway_air" "400,5688,21,0,-12,16,speedway_stand" "300,5741,8.6,-84,1,11,speedway_pit" "300,5450,120,0,-35,21.5,speedway_night" \
 "2000,3300,110,0,-40,11,campus_air" "1862,3595,5.6,-35,4,16,campus_quad" "1950,3700,380,0,-89,12,campus_top" \
 "5371,700,4.6,15,2,10.5,deco_street" "5398,840,4.3,90,8,11,deco_front" "5470,690,60,50,-30,10,deco_air" "5371,700,4.6,15,3,21.5,deco_night"
