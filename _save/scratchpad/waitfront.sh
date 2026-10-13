#!/bin/sh
# waitfront.sh: wait until my storytest compile has been running for 150 s (its front end has read every source file)
i=0
while [ $i -lt 5400 ]; do
  pid=$(pgrep -f "cc1plus.*main_gen[.]cpp" | head -1)
  if [ -n "$pid" ]; then
    et=$(ps -o etimes= -p $pid 2>/dev/null | tr -d ' ')
    if [ -n "$et" ] && [ "$et" -ge 150 ]; then echo "compile $pid running ${et}s: safe to edit"; exit 0; fi
  fi
  sleep 10; i=$((i+10))
done
echo "timeout"
