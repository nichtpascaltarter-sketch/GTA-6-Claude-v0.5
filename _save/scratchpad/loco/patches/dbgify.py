#!/usr/bin/env python3
# dbgify.py <animator.cpp>: the harness's traced-run prints for the wall guard (WGSCAN: each swing's scan fit; WGNONE: a
# probe taken as no ground; WGSW: each swing's probe, pull and fit) - debug trees only
import sys
p=sys.argv[1]; s=open(p).read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:90]))
    return s.replace(old,new)
s=sub(s, '#include "anim_internal.h"\n\nnamespace Anim {\nnamespace detail {', '#include "anim_internal.h"\n#define ANIM_DBG_HOOK 1\n\nnamespace Anim {\nbool gAnimDbg = false;   // DEBUG (harness: the traced run)\nnamespace detail {')
s=sub(s, """        stepped = (stepped || anyCliff) && bestCost < 1e8f;""", """        stepped = (stepped || anyCliff) && bestCost < 1e8f;
        if (gAnimDbg) {
            char cs[kGroundScan + 1];
            for (int k = 0; k < kGroundScan; k++) cs[k] = cliff[k] ? 'C' : '.';
            cs[kGroundScan] = 0;
            fprintf(stderr, "WGSCAN foot %d sw %.2f cliffs %s kFoot %d gFoot %+.2f stepped %d shift %+.3f land %+.2f cost %.2f | from %+.2f %+.2f dir %+.2f %+.2f | g", s, swingU[s], cs, kFoot, gFoot, (int)stepped,
                    bestShift, bestG, bestCost, A.scanFrom.x, A.scanFrom.y, A.scanDir.x, A.scanDir.y);
            for (int k = 0; k < kGroundScan; k++) fprintf(stderr, " %+.2f", g[k]);
            fprintf(stderr, "\\n");
        }""")
s=sub(s, """            if (A.probeAhead[s]) {
                if (none) A.wallSeen[s] = true;""", """            if (none && gAnimDbg) fprintf(stderr, "WGNONE foot %d ahead %d g %+.2f at %+.2f %+.2f\\n", s, (int)A.probeAhead[s], gIn[s], A.probeP[s].x, A.probeP[s].y);
            if (A.probeAhead[s]) {
                if (none) A.wallSeen[s] = true;""")
s=sub(s, '''        A.probeP[s] = vec3(m.x, m.y, 0.f);
''', '''        A.probeP[s] = vec3(m.x, m.y, 0.f);
        if (gAnimDbg && swinging)
            fprintf(stderr, "WGSW foot %d sw %.2f ahead %d probe %+.2f %+.2f gIn %+.2f pull %+.2f %+.2f shift %+.2f SL %+.2f\\n", s, swingU[s], (int)A.probeAhead[s], m.x, m.y, A.groundRaw[s],
                    A.wallPull[s].x, A.wallPull[s].y, A.stairShift[s], A.stairLand[s]);
''')
open(p,'w').write(s); print('dbgified')
