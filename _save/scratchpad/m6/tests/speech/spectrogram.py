#!/usr/bin/env python3
"""Spectrogram + phoneme segment overlay for TTS debugging (analysis only).
Usage: python spectrogram.py in.wav out.png [segs.txt] [--fmax 8000] [--t0 0 --t1 2]"""
import sys, argparse
import numpy as np
import soundfile as sf
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ap = argparse.ArgumentParser()
ap.add_argument("wav"); ap.add_argument("png"); ap.add_argument("segs", nargs="?")
ap.add_argument("--fmax", type=float, default=8000); ap.add_argument("--t0", type=float, default=0)
ap.add_argument("--t1", type=float, default=0); ap.add_argument("--win", type=float, default=0.006)
a = ap.parse_args()
x, sr = sf.read(a.wav, dtype="float32")
if x.ndim > 1: x = x[:, 0]
t1 = a.t1 if a.t1 > 0 else len(x) / sr
x = x[int(a.t0 * sr):int(t1 * sr)]
n = int(a.win * sr); hop = max(1, n // 4); nfft = 1024
w = np.hanning(n)
frames = [x[i:i + n] * w for i in range(0, len(x) - n, hop)]
S = np.abs(np.fft.rfft(np.array(frames), nfft)) + 1e-9
D = 20 * np.log10(S.T)
D -= D.max()
f = np.fft.rfftfreq(nfft, 1 / sr)
fig, ax = plt.subplots(2, 1, figsize=(max(10, 6 * (t1 - a.t0)), 7), sharex=True, gridspec_kw={"height_ratios": [4, 1]})
ax[0].imshow(D, origin="lower", aspect="auto", cmap="gray_r", vmin=-70, vmax=0,
             extent=[a.t0, a.t0 + len(frames) * hop / sr, 0, f[-1]])
ax[0].set_ylim(0, a.fmax)
tt = a.t0 + np.arange(len(x)) / sr
ax[1].plot(tt, x, lw=0.4)
if a.segs:
    for line in open(a.segs):
        p = line.split()
        name, st, du = p[0], float(p[1]), float(p[2])
        if st + du < a.t0 or st > t1: continue
        for axx in ax: axx.axvline(st, color="r", lw=0.5, alpha=0.6)
        ax[0].text(st + du / 2, a.fmax * 0.95, name, ha="center", color="red", fontsize=8)
plt.tight_layout(); plt.savefig(a.png, dpi=80)
