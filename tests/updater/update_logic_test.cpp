// Updater logic tests, natively without Windows: commits, JSON, GitHub's compare and commit-list answers, the pull
// requests merged since a commit, toolchain choice, the build and unpack commands and their environment, archive and
// exe checks, URLs, the records, and the bundled compiler's footer, payload.txt and SHA-256. The fixtures are GitHub's real answers for this repository (2026-10-20, with the
// commit trailers and the agent account's identity taken out): compare/<commit>...main?per_page=100&page=100 for a
// build behind main (837b39c, ahead 2), at main (bcb8a71, identical), on another branch (c7b2d62, diverged), an unknown
// commit (404), main...837b39c (behind), and commits?sha=main&per_page=6.
// Build and run from the repository root:
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/updater/update_logic_test.cpp -o /tmp/update_logic_test
//   /tmp/update_logic_test [--fixtures DIR] [program.exe ...]   (each extra file must pass the PE check)
#include "../../src/platform/update_logic.cpp"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Updater;

static int checks = 0, failures = 0;
#define EXPECT(cond) do { ++checks; if (!(cond)) { \
    ++failures; std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
} } while (0)

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static const char* const kHead = "bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e";    // main on 2026-10-20 (#53)
static const char* const kPr52 = "837b39c751fa63ce8458be5366dd0635c9f320dc";    // #52's merge
static const char* const kPr51 = "f595703c2bab567f653c16d7031b222a88284036";    // #51's merge

static void testCommits() {
    EXPECT(isCommitSha(kHead) && isCommitSha(std::string(40, '0')));
    const char* invalid[] = {"", "unknown", "bcb8a71", "BCB8A71A0A5F8EEA6D057663B06D30BB5D4FE37E", "bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37",
                             "bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e0", "gcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e",
                             " cb8a71a0a5f8eea6d057663b06d30bb5d4fe37e"};
    for (const char* s : invalid) EXPECT(!isCommitSha(s));
    EXPECT(shortCommit(kHead) == "bcb8a71");
    EXPECT(shortCommit("unknown").empty() && shortCommit("").empty());
}

static void testJson() {
    Json j;
    EXPECT(parseJson("\"a\\\"b\\\\c\\/d\\n\\t\\b\\f\\r\"", j) && j.type == JSON_STRING && j.str == "a\"b\\c/d\n\t\b\f\r");
    EXPECT(parseJson("\"\\u00e9\\u20AC\"", j) && j.str == "\xC3\xA9\xE2\x82\xAC");                  // e acute, euro sign
    EXPECT(parseJson("\"\\ud83c\\udf34\"", j) && j.str == "\xF0\x9F\x8C\xB4");                     // a surrogate pair
    EXPECT(parseJson("\"\\ud83cx\"", j) && j.str == "\xEF\xBF\xBDx");                               // lone high surrogate
    EXPECT(parseJson("\"\\udf34\"", j) && j.str == "\xEF\xBF\xBD");                                 // lone low surrogate
    EXPECT(parseJson("\"\\ud83c\\u0041\"", j) && j.str == "\xEF\xBF\xBD" "A");                     // high + non-surrogate
    EXPECT(parseJson("\"Porto Sol \xF0\x9F\x8C\xB4\"", j) && j.str == "Porto Sol \xF0\x9F\x8C\xB4"); // raw UTF-8 passes
    EXPECT(parseJson(" { \"size\" : 12882944 , \"neg\":-5, \"f\":1.5e3, \"big\":123456789012345678901, \"t\":true, \"n\":null,"
                     " \"a\":[1,[2,{}],[]], \"o\":{\"k\":\"v\"} } ", j) && j.type == JSON_OBJECT);
    EXPECT(j.getInt("size", -1) == 12882944);
    EXPECT(j.getInt("neg", 0) == -5);
    EXPECT(j.getInt("f", -1) == -1);
    EXPECT(j.getInt("big", -1) == -1);
    EXPECT(j.getInt("missing", 7) == 7);
    EXPECT(j.getBool("t", false) && !j.getBool("n", false) && j.getBool("size", true));
    EXPECT(j.get("a") && j.get("a")->items.size() == 3 && j.get("a")->items[1].items[1].type == JSON_OBJECT);
    EXPECT(j.get("o") && j.get("o")->getString("k") == "v" && j.getString("o").empty());
    EXPECT(j.get("n") && j.get("n")->type == JSON_NULL && !j.get("nope"));
    const char* invalid[] = {"", "{} x", "\"open", "\"a\nb\"", "\"\\x\"", "\"\\u12\"", "\"\\u12G4\"", "[1,]", "{\"a\" 1}", "{\"a\":}",
                             "{a:1}", "01", "-", "1.", "1e", "+1", "tru", "nul", "[1 2]", "{\"a\":1,}", "\xEF\xBB\xBF{}"};
    for (const char* s : invalid) EXPECT(!parseJson(s, j));
    std::string err;
    EXPECT(!parseJson("[1, 2", j, &err) && !err.empty());
    EXPECT(parseJson(std::string(64, '[') + std::string(64, ']'), j));
    EXPECT(!parseJson(std::string(66, '[') + std::string(66, ']'), j));
    std::string odd = "a\"b\\c\n\x01" "\xC3\xA9";
    EXPECT(parseJson(jsonQuote(odd), j) && j.str == odd);
}


