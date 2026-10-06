#!/bin/sh
# dev8 test build (FACES_LIFT override): albedo debug view, the A/B shot list, then the cheeks with a stronger lift
cd /tmp/faces
FACES_LIFT=1.33 EXE=/tmp/faces/nt_d8t.exe TIMEOUT=2400 /tmp/faces/shoot2.sh d8dbg1 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --debugview 1 $(cat dbg.args) > /tmp/faces/shoot_d8dbg1.out 2>&1
FACES_LIFT=1.33 EXE=/tmp/faces/nt_d8t.exe TIMEOUT=2400 /tmp/faces/shoot2.sh d8a --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat ab_day.args) $(cat ab_night.args) > /tmp/faces/shoot_d8a.out 2>&1
FACES_LIFT=1.6 EXE=/tmp/faces/nt_d8t.exe TIMEOUT=2400 /tmp/faces/shoot2.sh d8b --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat ab_cheeks.args) > /tmp/faces/shoot_d8b.out 2>&1
