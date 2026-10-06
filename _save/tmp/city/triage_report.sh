#!/bin/bash
# bagman triage summary: per run, passed / segfault / other, and the last log lines before a segfault
for exe in dev9 m8; do
  n=0; seg=0; pass=0
  for i in 1 2 3; do
    o=/tmp/city/bag_${exe}_$i.out; l=/tmp/city/bag_${exe}_${i}_log.txt
    [ -f "$l" ] || continue
    n=$((n+1))
    p=$(grep -c "PASSED at" $l); s=$(grep -c "Segmentation" $o 2>/dev/null)
    pass=$((pass+p)); seg=$((seg+s))
    st=$(grep "\[missiontest\] bagman" $l | tail -1 | cut -c1-140)
    echo "$exe #$i: passed $p, segfault $s, last: $st"
    if [ "$s" -gt 0 ]; then
      echo "  --- last 25 log lines before the segfault ($l):"
      tail -25 $l | cut -c1-200 | sed 's/^/  /'
    fi
  done
  echo "$exe: $n runs, $pass passed, $seg segfaults"
done
