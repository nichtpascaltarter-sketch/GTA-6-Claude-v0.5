#!/bin/sh
# tour stop 8 (downtown rain night) and the Clear Air missiontest, before and after, one game at a time
until [ -f /tmp/fx/after.done ]; do sleep 20; done
EXE=bin/nt_d12a.exe P=d12a_ts8 N=8 sh /tmp/fx/tour_stop.sh
EXE=bin/nt_d12b.exe P=d12b_ts8 N=8 sh /tmp/fx/tour_stop.sh
P=d12a_ca EXE=bin/nt_d12a.exe MT=clear_air TO=3000 sh /tmp/fx/mission_run2.sh
P=d12b_ca EXE=bin/nt_d12b.exe MT=clear_air TO=3000 sh /tmp/fx/mission_run2.sh
echo done > /tmp/fx/more.done
