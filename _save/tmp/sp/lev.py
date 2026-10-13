import sys, numpy as np, soundfile as sf
# usage: lev.py wav a:b ...  -> absolute band levels (dB) in 0-1k, 1-2.5k, 2.5-5k, 5-8k
x, sr = sf.read(sys.argv[1])
for rng in sys.argv[2:]:
    a,b=[float(v) for v in rng.split(':')]
    y=x[int(a*sr):int(b*sr)]; n=4096
    Y=np.abs(np.fft.rfft(y*np.hanning(len(y)),n))**2/len(y); f=np.fft.rfftfreq(n,1/sr)
    bands=[(0,1000),(1000,2500),(2500,5000),(5000,8000)]
    print(rng, "rms %5.1f dB |"%(10*np.log10(np.mean(y**2)+1e-20)), " ".join("%5.1f"%(10*np.log10(Y[(f>=lo)&(f<hi)].sum()+1e-20)) for lo,hi in bands))
