import sys
p = sys.argv[1]
s = open(p, encoding='utf-8').read()
old = """// alive, on its feet and still doing what the event asked (not fleeing / cowering because of something else)
bool calmActor(GameWorld& g, int id) {
    if (id < 0) return false;
    const Ped& p = g.peds[id];
    if (isDown(p) || p.state != PS_ONFOOT) return false;"""
new = """// alive, on its feet and still doing what the event asked (not fleeing / cowering because of something else) - or getting
// out through a car's door, the clip the event's own removePedFromVehicle started (PS_EXITING: an officer out for a traffic
// stop, the drivers of a fender bender), not a reason to call it off
bool calmActor(GameWorld& g, int id) {
    if (id < 0) return false;
    const Ped& p = g.peds[id];
    if (isDown(p) || (p.state != PS_ONFOOT && p.state != PS_EXITING)) return false;"""
if s.count(old) != 1:
    sys.exit("anchor count %d" % s.count(old))
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
