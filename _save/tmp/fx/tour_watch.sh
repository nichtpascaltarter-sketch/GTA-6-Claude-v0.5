#!/bin/sh
# Wait for the first screenshot of my own tour run (shotdir /tmp/fx/$P), then stop only that run.
P=$1
while [ -z "$(ls /tmp/fx/$P/ 2>/dev/null)" ]; do
  sleep 5
  ps aux | grep "fx.$P" | grep -q "[n]t_" || break
done
sleep 4
for p in $(ps aux | grep "fx.$P" | grep "[n]t_\|[x]vfb\|[w]ine" | awk '{print $2}'); do kill $p 2>/dev/null; done
sleep 2
for f in /tmp/fx/$P/*.bmp; do [ -f "$f" ] && convert "$f" "/tmp/fx/${P}_$(basename $f .bmp).png"; done
ls /tmp/fx/${P}_*.png
