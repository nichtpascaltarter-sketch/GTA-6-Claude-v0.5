#!/bin/sh
# phase B on main 61a3b7b + A: build nt_hb, then (after B's sign-off runs) self-test + the moon view
sh /tmp/night/buildb.sh hb /tmp/night/hb
grep -q "rc 0" /tmp/night/build_hb.done || { echo build-failed > /tmp/night/chain_hb.done; exit 1; }
until [ -f /tmp/night/resume_B.done ]; do sleep 30; done
. /tmp/night/views.sh
sh /tmp/night/selftest.sh hb bin/nt_hb.exe
SHOTS="$MOON" EXE=bin/nt_hb.exe sh /tmp/night/shoot.sh hb --exposurelog
echo done > /tmp/night/chain_hb.done
