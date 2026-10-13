#!/bin/sh
# host tools from the dev15 tree: preview (buzz knobs), cnt (triangle counts), hairlist (+ protagonists)
cd /home/user/GTA-6-Claude-v0.5
for t in preview cnt hairlist; do
  n=0; while [ "$(sh tools/memfree.sh)" -lt 2000 ] && [ $n -lt 240 ]; do sleep 15; n=$((n+1)); done
  echo "$t: memfree $(sh tools/memfree.sh) at $(date +%T)"
  cd /tmp/faces/dev15
  case $t in
    preview) nice -n 5 g++ -O2 -std=c++17 -w -I src tests/anim/preview.cpp -o /tmp/faces/pv15 2>&1 | tail -3 ;;
    cnt) nice -n 5 g++ -O1 -std=c++17 -w -I src -I . /tmp/faces/cnt/cnt.cpp -o /tmp/faces/cnt15 2>&1 | tail -3 ;;
    hairlist) nice -n 5 g++ -O1 -std=c++17 -w -I src -I . /tmp/faces/tools/hairlist.cpp -o /tmp/faces/hairlist 2>&1 | tail -3 ;;
  esac
  cd /home/user/GTA-6-Claude-v0.5
done
/tmp/faces/hairlist 2 | head -2
echo "tools done $(date +%T)"
