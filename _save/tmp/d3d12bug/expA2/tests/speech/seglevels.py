#!/usr/bin/env python3
"""Per-segment level (dB re loudest vowel), spectral peak and centroid from segs.txt (analysis only).
Usage: python seglevels.py segs.wav segs.txt"""
import sys
import numpy as np
import soundfile as sf
x, sr = sf.read(sys.argv[1], dtype="float64")
segs = [l.split() for l in open(sys.argv[2])]
rows = []
for s in segs:
    name, t0, du = s[0], float(s[1]), float(s[2])
    if name == "SIL":
        continue
    a, b = int((t0 + 0.25 * du) * sr), int((t0 + 0.75 * du) * sr)
    if b - a < 32:
        continue
    y = x[a:b]
    rms = np.sqrt(np.mean(y * y)) + 1e-12
    Y = np.abs(np.fft.rfft(y * np.hanning(len(y)), 4096)) ** 2
    f = np.fft.rfftfreq(4096, 1 / sr)
    m = f > 300
    cen = np.sum(f[m] * Y[m]) / np.sum(Y[m])
    pk = f[m][np.argmax(Y[m])]
    lo = 10 * np.log10(np.sum(Y[(f > 0) & (f < 1000)]) + 1e-20)
    hi = 10 * np.log10(np.sum(Y[(f > 3000)]) + 1e-20)
    rows.append((name, 20 * np.log10(rms), cen, pk, hi - lo, t0))
ref = max(r[1] for r in rows)
for name, db, cen, pk, tilt, t0 in rows:
    print("%-4s t=%.2f  level %6.1f dB  centroid %5.0f Hz  peak %5.0f Hz  hi-lo %6.1f dB" % (name, t0, db - ref, cen, pk, tilt))
