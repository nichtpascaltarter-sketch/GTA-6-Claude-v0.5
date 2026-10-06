#!/usr/bin/env python3
"""Autocorrelation F0 tracker + plot (analysis only). Usage: python pitch.py out.png a.wav [b.wav ...]"""
import sys
import numpy as np
import soundfile as sf
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def f0track(x, sr, fmin=60, fmax=450, hop=0.005, win=0.04):
    n = int(win * sr); h = int(hop * sr); out = []
    for i in range(0, len(x) - n, h):
        y = x[i:i + n] * np.hanning(n)
        if np.sqrt(np.mean(y ** 2)) < 0.01: out.append(np.nan); continue
        r = np.correlate(y, y, "full")[n - 1:]
        lo, hi = int(sr / fmax), int(sr / fmin)
        k = lo + np.argmax(r[lo:hi])
        out.append(sr / k if r[k] > 0.35 * r[0] else np.nan)
    return np.arange(len(out)) * hop, np.array(out)

paths = sys.argv[2:]
fig, axs = plt.subplots(len(paths), 1, figsize=(10, 2.2 * len(paths)))
if len(paths) == 1: axs = [axs]
for ax, p in zip(axs, paths):
    x, sr = sf.read(p)
    t, f = f0track(x, sr)
    ax.plot(t, f, ".", ms=2); ax.set_title(p.split("/")[-1], fontsize=8); ax.set_ylabel("Hz")
    v = f[~np.isnan(f)]
    if len(v): print("%-28s F0 min %5.0f max %5.0f mean %5.0f, last-voiced %5.0f, first %5.0f" % (p.split('/')[-1], v.min(), v.max(), v.mean(), v[-1], v[0]))
plt.tight_layout(); plt.savefig(sys.argv[1], dpi=70)
