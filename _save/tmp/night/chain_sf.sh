#!/bin/sh
# 10-20 final sign-off tree (HEAD 837b39c + post.hlsl, lights.hlsli, dynamic.hlsl, gameworld.cpp, PROGRESS.md): build
# (compile check on the current HEAD), then its self-test once chain_v4's sign-off runs are done (one game at a time)
[ -f /tmp/night/build_sf.done ] || sh /tmp/night/buildb.sh sf /tmp/night/sf
until [ -f /tmp/night/chain_v.done ]; do sleep 60; done
[ -f /tmp/night/log_sf_selftest.txt ] || sh /tmp/night/selftest.sh sf bin/nt_sf.exe
echo done > /tmp/night/chain_sf.done
