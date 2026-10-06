import re,sys
def mag(path):
    f=open(path,'rb').read()
    m=re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s',f)
    w,h=int(m.group(1)),int(m.group(2)); data=f[m.end():]
    pts=[]
    for y in range(0,h,2):
        for x in range(0,w,2):
            i=(y*w+x)*3
            r,g,b=data[i],data[i+1],data[i+2]
            if r>200 and b>150 and g<140: pts.append((x,y))
    return pts
pts=mag(sys.argv[1])
print(len(pts), pts[:3], pts[len(pts)//2:len(pts)//2+2], pts[-2:])
