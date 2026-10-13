#!/bin/sh
# ab.sh before.png after.png out.png [crop WxH+X+Y] : side-by-side before/after (optionally a 2x zoom of a crop)
B=$1; A=$2; O=$3; C=$4
if [ -n "$C" ]; then
  convert "$B" -crop $C +repage -filter point -resize 200% /tmp/fx/_ab_b.png
  convert "$A" -crop $C +repage -filter point -resize 200% /tmp/fx/_ab_a.png
else
  cp "$B" /tmp/fx/_ab_b.png; cp "$A" /tmp/fx/_ab_a.png
fi
convert /tmp/fx/_ab_b.png -gravity NorthWest -fill yellow -pointsize 18 -annotate +6+4 "before" /tmp/fx/_ab_b.png
convert /tmp/fx/_ab_a.png -gravity NorthWest -fill yellow -pointsize 18 -annotate +6+4 "after" /tmp/fx/_ab_a.png
convert /tmp/fx/_ab_b.png /tmp/fx/_ab_a.png +append "$O"
rm -f /tmp/fx/_ab_b.png /tmp/fx/_ab_a.png
