#!/usr/bin/env python3
# Carry the landed Direct3D 12 files into the main working tree, keeping the agents' uncommitted edits:
# for each changed path: unedited in the working tree (still the base version) -> the landed version;
# edited -> a 3-way merge (git merge-file) of base -> working tree and base -> landed. usage: merge_d3d12_wt.py BASE LANDED FILES.txt
import subprocess, sys, os, tempfile
REPO = '/home/user/GTA-6-Claude-v0.5'
base, landed, listf = sys.argv[1], sys.argv[2], sys.argv[3]
def show(rev, path):
    r = subprocess.run(['git', '-C', REPO, 'show', f'{rev}:{path}'], capture_output=True)
    return r.stdout if r.returncode == 0 else None
def mode(rev, path):
    r = subprocess.run(['git', '-C', REPO, 'ls-tree', rev, '--', path], capture_output=True, text=True)
    return r.stdout.split()[0] if r.stdout else None
paths = [l.strip() for l in open(listf) if l.strip()]
report = []
for p in paths:
    wt = os.path.join(REPO, p)
    b, l = show(base, p), show(landed, p)
    if l is None:
        report.append(('MISSING-IN-LANDED', p)); continue
    cur = open(wt, 'rb').read() if os.path.exists(wt) else None
    if cur is None or cur == b or cur == l:
        action = 'new' if cur is None else ('same' if cur == l else 'replaced')
        if cur != l:
            os.makedirs(os.path.dirname(wt) or '.', exist_ok=True)
            tmp = wt + '.d3d12new'
            open(tmp, 'wb').write(l)
            if mode(landed, p) == '100755': os.chmod(tmp, 0o755)
            elif os.path.exists(wt): os.chmod(tmp, os.stat(wt).st_mode & 0o777)
            os.replace(tmp, wt)
        report.append((action, p)); continue
    # edited in the working tree: 3-way merge
    with tempfile.TemporaryDirectory() as d:
        fb, fl, fc = os.path.join(d, 'base'), os.path.join(d, 'landed'), os.path.join(d, 'cur')
        open(fb, 'wb').write(b or b''); open(fl, 'wb').write(l); open(fc, 'wb').write(cur)
        r = subprocess.run(['git', 'merge-file', '-p', '-L', 'working tree', '-L', 'base', '-L', 'd3d12', fc, fb, fl], capture_output=True)
        if r.returncode < 0:
            report.append(('MERGE-ERROR', p)); continue
        tmp = wt + '.d3d12new'
        open(tmp, 'wb').write(r.stdout)
        os.chmod(tmp, os.stat(wt).st_mode & 0o777)
        os.replace(tmp, wt)
        report.append(('merged' if r.returncode == 0 else f'CONFLICTS({r.returncode})', p))
for a, p in report: print(a, p)
