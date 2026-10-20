#!/usr/bin/env python3
# apply_blk.py <to> <app.cpp>...: the locoplayer block in test trees' app.cpp -> block_player_<to>.cpp, from whichever
# known version (v3e, v3f, v3g) is there - test builds only
import sys
T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
to=sys.argv[1]; new=open(T+'block_player_%s.cpp' % to).read()
for p in sys.argv[2:]:
    s=open(p).read()
    if s.count(new)==1: print(p, 'has', to); continue
    for v in ('v3e','v3f','v3g'):
        old=open(T+'block_player_%s.cpp' % v).read()
        if v!=to and s.count(old)==1:
            open(p,'w').write(s.replace(old,new)); print(p, v, '->', to); break
    else: raise SystemExit('%s: no known block version found' % p)
