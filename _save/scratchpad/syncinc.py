# Sync the world include list of tools/worldcheck.cpp and the scratch tools with src/main.cpp
import re
SP='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad'
main=open('src/main.cpp').read()
world=re.findall(r'#include "(world/[a-z_]+\.cpp)"', main)
def patch(path, prefix, stub):
    s=open(path).read()
    lines=s.split('\n')
    idx=[i for i,l in enumerate(lines) if re.match(r'#include "(\.\./)?src/world/', l)]
    a,b=idx[0],idx[-1]
    new=['#include "%ssrc/%s"'%(prefix,w) for w in world]
    lines=lines[:a]+new+lines[b+1:]
    open(path,'w').write('\n'.join(lines))
patch('tools/worldcheck.cpp','../',None)
for f in ['streetprof.cpp','siteview.cpp','roadat.cpp','protocount2.cpp','roadcheck.cpp']:
    patch(SP+'/'+f,'',None)
print(len(world),'world files')
