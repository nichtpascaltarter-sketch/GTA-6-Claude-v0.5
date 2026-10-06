#!/bin/bash
# the 20 district views of batch 3 (base4 exe on 06580c3) as PNGs, and before/after sheets against the views of main
# before batch 1 (game_base)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
mkdir -p $S/city/game_b3
python3 - <<'PY'
from PIL import Image
import glob, os
S='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad'
for f in glob.glob('/tmp/city/base4_shots/*.bmp'):
    Image.open(f).convert('RGB').save(S + '/city/game_b3/' + os.path.basename(f)[:-4] + '.png')
print(len(glob.glob(S + '/city/game_b3/*.png')), 'pngs')
PY
cd $S/city && python3 make_sheets.py game_base game_b3 sheets/game_b3 "in-game, batches 1-3 (960x540, 10:00); before: main before batch 1"
