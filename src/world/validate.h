// worldv, the world validator (docs/HANDBUILT_WORLD.md section 9.1). It checks the hand-built world's .ntw files:
// - each file against its kind's schema and the canonical form (docs/NTW_FORMAT.md);
// - ids, and the paths that ids give their files (sections 5 and 8);
// - the references between records, within a file and across the world;
// - the library items' rooms, doors and stairs, and a plan's floors (validate_layout.h);
// - the roads, their signal plans and the transit lines on them, on the world model (validate_roads.h).
// Findings are diagnostics with a file, a line, a severity and a stable check id. Errors fail a run, and warnings are
// only listed. Host tools only (tools/worldv.cpp and the tests): nothing here goes into the game.
#pragma once
#include "ntw.h"
#include <string>
#include <vector>

namespace World {
namespace Validate {

enum Severity : u8 { SEV_ERROR, SEV_WARNING };

struct Diagnostic {
    std::string file;      // the path the file was found at (world/... in a world run)
    int line = 0;          // 0: the file as a whole
    Severity severity = SEV_ERROR;
    std::string check;     // a stable id from checkList(): what scripts and --only match on
    std::string message;
};

// Every check id, with what it reports
struct CheckInfo {
    const char* id;
    const char* what;
};
const std::vector<CheckInfo>& checkList();

struct Options {
    std::string world = "world";        // a world run: every .ntw file under this directory, found as worldc finds them
    std::vector<std::string> files;     // a file run instead, when not empty: these files and every .ntw file under these
                                        // directories, each checked on its own (no path, id or reference checks across
                                        // files)
    std::string only;                   // report only this file, the files under this directory, or the file that
                                        // defines this id; the rest of the world is read for its ids only
    std::string cache;                  // the results cache, written after a world run; empty: none
    bool incremental = false;           // reuse the cache's results for every file whose content is unchanged
    int jobs = 1;                       // threads for the checks of single files
};

struct Summary {
    size_t files = 0;       // files found
    size_t checked = 0;     // files whose own checks ran in this run
    size_t cached = 0;      // files whose results came from the cache
    size_t records = 0;
    size_t ids = 0;         // ids defined for the whole world
    size_t refs = 0;        // references resolved
    size_t reported = 0;    // files whose diagnostics are reported (all of them without --only)
    int errors = 0;         // as reported
    int warnings = 0;
    double seconds = 0.0;
    bool world = true;          // a world run (not a file run)
    bool onlyMatched = true;    // false: --only named no file, directory or id (nothing was reported)
    bool cacheWritten = true;   // false: the cache couldn't be written (the run's results stand)
    std::string roads;          // a world run with roads: the roads summary line (validate_roads.h formatRoadCounts)
};

// Runs the checks and returns the diagnostics sorted by file and line
void run(const Options& opt, std::vector<Diagnostic>& out, Summary& summary);

// The checks of one file on its own (a file run), on text in memory; `name` is the file's name for the diagnostics
void checkText(const std::string& name, const std::string& text, std::vector<Diagnostic>& out);

std::string formatDiagnostic(const Diagnostic& d);   // file:line: error: message [check]
std::string formatSummary(const Summary& s);         // worldv: 16 files ...: 0 errors, 0 warnings (0.04 s)

}  // namespace Validate
}  // namespace World
