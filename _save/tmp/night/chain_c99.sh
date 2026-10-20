#!/bin/sh
# phase C on HEAD 8993054 (after the city agent's storefront gate + landmark beacons): build, self-test, one aerial
sh /tmp/night/buildb.sh c99 /tmp/night/c99
grep -q "rc 0" /tmp/night/build_c99.done || { echo build-failed > /tmp/night/chain_c99.done; exit 1; }
sh /tmp/night/selftest.sh c99 bin/nt_c99.exe
sh /tmp/night/shoot1.sh c99 bin/nt_c99.exe "2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night 3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night" --exposurelog
echo done > /tmp/night/chain_c99.done
