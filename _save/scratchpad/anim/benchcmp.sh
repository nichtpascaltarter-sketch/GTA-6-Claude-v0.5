#!/bin/sh
# interleaved runs of the baseline and new animator benchmarks; per-scenario minimum over the runs
A=$(dirname "$0")
rm -f $A/bc_base.txt $A/bc_new.txt
for i in 1 2 3 4; do $A/bench_base >> $A/bc_base.txt; $A/bench_new >> $A/bc_new.txt; done
python3 - "$A" <<'PY'
import sys, collections
A=sys.argv[1]
def mins(f):
    d=collections.OrderedDict()
    for l in open(f):
        k=l[:28].strip(); v=float(l[28:].split()[0])
        d[k]=min(d.get(k,1e9),v)
    return d
b=mins(A+'/bc_base.txt'); n=mins(A+'/bc_new.txt')
for k in b: print(f"{k:28s} base {b[k]:6.2f} us   new {n[k]:6.2f} us   ({n[k]-b[k]:+.2f})")
PY
