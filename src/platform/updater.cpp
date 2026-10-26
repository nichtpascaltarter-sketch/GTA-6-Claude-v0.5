// Automatic updates (updater.h): main's newest commit is compared over WinHTTP, its source downloaded, unpacked with
// tar.exe and built with build.bat in the background; the verified exe (SHA-256 with BCrypt) installs and restarts at
// the next start or on "Restart now".
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <atomic>
#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <thread>
#include "../core/version.h"
#include "platform.h"
#include "update_logic.h"
#include "updater.h"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4   // Windows 8.1 and later (older SDK headers lack it)
#endif

namespace Updater {
namespace win_detail {

// The repository the game updates from (public: no sign-in, about 60 API requests an hour per address; a check makes
// one, two when main has moved on)
const char* const kRepo = "nichtpascaltarter-sketch/GTA-6-Claude-v0.5";
// Where the README explains what building updates needs (the notice's button when no compiler is installed)
const char* const kHelpUrl = "https://github.com/nichtpascaltarter-sketch/GTA-6-Claude-v0.5#updates";
const size_t kMaxJsonBytes = 4u << 20, kHeadBytes = 64u << 10;
const long long kMaxArchiveBytes = 256ll << 20;
const DWORD kUnpackTimeoutMs = 10u * 60u * 1000u, kBuildTimeoutMs = 60u * 60u * 1000u, kProbeTimeoutMs = 30u * 1000u;
// Set for the process an update starts: the commit it was updated from (its log continues the old one)
const wchar_t* const kEnvUpdatedFrom = L"NEONTIDE_UPDATED_FROM";

enum Phase : int { PH_IDLE = 0, PH_CHECKING, PH_DOWNLOADING, PH_UNPACKING, PH_BUILDING, PH_READY, PH_FAILED, PH_BLOCKED, PH_DONE };
enum CheckResult : int { CR_NONE = 0, CR_READY, CR_FAILED };   // --update-test exit codes 1, 0, 2

// What the check thread shares with the menus. Allocated once and never freed: a check still running when the game
// exits must not find its state torn down by static destruction.
struct Shared {
    std::atomic<int> phase{PH_IDLE};
    std::atomic<long long> got{0};                 // download progress, bytes
    std::atomic<unsigned long long> buildStart{0};   // GetTickCount64 when the build started
    std::atomic<bool> running{false};              // a check thread is alive
    std::atomic<bool> cancel{false};               // stop it (Automatic Updates turned off, the game exits)
    std::mutex m;                                  // guards the rest
    std::string commit;                            // the update's commit (short)
    std::string changes;                           // what it brings, one line
    std::string detail;                            // PH_BLOCKED: why it cannot be built here
    std::wstring log;                              // PH_FAILED: the build log
    bool quiet = false;                            // the game shuts down: no more log lines from the thread
};
Shared& shared() {
    static Shared* s = new Shared;
    return *s;
}

// Main-thread state
bool g_restart = false;          // "Restart now" chosen
bool g_noCheck = false;          // no check this session (an update was just installed but does not report its commit)
bool g_allowLocalHttp = false;   // --update-url
int g_lastSetting = -1;          // Automatic Updates as the menus last showed it (-1 before the first menu frame)

// Log lines that may come from the check thread: none once the game shuts down (the log closes, then static
// destruction runs while a detached check may still be waiting on the network or a build)
void ulog(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Shared& S = shared();
    std::lock_guard<std::mutex> lk(S.m);
    if (!S.quiet) LogPrintf("%s", buf);
}

std::wstring widen(const std::string& s, UINT codePage = CP_UTF8) {
    int n = s.empty() ? 0 : MultiByteToWideChar(codePage, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)Max(n, 0), L'\0');
    if (n > 0) MultiByteToWideChar(codePage, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w, UINT codePage = CP_UTF8) {
    int n = w.empty() ? 0 : WideCharToMultiByte(codePage, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)Max(n, 0), '\0');
    if (n > 0) WideCharToMultiByte(codePage, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// A commit for the log: its 7 digits, or "unknown"
std::string commitLabel(const std::string& sha) { return isCommitSha(sha) ? shortCommit(sha) : std::string("unknown"); }

std::wstring exePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return std::wstring();
        if (n < buf.size()) return std::wstring(buf.data(), n);
        if (buf.size() >= 32768) return std::wstring();
        buf.resize(buf.size() * 2);
    }
}

std::wstring envVar(const wchar_t* name) {
    wchar_t small[512];
    DWORD n = GetEnvironmentVariableW(name, small, 512);
    if (n == 0) return std::wstring();
    if (n < 512) return std::wstring(small, n);
    std::vector<wchar_t> big(n);
    DWORD m = GetEnvironmentVariableW(name, big.data(), n);
    return m > 0 && m < n ? std::wstring(big.data(), m) : std::wstring();
}

std::wstring systemDir() {
    wchar_t buf[MAX_PATH];
    UINT n = GetSystemDirectoryW(buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(buf, n) : std::wstring(L"C:\\Windows\\System32");
}

bool fileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool dirExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

long long fileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return -1;
    return (long long)(((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow);
}

bool readSmallFile(const std::wstring& path, std::string& out, size_t maxBytes) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char buf[4096];
    DWORD got = 0;
    bool ok = true;
    while (ReadFile(f, buf, (DWORD)sizeof(buf), &got, nullptr) && got > 0) {
        if (out.size() + got > maxBytes) {
            ok = false;
            break;
        }
        out.append(buf, got);
    }
    CloseHandle(f);
    return ok;
}

// Writes next to `path` first, then replaces it, so a crash never leaves half a file
bool writeFileReplacing(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &wrote, nullptr) && wrote == data.size();
    CloseHandle(f);
    ok = ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!ok) DeleteFileW(tmp.c_str());
    return ok;
}

// A virus scanner may hold a freshly written file for a moment
bool moveFile(const std::wstring& from, const std::wstring& to, DWORD flags, int tries) {
    for (int i = 0; i < tries; i++) {
        if (MoveFileExW(from.c_str(), to.c_str(), flags)) return true;
        if (i + 1 < tries) Sleep(200);
    }
    return false;
}