static void testGitHubUrls() {
    const std::string repo = "nichtpascaltarter-sketch/GTA-6-Claude-v0.5";
    EXPECT(compareUrl("https://api.github.com", repo, kPr52) ==
           "https://api.github.com/repos/nichtpascaltarter-sketch/GTA-6-Claude-v0.5/compare/837b39c751fa63ce8458be5366dd0635c9f320dc...main?per_page=100&page=100");
    EXPECT(commitsUrl("https://api.github.com/", repo, 3) == "https://api.github.com/repos/nichtpascaltarter-sketch/GTA-6-Claude-v0.5/commits?sha=main&per_page=3");
    EXPECT(archiveUrl("https://github.com", repo, kHead) ==
           "https://github.com/nichtpascaltarter-sketch/GTA-6-Claude-v0.5/archive/bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e.tar.gz");
    EXPECT(archiveUrl("http://127.0.0.1:8765/", "o/r", kHead) == std::string("http://127.0.0.1:8765/o/r/archive/") + kHead + ".tar.gz");
    for (const std::string& u : {compareUrl("https://api.github.com", repo, kPr52), commitsUrl("https://api.github.com", repo, 3),
                                 archiveUrl("https://github.com", repo, kHead)})
        EXPECT(urlAllowed(u, false));
    EXPECT(commitsToList(1) == 2 && commitsToList(2) == 3 && commitsToList(0) == 2 && commitsToList(99) == 100 && commitsToList(5000) == 100);
}

static void testCompare(const std::string& dir) {
    Comparison c;
    EXPECT(parseCompare(200, readFile(dir + "compare_ahead.json"), c) && c.status == CMP_AHEAD && c.aheadBy == 2 && c.behindBy == 0);
    EXPECT(parseCompare(200, readFile(dir + "compare_identical.json"), c) && c.status == CMP_IDENTICAL && c.aheadBy == 0 && c.behindBy == 0);
    EXPECT(parseCompare(200, readFile(dir + "compare_diverged.json"), c) && c.status == CMP_DIVERGED && c.aheadBy == 2 && c.behindBy == 1);
    EXPECT(parseCompare(200, readFile(dir + "compare_behind.json"), c) && c.status == CMP_BEHIND && c.aheadBy == 0 && c.behindBy == 2);
    EXPECT(parseCompare(404, readFile(dir + "compare_not_found.json"), c) && c.status == CMP_NOT_FOUND);
    EXPECT(!parseCompare(200, readFile(dir + "compare_not_found.json"), c) && c.status == CMP_UNREADABLE);   // its "status" is "404"
    // other answers and shapes are unreadable: a rate limit, a server error, statuses or counts that do not fit
    EXPECT(!parseCompare(403, "{\"message\": \"API rate limit exceeded\"}", c));
    EXPECT(!parseCompare(500, "", c) && !parseCompare(200, "", c) && !parseCompare(200, "[]", c) && !parseCompare(200, "not json", c));
    EXPECT(!parseCompare(200, "{\"status\": \"ahead\", \"ahead_by\": 0, \"behind_by\": 0}", c));
    EXPECT(!parseCompare(200, "{\"status\": \"ahead\", \"ahead_by\": 3, \"behind_by\": 1}", c));
    EXPECT(!parseCompare(200, "{\"status\": \"identical\", \"ahead_by\": 1, \"behind_by\": 0}", c));
    EXPECT(!parseCompare(200, "{\"status\": \"diverged\", \"ahead_by\": 1, \"behind_by\": 0}", c));
    EXPECT(!parseCompare(200, "{\"status\": \"ahead\", \"ahead_by\": \"2\", \"behind_by\": 0}", c));
    EXPECT(!parseCompare(200, "{\"status\": \"sideways\", \"ahead_by\": 2, \"behind_by\": 0}", c));
    EXPECT(parseCompare(200, "{\"status\": \"ahead\", \"ahead_by\": 7, \"behind_by\": 0}", c) && c.aheadBy == 7);
    EXPECT(std::string(compareStatusName(CMP_AHEAD)) == "ahead" && std::string(compareStatusName(CMP_NOT_FOUND)) == "not found");
}

