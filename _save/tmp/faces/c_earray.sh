#!/bin/sh
# usage: c_earray.sh TREE BIN  -- gate (host compile room), build the ear see-through tool against TREE
sh /tmp/faces/gate.sh 2000
cd "$1" && nice -n 5 g++ -O2 -std=c++17 -w -I src -I . /tmp/faces/tools/earray.cpp -o "$2" 2>&1 | grep -m3 error
echo "built $2 $(date +%T)"
