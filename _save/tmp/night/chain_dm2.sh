#!/bin/sh
# the dm run was OOM-killed before its last view: shoot it again (nt_m95), after the tours
until [ -f /tmp/night/resume_A.done ]; do sleep 30; done
SHOTS="-1584.69,-591.72,8.14,-96.93,-1.88,14.00,mimo_strip_mall_day" EXE=bin/nt_m95.exe sh /tmp/night/shoot.sh dm2
echo done > /tmp/night/chain_dm2.done
