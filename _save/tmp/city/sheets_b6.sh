#!/bin/bash
# batch 6 before/after pair sheets (before = nt_dev7 = batch 5 on 111efa9; after = nt_dev9 = 8fb1e66 + batch 6)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -q "jobs9 done" /tmp/city/jobs9.status 2>/dev/null; do sleep 20; done
mkdir -p $S/city/sheets/game_b6
cd /tmp/city
python3 $S/city/pair_sheet.py /tmp/city/dev9_before_shots /tmp/city/dev9_shots $S/city/sheets/game_b6/pairs_keys_motels.png "Batch 6 in game: Key Solano conch houses and Keys motels (before = batch 5)" ks_conch1 ks_conch2 km4 km8 > /tmp/city/sheets_b6.log 2>&1
python3 $S/city/pair_sheet.py /tmp/city/dev9_before_shots /tmp/city/dev9_shots $S/city/sheets/game_b6/pairs_eyebrow_stilt.png "Batch 6 in game: eyebrow houses, stilt houses, Key Solano from the air" eb3 eb9 eb10 tp_stilt1 ks_town_air >> /tmp/city/sheets_b6.log 2>&1
ls /tmp/city/dev9_before_shots | sed 's/\.bmp//' > /tmp/city/b6_before_list.txt
python3 $S/city/pair_sheet.py /tmp/city/dev9_before_shots /tmp/city/dev9_shots $S/city/sheets/game_b6/pairs_rest.png "Batch 6 in game: ranch and block-house side gables, flat-roofed Mediterranean houses" $(grep -v -x -e ks_conch1 -e ks_conch2 -e km4 -e km8 -e eb3 -e eb9 -e eb10 -e tp_stilt1 -e ks_town_air /tmp/city/b6_before_list.txt) >> /tmp/city/sheets_b6.log 2>&1
echo "sheets done $(date)" >> /tmp/city/sheets_b6.log
