// Automatic updates (updater.h): release check over WinHTTP, verified download next to the exe (SHA-256 with BCrypt),
// install and restart at the next start or on "Restart now".
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
#include <cstdarg>
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

// The releases the game updates from (a public repository: no sign-in, about 60 checks an hour per address)
const char* const kRepo = "nichtpascaltarter-sketch/GTA-6-Claude-v0.5";
const size_t kMaxListBytes = 4u << 20, kMaxChecksumBytes = 64u << 10, kHeadBytes = 64u << 10;
// Set for the process an update starts: the version it was updated from (its log continues the old one)
const wchar_t* const kEnvUpdatedFrom = L"NEONTIDE_UPDATED_FROM";

enum Phase : int { PH_IDLE = 0, PH_CHECKING, PH_DOWNLOADING, PH_READY, PH_AVAILABLE, PH_DONE };
enum CheckResult : int { CR_NONE = 0, CR_READY, CR_FAILED };   // --update-test exit codes 1, 0, 2

// What the check thread shares with the menus. Allocated once and never freed: a check still running when the game
// exits must not find its state torn down by static destruction.
struct Shared {
    std::atomic<int> phase{PH_IDLE};
    std::atomic<long long> got{0}, total{0};   // download progress, bytes
    std::atomic<bool> running{false};          // a check thread is alive
    std::atomic<bool> cancel{false};           // stop it (Automatic Updates turned off, the game exits)
    std::mutex m;                              // guards the rest
    std::string version, page;                 // the update offered and its release page
    bool quiet = false;                        // the game shuts down: no more log lines from the thread
};
Shared& shared() {
    static Shared* s = new Shared;
    return *s;
}

// Main-thread state
bool g_restart = false;          // "Restart now" chosen
bool g_noCheck = false;          // no check this session (an update was just installed but does not run)
bool g_allowLocalHttp = false;   // --update-url
int g_lastSetting = -1;          // Automatic Updates as the menus last showed it (-1 before the first menu frame)

// Log lines that may come from the check thread: none once the game shuts down (the log closes, then static
// destruction runs while a detached check may still be waiting on the network)
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

std::string narrow(const std::wstring& w) {
    int n = w.empty() ? 0 : WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)Max(n, 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

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

bool fileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
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

// Writes next to `path` first, then replaces it, so a crash never leaves half a record
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

// SHA-256, size and first bytes (for the PE check) of a file
bool hashFile(const std::wstring& path, std::string& hex, std::vector<uint8_t>& head, long long& size) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    Sha256 h;
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
        if (head.size() < kHeadBytes) head.insert(head.end(), buf.data(), buf.data() + Min((size_t)got, kHeadBytes - head.size()));
        size += got;
    }
    CloseHandle(f);
    hex = h.finish();
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
    int status = 0;          // HTTP status, 0 when no complete answer arrived
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
// Plain http only to this machine with the test override; WinHTTP never follows a redirect from https to http.
template <typename Sink>
HttpResult httpGet(const std::string& url, bool allowLocalHttp, const wchar_t* accept, Sink&& sink) {
    HttpResult r;
    Url u;
    if (!parseUrl(url, u) || !urlAllowed(url, allowLocalHttp)) {
        r.error = "refused URL " + url;
        return r;
    }
    auto failed = [&](const char* what) {
        r.status = 0;
        r.error = std::string(what) + ": " + netError(GetLastError());
        return r;
    };
    std::wstring agent = widen(std::string("NeonTide-Updater/") + NT_VERSION);
    NetHandle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) return failed("WinHttpOpen");
    WinHttpSetTimeouts(session.h, 5000, 5000, 5000, 10000);   // resolve, connect, send: 5 s; each receive: 10 s
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(session.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, (DWORD)sizeof(policy));
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
    // where the redirects ended (GitHub's downloads move to its asset host): still https
    wchar_t finalUrl[2048];
    DWORD urlLen = (DWORD)sizeof(finalUrl);
    if (WinHttpQueryOption(req.h, WINHTTP_OPTION_URL, finalUrl, &urlLen)) {
        std::string f = narrow(std::wstring(finalUrl, wcsnlen(finalUrl, urlLen / sizeof(wchar_t))));
        if (!urlAllowed(f, allowLocalHttp)) {
            r.error = "redirected to a refused URL";
            return r;
        }
    }
    if (status != 200) {
        r.error = StrFormat("HTTP %lu", (unsigned long)status);
        if ((status == 403 || status == 429) && headerValue(req.h, L"X-RateLimit-Remaining") == "0") r.error += " (rate limited)";
        if (status == 404) r.error += " (nothing published)";
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
    r.status = (int)status;
    return r;
}

