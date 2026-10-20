#!/bin/sh
# tour1.sh TAG EXE START COUNT : timing tour, retried (up to 3 tries) until every stop's shot exists
T=$1; X=$2; S=$3; C=$4
k=0
while [ $k -lt 3 ]; do
  sh /tmp/night/tour.sh $T $X $S $C
  n=$(ls /tmp/night/tour/${T}_auto_tour_*.png 2>/dev/null | wc -l)
  [ "$n" -ge "$C" ] && break
  k=$((k+1))
done
