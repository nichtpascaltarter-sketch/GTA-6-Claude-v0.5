# AI agent (set 18, the coordinator's request for the night agent): the parking test mode keeps its 10:30 only when no --time
# is given (it pinned it after the override, so a night run of it was impossible). Usage: parkingtime.py APP_CPP
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """                env.timeOfDay = 10.5f;
                game.timeScale = 1.5f;
                game.ai.lifeBoost = 6.f;"""
new = """                if (!Platform::argValue("time")) env.timeOfDay = 10.5f;   // (unless --time asks for another hour: a night run of it)
                game.timeScale = 1.5f;
                game.ai.lifeBoost = 6.f;"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok", p)
