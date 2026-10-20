import sys
# (on stance.py) a step's scan is asked again, while its landing is still moving, once that landing has left the stretch
# scanned: a standing step overtaken by the walk resuming heads 30-50 cm further on, off its first scan, and came down
# unfitted across a riser
# usage: rescan.py <animator.cpp>
fn = sys.argv[1]
s = open(fn).read()
old = """        // a step on stairs (quick or standing): the way along the foot where it lands is scanned once, early in the step
        if (A.stepT[s] >= 0.f && A.stepT[s] < 0.6f && A.stairSeen < 1.5f && !A.stepScanAsked[s] && !A.scanWant && terrain && in.footProbes) {"""
if s.count(old) != 1: sys.exit('anchor count %d' % s.count(old))
new = """        // a step on stairs (quick or standing): the way along the foot where it lands is scanned early in the step - and
        // again while the landing still moves if it has left the stretch scanned (a standing step overtaken by the walk
        // resuming heads well past its first scan)
        bool offScan = false;
        if (A.stepScanHave[s] && A.stepT[s] >= 0.f && A.stepT[s] < 0.75f) {
            const vec3 sd0 = A.stepScanDir[s];
            const float uH = dot(vec2(A.stepTo[s].x - A.stepScanFrom[s].x, A.stepTo[s].y - A.stepScanFrom[s].y), vec2(sd0.x, sd0.y));
            offScan = uH < -0.05f || uH + L > (kGroundScan - 0.5f) * kGroundScanStep - 0.05f;
        }
        if (A.stepT[s] >= 0.f && (A.stepT[s] < 0.6f || offScan) && A.stairSeen < 1.5f && (!A.stepScanAsked[s] || offScan) && !A.scanWant && terrain &&
            in.footProbes) {"""
s = s.replace(old, new)
open(fn, 'w').write(s)
