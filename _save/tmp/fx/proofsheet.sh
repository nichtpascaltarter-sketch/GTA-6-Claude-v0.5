#!/bin/sh
# proofsheet.sh A B : before/after sheet of the facecam shots at 1 m and 4 m for people 27 7 17 10, day and night.
# Columns: A 1 m, B 1 m, A 4 m, B 4 m (crops upscaled to 320 px). Output /tmp/fx/fc/proof_A_B_{day,night}.png
cd /tmp/fx/fc
A=$1; B=$2; S=_ps_$$
for tod in day night; do
  rows=""
  for p in 27 7 17 10; do
    tiles=""
    for d in 1 4; do
      if [ $d = 1 ]; then C=160x160+400+190; else C=120x120+420+212; fi
      for T in $A $B; do
        f=${T}_fc${p}_${d}m_${tod}.png
        [ -f "$f" ] || continue
        convert "$f" -crop $C +repage -resize 320x320 -gravity NorthWest -fill yellow -pointsize 15 -annotate +4+2 "$T  #$p  $d m  $tod" ${S}_t_${T}_${p}_${d}.png
        tiles="$tiles ${S}_t_${T}_${p}_${d}.png"
      done
    done
    convert $tiles +append ${S}_row_$p.png
    rows="$rows ${S}_row_$p.png"
  done
  out=proof_${A}_${B}_$tod.png
  convert $rows -append $out
  rm -f ${S}_t_*.png ${S}_row_*.png
  echo $out
done
