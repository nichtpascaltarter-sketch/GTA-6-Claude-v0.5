#!/bin/sh
while pgrep -f "shoot.sh st6" >/dev/null; do sleep 10; done
EXE=bin/nt_fx26.exe P=ml1 EXTRA="" sh /tmp/fx/melee_run.sh
