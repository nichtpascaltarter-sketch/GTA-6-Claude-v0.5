#!/usr/bin/env python3
# apply_v3f.py <app.cpp>...: the locoplayer block v3e -> v3f (the chasers) in test trees' app.cpp - test builds only
import sys
T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
old=open(T+'block_player_v3e.cpp').read(); new=open(T+'block_player_v3f.cpp').read()
for p in sys.argv[1:]:
    s=open(p).read()
    if s.count(new)==1: print(p, 'has v3f'); continue
    if s.count(old)!=1: raise SystemExit('%s: v3e block found %d times' % (p, s.count(old)))
    open(p,'w').write(s.replace(old,new)); print(p, 'v3e -> v3f')
