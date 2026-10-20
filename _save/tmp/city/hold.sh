#!/bin/bash
# the lead's hold: no new game of mine before 03:55 (or /tmp/city/go); then jobs23 (batches 12 + 14 + 15) may start.
# (The lone grove_street HQ "before" retry is dropped: batch 13 signs off without it.)
HOLD=$(date -d "2026-10-13 03:55:00 UTC" +%s)
until [ "$(date +%s)" -ge "$HOLD" ] || [ -f /tmp/city/go ]; do sleep 20; done
echo "jobs22e done (hold over; the grove_street HQ before retry dropped) $(date)" >> /tmp/city/jobs22.status
