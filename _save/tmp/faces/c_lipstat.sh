#!/bin/sh
# usage: c_lipstat.sh <tree> <outbin>
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
cd $1 && nice -n 5 g++ -O2 -std=c++17 -w -I src -I . /tmp/faces/tools/lipstat.cpp -o $2 2>&1 | grep error | head -5
