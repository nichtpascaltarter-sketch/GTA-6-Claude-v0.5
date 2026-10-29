#!/usr/bin/env python3
# Snapshot entries from signed lists ("path blob ..."), the last list winning, KEEPING each path's mode in the base
# (a new path gets 100644). usage: mkentries.py BASE LIST [LIST...] > entries.txt
import subprocess, sys
base, lists = sys.argv[1], sys.argv[2:]
modes = {}
out = subprocess.run(['git', '-C', '/home/user/GTA-6-Claude-v0.5', 'ls-tree', '-r', base], capture_output=True, text=True, check=True).stdout
for line in out.splitlines():
    meta, path = line.split('\t', 1)
    modes[path] = meta.split()[0]
e = {}
for L in lists:
    for line in open(L):
        p = line.split()
        if len(p) >= 2: e[p[0]] = p[1]
for p in sorted(e): print(f"{modes.get(p, '100644')} blob {e[p]}\t{p}")
