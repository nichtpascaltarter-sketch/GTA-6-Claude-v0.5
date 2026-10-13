#!/usr/bin/env python3
# Item 3: a gait plant on stairs without a fitted tread goes down at the higher of the near probe's ground (which runs
# ahead down a step or lags behind up one) and the landing probe's ground under the forefoot. usage: noplan.py <animator.cpp>
import sys
p = sys.argv[1]
s = open(p).read()
old = '''                    } else if (!A.footHold[s]) {
                        A.ballProbe[s] = false;
                        A.plantSlack[s] = 0.f;
                    }'''
new = '''                    } else if (!A.footHold[s]) {
                        A.ballProbe[s] = false;
                        A.plantSlack[s] = 0.f;
                        // (on stairs without a tread fitted: at the higher of the near probe's ground - which runs ahead down
                        // a step, or lags behind up one - and the landing probe's under the forefoot)
                        if (A.stairSeen < 1.5f) (s ? A.footR : A.footL) = Max(s ? A.footR : A.footL, A.groundAhead[s]);
                    }'''
assert s.count(old) == 1, 'plant block not found'
s = s.replace(old, new)
open(p, 'w').write(s)
print('patched', p)