// Creates a folder and its parents
bool makeDirs(const std::wstring& path) {
    if (path.empty() || dirExists(path)) return !path.empty();
    size_t cut = path.find_last_of(L"\\/", path.size() >= 2 ? path.size() - 2 : 0);
    if (cut != std::wstring::npos && cut > 2) makeDirs(path.substr(0, cut));
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// Deletes a file or a folder with everything in it. A link (a reparse point) is removed, never followed.
bool deleteTree(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), a & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    if ((a & FILE_ATTRIBUTE_DIRECTORY) == 0) return DeleteFileW(path.c_str()) != 0;
    if ((a & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) deleteTree(path + L"\\" + fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    return RemoveDirectoryW(path.c_str()) != 0;
}

// The names in a folder ("." and ".." left out); `dirs` receives which are folders
std::vector<std::wstring> listFolder(const std::wstring& path, std::vector<bool>* dirs = nullptr) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return names;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        names.push_back(fd.cFileName);
        if (dirs) dirs->push_back((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return names;
}

// <data folder>\updates\ (%LOCALAPPDATA%\NeonTide\updates\, or under --datadir), "" when it cannot be made
std::wstring updatesRoot() {
    std::wstring root = widen(Platform::userDataDir(), CP_ACP) + L"updates\\";
    return makeDirs(root.substr(0, root.size() - 1)) ? root : std::wstring();
}

// SHA-256 with the system's BCrypt provider
struct Sha256 {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> object;
    bool ok = false;
    Sha256() {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            alg = nullptr;
            return;
        }
        DWORD len = 0, got = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&len, (ULONG)sizeof(len), &got, 0))) return;
        object.resize(len);
        ok = BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, object.data(), len, nullptr, 0, 0));
        if (!ok) hash = nullptr;
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    void add(const void* data, size_t n) {
        if (ok && n > 0) ok = BCRYPT_SUCCESS(BCryptHashData(hash, (PUCHAR)data, (ULONG)n, 0));
    }
    // Lower-case hex, "" after an error
    std::string finish() {
        UCHAR d[32];
        if (!ok || !BCRYPT_SUCCESS(BCryptFinishHash(hash, d, (ULONG)sizeof(d), 0))) return std::string();
        ok = false;
        static const char kHex[] = "0123456789abcdef";
        std::string hex;
        for (UCHAR c : d) {
            hex.push_back(kHex[c >> 4]);
            hex.push_back(kHex[c & 15]);
        }
        return hex;
    }
};

// SHA-256, size and first bytes (for the PE check) of a file, and whether it contains `text`
bool hashFile(const std::wstring& path, std::string& hex, std::vector<uint8_t>& head, long long& size, const std::string& text, bool& found) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    found = false;
    if (f == INVALID_HANDLE_VALUE) return false;
    Sha256 h;
    StreamSearch search(text);
    std::vector<char> buf(1 << 20);
    DWORD got = 0;
    size = 0;
    head.clear();
    bool ok = true;
    for (;;) {
        if (!ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr)) {
            ok = false;
            break;
        }
        if (got == 0) break;
        h.add(buf.data(), got);
        search.add(buf.data(), got);
        if (head.size() < kHeadBytes) head.insert(head.end(), buf.data(), buf.data() + Min((size_t)got, kHeadBytes - head.size()));
        size += got;
    }
    CloseHandle(f);
    hex = h.finish();
    found = search.found();
    return ok && !hex.empty();
}

// ------------------------------------------------------------------------------------------------------------------
// HTTP: one GET at a time, synchronous, on the thread that asks; every handle is closed by that thread
struct NetHandle {
    HINTERNET h = nullptr;
    explicit NetHandle(HINTERNET x) : h(x) {}
    ~NetHandle() {
        if (h) WinHttpCloseHandle(h);
    }
    NetHandle(const NetHandle&) = delete;
    NetHandle& operator=(const NetHandle&) = delete;
};

struct HttpResult {
    int status = 0;          // HTTP status, 0 when no answer arrived
    bool complete = false;   // a 200 with the whole body
    long long bytes = 0;     // body bytes received
    std::string error;       // what went wrong (for the log)
};

std::string netError(DWORD e) {
    switch (e) {
    case ERROR_WINHTTP_TIMEOUT: return "timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "host not found (offline?)";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "cannot connect";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "connection lost";
    case ERROR_WINHTTP_SECURE_FAILURE: return "secure connection failed (certificate)";
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: return "invalid answer";
    case ERROR_WINHTTP_OPERATION_CANCELLED: return "cancelled";
    case ERROR_WINHTTP_REDIRECT_FAILED: return "redirect refused";
    default: return StrFormat("error %lu", (unsigned long)e);
    }
}

std::string headerValue(HINTERNET req, const wchar_t* name) {
    wchar_t buf[256];
    DWORD len = (DWORD)sizeof(buf);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, buf, &len, WINHTTP_NO_HEADER_INDEX)) return std::string();
    return narrow(std::wstring(buf, wcsnlen(buf, len / sizeof(wchar_t))));
}

// A GET with the updater's headers and timeouts; the body goes to `sink` in chunks (false stops the transfer).
// https only to GitHub, plain http only to this machine with the test override. Redirects (the source archive moves
// to codeload.github.com) are followed here, each target checked by the same rule before anything connects to it.
template <typename Sink>
HttpResult httpGet(const std::string& firstUrl, bool allowLocalHttp, const wchar_t* accept, int receiveTimeoutMs, Sink&& sink) {
    HttpResult r;
    auto failed = [&](const char* what) {
        r.error = std::string(what) + ": " + netError(GetLastError());
        return r;
    };
    std::wstring agent = widen(std::string("NeonTide-Updater/") + NT_VERSION);
    NetHandle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) return failed("WinHttpOpen");
    WinHttpSetTimeouts(session.h, 5000, 5000, 5000, receiveTimeoutMs);   // resolve, connect, send: 5 s; each receive
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(session.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, (DWORD)sizeof(policy));
    std::string url = firstUrl;
    for (int hop = 0; hop < 6; hop++) {
        Url u;
        if (!parseUrl(url, u) || !urlAllowed(url, allowLocalHttp)) {
            r.error = (hop ? "redirected to a refused URL " : "refused URL ") + url;
            return r;
        }
        NetHandle connection(WinHttpConnect(session.h, widen(u.host).c_str(), (INTERNET_PORT)u.port, 0));
        if (!connection.h) return failed("connect");
        NetHandle req(WinHttpOpenRequest(connection.h, L"GET", widen(u.path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.https ? WINHTTP_FLAG_SECURE : 0));
        if (!req.h) return failed("request");
        std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\n";
        if (!WinHttpSendRequest(req.h, headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return failed("send");
        if (!WinHttpReceiveResponse(req.h, nullptr)) return failed("response");
        DWORD status = 0, len = (DWORD)sizeof(status);
        WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                            WINHTTP_NO_HEADER_INDEX);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            wchar_t loc[2048];
            DWORD locLen = (DWORD)sizeof(loc);
            if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &locLen, WINHTTP_NO_HEADER_INDEX)) {
                r.status = (int)status;
                r.error = StrFormat("HTTP %lu without a location", (unsigned long)status);
                return r;
            }
            std::string next = narrow(std::wstring(loc, wcsnlen(loc, locLen / sizeof(wchar_t))));
            if (!next.empty() && next[0] == '/') {   // a path on the same server
                std::string host = u.host.find(':') != std::string::npos ? "[" + u.host + "]" : u.host;
                next = std::string(u.https ? "https://" : "http://") + host + ":" + std::to_string(u.port) + next;
            }
            url = next;
            continue;
        }
        r.status = (int)status;
        if (status != 200) {
            r.error = StrFormat("HTTP %lu", (unsigned long)status);
            if ((status == 403 || status == 429) && headerValue(req.h, L"X-RateLimit-Remaining") == "0") r.error += " (rate limited)";
            return r;
        }
        std::vector<char> buf(64 * 1024);
        for (;;) {
            if (shared().cancel) {
                r.error = "cancelled";
                return r;
            }
            DWORD got = 0;
            if (!WinHttpReadData(req.h, buf.data(), (DWORD)buf.size(), &got)) return failed("read");
            if (got == 0) break;
            r.bytes += got;
            if (!sink(buf.data(), (size_t)got)) {
                r.error = "stopped";
                return r;
            }
        }
        r.complete = true;
        return r;
    }
    r.error = "too many redirects";
    return r;
}

