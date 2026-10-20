#!/bin/sh
export PREFIX=/tmp/wine_fx_base EXE=bin/nt_fx_base.exe
SHOTS="1800,1150,5.2,0,0,12,warm" /tmp/fx/shoot.sh bwarm >/dev/null 2>&1
SHOTS="3400,-320,3.5,0,2,12,dt_noon 3400,-320,3.5,0,2,18.6,dt_gold 3400,-320,3.5,0,2,22,dt_night 5330,0,4.2,-90,-3,12,beach_noon -6000,-1000,2.4,30,-4,11,saw_noon -6500,6500,96.5,0,-5,11,forest_noon 1800,1100,5.2,0,0,16,flats" /tmp/fx/shoot.sh base --gputimers
cp /tmp/wine_fx_base/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/base_log.txt
SHOTS="3400,-320,3.5,0,2,21,dt_rain 1800,1100,5.2,0,0,12,flats_rain" /tmp/fx/shoot.sh base --rain 1
