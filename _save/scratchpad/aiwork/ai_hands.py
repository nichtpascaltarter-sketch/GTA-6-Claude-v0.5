# AI agent: paste the AI blocks for peds.cpp (couple hand in hand, the reach, the in-car look) into a frozen tree's
# peds.cpp, before the foot IK block of GameWorld::animatePed - for test builds only (the lead grafts them onto the real
# peds.cpp). Each block is skipped when its marker is already there. Usage: ai_hands2.py PEDS_CPP
import sys
AW='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork'
p=sys.argv[1]
s=open(p).read()
anchor="""    // foot IK: probe the ground where each foot is planted / about to land (Animator::footProbe) and the slope under"""
if s.count(anchor)!=1:
    print("ai_hands: anchor not found once in", p); sys.exit(1)
blocks=''
for marker, f in (('PedAI::handWith on both', 'couple_block.txt'), ('PedAI::reachAt while reachT', 'reach_block.txt'), ('VehAI::gawkT is ahead', 'look_block.txt')):
    if marker not in s: blocks+=open(AW+'/'+f).read()
if not blocks:
    print("ai_hands: already there"); sys.exit(0)
s=s.replace(anchor,blocks+anchor)
open(p,'w').write(s)
print("ai_hands: pasted into", p)