// ------------------------------------------------------------------------------------------------------------------
// When to update
struct Config {
    std::wstring exe;              // the running exe
    std::string api, web;          // GitHub's API and web addresses, or the test server for both
    bool allowLocalHttp = false;   // --update-url: plain http to this machine
};

Config makeConfig(const std::wstring& exe) {
    Config c;
    c.exe = exe;
    const char* url = Platform::argValue("update-url");
    c.allowLocalHttp = url != nullptr;
    c.api = url ? std::string(url) : std::string("https://api.github.com");
    c.web = url ? std::string(url) : std::string("https://github.com");
    return c;
}

// Automated runs never update: test harnesses (and CI) run the exe they built and stay off the network
bool automatedRun() {
    static const char* const kFlags[] = {"autotest",     "gfxselftest", "benchmark",   "autoplay",     "shot",      "viewer",
                                         "missiontest",  "mission",     "menushot",    "wildlifetest", "wildscene", "interiortest",
                                         "tidetest",     "holduptest",  "elevatortest", "gfxstress",   "fxdemo",    "weaponshowcase",
                                         "dumpuifont"};
    for (const char* f : kFlags)
        if (Platform::hasArg(f)) return true;
    return false;
}

// Settings > Gameplay > Automatic Updates, read from settings.ini (the game itself loads its settings later)
bool settingOn() {
#ifdef HAVE_GAME_UI
    std::string text;
    if (!readSmallFile(widen(Platform::userDataDir() + "settings.ini", CP_ACP), text, 1u << 20)) return true;
    UI::GameSettings s;
    UI::settingsFromText(text, s);
    return s.autoUpdate;
#else
    return true;
#endif
}

// Why this start neither installs nor checks for updates ("" when it does). The test override also checks during
// automated runs (never installing there).
const char* skipReason(bool check) {
    if (Platform::hasArg("no-update")) return "--no-update";
    if (check && Platform::hasArg("update-url")) return "";
    if (automatedRun()) return "automated run";
    if (!settingOn()) return "Automatic Updates is off in Settings";
    return "";
}

// The commit this process was updated from (NEONTIDE_UPDATED_FROM), taken out of the environment so it does not
// reach later processes
std::string takeUpdatedFrom() {
    wchar_t buf[64];
    DWORD n = GetEnvironmentVariableW(kEnvUpdatedFrom, buf, 64);
    if (n == 0 || n >= 64) return std::string();
    SetEnvironmentVariableW(kEnvUpdatedFrom, nullptr);
    std::string v = narrow(std::wstring(buf, n));
    return isCommitSha(v) || v == "unknown" ? v : std::string();
}

// ------------------------------------------------------------------------------------------------------------------
// Install
// Deletes <exe>.old from the last update, once the new version has shown it starts (its window and device are up).
// Right after an update the old process may still be exiting and its program file in use: the check thread retries
// for a few seconds.
void removeOld(const std::wstring& exe, int tries) {
    std::wstring old = exe + L".old";
    for (int i = 0; i < tries && fileExists(old); i++) {
        if (DeleteFileW(old.c_str())) {
            ulog("Updater: removed the previous version (%s)", narrow(old).c_str());
            return;
        }
        if (i + 1 < tries) Sleep(250);
    }
}

// Verifies <exe>.pending again (its record, built from this build's commit, size, SHA-256, PE image, the commit
// inside) and swaps it in: the running exe becomes <exe>.old and the pending file takes its name. A pending update
// that fails is deleted; one that cannot be moved into place stays for a later start. True when the new build is in
// place; `commit` receives its commit.
bool installPending(const std::wstring& exe, std::string& commit) {
    std::wstring pending = exe + L".pending", meta = exe + L".pending.json", old = exe + L".old";
    bool havePending = fileExists(pending), haveMeta = fileExists(meta);
    if (!havePending && !haveMeta) return false;
    std::string text, why, hex, peWhy;
    std::vector<uint8_t> head;
    long long size = -1;
    bool hasCommit = false;
    PendingInfo info;
    if (!havePending || !haveMeta) why = "incomplete";
    else if (!readSmallFile(meta, text, 256u << 10) || !pendingFromJson(text, info)) why = "unreadable record";
    else if (info.from != NT_COMMIT) why = "built for " + commitLabel(info.from) + ", not for this build (" + commitLabel(NT_COMMIT) + ")";
    else if (!hashFile(pending, hex, head, size, info.commit, hasCommit)) why = "unreadable file";
    else if (size != info.size) why = StrFormat("%lld bytes instead of %lld", size, info.size);
    else if (hex != info.sha256) why = "SHA-256 changed";
    else if (!peImageValid(head.data(), head.size(), (uint64_t)size, &peWhy)) why = "not a Windows program: " + peWhy;
    else if (!hasCommit) why = "it does not carry its commit";
    if (!why.empty()) {
        LOG("Updater: discarded the built update (%s)", why.c_str());
        DeleteFileW(pending.c_str());
        DeleteFileW(meta.c_str());
        return false;
    }
    std::string label = shortCommit(info.commit);
    DeleteFileW(old.c_str());
    if (!moveFile(exe, old, MOVEFILE_REPLACE_EXISTING, 3)) {
        LOG("Updater: could not move the running exe aside (error %lu): %s installs at a later start", GetLastError(), label.c_str());
        return false;
    }
    if (!moveFile(pending, exe, 0, 3)) {
        DWORD e = GetLastError();
        MoveFileExW(old.c_str(), exe.c_str(), 0);   // the running exe goes back
        LOG("Updater: could not put %s in place (error %lu): it installs at a later start", label.c_str(), e);
        return false;
    }
    DeleteFileW(meta.c_str());
    commit = info.commit;
    LOG("Updater: installed %s over %s (%s)", label.c_str(), commitLabel(NT_COMMIT).c_str(), narrow(exe).c_str());
    return true;
}

