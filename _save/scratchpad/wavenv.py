import numpy as np, sys, wave
def load(p):
    w = wave.open(p); n = w.getnframes(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32767.0
    return x.reshape(-1, ch)
for p in sys.argv[1:]:
    x = load(p); m = np.sqrt((x**2).mean(axis=1))
    sr = 48000
    t1 = float(sys.argv[0] and 0.15)
    print(p.split('/')[-1], "len %.2fs peak %.3f" % (len(x)/sr, np.abs(x).max()))
    # envelope in 3 ms windows, dB
    win = int(0.003*sr)
    env = [20*np.log10(np.sqrt((x[i:i+win]**2).mean())+1e-9) for i in range(0, min(len(x), int(0.3*sr)), win)]
    print(" ".join("%4.0f" % e for e in env))
