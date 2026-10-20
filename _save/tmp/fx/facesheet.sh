#!/bin/sh
# facesheet.sh TAG [TAG2] : contact sheet of the face crops (0.5 m and 1 m, day and night) for people 27 7 17 10;
# with TAG2, before/after pairs side by side. Output /tmp/fx/fc/sheet_TAG[_TAG2]_{day,night}.png
cd /tmp/fx/fc
A=$1; B=$2
for tod in day night; do
  rows=""
  for p in 27 7 17 10; do
    tiles=""
    for d in 0p5 1; do
      if [ $d = 0p5 ]; then C=300x300+330+80; else C=180x180+390+130; fi
      for T in $A $B; do
        f=${T}_fc${p}_${d}m_${tod}.png
        [ -f "$f" ] || continue
        convert "$f" -crop $C +repage -resize 300x300 -gravity NorthWest -fill yellow -pointsize 14 -annotate +4+2 "$T $p $d" _t_${T}_${p}_${d}.png
        tiles="$tiles _t_${T}_${p}_${d}.png"
      done
    done
    convert $tiles +append _row_$p.png
    rows="$rows _row_$p.png"
  done
  out=sheet_${A}${B:+_$B}_$tod.png
  convert $rows -append $out
  rm -f _t_*.png _row_*.png
  echo $out
done
