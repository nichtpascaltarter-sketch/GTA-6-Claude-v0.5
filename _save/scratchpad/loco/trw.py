import re,sys
# usage: trw.py <trace.err> <t0> <t1> <foot> : the traced run's frames (root, the foot) with the animator's swing / scan prints
L=open(sys.argv[1]).read().split('\n')
t0,t1,ft=float(sys.argv[2]),float(sys.argv[3]),sys.argv[4]
buf=[]
for ln in L:
    if ln.startswith('0 '):
        m=re.search(r't(\d+\.\d+) L(\d+) yaw\s*(-?[\d.]+) tr\s*(-?[\d.]+) v([\d.]+).*rp (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)',ln)
        if not m: buf=[]; continue
        t=float(m.group(1))
        if t0<=t<=t1:
            parts=ln.split(' | ')
            f=parts[0] if ft=='0' else parts[2]
            mm=re.search(r'pl(\d) st(-?[\d.]+) h\s*(-?[\d.]+) b\s*(-?[\d.]+) t\s*(-?[\d.]+)',f); hw=re.search(r'hw(-?[\d.]+),(-?[\d.]+)',f); fy=re.search(r'fy\s*(-?[\d.]+)',f)
            print('t%.3f yaw%6s v%s root %s %s | f%s pl%s h%s b%s t%s hw %s %s fy %s'%(t,m.group(3),m.group(5),m.group(6),m.group(7),ft,mm.group(1),mm.group(3),mm.group(4),mm.group(5),hw.group(1),hw.group(2),fy.group(1)))
            for b in buf: print('     ',b[:210])
        buf=[]
    elif ln.startswith('WGSW foot '+ft) or ln.startswith('WGSCAN foot '+ft) or ln.startswith('WGNONE foot '+ft):
        buf.append(ln)
