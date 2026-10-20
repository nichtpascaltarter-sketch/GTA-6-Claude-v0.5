#!/usr/bin/env python3
# apply_player.py <app.cpp>: adds --autoplay locoplayer (block_player.cpp) to an app.cpp that has the locomotion test
# blocks (locotour / locostairs) - test builds only, never for main
import sys, os
p = sys.argv[1]
s = open(p).read()
blk = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'block_player.cpp')).read()
def sub(s, old, new, n=1):
    c = s.count(old)
    if c != n:
        raise SystemExit('expected %d, found %d: %r' % (n, c, old[:100]))
    return s.replace(old, new)
s = sub(s, """        if (autoplay == "locotour" || autoplay == "locostairs") {
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);""", """        if (autoplay == "locotour" || autoplay == "locostairs" || autoplay == "locoplayer") {
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);""")
s = sub(s, """        } else if (autoplay == "locostairs") {
            updateLocoStairs(c, dt);""", """        } else if (autoplay == "locostairs") {
            updateLocoStairs(c, dt);
        } else if (autoplay == "locoplayer") {
            updateLocoPlayer(c, dt);""")
s = sub(s, """(!autoplay.empty() && (autoplay == "locotour" || autoplay == "locostairs" ? false""",
        """(!autoplay.empty() && (autoplay == "locotour" || autoplay == "locostairs" || autoplay == "locoplayer" ? false""")
s = sub(s, """} else if (autoplay == "tour" || autoplay == "locotour" || autoplay == "locostairs" || autoplay == "fpguns\"""",
        """} else if (autoplay == "tour" || autoplay == "locotour" || autoplay == "locostairs" || autoplay == "locoplayer" || autoplay == "fpguns\"""")
s = sub(s, """    // ---- --autoplay locopush (locomotion test build only)""", blk + """    // ---- --autoplay locopush (locomotion test build only)""")
open(p, 'w').write(s)
print('locoplayer added')
