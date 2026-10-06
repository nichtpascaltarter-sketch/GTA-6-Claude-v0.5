#!/bin/bash
# the final batch 8 tree (dev10 = frontage fill, conch bays, Keys motel split, mobile homes): citylab build, stats, perf,
# views; worldcheck
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/final10.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
st "start build"
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -I$S/dev10 citylab.cpp -o citylab_dev10 -lpthread > /tmp/city/clab10_build.log 2>&1
rc=$?; st "citylab build rc=$rc"; [ $rc -eq 0 ] || exit 1
cd /home/user/GTA-6-Claude-v0.5
timeout 900 nice -n 5 $S/citylab/citylab_dev10 --logmetric --stats --openlots 0,0,1 > /tmp/city/clab10_stats.txt 2>&1
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev10 /tmp/city/clab10_perf.txt
timeout 900 nice -n 5 $S/citylab/citylab_dev10 --farbreak > /tmp/city/clab10_far.txt 2>&1
mkdir -p $S/city/clay_b11f; timeout 900 nice -n 5 $S/citylab/citylab_dev10 --views $S/citylab/views_trailer.txt $S/city/clay_b11f > /dev/null 2>&1
st "stats perf done"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev10 worldcheck.cpp -o wc_dev10 -lpthread > /tmp/city/wc_dev10_build.log 2>&1
rc=$?; st "wc build rc=$rc"; [ $rc -eq 0 ] || exit 1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 3000 nice -n 5 $S/wc/wc_dev10 12 > $S/wc/dev10.txt 2>&1
echo "exit $?" >> $S/wc/dev10.txt
st "wc done: $(grep -c 'worldcheck: passed' $S/wc/dev10.txt) passed"
