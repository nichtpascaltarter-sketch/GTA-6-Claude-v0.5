import re, sys, statistics as st
# kitcmp2.py <tagA> <tagB>: the kit sweeps r11/kit_<tag>_{iv5,pace}.txt compared per family and way
R='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco/r11/'
def load(f):
    d={}
    for line in open(f):
        m=re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)]=float(m.group(2))
    return d
a,b=sys.argv[1],sys.argv[2]
for p in ('iv5','pace'):
    A=load(R+'kit_%s_%s.txt'%(a,p)); B=load(R+'kit_%s_%s.txt'%(b,p))
    fams=sorted(set(k.split('.')[1] for k in A if k.startswith('kit.')))
    for fam in fams:
        for way in ('up','down'):
            keys=sorted(k[:-len('.planted_sunk_3cm_pct')] for k in A if k.startswith('kit.'+fam+'.') and k.endswith('.'+way+'.planted_sunk_3cm_pct') and k in B)
            if not keys: continue
            sa=[A[k+'.planted_sunk_3cm_pct'] for k in keys]; sb=[B[k+'.planted_sunk_3cm_pct'] for k in keys]
            la=[A.get(k+'.planted_lifted_pct',0) for k in keys]; lb=[B.get(k+'.planted_lifted_pct',0) for k in keys]
            ma=max(A.get(k+'.slide_cm_step_max',0) for k in keys); mb=max(B.get(k+'.slide_cm_step_max',0) for k in keys)
            fa=sum(A.get(k+'.planted_float_frames',0) for k in keys); fb=sum(B.get(k+'.planted_float_frames',0) for k in keys)
            better=sum(1 for x,y in zip(sa,sb) if y<x-0.5); worse=sum(1 for x,y in zip(sa,sb) if y>x+0.5)
            same=sum(1 for k in keys if all(abs(A.get(k+'.'+m,0)-B.get(k+'.'+m,0))<1e-6 for m in ('planted_sunk_3cm_pct','planted_lifted_pct','slide_cm_step_max','planted_float_frames')))
            print('%-4s %-10s %-4s n%3d same %3d | sk3 %5.2f -> %5.2f | lifted %4.2f -> %4.2f | slmax %5.1f -> %5.1f | floats %5d -> %5d | better %d worse %d'%(p,fam,way,len(keys),same,st.mean(sa),st.mean(sb),st.mean(la),st.mean(lb),ma,mb,fa,fb,better,worse))
