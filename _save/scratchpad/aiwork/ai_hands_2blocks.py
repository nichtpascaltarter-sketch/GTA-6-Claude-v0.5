# AI agent: paste the AI hand blocks (couple hand in hand, the reach) into a frozen tree's peds.cpp, before the foot IK
# block of GameWorld::animatePed - for test builds only (the lead grafts them onto the real peds.cpp). Usage: ai_hands.py PEDS_CPP
import sys
AW='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork'
p=sys.argv[1]
s=open(p).read()
anchor="""    // foot IK: probe the ground where each foot is planted / about to land (Animator::footProbe) and the slope under"""
if s.count(anchor)!=1:
    print("ai_hands: anchor not found once in", p); sys.exit(1)
if 'PedAI::handWith on both' in s and 'PedAI::reachAt while reachT' in s:
    print("ai_hands: already there"); sys.exit(0)
blocks=''
if 'PedAI::handWith on both' not in s: blocks+=open(AW+'/couple_block.txt').read()
if 'PedAI::reachAt while reachT' not in s: blocks+=open(AW+'/reach_block.txt').read()
s=s.replace(anchor,blocks+anchor)
open(p,'w').write(s)
print("ai_hands: pasted into", p)
