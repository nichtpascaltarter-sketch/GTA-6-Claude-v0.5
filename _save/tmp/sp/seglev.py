import sys, numpy as np, soundfile as sf
# usage: seglev.py wav segs.txt PH1 PH2 ... : absolute band levels of the middle part of the first occurrence (after the carrier start)
x, sr = sf.read(sys.argv[1])
segs=[l.split() for l in open(sys.argv[2])]
want=sys.argv[3:]
bands=[(0,1000),(1000,2500),(2500,5000),(5000,8000)]
used=set()
for w in want:
    for i,s in enumerate(segs):
        if s[0]==w and i not in used and float(s[1])>0.6:
            used.add(i); t0=float(s[1]); d=float(s[2]); break
    else: continue
    a=t0+0.3*d; b=t0+0.9*d
    y=x[int(a*sr):int(b*sr)]; n=4096
    Y=np.abs(np.fft.rfft(y*np.hanning(len(y)),n))**2/len(y); f=np.fft.rfftfreq(n,1/sr)
    print("%-3s %.3f-%.3f rms %5.1f dB |"%(w,a,b,10*np.log10(np.mean(y**2)+1e-20)), " ".join("%5.1f"%(10*np.log10(Y[(f>=lo)&(f<hi)].sum()+1e-20)) for lo,hi in bands))
