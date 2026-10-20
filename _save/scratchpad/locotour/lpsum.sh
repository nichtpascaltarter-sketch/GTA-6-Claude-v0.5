#!/bin/sh
# usage: lpsum.sh <log>...: the locoplayer summary lines (per stair / way / gait: steps, slides, planted frames sunk 1 / 3
# cm, lifted, deep, lowest sole), the segments' times and speeds, and the bigslide / deep counts of each log
for f in "$@"; do
  echo "== $f"
  grep -a 'locoplayer:' $f | sed 's/^\[[ 0-9.]*\] //'
  grep -a 'locoplayer ' $f | grep -a -v 'locoplayer:\|deep \|bigslide\|seg \|done' | sed 's/^\[[ 0-9.]*\] //' | sed 's/ | planted foot-frames/ | pf/; s/slide cm\/step //; s/lowest sole/low/'
  grep -a 'locoplayer seg' $f | sed 's/^\[[ 0-9.]*\] //' | awk '{printf "%s ", $0; n++; if (n % 2 == 0) printf "\n"} END {printf "\n"}' | cut -c1-200
  echo "   bigslide $(grep -a -c 'locoplayer bigslide' $f), deep $(grep -a -c 'locoplayer deep' $f), done $(grep -a -c 'locoplayer done' $f)"
done
