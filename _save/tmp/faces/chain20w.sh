#!/bin/sh
# wait for the shadow debug run to finish, stop chain19 before its albedo run, then start chain20
until grep -q "^exit" /tmp/faces/shoot_dbg4.out 2>/dev/null; do sleep 10; done
CP=$(pgrep -f "/tmp/faces/chain19.sh" | head -1)
[ -n "$CP" ] && kill $CP
sleep 2
# (dbg1 may have started already: stop only its shoot2 / run.sh / game, which are mine)
for p in $(pgrep -f "shoot2.sh dbg1"); do pkill -P $p 2>/dev/null; kill $p 2>/dev/null; done
/tmp/faces/chain20.sh
