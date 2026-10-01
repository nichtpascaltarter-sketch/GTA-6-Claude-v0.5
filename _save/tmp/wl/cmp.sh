#!/bin/sh
# usage: cmp.sh before.png after.bmp out.png "label"
convert "$1" -resize 960x540 -gravity north -background black -splice 0x24 -fill white -pointsize 18 -annotate +0+2 "BEFORE $4" /tmp/wl/cmp/_a.png
convert "$2" -resize 960x540 -gravity north -background black -splice 0x24 -fill white -pointsize 18 -annotate +0+2 "AFTER $4" /tmp/wl/cmp/_b.png
convert /tmp/wl/cmp/_a.png /tmp/wl/cmp/_b.png -append "$3"