// Starts the exe again with this process's command line (the updater adds no flags of its own). `from` is the commit
// that was just replaced (nullptr when nothing changed); the new process gets it in NEONTIDE_UPDATED_FROM.
bool relaunch(const std::wstring& exe, const char* from) {
    if (from) SetEnvironmentVariableW(kEnvUpdatedFrom, widen(from).c_str());
    std::wstring cmd = GetCommandLineW();
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(L'\0');
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    DWORD err = GetLastError();
    SetEnvironmentVariableW(kEnvUpdatedFrom, nullptr);
    if (!ok) {
        LOG("Updater: could not start %s (error %lu)", narrow(exe).c_str(), err);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    LOG("Updater: started %s", narrow(exe).c_str());
    return true;
}

// The new build is in place but did not start: the player starts it (not in test runs). Only on the main thread.
void askToStartAgain(bool installed, bool test) {
    if (test) return;
    const char* msg = installed ? "Neon Tide was updated, but it could not restart itself.\n\nPlease start Neon Tide again."
                                : "Neon Tide could not restart itself.\n\nPlease start Neon Tide again.";
    MessageBoxA(nullptr, msg, "Neon Tide - update", MB_OK | MB_ICONINFORMATION);
}

// ------------------------------------------------------------------------------------------------------------------
// Child processes: the toolchain probe, tar.exe and the build. Below normal priority, no window, input from NUL,
// output to `out`; a job object ends the whole process tree when the game exits, on a timeout or when the update is
// cancelled.
struct ChildRun {
    bool started = false, finished = false, timedOut = false, cancelled = false;
    DWORD exitCode = 0, error = 0;
};

std::wstring environmentBlock(const std::vector<std::string>& entries) {
    std::wstring b;
    for (const std::string& e : entries) {
        b += widen(e);
        b.push_back(L'\0');
    }
    b.push_back(L'\0');
    return b;
}

std::vector<std::string> currentEnvironment() {
    std::vector<std::string> out;
    LPWCH env = GetEnvironmentStringsW();
    if (!env) return out;
    for (const wchar_t* p = env; *p; p += wcslen(p) + 1) out.push_back(narrow(p));
    FreeEnvironmentStringsW(env);
    return out;
}

ChildRun runChild(const ProcessCommand& pc, const std::wstring* envBlock, HANDLE out, DWORD timeoutMs) {
    ChildRun r;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    HANDLE outCopy = nullptr;
    if (in == INVALID_HANDLE_VALUE || !DuplicateHandle(GetCurrentProcess(), out, GetCurrentProcess(), &outCopy, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        r.error = GetLastError();
        if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
        return r;
    }
    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in;
    si.StartupInfo.hStdOutput = outCopy;
    si.StartupInfo.hStdError = outCopy;
    // the child inherits these two handles and no others of the game's
    HANDLE inherit[2] = {in, outCopy};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(Max(attrSize, (SIZE_T)64));
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)(void*)attrBuf.data();
    bool list = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) != 0;
    bool limited = list && UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);
    if (limited) si.lpAttributeList = attrs;
    DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT |
                  (limited ? EXTENDED_STARTUPINFO_PRESENT : 0);
    std::wstring app = widen(pc.application), dir = widen(pc.directory), cmd = widen(pc.commandLine);
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(L'\0');
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(app.c_str(), line.data(), nullptr, nullptr, TRUE, flags, envBlock ? (LPVOID)envBlock->c_str() : nullptr,
                             dir.c_str(), &si.StartupInfo, &pi);
    r.error = ok ? 0 : GetLastError();
    if (list) DeleteProcThreadAttributeList(attrs);
    CloseHandle(in);
    CloseHandle(outCopy);
    if (!ok) return r;
    r.started = true;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, (DWORD)sizeof(li)) || !AssignProcessToJobObject(job, pi.hProcess)) {
            ulog("Updater: %s runs without a job object (error %lu)", narrow(app).c_str(), GetLastError());
            CloseHandle(job);
            job = nullptr;
        }
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    ULONGLONG start = GetTickCount64();
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 250) == WAIT_OBJECT_0) {
            r.finished = true;
            break;
        }
        if (shared().cancel) {
            r.cancelled = true;
            break;
        }
        if (GetTickCount64() - start > timeoutMs) {
            r.timedOut = true;
            break;
        }
    }
    if (!r.finished) {
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 10000);
    }
    GetExitCodeProcess(pi.hProcess, &r.exitCode);
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);   // ends what the build left running (compiler servers)
    return r;
}

std::string childProblem(const ChildRun& r, const char* what) {
    if (!r.started) return StrFormat("%s did not start (error %lu)", what, (unsigned long)r.error);
    if (r.cancelled) return StrFormat("%s stopped", what);
    if (r.timedOut) return StrFormat("%s took too long", what);
    if (r.exitCode != 0) return StrFormat("%s exited with %lu", what, (unsigned long)r.exitCode);
    return std::string();
}

// A log file for the children's output, written at its end (each child gets an inheritable copy of the handle, and
// they take turns)
HANDLE openLog(const std::wstring& path, bool truncate) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, truncate ? CREATE_ALWAYS : OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    SetFilePointer(f, 0, nullptr, FILE_END);
    return f;
}

void logLine(HANDLE log, const std::string& s) {
    if (!log) return;
    std::string t = s + "\r\n";
    DWORD wrote = 0;
    WriteFile(log, t.data(), (DWORD)t.size(), &wrote, nullptr);
}

// The last lines of a log, into the game's log (why a build failed)
void logTail(const std::wstring& path, int lines) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size = {};
    GetFileSizeEx(f, &size);
    LARGE_INTEGER at;
    at.QuadPart = Max(0LL, (long long)size.QuadPart - 4096);
    SetFilePointerEx(f, at, nullptr, FILE_BEGIN);
    char buf[4096];
    DWORD got = 0;
    ReadFile(f, buf, (DWORD)sizeof(buf), &got, nullptr);
    CloseHandle(f);
    std::vector<std::string> all;
    std::string cur;
    for (DWORD i = 0; i < got; i++) {
        char c = buf[i];
        if (c == '\n') {
            if (!cur.empty()) all.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) all.push_back(cur);
    for (size_t i = all.size() > (size_t)lines ? all.size() - (size_t)lines : 0; i < all.size(); i++)
        ulog("Updater:   build.log| %s", asciiText(all[i]).c_str());
}

// ------------------------------------------------------------------------------------------------------------------
// The toolchain (update_logic.h: pickToolchain)
// vswhere's newest Visual Studio with the x64 C++ tools (build.bat asks the same), "" for none
std::string findVisualStudio(const std::wstring& work) {
    std::wstring pf = envVar(L"ProgramFiles(x86)");
    if (pf.empty()) pf = envVar(L"ProgramFiles");
    std::wstring vswhere = pf + L"\\Microsoft Visual Studio\\Installer\\vswhere.exe";
    if (pf.empty() || !fileExists(vswhere)) return std::string();
    std::wstring outPath = work + L"vswhere.txt";
    HANDLE out = openLog(outPath, true);
    if (!out) return std::string();
    ProcessCommand pc;
    pc.application = narrow(vswhere);
    pc.commandLine = quoteArgument(pc.application) +
                     " -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath";
    pc.directory = narrow(work);
    ChildRun r = runChild(pc, nullptr, out, kProbeTimeoutMs);
    CloseHandle(out);
    std::string text;
    readSmallFile(outPath, text, 64u << 10);
    DeleteFileW(outPath.c_str());
    if (!childProblem(r, "vswhere").empty()) return std::string();
    size_t e = text.find_first_of("\r\n");
    std::string first = text.substr(0, e);
    while (!first.empty() && first.back() == ' ') first.pop_back();
    return narrow(widen(first, CP_ACP));   // (vswhere writes the console's code page when redirected)
}

