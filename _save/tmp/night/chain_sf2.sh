#!/bin/sh
# waits for the (orphaned) nt_sf build, then runs its self-test after chain_v4 has finished everything (chain_g.done)
until [ -f /tmp/night/build_sf.done ]; do sleep 60; done
until [ -f /tmp/night/chain_g.done ]; do sleep 60; done
[ -f /tmp/night/log_sf_selftest.txt ] || sh /tmp/night/selftest.sh sf bin/nt_sf.exe
echo done > /tmp/night/chain_sf.done
