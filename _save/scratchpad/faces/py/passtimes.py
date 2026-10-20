# per shot: mean and minimum of the last 3 pass-timing reports before the shot was saved
# (gbuffer, shadows, lighting, total; the minimum is the least disturbed by other processes on a shared machine)
import sys, re
lines = open(sys.argv[1], errors='replace').read().split('\n')
reports, cur, inrep = [], None, False
out = []
keys = ('gbuffer', 'shadows', 'lighting', 'total')
for ln in lines:
    if 'Pass timings' in ln:
        cur = {}; inrep = True; continue
    if inrep:
        m = re.match(r'\s*(\S.*?)\s+([0-9.]+) ms\s*$', ln)
        if m:
            cur[m.group(1).strip()] = float(m.group(2))
            if m.group(1).strip() == 'total':
                reports.append(cur); inrep = False
            continue
        inrep = False
    m = re.search(r'Saved \S*\\([^\\]+)\.bmp', ln)
    if m:
        last = reports[-3:]
        if last:
            avg = {k: sum(r.get(k, 0) for r in last) / len(last) for k in keys}
            mn = {k: min(r.get(k, 1e9) for r in last) for k in keys}
            out.append('%-10s n=%d  last3: gbuffer %7.2f shadows %7.2f total %8.2f | min3: gbuffer %7.2f shadows %7.2f total %8.2f ms' % (
                m.group(1), len(reports), avg['gbuffer'], avg['shadows'], avg['total'], mn['gbuffer'], mn['shadows'], mn['total']))
        reports = []
print('\n'.join(out))
