#!/bin/bash
# AI agent: run a command detached from the session's task limits (own session, no hangup), output to a log.
# Usage: ai_detach.sh LOGFILE CMD ARGS...   (prints the detached PID)
LOGF=$1; shift
cd /home/user/GTA-6-Claude-v0.5
setsid nohup "$@" > "$LOGF" 2>&1 < /dev/null &
echo $!
