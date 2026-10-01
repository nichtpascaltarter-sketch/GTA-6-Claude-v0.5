#!/bin/sh
# current night look: tour stops 7 (club night) and 8 (downtown rain night) with the HEAD build
until [ -f /tmp/fx/c_apron.done ]; do sleep 20; done
EXE=bin/nt_d12c.exe P=c_ts7 N=7 sh /tmp/fx/tour_stop.sh
EXE=bin/nt_d12c.exe P=c_ts8 N=8 sh /tmp/fx/tour_stop.sh
echo done > /tmp/fx/c_night.done
