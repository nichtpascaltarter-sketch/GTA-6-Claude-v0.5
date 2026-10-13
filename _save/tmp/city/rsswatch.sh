#!/bin/bash
# peak RSS of the citylab run (VmHWM), sampled every second
mx=0
until [ -f /tmp/city/clab9t.pid ]; do sleep 1; done
p=$(cat /tmp/city/clab9t.pid)
while kill -0 $p 2>/dev/null; do for q in $p $(pgrep -P $p); do h=$(awk '/VmHWM/ {print $2}' /proc/$q/status 2>/dev/null); [ -n "$h" ] && [ "$h" -gt "$mx" ] && mx=$h; done; sleep 1; done
echo "peak VmHWM ${mx} kB" > /tmp/city/clab9t_rss.txt
