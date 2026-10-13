#!/bin/sh
# nasion metrics with the dev20 knobs: depth (glabella max - midline min over z -2..+14), z of the min, groove (x17 - mid, z +4..+14)
for who in p0 p1 "56433 0" "135623 3" "301922 3" "214813 6"; do
  printf "%-10s " "$who"
  /tmp/faces/midprof20 $who | awk 'NR>1 {z=$1+0; m=$2+0; s=$3+0; if (z>=-2 && z<=14 && (mn=="" || m<mn)) {mn=m; zm=z}; if (z>=10 && z<=30 && (mx=="" || m>mx)) mx=m; if (z>=4 && z<=14 && (g=="" || s-m>g)) g=s-m} END {printf "depth %4.1f (min at z%+3d) groove %4.1f | ", mx-mn, zm, g}'
done
echo
