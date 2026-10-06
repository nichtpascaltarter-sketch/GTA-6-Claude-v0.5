import sys
p = sys.argv[1]
s = open(p).read()
old = '''        rate = Min(rate, 2.4f) / ls;
        locoCycle = rate > 1e-3f ? v / rate : 0.f;   // ground covered per gait cycle (a foot's swing)'''
new = '''        rate = Min(rate, 2.4f) / ls;
        // a sharp turn on the move: quicker, shorter steps (the body swings round faster than a full stride could follow
        // its planted foot)
        rate *= 1.f + 0.4f * Saturate((fabsf(leanS) - 0.05f) / 0.15f) * (1.f - cw);
        locoCycle = rate > 1e-3f ? v / rate : 0.f;   // ground covered per gait cycle (a foot's swing)'''
assert s.count(old) == 1
s = s.replace(old, new)
open(p, 'w').write(s)
print('ok')
