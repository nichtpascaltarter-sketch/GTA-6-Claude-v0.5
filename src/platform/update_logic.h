// Updater logic that needs no operating system (tested natively by tests/updater/update_logic_test.cpp): a small JSON
// reader, GitHub's compare and commit-list answers, the pull requests merged since a commit, the choice of C++
// toolchain, the build and unpack commands with their environment, the checks of a downloaded archive and of a built
// exe, the URL rules and the pending-update record. Downloads, processes and the install are in updater.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Updater {

// ------------------------------------------------------------------------------------------------------------------
// Commits: a build carries the commit it was built from (NT_COMMIT, src/core/version.h), "unknown" without one
bool isCommitSha(const std::string& s);           // 40 lower-case hex digits
std::string shortCommit(const std::string& sha);  // its first 7 digits, "" when it is not a commit

// ------------------------------------------------------------------------------------------------------------------
// JSON (RFC 8259): objects, arrays, strings (escapes, \uXXXX with surrogate pairs, to UTF-8), numbers and literals.
// Strict: anything else, trailing text or nesting deeper than 64 fails.
enum JsonType : uint8_t { JSON_NULL = 0, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT };
struct Json {
    JsonType type = JSON_NULL;
    bool b = false;
    std::string str;                 // STRING; NUMBER keeps its source text (exact for large sizes)
    std::vector<Json> items;         // ARRAY elements, or OBJECT values (keys[i] names items[i])
    std::vector<std::string> keys;   // OBJECT member names
    const Json* get(const char* key) const;   // nullptr when missing or not an object
    std::string getString(const char* key) const;   // "" when missing or not a string
    long long getInt(const char* key, long long def) const;   // def unless an integer number
    bool getBool(const char* key, bool def) const;
};
bool parseJson(const std::string& text, Json& out, std::string* error = nullptr);
std::string jsonQuote(const std::string& s);   // a JSON string literal, quotes included

// ------------------------------------------------------------------------------------------------------------------
// GitHub, for this repository. The updater asks
//   <api>/repos/<repo>/compare/<commit>...main?per_page=100&page=100   how the running build relates to main (a page
//                                     past the first: it leaves out the commits and changed files that page 1 lists)
//   <api>/repos/<repo>/commits?sha=main&per_page=N                    main's newest commits, main's head first
//   <web>/<repo>/archive/<sha>.tar.gz                                 the source (a download, not an API request)
// with api = https://api.github.com and web = https://github.com, or the test server of --update-url for both.
std::string compareUrl(const std::string& api, const std::string& repo, const std::string& commit);
std::string commitsUrl(const std::string& api, const std::string& repo, int perPage);
std::string archiveUrl(const std::string& web, const std::string& repo, const std::string& sha);
// How many of main's commits to list for `aheadBy` new ones: those and the running build's own (2..100)
int commitsToList(int aheadBy);

// The compare answer. AHEAD (main has commits the running build lacks, and none the other way) is the only status
// that updates. A commit GitHub does not know (a local one) is NOT_FOUND.
enum CompareStatus : uint8_t { CMP_UNREADABLE = 0, CMP_AHEAD, CMP_IDENTICAL, CMP_BEHIND, CMP_DIVERGED, CMP_NOT_FOUND };
struct Comparison {
    CompareStatus status = CMP_UNREADABLE;
    int aheadBy = 0, behindBy = 0;   // commits main has that the build lacks, and the other way round
};
// A compare request's answer: its HTTP status (404 is NOT_FOUND) and body. False when it cannot be read.
bool parseCompare(int httpStatus, const std::string& json, Comparison& out);
const char* compareStatusName(CompareStatus s);

// One commit of a commit list (newest first)
struct CommitInfo {
    std::string sha, message, date;   // date: the committer's, ISO 8601
    int parents = 0;
};
// Reads a commit list; entries without a commit SHA are skipped. False when the text is not a JSON array.
bool parseCommitList(const std::string& json, std::vector<CommitInfo>& out);

// A merged pull request, from its merge commit's message: "Merge pull request #53: title" (this repository),
// "Merge pull request #53 from owner/branch" with the title on a later line (GitHub's default), or a squash
// merge's "title (#53)". The title is made printable (asciiText). False for other commits.
struct PullRequest {
    int number = 0;
    std::string title;
};
bool pullFromMessage(const std::string& message, PullRequest& out);

// What main adds after `base` (the running build's commit; "" for a build without one takes every commit listed):
// main's head, the pull requests merged after base (newest first, each once) and the number of commits listed before
// base. `complete` is false when the list does not reach base (more commits than were listed). False for an empty list.
struct NewChanges {
    std::string head, headDate;
    std::vector<PullRequest> pulls;
    int commits = 0;
    bool complete = false;
    bool sinceBase = false;   // a base was given
};
bool newChangesSince(const std::vector<CommitInfo>& commits, const std::string& base, NewChanges& out);
// The changes in one line of at most maxChars: "3 new updates: #55 Night lighting, #54 Taxi stands, #53 ..."
// ("1 new update: ...", "4 new commits" without pull requests, "10+ new updates: ..." for an incomplete list); without
// a base, main's latest pull requests: "Latest: #55 Night lighting, #54 ..." ("" without any)
std::string changesSummary(const NewChanges& c, size_t maxChars);
// UTF-8 text as the UI font draws it (ASCII 32..126): dashes, quotes and ellipses become ASCII, controls become
// spaces, other characters are dropped, runs of spaces shrink to one and the ends are trimmed
std::string asciiText(const std::string& utf8);