Toolchain findToolchain(const std::wstring& work) {
    ToolchainProbe p;
    p.vsInstall = findVisualStudio(work);
    p.vcvars = !p.vsInstall.empty() && fileExists(widen(p.vsInstall) + L"\\VC\\Auxiliary\\Build\\vcvars64.bat");
    for (const std::string& d : splitPathList(narrow(envVar(L"PATH")))) {
        std::wstring w = widen(d);
        p.clOnPath = p.clOnPath || fileExists(w + L"\\cl.exe");
        p.gxxOnPath = p.gxxOnPath || fileExists(w + L"\\g++.exe");
        p.windresOnPath = p.windresOnPath || fileExists(w + L"\\windres.exe");
    }
    for (const std::string& d : mingwCandidateDirs(narrow(envVar(L"SystemDrive")), narrow(envVar(L"USERPROFILE")))) {
        std::wstring w = widen(d);
        if (fileExists(w + L"\\g++.exe") && fileExists(w + L"\\windres.exe")) p.mingwDirs.push_back(d);
    }
    return pickToolchain(p);
}

// ------------------------------------------------------------------------------------------------------------------
// The check (background thread, or the main thread for --update-test)
CheckResult finish(int phase, CheckResult result) {
    shared().phase = phase;
    return result;
}

// The menus explain why this update cannot be built here
CheckResult blocked(const std::string& why) {
    Shared& S = shared();
    {
        std::lock_guard<std::mutex> lk(S.m);
        S.detail = why;
    }
    ulog("Updater: not built: %s", why.c_str());
    return finish(PH_BLOCKED, CR_FAILED);
}

CheckResult buildFailed(const std::wstring& dir, const std::string& toolchain, const std::string& why, bool remember) {
    Shared& S = shared();
    ulog("Updater: the update build failed (%s); this version keeps running. Its log: %s", why.c_str(), narrow(dir + L"build.log").c_str());
    logTail(dir + L"build.log", 12);
    if (remember) writeFileReplacing(dir + L"failed.txt", failedRecord(toolchain, why));
    {
        std::lock_guard<std::mutex> lk(S.m);
        S.log = dir + L"build.log";
    }
    return finish(PH_FAILED, CR_FAILED);
}

// The update folders of other commits go (each folder holds one commit's archive, source, build and log)
void cleanUpdates(const std::wstring& root, const std::string& keep) {
    if (root.empty()) return;
    std::vector<bool> dirs;
    std::vector<std::wstring> names = listFolder(root, &dirs);
    std::vector<std::string> folders;
    for (size_t i = 0; i < names.size(); i++)
        if (dirs[i]) folders.push_back(narrow(names[i]));
    for (const std::string& name : foldersToRemove(folders, keep))
        if (deleteTree(root + widen(name))) ulog("Updater: removed the old update folder %s", name.c_str());
}

// A verified build of `commit` from an earlier session still waits (the next start checks it in full)
bool pendingWaits(const std::wstring& exe, const std::string& commit) {
    std::string text;
    PendingInfo info;
    return readSmallFile(exe + L".pending.json", text, 256u << 10) && pendingFromJson(text, info) && info.commit == commit &&
           info.from == NT_COMMIT && fileSize(exe + L".pending") == info.size;
}

// Next to the game, files can be written (where the build becomes <exe>.pending)
bool canWriteNextTo(const std::wstring& exe) {
    std::wstring probe = exe + L".download";
    HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    CloseHandle(f);
    DeleteFileW(probe.c_str());
    return true;
}

// One game at a time downloads and builds (two running games share the update folders)
struct UpdateLock {
    HANDLE h = nullptr;
    bool held = false;
    UpdateLock() {
        h = CreateMutexW(nullptr, FALSE, L"Local\\NeonTide-update-build");
        held = !h || WaitForSingleObject(h, 0) != WAIT_TIMEOUT;   // (without a mutex, no lock)
    }
    ~UpdateLock() {
        if (held && h) ReleaseMutex(h);
        if (h) CloseHandle(h);
    }
    UpdateLock(const UpdateLock&) = delete;
    UpdateLock& operator=(const UpdateLock&) = delete;
};

// The source archive of `sha` to <dir>source.tar.gz (through .part)
bool downloadArchive(const Config& cfg, const std::string& sha, const std::wstring& dir, std::string& why) {
    Shared& S = shared();
    std::wstring part = dir + L"source.tar.gz.part", archive = dir + L"source.tar.gz";
    HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        why = StrFormat("cannot write %s (error %lu)", narrow(part).c_str(), GetLastError());
        return false;
    }
    S.got = 0;
    S.phase = PH_DOWNLOADING;
    std::string url = archiveUrl(cfg.web, kRepo, sha);
    ulog("Updater: downloading %s", url.c_str());
    std::vector<uint8_t> head;
    long long got = 0;
    bool tooLarge = false;
    DWORD writeError = 0;
    HttpResult hr = httpGet(url, cfg.allowLocalHttp, L"application/octet-stream", 30000, [&](const char* data, size_t n) {
        if (got + (long long)n > kMaxArchiveBytes) {
            tooLarge = true;
            return false;
        }
        DWORD wrote = 0;
        if (!WriteFile(f, data, (DWORD)n, &wrote, nullptr) || wrote != n) {
            writeError = Max(GetLastError(), (DWORD)1);
            return false;
        }
        if (head.size() < 512) head.insert(head.end(), (const uint8_t*)data, (const uint8_t*)data + Min(n, (size_t)512 - head.size()));
        got += (long long)n;
        S.got = got;
        return true;
    });
    CloseHandle(f);
    if (S.cancel) why = "cancelled";
    else if (tooLarge) why = "larger than " + std::to_string(kMaxArchiveBytes >> 20) + " MB";
    else if (writeError) why = StrFormat("could not write the file (error %lu)", (unsigned long)writeError);
    else if (!hr.complete) why = hr.error;
    else if (!archiveLooksValid(head.data(), head.size())) why = "not a source archive";
    else if (!moveFile(part, archive, MOVEFILE_REPLACE_EXISTING, 10)) why = StrFormat("could not keep it (error %lu)", GetLastError());
    if (!why.empty()) {
        DeleteFileW(part.c_str());
        return false;
    }
    ulog("Updater: downloaded the source of %s (%lld bytes)", shortCommit(sha).c_str(), got);
    return true;
}

