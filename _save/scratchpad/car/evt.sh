#!/bin/sh
# usage: evt.sh BIN model seat hidx dir(in|out) [heights]  -> the moving (non-seated) events of that run (3-height set by default)
H=${6:-1.58,1.76,1.94}
CARVIEW_H=$4 CARVIEW_DIR=$5 CARVIEW_HEIGHTS=$H CARVIEW_SEAT=$3 CARVIEW_EVENTS=1 $1 /dev/null --model $2 --check 2>&1 | grep -v "(seated)" | sed -E 's/ roof .* SHELL/ SHELL/; s/ CROSS ([0-9.]+)@.*\) ([0-9]+) frames.*/ CROSS \2/'
