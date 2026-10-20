#!/bin/sh
# shoot1.sh PREFIX EXE "view view ..." [extra args] : one game per view (an OOM kill loses one view), up to 3 tries
# each; PNGs /tmp/night/shots/PREFIX_<name>.png; marker /tmp/night/shots/PREFIX.done
P=$1; X=$2; VIEWS=$3; shift 3
for v in $VIEWS; do
  n=$(echo $v | awk -F, '{print $7}')
  k=0
  while [ ! -f /tmp/night/shots/${P}_$n.png ] && [ $k -lt 3 ]; do
    SHOTS="$v" EXE=$X GATE=${GATE:-4000} sh /tmp/night/shoot.sh ${P}_1 "$@"
    [ -f /tmp/night/shots/${P}_1_$n.png ] && mv /tmp/night/shots/${P}_1_$n.png /tmp/night/shots/${P}_$n.png
    k=$((k+1))
  done
done
rm -f /tmp/night/shots/${P}_1.done; rmdir /tmp/night/shots/${P}_1 2>/dev/null
echo done > /tmp/night/shots/$P.done
