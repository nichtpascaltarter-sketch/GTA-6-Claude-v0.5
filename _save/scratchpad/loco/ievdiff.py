import sys,collections
# usage: ievdiff.py <a.txt> <b.txt> [n]: per run and foot, lifted (> 1.5 cm) and sunk (> 3 cm) planted frames on the stairs, a vs b
def load(f):
    d=collections.defaultdict(lambda:[0,0.0,0,0.0,''])
    for ln in open(f):
        x=ln.split()
        if not x or x[0]!='IEV': continue
        low=float(x[9]); key=(x[1],int(x[4]),int(x[7]))
        if low>1.5:
            if not d[key][4]: d[key][4]=x[5]
            d[key][0]+=1; d[key][1]=max(d[key][1],low)
        if low<-3: d[key][2]+=1; d[key][3]=min(d[key][3],low)
    return d
A=load(sys.argv[1]); B=load(sys.argv[2]); n=int(sys.argv[3]) if len(sys.argv)>3 else 25
keys=set(A)|set(B)
diff=[]
for k in keys:
    a=A.get(k,[0,0,0,0,'']); b=B.get(k,[0,0,0,0,''])
    if a[0]!=b[0] or a[2]!=b[2]: diff.append((b[0]-a[0]+0.5*(b[2]-a[2]), k, a, b))
diff.sort(key=lambda x:-x[0])
ta=[sum(v[0] for v in A.values()), sum(v[2] for v in A.values())]; tb=[sum(v[0] for v in B.values()), sum(v[2] for v in B.values())]
print('totals: a lift %d sunk %d | b lift %d sunk %d'%(ta[0],ta[1],tb[0],tb[1]))
for dd,k,a,b in diff[:n]:
    print('%-26s run %3d foot %d | a lift %2d max %5.1f sunk %2d min %6.1f | b lift %2d max %5.1f sunk %2d min %6.1f first %s'%(k[0],k[1],k[2],a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3],b[4]))
