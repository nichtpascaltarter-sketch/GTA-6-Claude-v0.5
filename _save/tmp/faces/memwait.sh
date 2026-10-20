#!/bin/sh
# waits (no time limit) until tools/memfree.sh reports at least $1 MB
cd /home/user/GTA-6-Claude-v0.5
while [ "$(sh tools/memfree.sh)" -lt ${1:-2000} ]; do sleep 15; done
