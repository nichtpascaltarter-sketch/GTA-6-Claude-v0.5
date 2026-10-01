// TEST-ONLY shim for symbols from in-progress agent work (never committed).
namespace Game {
std::string speakableText(const std::string& s) { return s; }
bool openWorldBusy() { return false; }
void openWorldOnMissionEnd(GameWorld&, int, bool) {}
void updateOpenWorld(GameWorld&, float) {}
void Mission::autotest(GameWorld&, MissionTest&) {}
}
