# AI agent: the walking companion's stance and facing with hysteresis (the animation agent's request: pedai's follower flipped
# 0 <-> 7, "leader stopped: chat" / walking along, often frame to frame - ped 13 flipped 27 times in 57 s - and each flip
# restarted the upper body's crossfade, so the chat gestures never settled). PedAI::followMode / followModeT: stopped with the
# leader (taken under 0.25 m/s within 1.1 m, left over 0.45 m/s or past 1.5 m), walking along (taken over 0.25 m/s asked, left
# under 0.12), neither; each kept 0.8 s at least once taken. Usage: followfix.py SRC_GAME_DIR
import sys
D = sys.argv[1].rstrip("/")

def rep(s, old, new, what):
    assert s.count(old) == 1, "%s (%d)" % (what, s.count(old))
    return s.replace(old, new, 1)

p = D + "/ai_game.h"
s = open(p, encoding="utf-8").read()
s = rep(s, """    i8 slotSide = 0;              // ... and the side of the leader kept: +1 right, -1 left (0: not set yet - the slot's)
""", """    i8 slotSide = 0;              // ... and the side of the leader kept: +1 right, -1 left (0: not set yet - the slot's)
    u8 followMode = 0;            // ... and what the companion is at - 0 neither, 1 stopped with the leader, 2 walking along -
    float followModeT = 0.f;      // kept a while once taken, left past a wider mark than it was taken at (pedai.cpp)
""", "PedAI fields")
open(p, "w", encoding="utf-8", newline="").write(s)

p = D + "/pedai.cpp"
s = open(p, encoding="utf-8").read()
s = rep(s, """                        if (lspd < 0.3f && d < 1.2f) {
                            // leader stopped: face the group and chat
                            faceYaw = yawTo(pos, L.pos.toVec3().xy());
                            faceSet = true;
                            stance = 7;
                        } else if (length(desired) > 0.2f) {""", """                        // (stopped with the leader, walking along or neither: taken at one mark, left past a wider one, and
                        //  kept 0.8 s at least - flipping between them frame to frame, as a leader slowed or set off, turned
                        //  the companion to and fro and restarted the upper body's crossfade at every flip, so the chat
                        //  gestures never settled: the animation agent's ped 13 flipped 27 times in 57 s)
                        {
                            float dl = length(desired);
                            int mode = pa.followMode, next = mode;
                            if (mode == 1) {
                                if (lspd > 0.45f || d > 1.5f) next = dl > 0.2f ? 2 : 0;
                            } else if (lspd < 0.25f && d < 1.1f) {
                                next = 1;
                            } else if (mode == 2) {
                                if (dl < 0.12f) next = 0;
                            } else if (dl > 0.25f) {
                                next = 2;
                            }
                            pa.followModeT += dt;
                            if (next != mode && pa.followModeT > 0.8f) {
                                pa.followMode = (u8)next;
                                pa.followModeT = 0.f;
                            }
                        }
                        if (pa.followMode == 1) {
                            // leader stopped: face the group and chat
                            faceYaw = yawTo(pos, L.pos.toVec3().xy());
                            faceSet = true;
                            stance = 7;
                        } else if (pa.followMode == 2) {""", "follower modes")
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok: ai_game.h, pedai.cpp")