// ------------------------------------------------------------------------------------------------------------------
// When to update
struct Config {
    std::wstring exe;              // the running exe
    std::string url;               // the release list
    bool allowLocalHttp = false;   // --update-url: plain http to this machine
};

Config makeConfig(const std::wstring& exe) {
    Config c;
    c.exe = exe;
    const char* url = Platform::argValue("update-url");
    c.allowLocalHttp = url != nullptr;
    c.url = url ? std::string(url) : std::string("https://api.github.com/repos/") + kRepo + "/releases?per_page=10";
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

// The version this process was updated from (NEONTIDE_UPDATED_FROM), taken out of the environment so it does not
// reach later processes
std::string takeUpdatedFrom() {
    wchar_t buf[64];
    DWORD n = GetEnvironmentVariableW(kEnvUpdatedFrom, buf, 64);
    if (n == 0 || n >= 64) return std::string();
    SetEnvironmentVariableW(kEnvUpdatedFrom, nullptr);
    std::string v = narrow(std::wstring(buf, n));
    Version parsed;
    return parseVersion(v, parsed) ? v : std::string();
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

// Verifies <exe>.pending again (its record, a version newer than this build, size, SHA-256, PE image) and swaps it in:
// the running exe becomes <exe>.old and the pending file takes its name. A pending update that fails is deleted;
// one that cannot be moved into place stays for a later start. True when the new version is in place.
bool installPending(const std::wstring& exe, std::string& version) {
    std::wstring pending = exe + L".pending", meta = exe + L".pending.json", old = exe + L".old";
    bool havePending = fileExists(pending), haveMeta = fileExists(meta);
    if (!havePending && !haveMeta) return false;
    std::string text, why, hex, peWhy;
    std::vector<uint8_t> head;
    long long size = -1;
    PendingInfo info;
    if (!havePending || !haveMeta) why = "incomplete";
    else if (!readSmallFile(meta, text, 4096) || !pendingFromJson(text, info)) why = "unreadable record";
    else if (!isNewerVersion(info.version, NT_VERSION)) why = info.version + " is not newer than " NT_VERSION;
    else if (!hashFile(pending, hex, head, size)) why = "unreadable file";
    else if (size != info.size) why = StrFormat("%lld bytes instead of %lld", size, info.size);
    else if (hex != info.sha256) why = "SHA-256 changed";
    else if (!peImageValid(head.data(), head.size(), (uint64_t)size, &peWhy)) why = "not a Windows program: " + peWhy;
    if (!why.empty()) {
        LOG("Updater: discarded the downloaded update (%s)", why.c_str());
        DeleteFileW(pending.c_str());
        DeleteFileW(meta.c_str());
        return false;
    }
    DeleteFileW(old.c_str());
    if (!moveFile(exe, old, MOVEFILE_REPLACE_EXISTING, 3)) {
        LOG("Updater: could not move the running exe aside (error %lu): %s installs at a later start", GetLastError(), info.version.c_str());
        return false;
    }
    if (!moveFile(pending, exe, 0, 3)) {
        DWORD e = GetLastError();
        MoveFileExW(old.c_str(), exe.c_str(), 0);   // the running exe goes back
        LOG("Updater: could not put %s in place (error %lu): it installs at a later start", info.version.c_str(), e);
        return false;
    }
    DeleteFileW(meta.c_str());
    version = info.version;
    LOG("Updater: installed %s over %s (%s)", info.version.c_str(), NT_VERSION, narrow(exe).c_str());
    return true;
}

// Starts the exe again with this process's command line (the updater adds no flags of its own). `from` is the version
// that was just replaced (nullptr when the version did not change); the new process gets it in NEONTIDE_UPDATED_FROM.
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

// The new version is in place but did not start: the player starts it (not in test runs)
void askToStartAgain(const std::string& version, bool test) {
    if (test) return;
    std::string msg = version.empty() ? std::string("Neon Tide could not restart itself.\n\nPlease start Neon Tide again.")
                                      : "Neon Tide was updated to " + version + ", but it could not restart itself.\n\nPlease start Neon Tide again.";
    MessageBoxA(nullptr, msg.c_str(), "Neon Tide - update", MB_OK | MB_ICONINFORMATION);
}

// ------------------------------------------------------------------------------------------------------------------
// The check (background thread, or the main thread for --update-test)
CheckResult finish(int phase, CheckResult result) {
    shared().phase = phase;
    return result;
}

// A verified download of `version` from an earlier session still waits (the next start checks it in full)
bool pendingWaits(const std::wstring& exe, const std::string& version) {
    std::string text;
    PendingInfo info;
    return readSmallFile(exe + L".pending.json", text, 4096) && pendingFromJson(text, info) && info.version == version &&
           fileSize(exe + L".pending") == info.size;
}

CheckResult download(const Config& cfg, const std::string& version, const ReleaseAsset& exe, const std::string& want) {
    Shared& S = shared();
    std::wstring part = cfg.exe + L".download", pending = cfg.exe + L".pending";
    HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        ulog("Updater: cannot write next to the game (%s, error %lu): offering the release page", narrow(part).c_str(), GetLastError());
        return finish(PH_AVAILABLE, CR_FAILED);
    }
    S.got = 0;
    S.total = exe.size;
    S.phase = PH_DOWNLOADING;
    ulog("Updater: downloading %s", exe.url.c_str());
    Sha256 hash;
    std::vector<uint8_t> head;
    long long got = 0;
    bool tooLarge = false, writeFailed = false;
    HttpResult hr = httpGet(exe.url, cfg.allowLocalHttp, L"application/octet-stream", [&](const char* data, size_t n) {
        if (got + (long long)n > exe.size) {
            tooLarge = true;
            return false;
        }
        DWORD wrote = 0;
        if (!WriteFile(f, data, (DWORD)n, &wrote, nullptr) || wrote != n) {
            writeFailed = true;
            return false;
        }
        hash.add(data, n);
        if (head.size() < kHeadBytes) head.insert(head.end(), (const uint8_t*)data, (const uint8_t*)data + Min(n, kHeadBytes - head.size()));
        got += (long long)n;
        S.got = got;
        return true;
    });
    CloseHandle(f);
    std::string hex = hash.finish(), why, peWhy;
    if (S.cancel) why = "cancelled";
    else if (tooLarge) why = "larger than the listed size";
    else if (writeFailed) why = StrFormat("could not write the file (error %lu)", GetLastError());
    else if (hr.status != 200) why = hr.error;
    else if (got != exe.size) why = StrFormat("%lld of %lld bytes", got, exe.size);
    else if (!peImageValid(head.data(), head.size(), (uint64_t)got, &peWhy)) why = "not a Windows program: " + peWhy;
    else if (hex.empty()) why = "could not hash it";
    else if (!want.empty() && hex != want) why = "SHA-256 mismatch: the file has " + hex;
    if (!why.empty()) {
        DeleteFileW(part.c_str());
        if (S.cancel) {
            ulog("Updater: download of %s stopped", version.c_str());
            return finish(PH_IDLE, CR_NONE);
        }
        ulog("Updater: rejected the download of %s (%s)", version.c_str(), why.c_str());
        return finish(PH_AVAILABLE, CR_FAILED);
    }
    // verified: kept as <exe>.pending with the record the next start checks
    PendingInfo info;
    info.version = version;
    info.size = got;
    info.sha256 = hex;
    if (!moveFile(part, pending, MOVEFILE_REPLACE_EXISTING, 10) || !writeFileReplacing(cfg.exe + L".pending.json", pendingToJson(info))) {
        ulog("Updater: could not keep the download (error %lu)", GetLastError());
        DeleteFileW(part.c_str());
        DeleteFileW(pending.c_str());
        return finish(PH_AVAILABLE, CR_FAILED);
    }
    ulog("Updater: %s downloaded and verified (%lld bytes, SHA-256 %s, %s): it installs at the next start", version.c_str(), got, hex.c_str(),
         want.empty() ? "none published" : "matches the published one");
    return finish(PH_READY, CR_READY);
}

CheckResult runCheck(const Config& cfg) {
    Shared& S = shared();
    removeOld(cfg.exe, 12);
    ulog("Updater: checking %s (running %s)", cfg.url.c_str(), NT_VERSION);
    std::string list;
    HttpResult hr = httpGet(cfg.url, cfg.allowLocalHttp, L"application/vnd.github+json", [&](const char* data, size_t n) {
        if (list.size() + n > kMaxListBytes) return false;
        list.append(data, n);
        return true;
    });
    if (hr.status != 200) {
        ulog("Updater: no update (%s)", hr.error.c_str());
        return finish(S.cancel ? PH_IDLE : PH_DONE, CR_NONE);
    }
    std::vector<Release> releases;
    if (!parseReleases(list, releases)) {
        ulog("Updater: no update (the release list could not be read, %lld bytes)", hr.bytes);
        return finish(PH_DONE, CR_NONE);
    }
    int exeIndex = -1;
    std::string newest;
    int ri = pickUpdate(releases, NT_VERSION, &exeIndex, &newest);
    if (ri < 0) {
        ulog("Updater: up to date (running %s, newest release %s)", NT_VERSION, newest.empty() ? "none" : newest.c_str());
        return finish(PH_DONE, CR_NONE);
    }
    const Release& rel = releases[(size_t)ri];
    const ReleaseAsset& exe = rel.assets[(size_t)exeIndex];
    std::string version = displayVersion(rel.tag);
    {
        std::lock_guard<std::mutex> lk(S.m);
        S.version = version;
        S.page = urlAllowed(rel.htmlUrl, cfg.allowLocalHttp) ? rel.htmlUrl : std::string("https://github.com/") + kRepo + "/releases";
    }
    ulog("Updater: %s is available (%s, %lld bytes%s)", version.c_str(), exe.name.c_str(), exe.size, rel.prerelease ? ", pre-release" : "");
    if (pendingWaits(cfg.exe, version)) {
        ulog("Updater: %s was downloaded before and installs at the next start", version.c_str());
        return finish(PH_READY, CR_READY);
    }
    if (!urlAllowed(exe.url, cfg.allowLocalHttp)) {
        ulog("Updater: refused the download URL %s", exe.url.c_str());
        return finish(PH_AVAILABLE, CR_FAILED);
    }
    // the published checksum, the first one found: GitHub's digest, a checksum asset, the release notes
    std::string want = checksumFromDigest(exe.digest), from = "GitHub's asset digest";
    if (want.empty()) {
        int ci = pickChecksumAsset(rel, exe.name);
        if (ci >= 0) {
            const ReleaseAsset& ca = rel.assets[(size_t)ci];
            std::string text;
            HttpResult cr = httpGet(ca.url, cfg.allowLocalHttp, L"application/octet-stream", [&](const char* data, size_t n) {
                if (text.size() + n > kMaxChecksumBytes) return false;
                text.append(data, n);
                return true;
            });
            if (cr.status != 200) {
                ulog("Updater: the published checksum %s could not be read (%s)", ca.name.c_str(), cr.error.c_str());
                return finish(S.cancel ? PH_IDLE : PH_AVAILABLE, CR_FAILED);
            }
            want = checksumFromFile(text, exe.name, ca.name);
            from = ca.name;
        }
    }
    if (want.empty()) {
        want = checksumFromNotes(rel.body, exe.name);
        from = "the release notes";
    }
    if (want.empty()) ulog("Updater: no SHA-256 published for %s: checking its size and program header", exe.name.c_str());
    else ulog("Updater: published SHA-256 %s (%s)", want.c_str(), from.c_str());
    if (S.cancel) return finish(PH_IDLE, CR_NONE);
    return download(cfg, version, exe, want);
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

// The release page in the browser, from a short thread of its own (the shell may take a moment)
void openReleasePage() {
    std::string url;
    {
        Shared& S = shared();
        std::lock_guard<std::mutex> lk(S.m);
        url = S.page;
    }
    if (!urlAllowed(url, g_allowLocalHttp)) return;
    LOG("Updater: opening %s", url.c_str());
    std::wstring w = widen(url);
    try {
        std::thread([w] {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (SUCCEEDED(hr)) CoUninitialize();
        }).detach();
    } catch (...) {
    }
}

}  // namespace win_detail

using namespace win_detail;

bool beforeStart(int& exitCode) {
    Platform::parseCommandLine();
    // a process an update started continues the old version's log, and says so
    std::string from = takeUpdatedFrom();
    if (!from.empty()) {
        Platform::initLog(true);
        LOG("Updater: now running %s, updated from %s", NT_VERSION, from.c_str());
        if (!isNewerVersion(NT_VERSION, from)) {
            LOG("Updater: the installed update does not report a newer version: no update check this session");
            g_noCheck = true;
        }
    }
    bool test = Platform::hasArg("update-test");
    if (!test && skipReason(false)[0]) return false;   // nothing else: automated runs open their log as before
    Platform::initLog(false);
    std::wstring exe = exePath();
    if (exe.empty()) return false;
    if (test) LOG("Updater test: running %s from %s", NT_VERSION, narrow(exe).c_str());
    DeleteFileW((exe + L".download").c_str());   // left by a download that did not finish
    std::string installed;
    if (installPending(exe, installed)) {
        bool started = relaunch(exe, NT_VERSION);
        if (test) LOG("Updater test: done, installed %s%s", installed.c_str(), started ? " and started it" : ", but it did not start");
        if (!started) askToStartAgain(installed, test);
        exitCode = started || !test ? 0 : 2;
        return true;
    }
    if (!test) return false;
    // --update-test: one check, download and verification on this thread
    CheckResult r = g_noCheck ? CR_NONE : runCheck(makeConfig(exe));
    static const char* const kResults[] = {"no update", "update downloaded and verified, it installs at the next start",
                                           "update not downloaded"};
    LOG("Updater test: done, %s", kResults[r]);
    exitCode = r == CR_READY ? 0 : (r == CR_NONE ? 1 : 2);
    return true;
}

void startCheck() {
    if (g_noCheck) {   // (keeps <exe>.old: the update installed is not newer than the version it replaced)
        LOG("Updater: no update check (the installed update is not newer)");
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
    // Settings > Gameplay > Automatic Updates: Off stops a download and hides the notice, On checks now
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
        if (phase == PH_DOWNLOADING) {
            long long total = S.total, got = S.got;
            n.kind = UI::UPD_DOWNLOADING;
            n.progress = total > 0 ? Saturate((float)((double)got / (double)total)) : -1.f;
        } else if (phase == PH_READY) {
            n.kind = UI::UPD_READY;
        } else if (phase == PH_AVAILABLE) {
            n.kind = UI::UPD_AVAILABLE;
        }
        if (n.kind != UI::UPD_NONE) {
            std::lock_guard<std::mutex> lk(S.m);
            n.version = S.version;
        }
    }
    menu.update = n;
    if (act.type == UI::MA_UPDATE_RESTART) {
        act.type = UI::MA_NONE;
        if (on && phase == PH_READY) {
            LOG("Updater: restarting into %s", n.version.c_str());
            g_restart = true;
            act.type = UI::MA_QUIT_GAME;   // the game shuts down as usual; WinMain then installs and restarts
        }
    } else if (act.type == UI::MA_UPDATE_PAGE) {
        act.type = UI::MA_NONE;
        openReleasePage();
    } else if (act.type == UI::MA_RUN_BENCHMARK && S.running) {
        S.cancel = true;   // the benchmark measures the game alone; the download starts again with the next game
        LOG("Updater: download stopped for the benchmark");
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
    if (!relaunch(exe, ok ? NT_VERSION : nullptr)) askToStartAgain(installed, false);
}

}  // namespace Updater
