#!/bin/sh
while [ ! -f /tmp/fx/chain_dbg2.state ]; do sleep 15; done
EXE=bin/nt_fx30.exe P=fish0 MT=fish_boat EXTRA="" sh /tmp/fx/mission_run.sh
