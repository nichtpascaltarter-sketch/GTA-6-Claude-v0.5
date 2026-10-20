#!/usr/bin/env python3
# apply_blk2.py <version> <app.cpp>...: whatever locoplayer block is in a test tree's app.cpp (from its first line to the
# locopush block) -> block_player_<version>.cpp - test builds only
import sys
T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
new=open(T+'block_player_%s.cpp' % sys.argv[1]).read()
A='    // ---- --autoplay locoplayer (locomotion test build only)'
B='    // ---- --autoplay locopush (locomotion test build only)'
for p in sys.argv[2:]:
    s=open(p).read()
    assert s.count(A)==1 and s.count(B)==1, p
    i=s.index(A); j=s.index(B); assert i<j
    if s[i:j]==new: print(p, 'has', sys.argv[1]); continue
    open(p,'w').write(s[:i]+new+s[j:]); print(p, '->', sys.argv[1])
