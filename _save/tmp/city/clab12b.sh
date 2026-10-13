#!/bin/bash
# batch 9 (house forms) in dev12: after my main 8fb1e66 build, the citylab build with the forms, then --forms near
# Okahatchee, Harlow and the Grove
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab12b.status; }
until grep -q "m8 build rc" /tmp/city/jobs10.status 2>/dev/null; do sleep 15; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
st "citylab build"
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/dev12 citylab.cpp -o citylab_dev12 -lpthread > /tmp/city/clab12b_build.log 2>&1
rc=$?; st "citylab build rc=$rc"; [ $rc -eq 0 ] || exit 1
cd /home/user/GTA-6-Claude-v0.5
timeout 900 nice -n 5 $S/citylab/citylab_dev12 --forms 239,6735 --forms -4795,5200 --forms 1963,-3500 > /tmp/city/clab12b_forms.txt 2>&1
st "forms done"
