# Rewrites the kSamples array of tests/world/world_format_test.cpp from the sample files (one per kind).
import os, sys
test, sdir = sys.argv[1], sys.argv[2]
t = open(test).read()
a = t.index('static const Sample kSamples[] = {\n') + len('static const Sample kSamples[] = {\n')
b = t.index('};\n', a)
def lit(line):
    return '"' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"'
out = []
for name in sorted(os.listdir(sdir)):
    if not name.endswith('.ntw'): continue
    lines = open(os.path.join(sdir, name)).read().split('\n')
    assert lines[-1] == ''
    out.append('    {"%s",\n' % name + '\n'.join('        ' + lit(l) for l in lines[:-1]) + '},\n')
t = t[:a] + ''.join(out) + t[b:]
open(test, 'w').write(t)
print('embedded', len(out), 'samples')
