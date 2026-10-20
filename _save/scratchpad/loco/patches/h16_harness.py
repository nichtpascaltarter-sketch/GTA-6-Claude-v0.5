L = '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco/'
s = open(L + 'locosim.h').read()
a = '    in.footProbes = true;\n'
assert s.count(a) == 1
s = s.replace(a, a + """    // the ground scan the animator asked for (GameWorld::animatePed)
    in.groundScanValid = false;
    if (P.an.wantsGroundScan()) {
        vec3 s0 = P.an.groundScanFrom(), sd = P.an.groundScanDir();
        for (int k = 0; k < kGroundScan; k++) {
            vec3 mm = s0 + sd * (kGroundScanStep * k), ww = base + rotate(q, vec3(mm.x, mm.y, 0.f));
            in.groundScan[k] = Clamp(T.h(ww.x, ww.y) - base.z, -0.6f, 0.6f);
        }
        in.groundScanValid = true;
        gScans++;
    }
""")
b = 'static double gAnimSec = 0.0;'
assert s.count(b) == 1
s = s.replace(b, "static long gScans = 0;          // ground scans the animator asked for (the game's extra ground queries / 12)\n" + b)
open(L + 'locosim.h', 'w').write(s)
m = open(L + 'locometer.cpp').read()
a = '    printf("animator %.3f us per update over %ld updates\\n", gAnimN ? gAnimSec * 1e6 / gAnimN : 0.0, gAnimN);'
assert m.count(a) == 1, 'anchor'
m = m.replace(a, a + '\n    printf("ground scans %ld (one per %.0f updates)\\n", gScans, gScans ? (double)gAnimN / gScans : 0.0);')
open(L + 'locometer.cpp', 'w').write(m)
print('harness ok')
