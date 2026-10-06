#!/bin/sh
# proofsheet3.sh OUT DIST TAG... : one column per tag (1920x1080 facecam shots, native 320 px crops), rows people
# 27 7 17 10, for day and night. Output /tmp/fx/fc/OUT_{day,night}.png
cd /tmp/fx/fc
O=$1; D=$2; shift 2; S=_p3_$$
if [ "$D" = 1 ]; then C=320x320+800+380; else C=320x320+800+400; fi
for tod in day night; do
  rows=""
  for p in 27 7 17 10; do
    tiles=""
    for T in "$@"; do
      f=${T}_fc${p}_${D}m_${tod}.png
      [ -f "$f" ] || continue
      convert "$f" -crop $C +repage -gravity NorthWest -fill yellow -pointsize 15 -annotate +4+2 "$T  #$p  $D m  $tod" ${S}_t_${T}_${p}.png
      tiles="$tiles ${S}_t_${T}_${p}.png"
    done
    [ -n "$tiles" ] && convert $tiles +append ${S}_row_$p.png && rows="$rows ${S}_row_$p.png"
  done
  [ -n "$rows" ] && convert $rows -append ${O}_$tod.png && echo ${O}_$tod.png
  rm -f ${S}_t_*.png ${S}_row_*.png
done
