#!/bin/sh
# after chain23: quality 0 (no contact / feature shadows) for the cheek line
cd /tmp/faces
while pgrep -f "chain23.sh" > /dev/null; do sleep 15; done
FACES_LIFT=1.33 EXE=/tmp/faces/nt_d8t2.exe TIMEOUT=2400 /tmp/faces/shoot2.sh q0 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --quality 0 --shot -279.260,1479.420,6.000,-132.0,0,22,c38_0p5m_night --facecam 38,0.5,0 --shot -280.000,1480.000,6.000,-132.0,0,22,c27_0p5m_night --facecam 27,0.5,0 --shot -260.000,1480.000,6.000,-132.0,0,22,c1_0p5m_night --facecam 1,0.5,0 > /tmp/faces/shoot_q0.out 2>&1
