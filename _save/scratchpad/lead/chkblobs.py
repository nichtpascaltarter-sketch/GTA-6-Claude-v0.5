#!/usr/bin/env python3
# Check an agent's blob list ("path newblob on base ..." or "path newblob new"): every new blob exists, and every base
# is the file's blob at the given commit or in one of the given earlier blob lists (a chain of sign-offs).
# usage: chkblobs.py LIST COMMIT [EARLIER_LIST...]
import subprocess, sys

REPO = '/home/user/GTA-6-Claude-v0.5'

def git(*a):
    r = subprocess.run(['git', '-C', REPO] + list(a), capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ''

def read_list(path):
    rows = []
    for line in open(path):
        f = line.split()
        if len(f) >= 2:
            rows.append(f)
    return rows

lst, commit, earlier = sys.argv[1], sys.argv[2], sys.argv[3:]
prior = {}
for e in earlier:
    for f in read_list(e):
        prior[f[0]] = f[1][:8]
bad = 0
for f in read_list(lst):
    path, new = f[0], f[1]
    kind = git('cat-file', '-t', new)
    base = f[3][:8] if len(f) >= 4 and f[2] == 'on' else None
    at = git('rev-parse', '-q', '--verify', commit + ':' + path)[:8]
    if kind != 'blob':
        status = 'MISSING BLOB'
    elif base is None:
        status = 'new' if not at else 'NEW BUT EXISTS AT COMMIT (' + at + ')'
    elif base == at:
        status = 'on commit'
    elif prior.get(path) == base:
        status = 'on earlier sign-off'
    else:
        status = 'BASE MISMATCH (commit ' + (at or 'none') + ', earlier ' + prior.get(path, 'none') + ')'
    if status not in ('new', 'on commit', 'on earlier sign-off'):
        bad += 1
    print('%-48s %s  %s' % (path, new[:8], status))
print('ALL OK' if bad == 0 else '%d PROBLEM(S)' % bad)
