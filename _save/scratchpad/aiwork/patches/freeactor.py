p='/home/user/GTA-6-Claude-v0.5/src/game/events.cpp'
s=open(p).read()
old='''    pa.aimAt = -1;
    pa.eventId = -1;
    pa.navOk = false;
    if (p.brain.type == BRAIN_GOTO) {
        p.brain.type = BRAIN_WANDER;
        p.brain.edge = -1;
    }
}'''
new='''    pa.aimAt = -1;
    pa.eventId = -1;
    pa.navOk = false;
    if (p.brain.type == BRAIN_GOTO && !(p.faction == FAC_POLICE && (p.brain.target == -2 || p.brain.target == -3))) {   // (an officer back to the
        p.brain.type = BRAIN_WANDER;                                                                                    //  car / walking a prisoner
        p.brain.edge = -1;                                                                                              //  carries on)
    }
}'''
assert s.count(old)==1; s=s.replace(old,new)
open(p,'w').write(s)
print("freeActor patched")
