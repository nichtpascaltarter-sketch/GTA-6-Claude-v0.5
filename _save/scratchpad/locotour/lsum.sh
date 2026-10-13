#!/bin/sh
# usage: lsum.sh <log>...: the locostairs summary lines, bigslide / deep counts and the landing errors of each log
for f in "$@"; do
  echo "== $f"
  grep 'locostairs' $f | grep -v 'trace\|locostairs t \|deep\|bigslide\|locostairs:' | sed 's/^\[[ 0-9.]*\] //'
  echo "   bigslide $(grep -c 'locostairs bigslide' $f), deep $(grep -c 'locostairs deep' $f), trace $(grep -c 'locostairs trace' $f)"
done
