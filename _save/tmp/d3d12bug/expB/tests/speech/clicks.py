#!/usr/bin/env python3
"""Click / discontinuity detector (analysis only): flags samples where the second difference is far above
its local (20 ms) RMS, i.e. isolated impulsive events not explained by surrounding content.
Usage: python clicks.py a.wav [b.wav ...]"""
import sys
import numpy as np
import soundfile as sf
for p in sys.argv[1:]:
    x, sr = sf.read(p, dtype="float64")
    d2 = np.diff(x, 2)
    w = int(0.02 * sr)
    k = np.ones(w) / w
    loc = np.sqrt(np.convolve(d2 ** 2, k, mode="same")) + 1e-6
    ratio = np.abs(d2) / loc
    idx = np.where((ratio > 9.0) & (np.abs(d2) > 0.02))[0]
    # merge events closer than 5 ms
    ev = []
    for i in idx:
        if not ev or i - ev[-1] > int(0.005 * sr):
            ev.append(i)
    print("%-40s clicks: %d %s" % (p.split("/")[-1], len(ev), " ".join("%.3f" % (i / sr) for i in ev[:12])))
