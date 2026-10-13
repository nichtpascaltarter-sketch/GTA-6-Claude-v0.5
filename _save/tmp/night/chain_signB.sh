#!/bin/sh
# Phase B sign-off runs (after phase A's): nt_a1 (A) vs nt_b3 (A + B): self-test, moon view on nt_a1, day views on
# nt_b3, timing tour on nt_b3 (A's tour ta is the baseline). Night views: sa (nt_a1) vs b3 (nt_b3) already exist.
. /tmp/night/views.sh
until [ -f /tmp/night/chain_signA.done ]; do sleep 30; done
sh /tmp/night/selftest.sh b3 bin/nt_b3.exe
SHOTS="$MOON" EXE=bin/nt_a1.exe sh /tmp/night/shoot.sh sa_moon --exposurelog
SHOTS="$DAY" EXE=bin/nt_b3.exe sh /tmp/night/shoot.sh db
sh /tmp/night/tour.sh tb bin/nt_b3.exe 4 5
echo done > /tmp/night/chain_signB.done
