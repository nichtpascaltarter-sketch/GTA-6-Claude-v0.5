#!/bin/sh
# frame cost (pass timings with the device idle at the boundaries, --synctimers) of buzz-cut heads close up:
# perf_gpu.sh <exe> <tag> [env...]   shots: people 27, 6 at 0.5 m and 1 m by day, 150 frames each
cd /home/user/GTA-6-Claude-v0.5
EXE=$1; T=$2; shift 2
A="--shot -280.000,1480.000,6.000,-132.0,0,15.5,g27_0p5m --facecam 27,0.5,0 --shot -279.630,1479.710,6.000,-132.0,0,15.5,g27_1m --facecam 27,1,0 --shot -279.260,1479.420,6.000,-132.0,0,15.5,g6_0p5m --facecam 6,0.5,0 --shot -278.890,1479.130,6.000,-132.0,0,15.5,g6_1m --facecam 6,1,0"
env "$@" EXE=$EXE TIMEOUT=2400 /tmp/faces/shoot2.sh gpu_$T --viewer characters --protagonists --clip 0 --settle 150 --viewerlamp --synctimers $A > /tmp/faces/shoot_gpu_$T.out 2>&1
python3 -I /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/faces/py/passtimes.py /tmp/faces/log_gpu_$T.txt
