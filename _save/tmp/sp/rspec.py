import sys, numpy as np, soundfile as sf
x, sr = sf.read(sys.argv[1])
for rng in sys.argv[2:]:
    a,b=[float(v) for v in rng.split(':')]
    y=x[int(a*sr):int(b*sr)]; n=4096
    Y=np.abs(np.fft.rfft(y*np.hanning(len(y)),n))**2; f=np.fft.rfftfreq(n,1/sr)
    out=[]
    for lo in range(0,10500,500):
        m=(f>=lo)&(f<lo+500); out.append(10*np.log10(Y[m].mean()+1e-20))
    out=np.array(out); out-=out.max()
    print(rng, " ".join("%4.0f"%v for v in out))
print("kHz:      ", " ".join("%4.1f"%(i*0.5) for i in range(21)))
