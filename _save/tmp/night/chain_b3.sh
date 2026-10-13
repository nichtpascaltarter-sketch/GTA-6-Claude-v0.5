#!/bin/sh
# phase B test: nt_b3 (tree b = final A + B draft + night probe ambient), the 6 night views + the moon view
. /tmp/night/views.sh
SHOTS="$NIGHT $MOON" EXE=bin/nt_b3.exe sh /tmp/night/shoot.sh b3 --exposurelog
echo done > /tmp/night/chain_b3.done