// tar.exe unpacks the archive into <dir>x; its one top folder (named for the commit) becomes <dir>src
bool unpackArchive(const std::wstring& dir, const std::string& sha, HANDLE log, std::string& why) {
    std::wstring x = dir + L"x", src = dir + L"src";
    deleteTree(x);
    deleteTree(src);
    if (!CreateDirectoryW(x.c_str(), nullptr)) {
        why = StrFormat("cannot create %s (error %lu)", narrow(x).c_str(), GetLastError());
        return false;
    }
    ProcessCommand pc = unpackCommand(narrow(systemDir()), narrow(dir + L"source.tar.gz"), narrow(x));
    logLine(log, "> " + pc.commandLine);
    ChildRun r = runChild(pc, nullptr, log, kUnpackTimeoutMs);
    why = childProblem(r, "tar.exe");
    if (!why.empty()) return false;
    std::vector<bool> dirs;
    std::vector<std::wstring> names = listFolder(x, &dirs);
    if (names.size() != 1 || !dirs[0]) {
        why = StrFormat("the archive holds %u entries at the top instead of one folder", (unsigned)names.size());
        return false;
    }
    if (!archiveFolderMatches(narrow(names[0]), sha)) {
        why = "the archive's folder " + narrow(names[0]) + " does not name commit " + shortCommit(sha);
        return false;
    }
    if (!moveFile(x + L"\\" + names[0], src, 0, 10)) {
        why = StrFormat("could not move the source into place (error %lu)", GetLastError());
        return false;
    }
    RemoveDirectoryW(x.c_str());
    return true;
}

// Asks GitHub whether main is ahead of this build and what it brings. True when there is an update to build;
// otherwise `result` is the check's result (and its phase is set).
bool findUpdate(const Config& cfg, NewChanges& changes, CheckResult& result) {
    Shared& S = shared();
    const std::string base = NT_COMMIT;
    const bool known = isCommitSha(base);
    auto none = [&]() {   // no update: the check ends here
        result = finish(S.cancel ? PH_IDLE : PH_DONE, CR_NONE);
        return false;
    };
    ulog("Updater: comparing with main (running %s, commit %s)", NT_VERSION, commitLabel(base).c_str());
    std::string body;
    auto getJson = [&](const std::string& url) {
        body.clear();
        return httpGet(url, cfg.allowLocalHttp, L"application/vnd.github+json", 10000, [&](const char* data, size_t n) {
            if (body.size() + n > kMaxJsonBytes) return false;
            body.append(data, n);
            return true;
        });
    };
    int listCount = 10;
    if (known) {
        HttpResult hr = getJson(compareUrl(cfg.api, kRepo, base));
        Comparison cmp;
        if (!parseCompare(hr.status, body, cmp)) {
            ulog("Updater: no update (%s)", hr.status == 200 ? "the comparison could not be read" : hr.error.c_str());
            return none();
        }
        switch (cmp.status) {
        case CMP_IDENTICAL: {
            ulog("Updater: up to date (main is at %s)", shortCommit(base).c_str());
            UpdateLock lock;   // (not while another game builds)
            if (lock.held) cleanUpdates(updatesRoot(), base);   // keeps the folder with this build's log
            return none();
        }
        case CMP_BEHIND:
            ulog("Updater: no update: this build is %d commit%s ahead of main", cmp.behindBy, cmp.behindBy == 1 ? "" : "s");
            return none();
        case CMP_DIVERGED:
            ulog("Updater: no update: commit %s is not on main (diverged: %d commit%s of its own, %d new on main)", shortCommit(base).c_str(),
                 cmp.behindBy, cmp.behindBy == 1 ? "" : "s", cmp.aheadBy);
            return none();
        case CMP_NOT_FOUND:
            ulog("Updater: no update: GitHub does not know commit %s (a local commit?)", shortCommit(base).c_str());
            return none();
        default:
            break;
        }
        ulog("Updater: main has %d new commit%s", cmp.aheadBy, cmp.aheadBy == 1 ? "" : "s");
        listCount = commitsToList(cmp.aheadBy);
    } else {
        ulog("Updater: this build does not know its commit: it updates to main's newest");
    }
    if (S.cancel) return none();
    HttpResult hr = getJson(commitsUrl(cfg.api, kRepo, listCount));
    std::vector<CommitInfo> commits;
    if (!hr.complete || !parseCommitList(body, commits) || !newChangesSince(commits, known ? base : std::string(), changes)) {
        ulog("Updater: no update (main's commits: %s)", hr.complete ? "unreadable" : hr.error.c_str());
        return none();
    }
    if (changes.head == base) {
        ulog("Updater: up to date (main is at %s)", shortCommit(base).c_str());
        return none();
    }
    std::string summary = changesSummary(changes, 150);
    ulog("Updater: main is at %s (%s): %s", shortCommit(changes.head).c_str(), changes.headDate.c_str(),
         summary.empty() ? "no pull requests listed" : summary.c_str());
    for (const PullRequest& pr : changes.pulls) ulog("Updater:   #%d %s", pr.number, pr.title.c_str());
    std::lock_guard<std::mutex> lk(S.m);
    S.commit = shortCommit(changes.head);
    S.changes = summary;
    return true;
}

