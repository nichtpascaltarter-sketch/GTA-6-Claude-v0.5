#!/bin/sh
# Keep the lead flag fresh while showcase.exe runs (at most 40 min), so agents' run.sh/build.sh hold new launches
# while memory is tight and the showcase is not OOM-killed. The flag then lapses on its own (agents ignore it after 5 min).
n=0
while ps -eo args= | awk '$0 ~ /showcase[.]exe/ && $0 !~ /awk|hold_for/ {f = 1} END {exit !f}'; do
  touch /tmp/neontide_lead_wants; sleep 30; n=$((n + 1)); [ $n -lt 80 ] || break
done
