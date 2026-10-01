#!/bin/sh
# after-shots for milestones 3 and 4 (cemetery, churchyards, hospitals) with the d5 snapshot
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
TIMEOUT=6000 sh $SP/pl/shots.sh $SP/pl/snap_d5/bin/places_agent.exe $SP/pl/after3 \
 "1680,1030,5.5,0,14,11,tide_st" "1600,985,95,-40,-30,11,tide_air" "1600,985,95,-40,-30,21.5,tide_night" \
 "1507,-750,5.6,-90,1,10,cem_gate" "1545,-717.5,5.6,-90,-3,16,cem_aisle" "1618,-752,5.6,-90,6,17,cem_chapel" "1600,-960,80,0,-40,11,cem_air" "1570,-752,5.6,-90,4,21.5,cem_night" \
 "3620,3100,90,-40,-30,11,mid_air" "-2460,1575,45,-40,-28,11,wb_air" \
 "-114,6562,58,-45,-35,11,okam_air" "-50,6834,10,0,2,10.5,oka_st" "15,6815,40,45,-30,11,oka_air" \
 "4510,7960,58,-45,-35,11,fch_air" "4512,7862,9.8,20,3,15,fort_st" "4560,7850,42,65,-32,11,fort_air"
