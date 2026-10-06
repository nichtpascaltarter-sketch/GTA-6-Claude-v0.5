#!/bin/sh
# wait until tools/memfree.sh reports at least $1 MB (default 1500), up to 20 minutes
need=${1:-1500}; n=0
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt "$need" ] && [ $n -lt 60 ]; do sleep 20; n=$((n+1)); done
sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh
