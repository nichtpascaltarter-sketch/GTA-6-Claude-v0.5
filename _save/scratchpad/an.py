import numpy as np, sys, wave
def load(p):
    w = wave.open(p); n = w.getnframes(); ch = w.getnchannels()
    d = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32767.0
    return d.reshape(-1, ch), w.getframerate()
def bands(x, sr):
    m = x.mean(axis=1)
    N = 8192
    segs = [m[i:i+N]*np.hanning(N) for i in range(0, len(m)-N, N//2)]
    P = np.mean([np.abs(np.fft.rfft(s))**2 for s in segs], axis=0)
    f = np.fft.rfftfreq(N, 1/sr)
    edges = [20, 60, 150, 400, 1000, 2500, 6000, 12000, 20000]
    tot = P.sum()
    out = []
    for a, b in zip(edges[:-1], edges[1:]):
        out.append(10*np.log10(P[(f>=a)&(f<b)].sum()/tot + 1e-12))
    cent = (P*f).sum()/P.sum()
    return out, cent
for p in sys.argv[1:]:
    x, sr = load(p)
    b, c = bands(x, sr)
    rms = 20*np.log10(np.sqrt((x**2).mean())+1e-12)
    # loudness over 1s windows
    w = sr
    lv = [20*np.log10(np.sqrt((x[i:i+w]**2).mean())+1e-9) for i in range(0, len(x)-w, w)]
    corr = np.corrcoef(x[:,0], x[:,1])[0,1]
    print(f"{p}: rms {rms:.1f} dB, centroid {c:.0f} Hz, L/R corr {corr:.2f}, bands(20-60,60-150,150-400,400-1k,1-2.5k,2.5-6k,6-12k,12-20k) " + " ".join(f"{v:.0f}" for v in b))
    print("   1s levels:", " ".join(f"{v:.0f}" for v in lv[:30]))
