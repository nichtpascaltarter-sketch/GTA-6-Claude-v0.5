#!/bin/bash
# Venue shots of one run -> PNGs, a strip per venue and a 3x3 contact sheet. Usage: venue_sheets.sh TAG
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
TAG=$1
OUT=$SP/venue_shots_$TAG
mkdir -p $OUT
cd /tmp/ai/venues || exit 1
for f in auto_venues_port_gate_*.bmp auto_venues_airport_forecourt_*.bmp auto_venues_sawgrass_*.bmp; do
  [ -f "$f" ] && convert "$f" "$OUT/${f%.bmp}.png"
done
cd $OUT
for v in port_gate airport_forecourt sawgrass; do
  ls auto_venues_${v}_*.png >/dev/null 2>&1 && convert auto_venues_${v}_0.png auto_venues_${v}_1.png auto_venues_${v}_2.png +append strip_$v.png
done
convert strip_port_gate.png strip_airport_forecourt.png strip_sawgrass.png -append sheet.png 2>/dev/null
ls -la $OUT
