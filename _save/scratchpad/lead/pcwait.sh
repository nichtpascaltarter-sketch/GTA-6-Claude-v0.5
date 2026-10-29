#!/bin/sh
# Wait on a PC package. usage: pcwait.sh <id> <start-deadline-epoch> [<done-deadline-epoch>]
# Prints one line when it starts, then exits on done, on no start by the deadline, or on the done deadline.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
id=$1; sdl=$2; ddl=${3:-$(( $2 + 7200 ))}; said=
while :; do
  out=$(sh $SP/pc/fetch_result.sh $id 2>&1)
  case "$out" in
    *": done"*) echo "$(date -u +%H:%M) $out"; exit 0;;
    *": started "*) [ -z "$said" ] && { echo "$(date -u +%H:%M) $out"; said=1; };;
  esac
  now=$(date +%s)
  if [ -z "$said" ] && [ $now -gt $sdl ]; then echo "$(date -u +%H:%M) NOT STARTED by deadline: $out"; exit 3; fi
  [ $now -gt $ddl ] && { echo "$(date -u +%H:%M) TIMEOUT: $out"; exit 4; }
  sleep 30
done