static void testCommitList(const std::string& dir) {
    std::vector<CommitInfo> cs;
    EXPECT(parseCommitList(readFile(dir + "commits_main.json"), cs) && cs.size() == 6);
    if (cs.size() != 6) return;
    EXPECT(cs[0].sha == kHead && cs[0].parents == 2 && cs[0].date == "2026-10-20T04:41:51Z");
    EXPECT(cs[1].sha == "2c80a3802485f8a31b3720a9e19f25cee2cb3808" && cs[1].parents == 1);
    EXPECT(cs[2].sha == kPr52 && cs[4].sha == kPr51);
    EXPECT(cs[0].message.find("Merge pull request #53: the hand-built world's file format frozen at v1") == 0);
    // a build of #52 (837b39c): two commits later, #53
    NewChanges n;
    EXPECT(newChangesSince(cs, kPr52, n) && n.head == kHead && n.complete && n.commits == 2 && n.pulls.size() == 1);
    if (n.pulls.size() == 1)
        EXPECT(n.pulls[0].number == 53 && n.pulls[0].title == "the hand-built world's file format frozen at v1, and worldv checks the world files");
    EXPECT(changesSummary(n, 200) == "1 new update: #53 the hand-built world's file format frozen at v1, and worldv checks the world files");
    // a build of #51 (f595703): #53 and #52, newest first
    EXPECT(newChangesSince(cs, kPr51, n) && n.complete && n.commits == 4 && n.pulls.size() == 2);
    if (n.pulls.size() == 2) EXPECT(n.pulls[0].number == 53 && n.pulls[1].number == 52);
    EXPECT(changesSummary(n, 60) == "2 new updates: #53 the hand-built world's file format...");
    EXPECT(changesSummary(n, 1000).find("2 new updates: #53 the hand-built world's file format frozen at v1, and worldv checks the world files, "
                                        "#52 the game updates itself") == 0);
    // a build without a commit lists main's latest
    EXPECT(newChangesSince(cs, "", n) && !n.complete && !n.sinceBase && n.commits == 6 && n.pulls.size() == 3);
    EXPECT(changesSummary(n, 30) == "Latest: #53 the hand-built...");
    EXPECT(changesSummary(n, 1000).find("Latest: #53 the hand-built world's file format frozen at v1, and worldv checks the world files, #52 ") == 0);
    // a commit older than the list
    EXPECT(newChangesSince(cs, std::string(40, 'a'), n) && !n.complete && n.sinceBase && n.commits == 6);
    EXPECT(changesSummary(n, 25) == "3+ new updates: #53...");
    // main's head itself: nothing new
    EXPECT(newChangesSince(cs, kHead, n) && n.complete && n.commits == 0 && n.pulls.empty());
    std::vector<CommitInfo> none;
    EXPECT(!newChangesSince(none, kHead, n));
    // shapes: an error object is not a list; entries without a SHA are skipped
    EXPECT(!parseCommitList("{\"message\": \"Not Found\"}", cs) && cs.empty());
    EXPECT(!parseCommitList("", cs));
    EXPECT(parseCommitList(std::string("[1, {\"sha\": \"xyz\"}, {\"sha\": \"") + kHead + "\", \"commit\": {\"message\": \"m\"}, \"parents\": [{}, {}]}]", cs) &&
           cs.size() == 1 && cs[0].message == "m" && cs[0].parents == 2 && cs[0].date.empty());
}

static void testPullMessages() {
    PullRequest p;
    EXPECT(pullFromMessage("Merge pull request #53: the hand-built world's file format frozen at v1\n\nbody", p) && p.number == 53 &&
           p.title == "the hand-built world's file format frozen at v1");
    // GitHub's default merge message: the title on the first line of the body
    EXPECT(pullFromMessage("Merge pull request #7 from someone/night-lights\n\nNight lighting realism\n\nmore", p) && p.number == 7 &&
           p.title == "Night lighting realism");
    EXPECT(pullFromMessage("Merge pull request #8 from someone/x\r\n\r\n  Taxi stands  \r\n", p) && p.number == 8 && p.title == "Taxi stands");
    EXPECT(pullFromMessage("Merge pull request #9 from someone/x", p) && p.number == 9 && p.title.empty());
    EXPECT(pullFromMessage("Merge pull request #10", p) && p.number == 10 && p.title.empty());
    // a squash merge
    EXPECT(pullFromMessage("Fix Esc immediately closing the pause menu (#44)\n\n* details", p) && p.number == 44 &&
           p.title == "Fix Esc immediately closing the pause menu");
    // titles become printable ASCII
    EXPECT(pullFromMessage("Merge pull request #12: Calle Luna \xE2\x80\x93 night \xE2\x80\x9C" "neon\xE2\x80\x9D \xF0\x9F\x8C\xB4 caf\xC3\xA9", p) &&
           p.title == "Calle Luna - night \"neon\" cafe");
    // not pull requests
    const char* others[] = {"The game updates itself", "Merge branch 'main' into feature", "Merge pull request #: x", "Merge pull request #12x: y",
                            "Merge pull request #0: zero", "Revert \"Merge pull request #52: x\"", "Fix the (#12) thing", "Title (#)", "Title (#1a)",
                            "Title(#12)", "", "Merge pull request #1234567890: too long"};
    for (const char* m : others) EXPECT(!pullFromMessage(m, p));
}

static void testSummary() {
    NewChanges n;
    n.sinceBase = true;
    n.complete = true;
    n.commits = 4;
    EXPECT(changesSummary(n, 100) == "4 new commits");
    n.commits = 1;
    EXPECT(changesSummary(n, 100) == "1 new commit");
    n.complete = false;
    EXPECT(changesSummary(n, 100) == "1+ new commits");
    n.complete = true;
    n.pulls = {{55, "Night lighting"}, {54, ""}, {53, "Taxi stands"}};
    EXPECT(changesSummary(n, 100) == "3 new updates: #55 Night lighting, #54, #53 Taxi stands");
    EXPECT(changesSummary(n, 40) == "3 new updates: #55 Night lighting...");
    EXPECT(changesSummary(n, 2) == ".." && changesSummary(n, 0).empty());
    std::string s = changesSummary(n, 30);
    EXPECT(s.size() <= 30 && s.find("...") == s.size() - 3);
    n.pulls.resize(1);
    EXPECT(changesSummary(n, 100) == "1 new update: #55 Night lighting");
    n.sinceBase = false;   // a build without a commit: main's latest
    EXPECT(changesSummary(n, 100) == "Latest: #55 Night lighting");
    n.pulls.clear();
    EXPECT(changesSummary(n, 100).empty());
}

