// The host services worldv and its test build on, in place of tools/native_stubs.cpp:
// - the platform's: logging, fatal errors, formatting, time. Log lines are kept in gHostLog, not printed: worldv
//   reports what the lane graph's build logs in its own roads summary line;
// - the world's globals that LaneGraph::build (src/game/lanes.cpp) reads beside the RoadNetwork. worldv builds the lane
//   graph from the hand-built roads alone, with no buildings and no sites, so their queries find nothing.
#include "../src/core/base.h"
#include "../src/world/buildings.h"
#include "../src/world/sites.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

std::vector<std::string> gHostLog;   // what LOG wrote, in order
std::mutex gHostLogLock;

void LogPrintf(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> hold(gHostLogLock);
    gHostLog.push_back(buf);
}
void FatalError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
std::string StrFormat(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}
double TimeSeconds() {
    static auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

namespace World {
BuildingSet* gBuildings = nullptr;
SiteSet* gSites = nullptr;
bool siteColliderNear(vec3, float, float) { return false; }
bool BuildingSet::pointInBuilding(vec2, float, float*) const { return false; }
}  // namespace World
