#!/bin/sh
# batch 13 before (nt_dev15) / after (nt_dev20) sheets
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
D=$S/city/sheets/b13; mkdir -p $D; cd $D
T="batch 13: before (nt_dev15) | after (nt_dev20)"
python3 /tmp/city/tools/pair_sheet.py roofs1.png "$T" /tmp/city/dev20_before_shots /tmp/city/dev20_shots before after downtown_air downtown_air_close solbeach_air beach_condos
python3 /tmp/city/tools/pair_sheet.py roofs2.png "$T" /tmp/city/dev20_before_shots /tmp/city/dev20_shots before after nps_roofs calleluna_roofs downtown_air_night
python3 /tmp/city/tools/pair_sheet.py keycoral.png "$T" /tmp/city/dev20_before_shots /tmp/city/dev20_shots before after keycoral_air keycoral_air2 keycoral_st1 keycoral_st2
python3 /tmp/city/tools/pair_sheet.py ranch.png "$T" /tmp/city/dev20_before_shots /tmp/city/dev20_shots before after westbrook_air westbrook_st1 westbrook_st2 grove_air grove_st1
