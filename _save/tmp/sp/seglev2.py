import sys, numpy as np, soundfile as sf
x, sr = sf.read(sys.argv[1]); segs=[l.split() for l in open(sys.argv[2])]
out=[]
for s in segs:
    if s[0]=="SIL": continue
    t0=float(s[1]); d=float(s[2]); a=t0+0.25*d; b=t0+0.85*d
    y=x[int(a*sr):int(b*sr)]
    if len(y)<16: continue
    out.append("%s:%.0f" % (s[0], 10*np.log10(np.mean(y**2)+1e-12)))
print(" ".join(out))
