# AI agent: an officer already on its way round to its seat's door does not stop to wait by the car for its own hold -
# with the door walk holding the car (seatfix3), the "wait by the car" branch saw that hold and the officer stood, the hold
# ran out, a step on, the hold again: on nt_ai100c neither officer got in for 20 s. Usage: seatfix4.py POLICE_CPP
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """            } else if (time < vehAI(hv).escortHold) {"""
new = """            } else if (time < vehAI(hv).escortHold && pa.doorWalkT <= 0.f) {   // (not once on the way round to its door:
                                                                               //  that walk holds the car itself)"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok", p)
