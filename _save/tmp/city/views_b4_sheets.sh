#!/bin/bash
# batch 4 in-game: the check shots (new types) as labelled sheets, and the 20 district views against batch 3's
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
mkdir -p $S/city/game_b4 $S/city/sheets/game_b4 $S/city/sheets/game_b4_new_types
python3 - <<'PY'
from PIL import Image, ImageDraw, ImageFont
import glob, os
S='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad'
for f in glob.glob('/tmp/city/dev4_shots/*.bmp'):
    Image.open(f).convert('RGB').save(S + '/city/game_b4/' + os.path.basename(f)[:-4] + '.png')
try: font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18)
except Exception: font = ImageFont.load_default()
def sheet(names, out, title):
    ims=[(n, Image.open(S + f'/city/game_b4/{n}.png').convert('RGB').resize((640,360))) for n in names if os.path.exists(S + f'/city/game_b4/{n}.png')]
    if not ims: return
    rows=(len(ims)+1)//2
    sh=Image.new('RGB',(2*640+12, 34+rows*366),(12,12,14)); d=ImageDraw.Draw(sh)
    d.text((8,6),title,fill=(255,255,255),font=font)
    for i,(n,im) in enumerate(ims):
        x=(i%2)*652; y=34+(i//2)*366
        sh.paste(im,(x,y)); d.rectangle([x,y,x+len(n)*11+10,y+22],fill=(0,0,0)); d.text((x+5,y+2),n,fill=(140,230,140),font=font)
    sh.save(out); print(out, len(ims))
O=S+'/city/sheets/game_b4_new_types/'
sheet(['nc_cbs_air','cbs_nc_box','cbs_nc_L','wb_cbs_air','cbs_wb_L','mimo_carport_b','cbs_bay'], O+'block_houses_ingame.png', 'Batch 4 in-game: 1950s block houses, carports, awnings, a front bay')
sheet(['oka_air','vic_oka_L1','vic_oka_L2','vic_fc_box','vic_har_L','bung_L_oka','bung_L_nc'], O+'victorians_bungalows_ingame.png', 'Batch 4 in-game: folk Victorians, L bungalows (Okahatchee, Fort Castell, Harlow)')
sheet(['ranch_bay_1','ranch_bay_3','strip_mission','strip_mimo','strip_modern','kc_villas_air'], O+'ranches_strips_villas_ingame.png', 'Batch 4 in-game: front-bay ranches, strip courts on the ground, Key Coral villas')
PY
cd $S/city && python3 make_sheets.py game_b3 game_b4 sheets/game_b4 "in-game, batch 4 vs batch 3 (960x540, 10:00)"