// Builds the unpacked source in <dir>src\ with build.bat and keeps the exe as <exe>.pending
CheckResult buildSource(const Config& cfg, const NewChanges& changes, const Toolchain& tc, const std::wstring& dir, HANDLE log) {
    Shared& S = shared();
    const std::string& sha = changes.head;
    // build.bat with CRLF line ends (cmd.exe misreads labels in a batch file with LF ones)
    std::wstring src = dir + L"src";
    std::string bat;
    if (!readSmallFile(src + L"\\build.bat", bat, 1u << 20)) {
        logLine(log, "Updater: the source has no build.bat");
        return buildFailed(dir, tc.label, "the source has no build.bat", true);
    }
    std::string crlf = withCrlf(bat);
    if (crlf != bat) writeFileReplacing(src + L"\\build.bat", crlf);
    ProcessCommand pc = buildCommand(narrow(systemDir()), narrow(src));
    std::wstring env = environmentBlock(buildEnvironment(currentEnvironment(), sha, tc.pathPrefix));
    logLine(log, "> " + pc.commandLine + "   (in " + pc.directory + ", NT_COMMIT=" + sha +
                     (tc.pathPrefix.empty() ? std::string() : ", PATH from " + tc.pathPrefix) + ")");
    S.buildStart = GetTickCount64();
    S.phase = PH_BUILDING;
    ulog("Updater: building %s (%s)", shortCommit(sha).c_str(), narrow(dir + L"build.log").c_str());
    ChildRun r = runChild(pc, &env, log, kBuildTimeoutMs);
    double seconds = (double)(GetTickCount64() - S.buildStart) / 1000.0;
    if (r.cancelled) {
        logLine(log, "Updater: the build was stopped");
        ulog("Updater: the build of %s stopped", shortCommit(sha).c_str());
        return finish(PH_IDLE, CR_NONE);
    }
    // the exe it built: an x64 program that carries its commit, copied next to the game and checked there
    std::wstring built = src + L"\\bin\\NeonTide.exe", part = cfg.exe + L".download", pending = cfg.exe + L".pending";
    std::string why = childProblem(r, "build.bat"), hex, peWhy;
    std::vector<uint8_t> head;
    long long size = -1;
    bool hasCommit = false;
    if (why.empty() && !fileExists(built)) why = "build.bat made no bin\\NeonTide.exe";
    if (why.empty() && !CopyFileW(built.c_str(), part.c_str(), FALSE)) why = StrFormat("could not copy the exe next to the game (error %lu)", GetLastError());
    if (why.empty() && !hashFile(part, hex, head, size, sha, hasCommit)) why = "could not read the copied exe";
    if (why.empty() && !peImageValid(head.data(), head.size(), (uint64_t)size, &peWhy)) why = "the build is not a Windows program: " + peWhy;
    if (why.empty() && !hasCommit) why = "the build does not carry its commit (NT_COMMIT)";
    if (!why.empty()) {
        logLine(log, StrFormat("Updater: %s (after %.0f s)", why.c_str(), seconds));
        DeleteFileW(part.c_str());
        return buildFailed(dir, tc.label, why, true);
    }
    PendingInfo info;
    info.commit = sha;
    info.from = isCommitSha(NT_COMMIT) ? std::string(NT_COMMIT) : std::string("unknown");
    info.size = size;
    info.sha256 = hex;
    info.toolchain = tc.label;
    info.pulls = changes.pulls;
    info.commits = changes.commits;
    if (!moveFile(part, pending, MOVEFILE_REPLACE_EXISTING, 10) || !writeFileReplacing(cfg.exe + L".pending.json", pendingToJson(info))) {
        why = StrFormat("could not keep the build next to the game (error %lu)", GetLastError());
        logLine(log, "Updater: " + why);
        DeleteFileW(part.c_str());
        DeleteFileW(pending.c_str());
        return buildFailed(dir, tc.label, why, false);
    }
    logLine(log, StrFormat("Updater: built in %.0f s: %lld bytes, SHA-256 %s", seconds, size, hex.c_str()));
    ulog("Updater: built %s in %.0f s (%lld bytes, SHA-256 %s): it installs at the next start", shortCommit(sha).c_str(), seconds, size, hex.c_str());
    return finish(PH_READY, CR_READY);
}

// Downloads, unpacks and builds the update in its own folder (what it needs is checked first)
CheckResult buildUpdate(const Config& cfg, const NewChanges& changes) {
    Shared& S = shared();
    const std::string& sha = changes.head;
    if (pendingWaits(cfg.exe, sha)) {
        ulog("Updater: %s was built before and installs at the next start", shortCommit(sha).c_str());
        return finish(PH_READY, CR_READY);
    }
    // what building needs: a folder of its own, a C++ toolchain, tar.exe and a game folder that can be written
    std::wstring root = updatesRoot();
    if (root.empty()) return blocked("Neon Tide has no folder for updates (" + Platform::userDataDir() + "updates).");
    Toolchain tc = findToolchain(root);
    if (tc.kind == TC_NONE)
        return blocked("Updates are built from source and need a C++ compiler: Visual Studio Build Tools (free) or w64devkit.");
    if (!fileExists(systemDir() + L"\\tar.exe")) return blocked("Updates are unpacked with tar.exe, which comes with Windows 10 version 1803 and later.");
    if (!canWriteNextTo(cfg.exe)) return blocked("Neon Tide's folder is read-only, so it cannot update itself there.");
    UpdateLock lock;
    if (!lock.held) {
        ulog("Updater: another Neon Tide is updating: not built this time");
        return finish(PH_DONE, CR_NONE);
    }
    std::wstring dir = root + widen(sha) + L"\\";
    if (!makeDirs(dir.substr(0, dir.size() - 1))) return blocked("The update folder " + narrow(dir) + " cannot be created.");
    std::string failed;
    if (readSmallFile(dir + L"failed.txt", failed, 4096) && failedToolchain(failed) == asciiText(tc.label)) {
        ulog("Updater: the build of %s failed before with %s: not built again (%s)", shortCommit(sha).c_str(), tc.label.c_str(),
             narrow(dir + L"build.log").c_str());
        std::lock_guard<std::mutex> lk(S.m);
        S.log = dir + L"build.log";
        return finish(PH_FAILED, CR_FAILED);
    }
    cleanUpdates(root, sha);
    ulog("Updater: building with %s", tc.label.c_str());
    // the source (an archive from an earlier try is complete: it is only kept under its final name)
    std::string why;
    if (!fileExists(dir + L"source.tar.gz") && !downloadArchive(cfg, sha, dir, why)) {
        if (S.cancel) {
            ulog("Updater: download of %s stopped", shortCommit(sha).c_str());
            return finish(PH_IDLE, CR_NONE);
        }
        ulog("Updater: the source of %s was not downloaded (%s): the next start tries again", shortCommit(sha).c_str(), why.c_str());
        return finish(PH_DONE, CR_FAILED);
    }
    if (S.cancel) return finish(PH_IDLE, CR_NONE);
    S.phase = PH_UNPACKING;
    struct LogFile {
        HANDLE h;
        ~LogFile() {
            if (h) CloseHandle(h);
        }
    } log{openLog(dir + L"build.log", true)};
    logLine(log.h, "Neon Tide update: commit " + sha + " for the build of " + commitLabel(NT_COMMIT) + ", " + tc.label);
    if (!unpackArchive(dir, sha, log.h, why)) {
        logLine(log.h, "Updater: " + why);
        if (S.cancel) return finish(PH_IDLE, CR_NONE);
        ulog("Updater: the source of %s could not be unpacked (%s): the next start downloads it again", shortCommit(sha).c_str(), why.c_str());
        DeleteFileW((dir + L"source.tar.gz").c_str());
        deleteTree(dir + L"x");
        deleteTree(dir + L"src");
        return finish(PH_DONE, CR_FAILED);
    }
    CheckResult r = buildSource(cfg, changes, tc, dir, log.h);
    deleteTree(dir + L"src");   // (the source and the build's files: hundreds of MB)
    if (r == CR_READY) {        // only the log stays; otherwise the archive too, for a try with another compiler
        DeleteFileW((dir + L"source.tar.gz").c_str());
        DeleteFileW((dir + L"failed.txt").c_str());
    }
    return r;
}

CheckResult runCheck(const Config& cfg) {
    removeOld(cfg.exe, 12);
    NewChanges changes;
    CheckResult result = CR_NONE;
    if (!findUpdate(cfg, changes, result)) return result;
    return buildUpdate(cfg, changes);
}

