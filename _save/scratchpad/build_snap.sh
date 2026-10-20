#!/bin/sh
# Builds the vehicle test harness against the last committed tree (HEAD) with the working-tree vehicle sim + tests
# overlaid, so other agents' in-progress files cannot break the link.
set -e
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
SNAP=$S/snap
OUT=${1:-$S/bin/tv}
rm -rf "$SNAP"
mkdir -p "$SNAP"
cd "$REPO"
git archive HEAD src tools tests | tar -x -C "$SNAP"
cp src/sim/vehicle_sim.cpp src/sim/vehicle_sim.h src/sim/vehicle_sim_*.cpp "$SNAP/src/sim/"
cp tests/vehicle/*.cpp tests/vehicle/*.h tests/vehicle/*.sh "$SNAP/tests/vehicle/"
cd "$SNAP"
nice -n 10 sh tests/vehicle/build.sh "$OUT"
