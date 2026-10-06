import sys
p = sys.argv[1]
s = open(p).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)
rep('''                bool touching = Min(Min(heel[s].z, ball[s].z), toeZ[s]) < 0.004f * scale;''',
    '''                bool touching = Min(Min(heel[s].z, ball[s].z), toeZ[s]) < 0.004f * scale;
                // (not early where it lands on another level than it swings over - a curb, a stair: down early, the
                // foot would straddle the edge; it comes down at the gait's own heel strike, where it was heading)
                const bool level = fabsf(A.groundAhead[s] - (s ? A.footR : A.footL)) < 0.04f;''')
rep('''                               (swingU[s] > 0.7f && touching) || (A.planted[s] && touching && swingU[s] < 0.25f);''',
    '''                               (swingU[s] > 0.7f && touching && level) || (A.planted[s] && touching && swingU[s] < 0.25f);''')
rep('''            if (walking && late && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] < 0.006f * scale) {''',
    '''            if (walking && late && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] < 0.006f * scale &&
                (swingU[s] <= 0.f || fabsf(A.groundAhead[s] - (s ? A.footR : A.footL)) < 0.04f)) {''')
open(p, 'w').write(s)
print('ok')
