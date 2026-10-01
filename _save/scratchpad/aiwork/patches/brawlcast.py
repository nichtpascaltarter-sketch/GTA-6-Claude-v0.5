p='/home/user/GTA-6-Claude-v0.5/src/game/events.cpp'
s=open(p).read()
old='''hash32((u32)slot + (u32)e.pos.x)'''
new='''hash32((u32)slot + (u32)(int)e.pos.x)'''
assert s.count(old)==1; s=s.replace(old,new)
open(p,'w').write(s)
print("cast patched")
