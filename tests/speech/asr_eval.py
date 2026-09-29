#!/usr/bin/env python3
"""Offline intelligibility check for the procedural TTS (analysis only, never shipped).

Reads the manifest written by test_speech (TSV: wav_path, voice, reference text), runs one or more
sherpa-onnx offline recognizers over every file and reports word error rate per model and voice.

Setup (outside the repo):
  python3 -m venv asrenv && asrenv/bin/pip install sherpa-onnx soundfile numpy whisper-normalizer jiwer
  Models (sherpa-onnx GitHub release "asr-models"): sherpa-onnx-whisper-base.en, sherpa-onnx-whisper-small.en,
  sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8, sherpa-onnx-zipformer-en-2023-06-26.
Usage:
  asrenv/bin/python tests/speech/asr_eval.py --models MODEL_DIR --manifest out/manifest.tsv [--asr whisper-base,parakeet]
"""
import argparse, os, re, sys, collections
import numpy as np
import soundfile as sf
import sherpa_onnx
import jiwer
from whisper_normalizer.english import EnglishTextNormalizer

_norm = EnglishTextNormalizer()


def normalize(text):
    text = re.sub(r"(\S)\$", r"\1 $", text)                       # "got$250" -> "got $250"
    text = re.sub(r"\b(\d{1,2})[.:](\d{2})\b", r"\1\2", text)       # "10.30" / "10:30" -> "1030"
    t = _norm(text)
    toks = t.split()
    out = []
    for tok in toks:  # merge adjacent digit groups ("10 30" == "1030") on both sides
        if out and tok.isdigit() and out[-1].isdigit():
            out[-1] += tok
        else:
            out.append(tok)
    return " ".join(out)


def make_recognizer(name, mdir):
    nt = max(1, os.cpu_count() or 1)
    if name.startswith("whisper-"):
        size = name.split("-", 1)[1]
        d = os.path.join(mdir, "sherpa-onnx-whisper-%s.en" % size)
        return sherpa_onnx.OfflineRecognizer.from_whisper(
            encoder=os.path.join(d, "%s.en-encoder.int8.onnx" % size),
            decoder=os.path.join(d, "%s.en-decoder.int8.onnx" % size),
            tokens=os.path.join(d, "%s.en-tokens.txt" % size),
            language="en", task="transcribe", num_threads=nt)
    if name == "parakeet":
        d = os.path.join(mdir, "sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8")
        return sherpa_onnx.OfflineRecognizer.from_transducer(
            encoder=os.path.join(d, "encoder.int8.onnx"), decoder=os.path.join(d, "decoder.int8.onnx"),
            joiner=os.path.join(d, "joiner.int8.onnx"), tokens=os.path.join(d, "tokens.txt"),
            model_type="nemo_transducer", num_threads=nt)
    if name == "zipformer":
        d = os.path.join(mdir, "sherpa-onnx-zipformer-en-2023-06-26")
        return sherpa_onnx.OfflineRecognizer.from_transducer(
            encoder=os.path.join(d, "encoder-epoch-99-avg-1.int8.onnx"),
            decoder=os.path.join(d, "decoder-epoch-99-avg-1.int8.onnx"),
            joiner=os.path.join(d, "joiner-epoch-99-avg-1.int8.onnx"),
            tokens=os.path.join(d, "tokens.txt"), num_threads=nt)
    raise ValueError(name)


def load_wav(path):
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    x = x[:, 0]
    # pad with 0.3 s of silence on both ends (some models dislike speech right at the edges)
    pad = np.zeros(int(0.3 * sr), dtype=np.float32)
    return np.concatenate([pad, x, pad]), sr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--asr", default="whisper-base,parakeet")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--words", action="store_true", help="word test: score only the target word of 'Say the word X again.'")
    a = ap.parse_args()
    rows = []
    base = os.path.dirname(os.path.abspath(a.manifest))
    for line in open(a.manifest, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line:
            continue
        p, voice, ref = line.split("\t")
        if not os.path.isabs(p):
            p = os.path.join(base, p)
        rows.append((p, voice, ref))
    summary = {}
    for name in a.asr.split(","):
        rec = make_recognizer(name, a.models)
        streams = []
        for p, voice, ref in rows:
            x, sr = load_wav(p)
            s = rec.create_stream()
            s.accept_waveform(sr, x)
            streams.append(s)
        rec.decode_streams(streams)
        if a.words:
            ok = collections.Counter(); tot = collections.Counter(); wrong = []
            # accepted homophones / spellings (cot-caught merger is normal in American English)
            alts = {"mat": ["matt"], "hole": ["whole"], "cash": ["cache"], "cot": ["caught"], "bought": ["bot"],
                    "yak": ["yack"], "heart": ["hart"], "bite": ["byte"], "pal": ["pall"], "boy": ["buoy"], "hot": ["haught"]}
            for (p, voice, ref), s in zip(rows, streams):
                target = ref.split()[3]
                hyp = normalize(s.result.text)
                hw = hyp.split()
                hit = normalize(target) in hw or any(normalize(x) in hw for x in alts.get(target, []))
                tot[voice] += 1; ok[voice] += hit
                if not hit: wrong.append((voice, target, hyp))
            for v in sorted(tot): print("   %-10s word accuracy %5.1f%% (%d/%d)" % (v, 100.0 * ok[v] / tot[v], ok[v], tot[v]))
            print("== %s: word accuracy %.1f%%" % (name, 100.0 * sum(ok.values()) / max(1, sum(tot.values()))))
            miss = collections.defaultdict(list)
            for v, t, h in wrong: miss[t].append(h)
            for t in sorted(miss, key=lambda t: -len(miss[t])): print("   %-8s x%d: %s" % (t, len(miss[t]), " | ".join(miss[t])))
            continue
        per_voice = collections.defaultdict(lambda: [0, 0])  # errors, words
        tot_err = tot_words = 0
        for (p, voice, ref), s in zip(rows, streams):
            hyp = s.result.text.strip()
            h = normalize(hyp)
            # reference alternatives: "sol|soul" matches either spelling (homophones / name spellings)
            hw = set(h.split())
            toks = []
            for tok in ref.split():
                if "|" in tok:
                    alts = tok.split("|")
                    pick = next((a for a in alts if normalize(a) in hw), alts[0])
                    toks.append(pick)
                else:
                    toks.append(tok)
            r = normalize(" ".join(toks))
            m = jiwer.process_words(r, h if h else "<empty>")
            err = m.substitutions + m.deletions + m.insertions
            n = len(r.split())
            per_voice[voice][0] += err
            per_voice[voice][1] += n
            tot_err += err
            tot_words += n
            if not a.quiet:
                flag = "OK " if err == 0 else "ERR"
                print("[%s] %-9s %-10s REF: %s\n%26s HYP: %s" % (flag, name, voice, r, "", h))
        print("== %s: overall WER %.1f%% (%d/%d)" % (name, 100.0 * tot_err / max(1, tot_words), tot_err, tot_words))
        for v in sorted(per_voice):
            e, n = per_voice[v]
            print("   %-12s WER %5.1f%%  (%d/%d)" % (v, 100.0 * e / max(1, n), e, n))
        summary[name] = 100.0 * tot_err / max(1, tot_words)
    print("SUMMARY " + " ".join("%s=%.1f%%" % (k, v) for k, v in summary.items()))


if __name__ == "__main__":
    main()
