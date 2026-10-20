#!/bin/sh
# phase C first test (retry after an OOM kill): 1080p Q3 runs wait for 4 GB free (they peak near 3.6 GB)
. /tmp/night/views.sh
GATE=4000 SHOTS="$NIGHT" EXE=bin/nt_c1.exe sh /tmp/night/shoot.sh c1 --exposurelog
GATE=4000 sh /tmp/night/tourshot.sh vb bin/nt_hb.exe 7 2
GATE=4000 sh /tmp/night/tourshot.sh vc bin/nt_c1.exe 7 2
echo done > /tmp/night/chain_c1b.done
