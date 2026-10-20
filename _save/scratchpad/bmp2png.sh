#!/bin/sh
# convert every BMP (older than 15 s, so not one still being written) in a directory to a 960x540 PNG and delete it
cd "$1" || exit 1
for f in $(find . -maxdepth 1 -name '*.bmp' -mmin +0.25 | sed 's|^\./||'); do
  n="${f%.bmp}"
  convert "$f" -resize 960x540 "$n.png" && rm -f "$f"
done
ls *.png 2>/dev/null | wc -l
