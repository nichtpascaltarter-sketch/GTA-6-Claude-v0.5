#!/bin/bash
# AI agent: harness probes of node 2478 (Sol Beach deadlock) and its connectors, one process each, behind the gate
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
R=$SP/aiwork/ts_run.sh
$R $SP/ts/node2478.txt --node 2478
$R $SP/ts/conn19259.txt --conn 19259
$R $SP/ts/conn19596.txt --conn 19596
$R $SP/ts/conn19260.txt --conn 19260
echo done > $SP/ts/probe.done