static void testAscii() {
    EXPECT(asciiText("plain text") == "plain text");
    EXPECT(asciiText("  two   spaces\tand\na line  ") == "two spaces and a line");
    EXPECT(asciiText("a \xE2\x80\x94 b \xE2\x80\xA6 c \xE2\x80\x98q\xE2\x80\x99") == "a - b ... c 'q'");
    EXPECT(asciiText("\xC3\x80\xC3\xA7\xC3\xB1\xC3\x9F\xC3\xA6\xC3\x86") == "AcnssaeAE");
    EXPECT(asciiText("x \xC2\xB7 y \xE2\x86\x92 z") == "x - y -> z");
    EXPECT(asciiText("emoji \xF0\x9F\x8C\xB4 dropped") == "emoji dropped");
    EXPECT(asciiText("broken \xE2\x80 seq \xFF end \xC3") == "broken seq end");
    EXPECT(asciiText("ctrl\x01\x7f" "chars") == "ctrl chars");
    EXPECT(asciiText("").empty() && asciiText("\xF0\x9F\x8C\xB4").empty());
}

static void testToolchains() {
    ToolchainProbe p;
    EXPECT(pickToolchain(p).kind == TC_NONE);
    p.mingwDirs = {"C:\\w64devkit\\bin", "C:\\msys64\\ucrt64\\bin"};
    Toolchain t = pickToolchain(p);
    EXPECT(t.kind == TC_MINGW_DIR && t.pathPrefix == "C:\\w64devkit\\bin" && t.label == "MinGW-w64 (C:\\w64devkit\\bin)");
    p.gxxOnPath = true;   // g++ without windres on PATH: build.bat could not make the resource
    EXPECT(pickToolchain(p).kind == TC_MINGW_DIR);
    p.windresOnPath = true;
    t = pickToolchain(p);
    EXPECT(t.kind == TC_MINGW_PATH && t.pathPrefix.empty());
    p.clOnPath = true;
    EXPECT(pickToolchain(p).kind == TC_MSVC_PATH);
    p.vsInstall = "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools";
    EXPECT(pickToolchain(p).kind == TC_MSVC_PATH);   // no vcvars64.bat: build.bat goes on to cl.exe
    p.vcvars = true;
    t = pickToolchain(p);
    EXPECT(t.kind == TC_MSVC && t.label == "MSVC (C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools)" && t.pathPrefix.empty());
    std::vector<std::string> d = mingwCandidateDirs("C:", "C:\\Users\\Ana Maria\\");
    EXPECT(d.size() == 6 && d[0] == "C:\\w64devkit\\bin" && d[1] == "C:\\Users\\Ana Maria\\w64devkit\\bin" &&
           d[2] == "C:\\Users\\Ana Maria\\scoop\\apps\\w64devkit\\current\\bin" && d[3] == "C:\\msys64\\ucrt64\\bin" &&
           d[4] == "C:\\msys64\\mingw64\\bin" && d[5] == "C:\\mingw64\\bin");
    EXPECT(mingwCandidateDirs("D:\\", "").size() == 4 && mingwCandidateDirs("", "").empty());
    std::vector<std::string> path = splitPathList("C:\\Windows\\system32;;\"C:\\Program Files\\Git\\cmd\"; C:\\w64devkit\\bin ;");
    EXPECT(path.size() == 3 && path[0] == "C:\\Windows\\system32" && path[1] == "C:\\Program Files\\Git\\cmd" && path[2] == "C:\\w64devkit\\bin");
    EXPECT(splitPathList("").empty());
    // the bundled compiler: last, after every other one, or alone with --update-bundled
    ToolchainProbe b;
    b.bundled = "w64devkit 2.9.1";
    t = pickToolchain(b);
    EXPECT(t.kind == TC_BUNDLED && t.label == "bundled MinGW-w64 (w64devkit 2.9.1)" && t.pathPrefix.empty());
    b.mingwDirs = {"C:\\w64devkit\\bin"};
    EXPECT(pickToolchain(b).kind == TC_MINGW_DIR);
    b.vsInstall = "C:\\VS";
    b.vcvars = true;
    EXPECT(pickToolchain(b).kind == TC_MSVC);
    b.onlyBundled = true;
    EXPECT(pickToolchain(b).kind == TC_BUNDLED);
    b.bundled.clear();
    EXPECT(pickToolchain(b).kind == TC_NONE);   // --update-bundled without one: no build
}

