#!/usr/bin/env python3
# Adds the test-only --autoplay locotour to a tree's src/game/app.cpp (HEAD's version). usage: apply.py <tree>
import sys, os
tree = sys.argv[1]
p = os.path.join(tree, 'src/game/app.cpp')
s = open(p).read()
here = os.path.dirname(os.path.abspath(__file__))
block = open(os.path.join(here, 'block.cpp')).read() + open(os.path.join(here, 'block_stairs.cpp')).read()

def rep(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)

# the run's setup
rep('''        if (autoplay == "tour") {
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);   // no prologue phone call: free roam only
''', '''        if (autoplay == "locotour" || autoplay == "locostairs") {
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);
            tourStop = -1;
            tourT = 0.f;
            tourShot = tourDone = false;
            autoDuration = 1e9f;   // ends after its minute (updateLocoTour)
            weather.locked = true;
        }
        if (autoplay == "tour") {
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);   // no prologue phone call: free roam only
''')
# per frame
rep('''        } else if (autoplay == "tour") {
            updateTour(c, dt);
''', '''        } else if (autoplay == "tour") {
            updateTour(c, dt);
        } else if (autoplay == "locotour") {
            updateLocoTour(c, dt);
        } else if (autoplay == "locostairs") {
            updateLocoStairs(c, dt);
''')
# the functions, before updateTour
rep('''    void updateTour(Controls& c, float dt) {
''', block + '''    void updateTour(Controls& c, float dt) {
''')
# no periodic shots: frames render every --renderevery except the bursts
rep('''                                (!autoplay.empty() && (autoplay == "tour"      ? tourT > 6.4f
''', '''                                (!autoplay.empty() && (autoplay == "locotour" || autoplay == "locostairs" ? false
                                                       : autoplay == "tour"      ? tourT > 6.4f
''')
# ends when done
rep('''                } else if (autoplay == "tour" || autoplay == "fpguns" || autoplay == "camfade" || autoplay == "uishots") {
''', '''                } else if (autoplay == "tour" || autoplay == "locotour" || autoplay == "locostairs" || autoplay == "fpguns" || autoplay == "camfade" || autoplay == "uishots") {
''')
open(p, 'w').write(s)
print('patched', p)
