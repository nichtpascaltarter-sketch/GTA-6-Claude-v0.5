import sys,collections
# usage: ievreg.py <iev.txt>...: lifted (> 1.5 cm) / sunk (> 3 cm) planted frames by region (switchback: landing y > top, flights) and scenario
for f in sys.argv[1:]:
    c=collections.Counter()
    for ln in open(f):
        x=ln.split()
        if not x or x[0]!='IEV': continue
        sc=x[1].split('_1.0')[0]; low=float(x[9])
        i=x.index('at'); y=float(x[i+2]); xx=float(x[i+1].rstrip(','))
        top=2+9*0.28 if 'tight' not in sc else 2+9*0.27
        reg='land' if y>top else 'flight'
        if low>1.5: c[(sc,reg,'lift')]+=1
        if low<-3: c[(sc,reg,'sunk')]+=1
    print(f.split('/')[-1], ' '.join('%s:%s:%s=%d'%(k[0].replace('switchback','sb'),k[1],k[2],v) for k,v in sorted(c.items())))