static void testCommands() {
    // quoting as CommandLineToArgvW reads it back
    EXPECT(quoteArgument("plain") == "plain");
    EXPECT(quoteArgument("") == "\"\"");
    EXPECT(quoteArgument("C:\\Users\\Ana Maria\\x.tar.gz") == "\"C:\\Users\\Ana Maria\\x.tar.gz\"");
    EXPECT(quoteArgument("C:\\dir with space\\") == "\"C:\\dir with space\\\\\"");   // a closing quote after backslashes
    EXPECT(quoteArgument("say \"hi\"") == "\"say \\\"hi\\\"\"");
    EXPECT(quoteArgument("a\\\\\"b") == "\"a\\\\\\\\\\\"b\"");
    EXPECT(quoteArgument("C:\\no\\space\\") == "C:\\no\\space\\");
    ProcessCommand b = buildCommand("C:\\Windows\\system32", "C:\\Users\\Ana Maria\\AppData\\Local\\NeonTide\\updates\\x\\src");
    EXPECT(b.application == "C:\\Windows\\system32\\cmd.exe" && b.commandLine == "C:\\Windows\\system32\\cmd.exe /d /c .\\build.bat" &&
           b.directory == "C:\\Users\\Ana Maria\\AppData\\Local\\NeonTide\\updates\\x\\src");
    ProcessCommand u = unpackCommand("C:\\Windows\\System32\\", "C:\\Users\\Ana Maria\\u\\source.tar.gz", "C:\\Users\\Ana Maria\\u\\x\\");
    EXPECT(u.application == "C:\\Windows\\System32\\tar.exe" &&
           u.commandLine == "C:\\Windows\\System32\\tar.exe -xf \"C:\\Users\\Ana Maria\\u\\source.tar.gz\" -C \"C:\\Users\\Ana Maria\\u\\x\"" &&
           u.directory == "C:\\Users\\Ana Maria\\u\\x\\");
    // the environment: NT_COMMIT set, VERSION out, the PATH prefix in front, names kept, sorted without case
    std::vector<std::string> env = {"=C:=C:\\Games", "windir=C:\\Windows", "Path=C:\\Windows\\system32;C:\\Windows", "version=0.99",
                                    "NT_COMMIT=0000000000000000000000000000000000000000", "ALLUSERSPROFILE=C:\\ProgramData", "no-equals"};
    std::vector<std::string> e = buildEnvironment(env, kHead, "C:\\w64devkit\\bin");
    std::vector<std::string> want = {"=C:=C:\\Games", "ALLUSERSPROFILE=C:\\ProgramData", std::string("NT_COMMIT=") + kHead,
                                     "Path=C:\\w64devkit\\bin;C:\\Windows\\system32;C:\\Windows", "windir=C:\\Windows"};
    EXPECT(e == want);
    e = buildEnvironment({"PATH=", "B=2", "a=1"}, kHead, "");
    want = {"a=1", "B=2", std::string("NT_COMMIT=") + kHead, "PATH="};
    EXPECT(e == want);
    e = buildEnvironment({"B=2"}, kHead, "D:\\mingw64\\bin");
    want = {"B=2", std::string("NT_COMMIT=") + kHead, "PATH=D:\\mingw64\\bin"};
    EXPECT(e == want);
    e = buildEnvironment({"PATH=", "B=2"}, kHead, "D:\\mingw64\\bin");
    EXPECT(e.size() == 3 && e[2] == "PATH=D:\\mingw64\\bin");
    // NT_MINGW: set for the bundled compiler, never inherited
    e = buildEnvironment({"nt_mingw=C:\\old", "Path=C:\\Windows"}, kHead, "C:\\tc\\bin", "C:\\tc\\bin");
    want = {std::string("NT_COMMIT=") + kHead, "NT_MINGW=C:\\tc\\bin", "Path=C:\\tc\\bin;C:\\Windows"};
    EXPECT(e == want);
    e = buildEnvironment({"NT_MINGW=C:\\old"}, kHead, "");
    want = {std::string("NT_COMMIT=") + kHead};
    EXPECT(e == want);
}

static std::string sha256Of(const std::string& s) {
    PortableSha256 h;
    h.add(s.data(), s.size());
    return h.hex();
}