// Starts the background check (one at a time)
void launch(const Config& cfg) {
    Shared& S = shared();
    if (S.running.exchange(true)) return;
    S.cancel = false;
    S.phase = PH_CHECKING;
    g_allowLocalHttp = cfg.allowLocalHttp;
    try {
        std::thread([cfg] {
            runCheck(cfg);
            shared().running = false;
        }).detach();
    } catch (...) {
        S.running = false;
        S.phase = PH_IDLE;
        LOG("Updater: could not start the update check");
    }
}

// A page or file in the shell, from a short thread of its own (the shell may take a moment)
void openInShell(const std::wstring& target) {
    if (target.empty()) return;
    LOG("Updater: opening %s", narrow(target).c_str());
    try {
        std::thread([target] {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (SUCCEEDED(hr)) CoUninitialize();
        }).detach();
    } catch (...) {
    }
}

}  // namespace win_detail

using namespace win_detail;

bool beforeStart(int& exitCode) {
    Platform::parseCommandLine();
    // a process an update started continues the old build's log, and says so
    std::string from = takeUpdatedFrom();
    if (!from.empty()) {
        Platform::initLog(true);
        LOG("Updater: now running %s (version %s), updated from %s", commitLabel(NT_COMMIT).c_str(), NT_VERSION, commitLabel(from).c_str());
        if (!isCommitSha(NT_COMMIT) || from == NT_COMMIT) {
            LOG("Updater: the installed update does not report a new commit: no update check this session");
            g_noCheck = true;
        }
    }
    bool test = Platform::hasArg("update-test");
    if (!test && skipReason(false)[0]) return false;   // nothing else: automated runs open their log as before
    Platform::initLog(false);
    std::wstring exe = exePath();
    if (exe.empty()) return false;
    if (test) LOG("Updater test: running %s (commit %s) from %s", NT_VERSION, commitLabel(NT_COMMIT).c_str(), narrow(exe).c_str());
    DeleteFileW((exe + L".download").c_str());   // left by a copy that did not finish
    std::string installed;
    if (installPending(exe, installed)) {
        bool started = relaunch(exe, NT_COMMIT);
        if (test) LOG("Updater test: done, installed %s%s", commitLabel(installed).c_str(), started ? " and started it" : ", but it did not start");
        if (!started) askToStartAgain(true, test);
        exitCode = started || !test ? 0 : 2;
        return true;
    }
    if (!test) return false;
    // --update-test: one check, download, build and verification on this thread
    CheckResult r = g_noCheck ? CR_NONE : runCheck(makeConfig(exe));
    static const char* const kResults[] = {"no update", "update built and verified, it installs at the next start", "update not built"};
    LOG("Updater test: done, %s", kResults[r]);
    exitCode = r == CR_READY ? 0 : (r == CR_NONE ? 1 : 2);
    return true;
}

bool isAutomatedRun() { return automatedRun(); }

void startCheck() {
    if (g_noCheck) {   // (keeps <exe>.old: the update installed does not report its commit)
        LOG("Updater: no update check (the installed update does not report a new commit)");
        return;
    }
    const char* why = skipReason(true);
    if (why[0]) {
        LOG("Updater: no update check (%s)", why);
        std::wstring exe = automatedRun() ? std::wstring() : exePath();
        if (!exe.empty()) removeOld(exe, 1);   // (the check thread does it when it runs)
        return;
    }
    std::wstring exe = exePath();
    if (!exe.empty()) launch(makeConfig(exe));
}

#ifdef HAVE_GAME_UI
void menuFrame(UI::MenuState& menu, UI::MenuAction& act) {
    Shared& S = shared();
    // Settings > Gameplay > Automatic Updates: Off stops a download or build and hides the notice, On checks now
    int on = menu.settings.autoUpdate ? 1 : 0;
    if (g_lastSetting >= 0 && on != g_lastSetting) {
        if (!on && S.running) {
            S.cancel = true;
            LOG("Updater: Automatic Updates turned off");
        }
        if (on && !S.running && S.phase == PH_IDLE && !g_noCheck && !Platform::hasArg("no-update") &&
            (!automatedRun() || Platform::hasArg("update-url"))) {
            std::wstring exe = exePath();
            if (!exe.empty()) launch(makeConfig(exe));
        }
    }
    g_lastSetting = on;
    UI::UpdateNotice n;
    int phase = S.phase;
    if (on) {
        switch (phase) {
        case PH_CHECKING: n.kind = UI::UPD_CHECKING; break;
        case PH_DOWNLOADING: n.kind = UI::UPD_DOWNLOADING; break;
        case PH_UNPACKING: n.kind = UI::UPD_UNPACKING; break;
        case PH_BUILDING: n.kind = UI::UPD_BUILDING; break;
        case PH_READY: n.kind = UI::UPD_READY; break;
        case PH_FAILED: n.kind = UI::UPD_FAILED; break;
        case PH_BLOCKED: n.kind = UI::UPD_BLOCKED; break;
        default: break;
        }
        if (n.kind != UI::UPD_NONE) {
            n.bytes = S.got;
            unsigned long long start = S.buildStart;
            n.seconds = phase == PH_BUILDING && start ? (int)((GetTickCount64() - start) / 1000ull) : 0;
            std::lock_guard<std::mutex> lk(S.m);
            n.commit = S.commit;
            n.changes = S.changes;
            n.detail = S.detail;
        }
    }
    menu.update = n;
    if (act.type == UI::MA_UPDATE_RESTART) {
        act.type = UI::MA_NONE;
        if (on && phase == PH_READY) {
            LOG("Updater: restarting into %s", n.commit.c_str());
            g_restart = true;
            act.type = UI::MA_QUIT_GAME;   // the game shuts down as usual; WinMain then installs and restarts
        }
    } else if (act.type == UI::MA_UPDATE_OPEN) {
        act.type = UI::MA_NONE;
        if (phase == PH_FAILED) {
            std::wstring log;
            {
                std::lock_guard<std::mutex> lk(S.m);
                log = S.log;
            }
            if (fileExists(log)) openInShell(log);
        } else if (phase == PH_BLOCKED) {
            openInShell(widen(kHelpUrl));
        }
    } else if (act.type == UI::MA_RUN_BENCHMARK && S.running) {
        S.cancel = true;   // the benchmark measures the game alone; the update starts again with the next game
        LOG("Updater: update stopped for the benchmark");
    }
}
#endif

bool stop() {
    Shared& S = shared();
    S.cancel = true;
    std::lock_guard<std::mutex> lk(S.m);
    S.quiet = true;
    return g_restart;
}

void restart() {
    if (!g_restart) return;
    Platform::initLog(true);   // the log closed with the game
    std::wstring exe = exePath();
    std::string installed;
    bool ok = installPending(exe, installed);
    if (!ok) LOG("Updater: restarting without the update");
    if (!relaunch(exe, ok ? NT_COMMIT : nullptr)) askToStartAgain(ok, false);
}

}  // namespace Updater
