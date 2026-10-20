import sys,re,os
# usage: sbwcmp.py <binA> <binB> ... : the wall metrics and sinks per suite/speed, side by side
L='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco/r11/'
pre='sbw'
args=sys.argv[1:]
if args and args[0]=='-g': pre='sbg'; args=args[1:]
bins=args
keys=['planted_in_wall_frames','planted_in_wall_3cm_frames','planted_in_wall_max_cm','planted_deep_frames','planted_sunk_3cm_pct','planted_lifted_pct']
short={'planted_in_wall_frames':'wall','planted_in_wall_3cm_frames':'wall3','planted_in_wall_max_cm':'wmax','planted_deep_frames':'deep','planted_sunk_3cm_pct':'sunk3','planted_lifted_pct':'lift'}
def load(f):
    d={}
    if not os.path.exists(f): return d
    for ln in open(f):
        m=re.match(r'M (\S+)\s+(-?[\d.]+)',ln)
        if m: d[m.group(1)]=float(m.group(2))
    return d
for w in ('1','0'):
    for v in ('1.55','4.1','6.9'):
        D=[load(L+'%s%s_%s_%s.txt'%(pre,w,b,v)) for b in bins]
        for sc in ('switchback.up_1.0','switchback.down_1.0','switchback_tight.up_1.0','switchback_tight.down_1.0'):
            row='w%s %-4s %-26s'%(w,v,sc)
            for b,d in zip(bins,D):
                row+=' | '+' '.join('%s %5.1f'%(short[k], d.get(sc+'.'+k,-1)) for k in keys if (w=='1' or 'wall' not in k))
            print(row)
