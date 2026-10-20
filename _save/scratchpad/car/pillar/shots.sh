#!/bin/sh
# per car model: the last left door's rear top corner (C-pillar edge), closed and open, HEAD vs now
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
H=$S/headtree
for m in $(seq 0 26); do
  info=$($S/car/carview /dev/null --model $m --info 2>/dev/null)
  name=$(echo "$info" | head -1 | sed -E 's/ \(.*//')
  # the last left door: door 2 on four-door bodies, else door 0
  nd=$(echo "$info" | grep -c "^ door [0-9] [LR]")
  [ "$nd" -eq 0 ] && continue
  if [ "$nd" -ge 4 ]; then k=2; else k=0; fi
  line=$(echo "$info" | grep "^ door $k outline")
  # rear top corner: the outline point with the smallest y among those above the belt (z > 0.9 * max z)
  tgt=$(echo "$line" | sed -E 's/^ door [0-9] outline: //' | tr ')' '\n' | sed -E 's/ *\(//' | awk 'NF==2 {print}' | sort -k2 -n -r | awk 'NR==1 {zt=$2} {if ($2 > zt - 0.35) print}' | sort -k1 -n | head -1)
  ty=$(echo $tgt | awk '{print $1}'); tz=$(echo $tgt | awk '{print $2 - 0.12}')
  hx=$(echo "$info" | grep "^ door $k L" | sed -E 's/.*hinge \(([-0-9.]+) .*/\1/')
  for st in closed open; do
    if [ $st = open ]; then O="--open 1 --door $k"; else O="--open 0"; fi
    $H/cv_head h.ppm --model $m $O --yaw -90 --pitch 3 --dist 1.6 --target $hx,$ty,$tz --fov 40 --w 420 --h 300 --ss 2 > /dev/null 2>&1
    $S/car/carview n.ppm --model $m $O --yaw -90 --pitch 3 --dist 1.6 --target $hx,$ty,$tz --fov 40 --w 420 --h 300 --ss 2 > /dev/null 2>&1
    convert h.ppm -gravity north -fill black -pointsize 16 -annotate +0+3 "$m $name HEAD $st" h_$st.png
    convert n.ppm -gravity north -fill black -pointsize 16 -annotate +0+3 "$m $name now $st" n_$st.png
  done
  montage h_closed.png n_closed.png h_open.png n_open.png -tile 4x -geometry +2+2 -background gray20 row_$(printf %02d $m).png
  rm -f h.ppm n.ppm h_*.png n_*.png
done
