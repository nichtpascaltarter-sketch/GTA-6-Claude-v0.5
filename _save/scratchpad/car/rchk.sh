#!/bin/sh
# usage: rchk.sh BIN "models" "seats" "heights" -> compact lines: model seat h in/out hands SHELL frames CROSS frames pop
for m in $2; do for st in $3; do
  CARVIEW_HEIGHTS=$4 CARVIEW_SEAT=$st $1 /dev/null --model $m --check 2>&1 | grep " seat " | \
    perl -ne 'if (/^(.{12}) seat (\d) h ([\d.]+) (in |out):.*hand out ([\d.]+) in ([\d.]+) +SHELL ([\d.]+)\@[-\d.]+\([^)]*\) (\d+)\/(\d+) frames, seated ([\d.]+) +CROSS ([\d.]+)\@[-\d.]+\([^)]*\) (\d+) frames.* pop ([\d.]+)\@/) { printf "%s s%s %s %s hand %s %s  SHELL %3d/%3d (%.3f) seated %.3f  CROSS %3d  pop %s\n", $1,$2,$3,$4,$5,$6,$8,$9,$7,$10,$12,$13; } else { print "?? $_"; }'
done; done
