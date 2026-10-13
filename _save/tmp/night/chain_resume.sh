#!/bin/sh
# Resume after the 10-06 stop: phase A's remaining runs (day before set, tours), A on HEAD (build + self-test + one
# night shot), then phase B's sign-off runs. Markers /tmp/night/shots/*.done, /tmp/night/tour/*.done.
. /tmp/night/views.sh
SHOTS="$DAY" EXE=bin/nt_m95.exe sh /tmp/night/shoot.sh dm
sh /tmp/night/tour.sh tm bin/nt_m95.exe 4 5
sh /tmp/night/tour.sh ta bin/nt_a1.exe 4 5
echo done > /tmp/night/resume_A.done
until [ -f /tmp/night/build_ha.done ]; do sleep 30; done
sh /tmp/night/selftest.sh ha bin/nt_ha.exe
SHOTS="3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night" EXE=bin/nt_ha.exe sh /tmp/night/shoot.sh ha --exposurelog
echo done > /tmp/night/resume_HA.done
sh /tmp/night/selftest.sh b3 bin/nt_b3.exe
SHOTS="$MOON" EXE=bin/nt_a1.exe sh /tmp/night/shoot.sh sa_moon --exposurelog
SHOTS="$DAY" EXE=bin/nt_b3.exe sh /tmp/night/shoot.sh db
sh /tmp/night/tour.sh tb bin/nt_b3.exe 4 5
echo done > /tmp/night/resume_B.done
