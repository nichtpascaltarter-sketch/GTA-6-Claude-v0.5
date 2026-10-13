#!/bin/sh
# phase A on main 61a3b7b (MSVC-fix merge): build, self-test, one night shot
sh /tmp/night/buildb.sh h61 /tmp/night/h61
grep -q "rc 0" /tmp/night/build_h61.done || { echo build-failed > /tmp/night/chain_h61.done; exit 1; }
until [ -f /tmp/night/resume_HA.done ]; do sleep 30; done
sh /tmp/night/selftest.sh h61 bin/nt_h61.exe
SHOTS="3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night" EXE=bin/nt_h61.exe sh /tmp/night/shoot.sh h61 --exposurelog
echo done > /tmp/night/chain_h61.done
