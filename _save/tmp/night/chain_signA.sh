#!/bin/sh
# Phase A sign-off runs: nt_m95 (main 95dc026) vs nt_a1 (/tmp/night/a): self-test, night + day views 1080p Q3, timing tour
. /tmp/night/views.sh
until [ -f /tmp/night/build_a1.done ]; do sleep 30; done
grep -q "rc 0" /tmp/night/build_a1.done || { echo "build failed" > /tmp/night/chain_signA.done; exit 1; }
until [ -f /tmp/night/chain_b2.done ]; do sleep 30; done
sh /tmp/night/selftest.sh a1 bin/nt_a1.exe
SHOTS="$NIGHT" EXE=bin/nt_a1.exe sh /tmp/night/shoot.sh sa --exposurelog
SHOTS="$NIGHT" EXE=bin/nt_m95.exe sh /tmp/night/shoot.sh sm --exposurelog
SHOTS="$DAY" EXE=bin/nt_a1.exe sh /tmp/night/shoot.sh da
SHOTS="$DAY" EXE=bin/nt_m95.exe sh /tmp/night/shoot.sh dm
sh /tmp/night/tour.sh tm bin/nt_m95.exe 4 5
sh /tmp/night/tour.sh ta bin/nt_a1.exe 4 5
echo done > /tmp/night/chain_signA.done
