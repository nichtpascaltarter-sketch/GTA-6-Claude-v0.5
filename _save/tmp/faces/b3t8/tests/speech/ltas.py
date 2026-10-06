#!/usr/bin/env python3
"""Long-term average spectrum band levels (dB re total) for comparing synthetic and natural speech.
Only frames within 30 dB of the loudest frame are used. Usage: python ltas.py a.wav [b.wav ...]"""
import sys
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

bands = [(50, 300), (300, 600), (600, 1200), (1200, 2400), (2400, 4000), (4000, 5500), (5500, 7800)]
def ltas(path):
    x, sr = sf.read(path, dtype="float64")
    if x.ndim > 1: x = x[:, 0]
    if sr != 16000:
        x = resample_poly(x, 16000, sr); sr = 16000
    n = 512
    fr = np.array([x[i:i + n] * np.hanning(n) for i in range(0, len(x) - n, n // 2)])
    e = np.sum(fr ** 2, axis=1)
    fr = fr[e > e.max() * 1e-3]
    P = np.mean(np.abs(np.fft.rfft(fr, axis=1)) ** 2, axis=0)
    f = np.fft.rfftfreq(n, 1 / sr)
    tot = np.sum(P)
    return [10 * np.log10(np.sum(P[(f >= a) & (f < b)]) / tot + 1e-20) for a, b in bands]
print("%-40s" % "file" + "".join("%9s" % ("%d-%d" % b) for b in bands))
for p in sys.argv[1:]:
    print("%-40s" % p[-40:] + "".join("%9.1f" % v for v in ltas(p)))
