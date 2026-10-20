#!/bin/sh
# 10-20 room-light agreement test: build nt_rl1 (tree rl = HEAD + city's roomlight.h [unlanded] + roomlight.hlsli,
# roomlighttest.hlsl, render/roomlight_test.cpp, renderer.cpp hook), then one short run with --roomlighttest
[ -f /tmp/night/build_rl1.done ] || sh /tmp/night/buildb.sh rl1 /tmp/night/rl
grep -q "rc 0" /tmp/night/build_rl1.done || { echo build-failed > /tmp/night/chain_rl.done; exit 1; }
until [ -f /tmp/night/chain_r.done ]; do sleep 60; done
GATE=3000 W=640 H=360 Q=0 SETTLE=2 SHOTS="3168.00,-242.90,3.56,-70.00,8.00,21.50,rltest" EXE=bin/nt_rl1.exe sh /tmp/night/shoot.sh rlt --roomlighttest
grep "room-light self-test" /tmp/night/shots/log_rlt.txt > /tmp/night/rltest_result.txt
echo done > /tmp/night/chain_rl.done
