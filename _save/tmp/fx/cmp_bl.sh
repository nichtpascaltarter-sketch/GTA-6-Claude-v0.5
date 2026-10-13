#!/bin/sh
# cmp_bl.sh A B : compare every A/B shot of the bindless runs (tags A and B), print the metrics
A=$1; B=$2
for n in streets_downtown_night streets_residential_night ts2_auto_tour_02_downtown_afternoon ts4_auto_tour_04_midtown_park ts7_auto_tour_07_club_night ts8_auto_tour_08_downtown_rain_night; do
  fa=/tmp/fx/${A}_$n.png; fb=/tmp/fx/${B}_$n.png
  if [ -f "$fa" ] && [ -f "$fb" ]; then /tmp/fx/cmp.sh "$fa" "$fb"; else echo "missing: $fa or $fb"; fi
done
fa=/tmp/fx/${A}_streets_downtown_day.png; [ -f "$fa" ] || fa=/tmp/fx/${A}_streets2_downtown_day.png
fb=/tmp/fx/${B}_streets_downtown_day.png; [ -f "$fb" ] || fb=/tmp/fx/${B}_streets2_downtown_day.png
if [ -f "$fa" ] && [ -f "$fb" ]; then /tmp/fx/cmp.sh "$fa" "$fb"; else echo "missing: $fa or $fb"; fi
