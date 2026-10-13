#!/bin/sh
# tour stop 8 (downtown rain night) for A and B: tb was OOM-killed before it
sh /tmp/night/tour.sh ta8 bin/nt_a1.exe 8 1
sh /tmp/night/tour.sh tb8 bin/nt_b3.exe 8 1
echo done > /tmp/night/chain_t8.done
