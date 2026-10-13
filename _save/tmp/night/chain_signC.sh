#!/bin/sh
# Phase C sign-off: main ab5995c (nt_m59) vs ab5995c + C (nt_cF): builds, self-test, night + day 1080p Q3 (4 GB gate),
# timing tour stops 4-8 (960x540). Waits for the shop retest (chain_c5) first.
. /tmp/night/views.sh
# (the shop retest is parked)
sh /tmp/night/buildb.sh cF /tmp/night/cF
grep -q "rc 0" /tmp/night/build_cF.done || { echo build-failed > /tmp/night/chain_signC.done; exit 1; }
sh /tmp/night/buildb.sh m59 /tmp/night/m59
grep -q "rc 0" /tmp/night/build_m59.done || { echo build-failed > /tmp/night/chain_signC.done; exit 1; }
sh /tmp/night/selftest.sh cF bin/nt_cF.exe
GATE=4000 SHOTS="$NIGHT" EXE=bin/nt_cF.exe sh /tmp/night/shoot.sh nc --exposurelog
GATE=4000 SHOTS="$NIGHT" EXE=bin/nt_m59.exe sh /tmp/night/shoot.sh nm --exposurelog
GATE=4000 SHOTS="$DAY" EXE=bin/nt_cF.exe sh /tmp/night/shoot.sh dc
GATE=4000 SHOTS="$DAY" EXE=bin/nt_m59.exe sh /tmp/night/shoot.sh dmm
sh /tmp/night/tour.sh tm59 bin/nt_m59.exe 4 5
sh /tmp/night/tour.sh tcF bin/nt_cF.exe 4 5
echo done > /tmp/night/chain_signC.done
