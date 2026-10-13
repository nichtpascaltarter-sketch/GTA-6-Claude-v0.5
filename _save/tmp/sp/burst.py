import sys, numpy as np, soundfile as sf
x, sr = sf.read(sys.argv[1]); segs=[l.split() for l in open(sys.argv[2])]
bur={'P':6,'B':5,'T':10,'D':8,'K':14,'G':12}
def bands(y):
    n=2048; Y=np.abs(np.fft.rfft(y*np.hanning(len(y)),n))**2; f=np.fft.rfftfreq(n,1/sr)
    return [10*np.log10(np.sum(Y[(f>=a)&(f<b)])/max(1,len(y))+1e-20) for a,b in [(300,1000),(1000,2000),(2000,3500),(3500,5500),(5500,8000)]]
for i,s in enumerate(segs):
    if s[0] not in bur: continue
    t0,du=float(s[1]),float(s[2]); t1=t0+du
    b=x[int((t1-bur[s[0]]/1000)*sr):int((t1+0.002)*sr)]
    # env in 1 ms steps
    env=[20*np.log10(np.sqrt(np.mean(x[int((t1-0.02+k*0.001)*sr):int((t1-0.019+k*0.001)*sr)]**2))+1e-9) for k in range(30)]
    nx=segs[i+1]; v0=float(nx[1])+float(nx[3]) if len(nx)>3 else float(nx[1])
    v=x[int((v0+0.005)*sr):int((v0+0.025)*sr)]
    bb=bands(b); vb=bands(v)
    print(s[0], "t1=%.3f"%t1, "burst-vowel by band:", " ".join("%5.1f"%(p-q) for p,q in zip(bb,vb)), " burst rms %.1f vowel rms %.1f"%(20*np.log10(np.sqrt(np.mean(b**2))+1e-9), 20*np.log10(np.sqrt(np.mean(v**2))+1e-9)))
    print("   env(1ms, t1-20..t1+10):", " ".join("%.0f"%e for e in env))
