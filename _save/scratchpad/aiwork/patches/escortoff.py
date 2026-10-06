p='/home/user/GTA-6-Claude-v0.5/src/game/police.cpp'
s=open(p).read()
old='''        // behind them, a little to the right: the hand on the arm
        vec2 sf = AI::yawDir(sp.yaw);
        vec2 goal = spos - sf * 0.68f + AI::rightOf(sf) * 0.28f;'''
new='''        // behind them, a little to the right: the left hand on their right upper arm (peds.cpp feeds the animator's hold;
        // 0.56 m back and 0.3 m over keeps them just outside the ped separation)
        vec2 sf = AI::yawDir(sp.yaw);
        vec2 goal = spos - sf * 0.56f + AI::rightOf(sf) * 0.3f;'''
assert s.count(old)==1; s=s.replace(old,new)
open(p,'w').write(s)
print("escort offset patched")
