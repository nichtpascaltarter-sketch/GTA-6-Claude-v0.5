#!/bin/sh
# proofsheet2.sh A B [OUT] : before/after sheet of 1920x1080 facecam shots, native-resolution 320 px crops (no scaling):
# columns A 1 m, B 1 m, A 4 m, B 4 m; rows people 27 7 17 10. Output /tmp/fx/fc/OUT_{day,night}.png (OUT default proof2_A_B)
cd /tmp/fx/fc
A=$1; B=$2; O=${3:-proof2_${A}_${B}}; S=_p2_$$
for tod in day night; do
  rows=""
  for p in 27 7 17 10; do
    tiles=""
    for d in 1 4; do
      if [ $d = 1 ]; then C=320x320+800+380; else C=320x320+800+400; fi
      for T in $A $B; do
        f=${T}_fc${p}_${d}m_${tod}.png
        [ -f "$f" ] || continue
        convert "$f" -crop $C +repage -gravity NorthWest -fill yellow -pointsize 15 -annotate +4+2 "$T  #$p  $d m  $tod" ${S}_t_${T}_${p}_${d}.png
        tiles="$tiles ${S}_t_${T}_${p}_${d}.png"
      done
    done
    [ -n "$tiles" ] && convert $tiles +append ${S}_row_$p.png && rows="$rows ${S}_row_$p.png"
  done
  [ -n "$rows" ] && convert $rows -append ${O}_$tod.png && echo ${O}_$tod.png
  rm -f ${S}_t_*.png ${S}_row_*.png
done