static void testPayload() {
    // SHA-256: FIPS 180-4's examples, a million 'a' in uneven pieces, and a hasher used again after hex()
    EXPECT(sha256Of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT(sha256Of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT(sha256Of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    PortableSha256 m;
    std::string as(1000, 'a');
    for (size_t done = 0, step = 1; done < 1000000; done += step, step = step % 997 + 1) m.add(as.data(), std::min(step, 1000000 - done));
    EXPECT(m.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    m.add("abc", 3);
    EXPECT(m.hex() == sha256Of("abc"));
    for (size_t n : {55, 56, 63, 64, 65, 119, 120})   // the padding's edges
        EXPECT(sha256Of(std::string(n, 'x')).size() == 64 && sha256Of(std::string(n, 'x')) != sha256Of(std::string(n + 1, 'x')));
    EXPECT(sha256Of(std::string(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    EXPECT(sha256Of(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    // the footer at the end of an exe
    Payload p;
    p.size = 67108864;
    p.sha256 = sha256Of("payload");
    p.label = "w64devkit 2.9.1";
    std::string f = payloadFooter(p);
    EXPECT(f.size() == kPayloadFooterBytes && f.substr(88) == "NTPAYLD1" && (uint8_t)f[0] == 0x23 && (uint8_t)f[35] == 0x04);
    std::string file = std::string(100, 'x') + f;   // the last bytes of a file
    uint64_t fileSize = p.size + kPayloadFooterBytes + 16 * 1024 * 1024;
    Payload q;
    EXPECT(parsePayloadFooter((const uint8_t*)file.data(), file.size(), fileSize, q) && q.size == p.size && q.sha256 == p.sha256 &&
           q.label == p.label);
    EXPECT(parsePayloadFooter((const uint8_t*)f.data(), f.size(), p.size + kPayloadFooterBytes + kPayloadMinExeBytes, q));
    EXPECT(!parsePayloadFooter((const uint8_t*)f.data(), f.size(), p.size + kPayloadFooterBytes + kPayloadMinExeBytes - 1, q) && q.size == 0);
    EXPECT(!parsePayloadFooter((const uint8_t*)f.data(), f.size() - 1, fileSize, q));
    std::string bad = f;
    bad[95] = '2';   // another format
    EXPECT(!parsePayloadFooter((const uint8_t*)bad.data(), bad.size(), fileSize, q));
    bad = f;
    bad[40 + 20] = 'x';   // text after the label's end
    EXPECT(!parsePayloadFooter((const uint8_t*)bad.data(), bad.size(), fileSize, q));
    bad = f;
    for (int i = 32; i < 40; i++) bad[i] = 0;   // an empty payload
    EXPECT(!parsePayloadFooter((const uint8_t*)bad.data(), bad.size(), fileSize, q));
    bad = f;
    bad[40] = 0;   // no label
    EXPECT(!parsePayloadFooter((const uint8_t*)bad.data(), bad.size(), fileSize, q));
    Payload l = p;
    l.label = std::string(48, 'L');
    std::string f48 = payloadFooter(l);
    EXPECT(f48.size() == kPayloadFooterBytes && parsePayloadFooter((const uint8_t*)f48.data(), f48.size(), fileSize, q) && q.label == l.label);
    l.label += "L";
    EXPECT(payloadFooter(l).empty());
    l.label = "caf\xc3\xa9";
    EXPECT(payloadFooter(l).empty());
    l = p;
    l.sha256[3] = 'A';   // upper-case hex is not the form records use
    EXPECT(payloadFooter(l).empty() && !isSha256Hex(l.sha256) && isSha256Hex(p.sha256));
    // payload.txt and the unpack folder
    std::string rec = payloadRecord(p);
    EXPECT(rec == "sha256 " + p.sha256 + "\nsize 67108864\nlabel w64devkit 2.9.1\n");
    EXPECT(parsePayloadRecord(rec, q) && q.size == p.size && q.sha256 == p.sha256 && q.label == p.label);
    EXPECT(parsePayloadRecord("label w64devkit 2.9.1\r\nnote made by build.bat\r\nsize 12\r\nsha256 " + p.sha256 + "\r\n", q) && q.size == 12);
    EXPECT(!parsePayloadRecord("sha256 " + p.sha256 + "\nlabel x\n", q) && q.label.empty());   // no size
    EXPECT(!parsePayloadRecord("sha256 " + p.sha256 + "\nsize 0\nlabel x\n", q));
    EXPECT(!parsePayloadRecord("sha256 " + p.sha256 + "\nsize -5\nlabel x\n", q));
    EXPECT(!parsePayloadRecord("sha256 1234\nsize 5\nlabel x\n", q));
    EXPECT(!parsePayloadRecord("sha256 " + p.sha256 + "\nsize 5\nlabel \n", q));
    EXPECT(payloadFolder(p.sha256) == p.sha256.substr(0, 16) && payloadFolder(p.sha256).size() == 16 && payloadFolder("abc").empty());
}

static void testChecks() {
    const uint8_t gz[] = {0x1f, 0x8b, 0x08, 0x00};
    EXPECT(archiveLooksValid(gz, sizeof(gz)));
    std::vector<uint8_t> tarHead(512, 0);
    memcpy(tarHead.data() + 257, "ustar", 5);
    EXPECT(archiveLooksValid(tarHead.data(), tarHead.size()));
    EXPECT(!archiveLooksValid(tarHead.data(), 300));
    const char html[] = "<!DOCTYPE html><html><body>Not Found</body></html>";
    EXPECT(!archiveLooksValid((const uint8_t*)html, sizeof(html)) && !archiveLooksValid(nullptr, 0) && !archiveLooksValid(gz, 1));
    // the top folders of GitHub's archives of bcb8a71 (github.com/.../archive/<sha>.tar.gz and the API's tarball)
    EXPECT(archiveFolderMatches("GTA-6-Claude-v0.5-bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e", kHead));
    EXPECT(archiveFolderMatches("nichtpascaltarter-sketch-GTA-6-Claude-v0.5-bcb8a71", kHead));
    EXPECT(!archiveFolderMatches("GTA-6-Claude-v0.5-837b39c751fa63ce8458be5366dd0635c9f320dc", kHead));
    EXPECT(!archiveFolderMatches("GTA-6-Claude-v0.5-bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e", "unknown"));
    EXPECT(!archiveFolderMatches("-bcb8a71", kHead) && !archiveFolderMatches("bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e", kHead));
    EXPECT(!archiveFolderMatches("GTA-6-Claude-v0.5-bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e.evil", kHead));
    EXPECT(withCrlf("@echo off\ngoto msvc\r\n:msvc\n") == "@echo off\r\ngoto msvc\r\n:msvc\r\n");
    EXPECT(withCrlf("\n\n") == "\r\n\r\n" && withCrlf("") == "" && withCrlf("a\r\nb") == "a\r\nb");
    // the commit in a stream of chunks, split anywhere
    std::string exe = std::string(5000, 'x') + kHead + std::string(300, 'y');
    for (size_t chunk : {(size_t)1, (size_t)7, (size_t)39, (size_t)40, (size_t)41, (size_t)4096, exe.size()}) {
        StreamSearch s(kHead);
        for (size_t i = 0; i < exe.size(); i += chunk) s.add(exe.data() + i, std::min(chunk, exe.size() - i));
        EXPECT(s.found());
    }
    StreamSearch miss(kHead);
    std::string other = std::string(5000, 'x') + std::string(kHead).substr(0, 39) + "z";
    miss.add(other.data(), other.size());
    EXPECT(!miss.found());
    std::vector<std::string> names = {kHead, kPr52, "readme.txt", "BCB8A71A0A5F8EEA6D057663B06D30BB5D4FE37E", kPr51, "..", "abc"};
    std::vector<std::string> gone = foldersToRemove(names, kHead);
    EXPECT(gone.size() == 2 && gone[0] == kPr52 && gone[1] == kPr51);
    EXPECT(foldersToRemove(names, "").size() == 3);
}

// A minimal x64 PE header: MZ at 0, PE at 0x80, three sections inside a 0x5000-byte file
static std::vector<uint8_t> fakePe() {
    std::vector<uint8_t> h(0x400, 0);
    auto put16 = [&](size_t o, uint32_t v) { h[o] = (uint8_t)(v & 0xff); h[o + 1] = (uint8_t)(v >> 8); };
    auto put32 = [&](size_t o, uint32_t v) { put16(o, v & 0xffff); put16(o + 2, v >> 16); };
    h[0] = 'M';
    h[1] = 'Z';
    put32(0x3C, 0x80);
    h[0x80] = 'P';
    h[0x81] = 'E';
    put16(0x84, 0x8664);   // AMD64
    put16(0x86, 3);        // sections
    put16(0x94, 240);      // optional header size
    put16(0x96, 0x0022);   // executable image, large address aware
    size_t opt = 0x98;
    put16(opt, 0x20B);           // PE32+
    put32(opt + 16, 0x1000);     // entry point
    put32(opt + 56, 0x6000);     // size of image
    put32(opt + 60, 0x400);      // size of headers
    put16(opt + 68, 2);          // Windows GUI
    for (uint32_t s = 0; s < 3; s++) {
        size_t o = opt + 240 + s * 40;
        put32(o + 16, 0x1000);              // raw size
        put32(o + 20, 0x400 + s * 0x1000);  // raw position
    }
    return h;
}

static void testPe() {
    std::vector<uint8_t> h = fakePe();
    std::string why;
    EXPECT(peImageValid(h.data(), h.size(), 0x5000, &why));
    EXPECT(!peImageValid(h.data(), h.size(), 0x3000, &why) && !why.empty());   // truncated: a section past the end
    std::vector<uint8_t> b = h;
    b[0] = 'P';
    b[1] = 'K';
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // a zip
    b = h;
    b[0x84] = 0x4c;
    b[0x85] = 0x01;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // x86
    b = h;
    b[0x97] |= 0x20;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // a DLL
    b = h;
    b[0x98] = 0x0B;
    b[0x99] = 0x01;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // PE32
    b = h;
    b[0x3C] = 0xF0;
    b[0x3D] = 0xFF;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // PE header outside the file
    b = h;
    b[0x81] = 'X';
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // no PE signature
    b = h;
    b[0x98 + 68] = 1;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // a driver, not a program
    EXPECT(!peImageValid(h.data(), 0x100, 0x5000));       // section table not in the bytes read
    const char html[] = "<!DOCTYPE html><html><body>Not Found</body></html>";
    EXPECT(!peImageValid((const uint8_t*)html, sizeof(html), sizeof(html)));
    EXPECT(!peImageValid(nullptr, 0, 0));
}


static void testUrls() {
    Url u;
    EXPECT(parseUrl("https://api.github.com/repos/o/r/commits?sha=main&per_page=3", u) && u.https && u.host == "api.github.com" && u.port == 443 &&
           u.path == "/repos/o/r/commits?sha=main&per_page=3");
    EXPECT(parseUrl("http://127.0.0.1:8765/repos/o/r/compare/x...main", u) && !u.https && u.host == "127.0.0.1" && u.port == 8765 &&
           u.path == "/repos/o/r/compare/x...main");
    EXPECT(parseUrl("HTTPS://GitHub.COM", u) && u.host == "github.com" && u.path == "/");
    EXPECT(parseUrl("https://github.com?x=1#frag", u) && u.path == "/?x=1");
    EXPECT(parseUrl("https://[::1]:8443/x", u) && u.host == "::1" && u.port == 8443);
    const char* invalid[] = {"ftp://x/y", "file:///C:/x", "https://", "https:///x", "https://user@host/", "https://host:0/", "https://host:99999/",
                             "https://host:/", "https://ho st/", "javascript:alert(1)", "https://[::1/", "https://host:12a/", "//host/x", "mailto:x@y"};
    for (const char* s : invalid) EXPECT(!parseUrl(s, u));
    // https to GitHub only (the archive moves to codeload.github.com)
    for (const char* s : {"https://api.github.com/repos/o/r", "https://github.com/o/r/archive/x.tar.gz", "https://codeload.github.com/o/r/tar.gz/x",
                          "https://GITHUB.com/x"})
        EXPECT(urlAllowed(s, false) && urlAllowed(s, true));
    for (const char* s : {"https://example.com/a", "https://evilgithub.com/x", "https://github.com.evil.com/x", "https://objects.githubusercontent.com/x",
                          "http://github.com/x", "http://api.github.com/x"})
        EXPECT(!urlAllowed(s, false) && !urlAllowed(s, true));
    EXPECT(urlAllowed("http://127.0.0.1:8765/a", true) && urlAllowed("http://localhost/a", true) && urlAllowed("http://[::1]:80/a", true));
    EXPECT(!urlAllowed("http://127.0.0.1:8765/a", false));
    EXPECT(!urlAllowed("http://example.com/a", true) && !urlAllowed("http://127.0.0.2/a", true) && !urlAllowed("http://localhost.evil.com/", true));
    EXPECT(!urlAllowed("https://127.0.0.1/a", true));
    EXPECT(!urlAllowed("file:///C:/Windows/notepad.exe", true));
}

static void testRecords() {
    PendingInfo p;
    p.commit = kHead;
    p.from = kPr52;
    p.size = 12882944;
    p.sha256 = std::string(64, 'a');
    p.toolchain = "MinGW-w64 (C:\\w64devkit\\bin)";
    p.commits = 2;
    p.pulls = {{53, "the hand-built world's \"v1\" format"}, {52, "updates"}};
    PendingInfo q;
    EXPECT(pendingFromJson(pendingToJson(p), q) && q.commit == kHead && q.from == kPr52 && q.size == 12882944 && q.sha256 == p.sha256 &&
           q.toolchain == p.toolchain && q.commits == 2 && q.pulls.size() == 2);
    if (q.pulls.size() == 2) EXPECT(q.pulls[0].number == 53 && q.pulls[0].title == "the hand-built world's \"v1\" format" && q.pulls[1].number == 52);
    p.from = "unknown";
    EXPECT(pendingFromJson(pendingToJson(p), q) && q.from == "unknown");
    p.sha256 = std::string(64, 'B');   // read in lower case
    EXPECT(pendingFromJson(pendingToJson(p), q) && q.sha256 == std::string(64, 'b'));
    p.sha256 = std::string(64, 'a');
    PendingInfo bad = p;
    bad.from = kHead;   // not an update
    EXPECT(!pendingFromJson(pendingToJson(bad), q) && q.commit.empty());
    bad = p;
    bad.commit = "unknown";
    EXPECT(!pendingFromJson(pendingToJson(bad), q));
    bad = p;
    bad.from = "bcb8a71";
    EXPECT(!pendingFromJson(pendingToJson(bad), q));
    bad = p;
    bad.size = 0;
    EXPECT(!pendingFromJson(pendingToJson(bad), q));
    bad = p;
    bad.sha256 = "abc";
    EXPECT(!pendingFromJson(pendingToJson(bad), q));
    EXPECT(!pendingFromJson("{\"commit\": \"" + std::string(kHead) + "\"", q));
    EXPECT(!pendingFromJson("[]", q));
    // a build that failed: its toolchain
    std::string f = failedRecord("MSVC (C:\\VS\\BuildTools)", "build.bat exited with 1");
    EXPECT(f == "toolchain=MSVC (C:\\VS\\BuildTools)\nwhy=build.bat exited with 1\n" && failedToolchain(f) == "MSVC (C:\\VS\\BuildTools)");
    EXPECT(failedToolchain("why=x\r\ntoolchain=MinGW-w64 (g++ on PATH)\r\n") == "MinGW-w64 (g++ on PATH)");
    EXPECT(failedToolchain("").empty() && failedToolchain("toolchain=\n").empty());
}

int main(int argc, char** argv) {
    std::string dir = __FILE__;
    size_t slash = dir.find_last_of("/\\");
    dir = (slash == std::string::npos ? std::string() : dir.substr(0, slash + 1)) + "fixtures/";
    std::vector<std::string> programs;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--fixtures" && i + 1 < argc) {
            dir = std::string(argv[++i]) + "/";
        } else {
            programs.push_back(a);
        }
    }
    testCommits();
    testJson();
    testGitHubUrls();
    testCompare(dir);
    testCommitList(dir);
    testPullMessages();
    testSummary();
    testAscii();
    testToolchains();
    testCommands();
    testChecks();
    testPe();
    testUrls();
    testRecords();
    testPayload();
    for (const std::string& path : programs) {
        std::string data = readFile(path), why;
        bool ok = peImageValid((const uint8_t*)data.data(), std::min<size_t>(data.size(), 65536), data.size(), &why);
        std::printf("%s: %s%s\n", path.c_str(), ok ? "valid x64 PE" : "not a valid program: ", why.c_str());
        EXPECT(ok);
    }
    if (failures) {
        std::fprintf(stderr, "Updater logic tests: %d of %d checks failed.\n", failures, checks);
        return 1;
    }
    std::printf("Updater logic tests passed (%d checks).\n", checks);
    return 0;
}