// ------------------------------------------------------------------------------------------------------------------
// The C++ toolchain build.bat will use, in build.bat's own order: Visual Studio's C++ tools found by vswhere (with
// vcvars64.bat), cl.exe on PATH, g++.exe with windres.exe on PATH, then g++ and windres in a usual MinGW-w64 folder
// (w64devkit, MSYS2), which the build gets in front of its PATH.
enum ToolchainKind : uint8_t { TC_NONE = 0, TC_MSVC, TC_MSVC_PATH, TC_MINGW_PATH, TC_MINGW_DIR };
struct ToolchainProbe {
    std::string vsInstall;                // vswhere's installationPath with the x64 C++ tools, "" for none
    bool vcvars = false;                  // <vsInstall>\VC\Auxiliary\Build\vcvars64.bat exists
    bool clOnPath = false, gxxOnPath = false, windresOnPath = false;
    std::vector<std::string> mingwDirs;   // the candidate folders (mingwCandidateDirs order) that hold g++ and windres
};
struct Toolchain {
    ToolchainKind kind = TC_NONE;
    std::string pathPrefix;   // TC_MINGW_DIR: the folder put in front of PATH
    std::string label;        // for the log and the records, e.g. "MSVC (C:\...\BuildTools)", "MinGW-w64 (C:\w64devkit\bin)"
};
Toolchain pickToolchain(const ToolchainProbe& p);
// The usual MinGW-w64 folders, in order: w64devkit at the system drive's root, in the user's folder and from Scoop,
// then MSYS2's UCRT64 and MINGW64, then <drive>\mingw64. Empty arguments leave their entries out.
std::vector<std::string> mingwCandidateDirs(const std::string& systemDrive, const std::string& userProfile);
// The folders of a PATH value (";"-separated, quotes removed, empty entries skipped)
std::vector<std::string> splitPathList(const std::string& path);

// ------------------------------------------------------------------------------------------------------------------
// The processes the updater starts, both from the system folder (%SystemRoot%\System32): cmd.exe /d /c .\build.bat in
// the source folder (/d: without AutoRun commands), and tar.exe -xf <archive> -C <folder> (gzip is detected).
struct ProcessCommand {
    std::string application;   // full path of the program
    std::string commandLine;   // the whole command line, program name first
    std::string directory;     // the working folder
};
ProcessCommand buildCommand(const std::string& systemDir, const std::string& sourceDir);
ProcessCommand unpackCommand(const std::string& systemDir, const std::string& archive, const std::string& folder);
// The build's environment ("NAME=value" entries, as GetEnvironmentStrings lists them): NT_COMMIT set to `commit`,
// VERSION removed (the source's version.h names the version), `pathPrefix` (if any) in front of PATH. Names match
// without case and keep their spelling. Sorted without case, as CreateProcess wants it.
std::vector<std::string> buildEnvironment(const std::vector<std::string>& env, const std::string& commit, const std::string& pathPrefix);
// A command-line argument, quoted the way CommandLineToArgvW and the C runtime read it back when it needs quotes
std::string quoteArgument(const std::string& arg);

// ------------------------------------------------------------------------------------------------------------------
// Checks
// The start of a downloaded source archive: gzip, or an uncompressed POSIX tar
bool archiveLooksValid(const uint8_t* head, size_t size);
// The archive's single top folder names the commit: GitHub's "<repo>-<sha>" (the API tarball's "<owner>-<repo>-<sha7>")
bool archiveFolderMatches(const std::string& folder, const std::string& sha);
// LF line ends made CRLF (cmd.exe can miss labels in batch files with LF line ends); CRLF stays as it is
std::string withCrlf(const std::string& text);
// Searches a stream of bytes for a text (a built exe must contain its 40-digit commit)
class StreamSearch {
public:
    explicit StreamSearch(const std::string& needle);
    void add(const void* data, size_t n);
    bool found() const { return hit; }

private:
    std::string needle, tail;
    bool hit = false;
};
// A built file is a 64-bit Windows executable: MZ, PE signature, AMD64, PE32+ optional header, not a DLL and every
// section inside the file. `head` holds the start of the file (64 KB covers the headers), `fileSize` its size.
bool peImageValid(const uint8_t* head, size_t headSize, uint64_t fileSize, std::string* why = nullptr);
// The update folders (named by their commit) to delete: every commit folder but `keep`; other names are never touched
std::vector<std::string> foldersToRemove(const std::vector<std::string>& names, const std::string& keep);

// ------------------------------------------------------------------------------------------------------------------
// URLs: https only to github.com and its subdomains (api.github.com, codeload.github.com), except plain http to this
// machine (127.0.0.1, localhost, [::1]) when the test override allows it
struct Url {
    bool https = false;
    std::string host;
    int port = 0;
    std::string path;   // with the query, "/" at least
};
bool parseUrl(const std::string& text, Url& out);
bool urlAllowed(const std::string& text, bool allowLocalHttp);

// ------------------------------------------------------------------------------------------------------------------
// <exe>.pending.json: what the next start checks again before installing <exe>.pending
struct PendingInfo {
    std::string commit;      // the commit it was built from
    std::string from;        // the commit of the build that made it ("unknown" for a build without one)
    std::string sha256;
    long long size = -1;
    std::string toolchain;
    std::vector<PullRequest> pulls;   // merged after `from`, newest first
    int commits = 0;
};
std::string pendingToJson(const PendingInfo& p);
bool pendingFromJson(const std::string& text, PendingInfo& out);

// updates\<sha>\failed.txt: a build of that commit failed with this toolchain (not tried again with the same one)
std::string failedRecord(const std::string& toolchain, const std::string& why);
std::string failedToolchain(const std::string& record);   // "" when unreadable

}  // namespace Updater
