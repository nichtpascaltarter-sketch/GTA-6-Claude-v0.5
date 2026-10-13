#!/bin/sh
# cmp.sh A B : RMSE (normalized) and the share of pixels that differ by more than 2/255 between two images
A=$1; B=$2
r=$(compare -metric RMSE "$A" "$B" null: 2>&1 | sed -E 's/.*\(([0-9.e-]+)\).*/\1/')
n=$(compare -metric AE -fuzz 0.8% "$A" "$B" null: 2>&1)
t=$(identify -format "%[fx:w*h]" "$A")
echo "$(basename $A) vs $(basename $B): RMSE $r, pixels off by >2/255: $n of $t"
