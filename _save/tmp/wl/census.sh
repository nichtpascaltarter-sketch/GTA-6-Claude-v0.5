#!/bin/sh
# usage: census.sh log.txt  - the tour shots and the last census line before each shot
awk '/autoplay tour stop/ {stop=$0} /population: census/ {c=$0} /autoplay tour shot/ {print "----"; print substr(c, 1, 260); print substr($0, 1, 200)}' "$1"
