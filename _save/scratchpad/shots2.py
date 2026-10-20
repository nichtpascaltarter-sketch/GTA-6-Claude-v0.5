#!/usr/bin/env python3
# usage: shots2.py "i:view i:view ..." [hour] [extra args...]
# view: fl (front-left 3/4), fr, rl, rr, side, top ; distance from model bounds (meta.txt)
import sys, math, re, subprocess, os
SP = '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad'
G = 6.0
HT = {}
for line in open(SP + '/heights.txt'):
    a, b = line.split()
    HT[int(a)] = float(b)
bounds = {}
for line in open(SP + '/meta.txt'):
    m = re.match(r'\s+bounds \(([-\d. ]+)\)-\(([-\d. ]+)\)', line)
    if m:
        mn = list(map(float, m.group(1).split())); mx = list(map(float, m.group(2).split()))
        bounds[len(bounds)] = (mn, mx)
specs = sys.argv[1].split()
hour = sys.argv[2] if len(sys.argv) > 2 else '10'
extra = sys.argv[3:]
dirs = {'fl': (-0.62, 0.75, 0.28), 'fr': (0.62, 0.75, 0.28), 'rl': (-0.62, -0.75, 0.3), 'rr': (0.62, -0.75, 0.3),
        'side': (-1.0, 0.08, 0.12), 'top': (-0.3, 0.35, 1.0), 'front': (0.0, 1.0, 0.18), 'low': (-0.55, 0.8, 0.08)}
args = []
names = []
for sp in specs:
    i, v = sp.split(':') if ':' in sp else (sp, 'fl')
    i = int(i)
    mn, mx = bounds[i]
    c = [(a + b) * 0.5 for a, b in zip(mn, mx)]
    size = max(mx[1] - mn[1], mx[0] - mn[0], mx[2] - mn[2])
    diag = math.sqrt(sum((b - a) ** 2 for a, b in zip(mn, mx)))
    d = dirs[v]
    n = math.sqrt(sum(x * x for x in d))
    d = [x / n for x in d]
    dist = diag * 0.85 + 1.0
    boat = i in (38, 39, 40, 41)
    tx, ty, tz = -300 + 8 * i + c[0], 1500 + c[1], HT.get(i, G) + c[2] + (0.4 if boat else 0)
    cx, cy, cz = tx + d[0] * dist, ty + d[1] * dist, tz + d[2] * dist
    dx, dy, dz = tx - cx, ty - cy, tz - cz
    yaw = math.degrees(math.atan2(-dx, dy))
    pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    nm = 'vm2n_%d_%s' % (i, v)
    names.append(nm)
    args += ['--shot', '%.2f,%.2f,%.2f,%.1f,%.1f,%s,%s' % (cx, cy, cz, yaw, pitch, hour, nm)]
env = dict(os.environ, WINEPREFIX='/tmp/neontide_wine_vm', EXE='bin/nt_vehicles.exe', TIMEOUT='3600')
cmd = ['tools/run.sh', '--viewer', 'vehicles', '--width', '1280', '--height', '720', '--settle', '12'] + args + extra
with open(SP + '/wine2.log', 'w') as lf:
    subprocess.call(cmd, cwd=SP + '/ntb', env=env, stdout=lf, stderr=subprocess.STDOUT)
for nm in names:
    src = '/tmp/%s.bmp' % nm
    if os.path.exists(src):
        subprocess.call(['convert', src, SP + '/out/%s.png' % nm])
        print('ok', nm)
    else:
        print('missing', nm)
