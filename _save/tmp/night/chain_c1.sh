#!/bin/sh
# phase C first test: build nt_c1 (tree hc), then night views + moon on nt_c1 and tour stops 7-8 at 1080p on nt_hb and nt_c1
. /tmp/night/views.sh
sh /tmp/night/buildb.sh c1 /tmp/night/hc
grep -q "rc 0" /tmp/night/build_c1.done || { echo build-failed > /tmp/night/chain_c1.done; exit 1; }
SHOTS="$NIGHT" EXE=bin/nt_c1.exe sh /tmp/night/shoot.sh c1 --exposurelog
sh /tmp/night/tourshot.sh vb bin/nt_hb.exe 7 2
sh /tmp/night/tourshot.sh vc bin/nt_c1.exe 7 2
echo done > /tmp/night/chain_c1.done
