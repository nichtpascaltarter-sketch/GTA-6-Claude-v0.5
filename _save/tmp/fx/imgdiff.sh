#!/bin/sh
# imgdiff.sh A B : per tour stop, the images of runs A and B: RMSE, share of pixels differing by more than 8/255, and
# a sheet per stop (A | B | difference x4) in /tmp/fx/diff_A_B_NN.png
cd /tmp/fx
A=$1; B=$2
for fa in ${A}_auto_tour_*.png; do
  s=${fa#${A}_}
  fb=${B}_$s
  [ -f "$fb" ] || continue
  rmse=$(compare -metric RMSE "$fa" "$fb" null: 2>&1 | awk '{print $2}' | tr -d '()')
  big=$(compare -metric AE -fuzz 3.2% "$fa" "$fb" null: 2>&1)
  n=$(echo $s | sed 's/auto_tour_\([0-9]*\)_.*/\1/')
  convert "$fa" "$fb" -compose difference -composite -evaluate multiply 4 /tmp/fx/_d.png
  convert "$fa" "$fb" /tmp/fx/_d.png +append -resize 50% /tmp/fx/diff_${A}_${B}_$n.png
  echo "$s rmse $rmse  pixels>8/255: $big of 518400"
done
rm -f /tmp/fx/_d.png
