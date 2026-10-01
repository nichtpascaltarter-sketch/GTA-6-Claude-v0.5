#!/bin/sh
# run two shot sets in parallel on separate prefixes: par.sh <name> "<shots>" "<args A>" "<args B>"
N=$1; S=$2
( PREFIX=/tmp/wine_fx SHOTS="$S" /tmp/fx/shoot.sh ${N}A $3 > /tmp/fx/par_${N}A.out 2>&1 ) &
( PREFIX=/tmp/wine_fx2 SHOTS="$S" /tmp/fx/shoot.sh ${N}B $4 > /tmp/fx/par_${N}B.out 2>&1 ) &
wait
cat /tmp/fx/par_${N}A.out /tmp/fx/par_${N}B.out
