# Writes every ``` block of the given markdown files that starts with an NTW header to <outdir>/<doc>_<n>.ntw
import os, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for f in os.listdir(out):
    if f.endswith('.ntw'): os.remove(os.path.join(out, f))
for doc in sys.argv[2:]:
    lines = open(doc, encoding='utf-8').read().split('\n')
    base = os.path.basename(doc).split('.')[0].lower()
    n, i = 0, 0
    while i < len(lines):
        if lines[i].startswith('```'):
            j = i + 1
            while not lines[j].startswith('```'): j += 1
            body = lines[i + 1:j]
            if body and body[0].startswith('ntw 1 '):
                n += 1
                name = '%s_%02d_line%d.ntw' % (base, n, i + 2)
                open(os.path.join(out, name), 'w').write('\n'.join(body) + '\n')
            i = j + 1
        else:
            i += 1
    print(doc, n, 'examples')
