import sys, numpy as np, soundfile as sf
x, sr = sf.read(sys.argv[1]); segs=[l.split() for l in open(sys.argv[2])]
for s in segs:
    name,t0,du=s[0],float(s[1]),float(s[2])
    if name=='SIL': continue
    a,b=int((t0+0.3*du)*sr),int((t0+0.7*du)*sr); y=x[a:b]
    n=8192; Y=20*np.log10(np.abs(np.fft.rfft(y*np.hanning(len(y)),n))+1e-9); f=np.fft.rfftfreq(n,1/sr)
    # envelope: max in 250-Hz bins
    env=[]
    for lo in range(0,8000,250):
        m=(f>=lo)&(f<lo+250); env.append(Y[m].max())
    env=np.array(env); env-=env.max()
    print(name, " ".join("%4.0f"%v for v in env[:28]))
print("bins(kHz):", " ".join("%4.2f"%(i*0.25) for i in range(28)))
