#!/bin/sh
# before runs with nt_d12a (HEAD), one game at a time
cd /tmp/fx
export EXE=bin/nt_d12a.exe TIMEOUT=2400
SHOTS="4046.2,-782.1,4.7,-108.7,-3,22.3,kt_b 4060,-782,6.5,95,-15,22.3,kt_c" sh /tmp/fx/shoot.sh d12a_r1 --rain 1.0 --settle 16 --quality 1
SHOTS="4046.2,-782.1,4.7,-108.7,-3,22.3,kt_b5" sh /tmp/fx/shoot.sh d12a_r3 --rain 1.0 --settle 4 --quality 1 --debugview 5
SHOTS="4046.2,-782.1,4.7,-108.7,-3,12.5,kt_bday 4290,-400,4.2,0,-55,12.5,conc_close 4100,-215,4.0,-90,-15,12.5,pq_road" sh /tmp/fx/shoot.sh d12a_r2 --settle 8 --quality 1
echo done > /tmp/fx/before.done
