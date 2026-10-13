// Shared UI helpers: palette, layout, input prompt glyphs, rich text with color codes, formatting.
#include "ui_internal.h"

namespace UI {
namespace uix {

const u32 kNavy = C(0.043f, 0.071f, 0.176f);
const u32 kNavyDeep = C(0.022f, 0.035f, 0.098f);
const u32 kNavyPanel = C(0.035f, 0.055f, 0.140f, 0.86f);
const u32 kInk = C(0.030f, 0.040f, 0.090f);
const u32 kPink = C(1.00f, 0.24f, 0.60f);
const u32 kPinkHot = C(1.00f, 0.12f, 0.48f);
const u32 kCyan = C(0.18f, 0.90f, 1.00f);
const u32 kCyanDeep = C(0.06f, 0.58f, 0.80f);
const u32 kWhite = C(1.f, 1.f, 1.f);
const u32 kText = C(0.95f, 0.96f, 1.00f);
const u32 kTextDim = C(0.70f, 0.74f, 0.86f);
const u32 kTextMute = C(0.45f, 0.49f, 0.62f);
const u32 kGold = C(1.00f, 0.79f, 0.30f);
const u32 kYellow = C(1.00f, 0.86f, 0.26f);
const u32 kRed = C(1.00f, 0.27f, 0.33f);
const u32 kRedDeep = C(0.78f, 0.06f, 0.18f);
const u32 kGreen = C(0.36f, 0.92f, 0.52f);
const u32 kGreenMoney = C(0.47f, 0.92f, 0.60f);
const u32 kBlue = C(0.32f, 0.64f, 1.00f);
const u32 kOrange = C(1.00f, 0.60f, 0.24f);
const u32 kPurple = C(0.62f, 0.40f, 1.00f);
const u32 kHealth = C(0.30f, 0.84f, 0.50f);
const u32 kHealthLow = C(1.00f, 0.30f, 0.34f);
const u32 kArmor = C(0.30f, 0.68f, 1.00f);

namespace common_detail {
double g_time = 0.0;
int g_timeFrame = -1;
}  // namespace common_detail

float uiTime() { return (float)common_detail::g_time; }
void advanceTime(float dt) {
    int f = frameIndex();
    if (f == common_detail::g_timeFrame) return;
    common_detail::g_timeFrame = f;
    common_detail::g_time += Clamp(dt, 0.f, 0.25f);
}

Layout layout() {
    Layout l;
    l.W = (float)screenWidth();
    l.H = (float)screenHeight();
    l.s = l.H / 1080.f;
    float w169 = Min(l.W, l.H * 16.f / 9.f);
    l.x0 = (l.W - w169) * 0.5f;
    l.x1 = l.x0 + w169;
    l.left = l.x0 + 54.f * l.s;
    l.right = l.x1 - 54.f * l.s;
    l.top = 44.f * l.s;
    l.bottom = l.H - 42.f * l.s;
    return l;
}

// ------------------------------------------------------------------------------------------------------------------
// Formatting
std::string fmtMoney(long long v, bool sign) {
    bool neg = v < 0;
    unsigned long long a = neg ? (unsigned long long)(-v) : (unsigned long long)v;
    std::string digits = std::to_string(a);
    std::string out;
    int n = (int)digits.size();
    for (int i = 0; i < n; i++) {
        out.push_back(digits[i]);
        if ((n - 1 - i) % 3 == 0 && i != n - 1) out.push_back(',');
    }
    return std::string(neg ? "-" : (sign ? "+" : "")) + "$" + out;
}
std::string fmtTime(float seconds) {
    int t = (int)ceilf(Max(0.f, seconds));
    int h = t / 3600, m = (t / 60) % 60, s = t % 60;
    if (h > 0) return StrFormat("%d:%02d:%02d", h, m, s);
    return StrFormat("%d:%02d", m, s);
}
std::string fmtClock(float hours) {
    float hh = fmodf(Max(hours, 0.f), 24.f);
    int h = (int)hh, m = (int)((hh - h) * 60.f);
    return StrFormat("%02d:%02d", h, Min(m, 59));
}
std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)toupper((unsigned char)c);
    return o;
}

// ------------------------------------------------------------------------------------------------------------------
// Input prompts
namespace prompt_detail {

enum PadKind { PK_NONE, PK_FACE, PK_BUMPER, PK_TRIGGER, PK_STICK, PK_MENU, PK_VIEW, PK_DPAD };

struct PadInfo { PadKind kind; u32 color; const char* label; int dir; };

PadInfo padInfo(const char* k) {
    std::string s = upper(k);
    if (s == "A") return {PK_FACE, C(0.42f, 0.83f, 0.30f), "A", 0};
    if (s == "B") return {PK_FACE, C(0.95f, 0.30f, 0.27f), "B", 0};
    if (s == "X") return {PK_FACE, C(0.26f, 0.58f, 0.96f), "X", 0};
    if (s == "Y") return {PK_FACE, C(0.98f, 0.78f, 0.22f), "Y", 0};
    if (s == "LB" || s == "RB") return {PK_BUMPER, kWhite, s == "LB" ? "LB" : "RB", 0};
    if (s == "LT" || s == "RT") return {PK_TRIGGER, kWhite, s == "LT" ? "LT" : "RT", 0};
    if (s == "LS" || s == "LSTICK") return {PK_STICK, kWhite, "L", 0};
    if (s == "RS" || s == "RSTICK") return {PK_STICK, kWhite, "R", 0};
    if (s == "START" || s == "MENU") return {PK_MENU, kWhite, "", 0};
    if (s == "BACK" || s == "VIEW") return {PK_VIEW, kWhite, "", 0};
    if (s == "UP") return {PK_DPAD, kWhite, "", 1};
    if (s == "DOWN") return {PK_DPAD, kWhite, "", 2};
    if (s == "LEFT") return {PK_DPAD, kWhite, "", 3};
    if (s == "RIGHT") return {PK_DPAD, kWhite, "", 4};
    if (s == "DPAD") return {PK_DPAD, kWhite, "", 0};
    return {PK_NONE, kWhite, k, 0};
}

int mouseKind(const std::string& s) {
    if (s == "LMB" || s == "MOUSE1") return 1;
    if (s == "RMB" || s == "MOUSE2") return 2;
    if (s == "MMB" || s == "WHEEL" || s == "SCROLL") return 3;
    if (s == "MOUSE") return 4;
    return 0;
}

TextStyle keyStyle(float h) {
    TextStyle st;
    st.font = FONT_HEADING;
    st.size = h * 0.66f;
    st.color = kInk;
    return st;
}

}  // namespace prompt_detail

using namespace prompt_detail;

float promptWidth(const char* key, bool pad, float h) {
    if (pad) {
        PadInfo pi = padInfo(key);
        switch (pi.kind) {
        case PK_BUMPER: return h * 1.45f;
        case PK_TRIGGER: return h * 1.15f;
        case PK_NONE: break;
        default: return h;
        }
    }
    std::string s = upper(key);
    if (mouseKind(s)) return h * 0.78f;
    TextStyle st = keyStyle(h);
    return Max(h, textWidth(s.c_str(), st) + h * 0.55f);
}

float drawPrompt(float x, float y, const char* key, bool pad, float h, float alpha) {
    float w = promptWidth(key, pad, h);
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    if (pad) {
        PadInfo pi = padInfo(key);
        u32 dark = withAlpha(C(0.10f, 0.11f, 0.15f), 0.95f * alpha);
        u32 rim = withAlpha(C(0.55f, 0.58f, 0.68f), 0.9f * alpha);
        TextStyle st;
        st.font = FONT_HEADING;
        st.align = ALIGN_CENTER;
        switch (pi.kind) {
        case PK_FACE: {
            float r = h * 0.5f;
            circle(cx, cy + h * 0.04f, r, withAlpha(0xff000000u, 0.35f * alpha));
            circle(cx, cy, r, dark);
            circle(cx, cy, r - 0.5f, withAlpha(pi.color, 0.85f * alpha), Max(1.2f, h * 0.07f));
            st.size = h * 0.72f;
            st.color = withAlpha(pi.color, alpha);
            text(cx, cy - st.size * 0.52f, pi.label, st);
            return w;
        }
        case PK_BUMPER:
        case PK_TRIGGER: {
            float r = pi.kind == PK_BUMPER ? h * 0.32f : h * 0.42f;
            roundRect(x, y + h * 0.04f, w, h, r, withAlpha(0xff000000u, 0.35f * alpha));
            roundRect(x, y, w, h, r, dark, Max(1.f, h * 0.06f), rim);
            st.size = h * 0.56f;
            st.color = withAlpha(kWhite, alpha);
            text(cx, cy - st.size * 0.55f, pi.label, st);
            return w;
        }
        case PK_STICK: {
            float r = h * 0.5f;
            circle(cx, cy, r, dark);
            circle(cx, cy, r * 0.72f, rim, Max(1.f, h * 0.06f));
            st.size = h * 0.5f;
            st.color = withAlpha(kWhite, alpha);
            text(cx, cy - st.size * 0.55f, pi.label, st);
            return w;
        }
        case PK_MENU:
        case PK_VIEW: {
            float r = h * 0.5f;
            circle(cx, cy, r, dark);
            circle(cx, cy, r - 0.5f, rim, Max(1.f, h * 0.05f));
            u32 wc = withAlpha(kWhite, alpha);
            if (pi.kind == PK_MENU) {
                for (int k = -1; k <= 1; k++) rect(cx - r * 0.42f, cy + k * r * 0.3f - h * 0.035f, r * 0.84f, h * 0.07f, wc);
            } else {
                roundRect(cx - r * 0.45f, cy - r * 0.38f, r * 0.62f, r * 0.5f, 1.f, 0, Max(1.f, h * 0.06f), wc);
                roundRect(cx - r * 0.17f, cy - r * 0.12f, r * 0.62f, r * 0.5f, 1.f, dark, Max(1.f, h * 0.06f), wc);
            }
            return w;
        }
        case PK_DPAD: {
            drawIcon(ICO_DPAD, cx, cy, h * 1.28f, dark, Max(1.f, h * 0.05f), rim);
            if (pi.dir) {
                vec2 d = pi.dir == 1 ? vec2(0, -1) : pi.dir == 2 ? vec2(0, 1) : pi.dir == 3 ? vec2(-1, 0) : vec2(1, 0);
                vec2 c = vec2(cx, cy) + d * (h * 0.3f);
                roundRect(c.x - h * 0.12f, c.y - h * 0.12f, h * 0.24f, h * 0.24f, h * 0.05f, withAlpha(kWhite, alpha));
            }
            return w;
        }
        default: break;
        }
    }
    std::string s = upper(key);
    int mk = mouseKind(s);
    if (mk) {
        u32 wc = withAlpha(kWhite, alpha);
        drawIcon(ICO_MOUSE, cx, cy, h * 1.2f, wc, 1.5f, withAlpha(0xff000000u, 0.5f * alpha));
        if (mk == 1) drawIcon(ICO_MOUSE_L, cx, cy, h * 1.2f, withAlpha(kPink, alpha));
        if (mk == 2) drawIcon(ICO_MOUSE_R, cx, cy, h * 1.2f, withAlpha(kPink, alpha));
        if (mk == 3) drawIcon(ICO_MOUSE_WHEEL, cx, cy, h * 1.2f, withAlpha(kPink, alpha));
        return w;
    }
    // keyboard keycap
    roundRect(x, y + h * 0.07f, w, h, h * 0.2f, withAlpha(C(0.45f, 0.48f, 0.58f), alpha));
    roundRectGradient(x, y, w, h, h * 0.2f, withAlpha(C(1.f, 1.f, 1.f), alpha), withAlpha(C(0.86f, 0.88f, 0.94f), alpha));
    TextStyle st = keyStyle(h);
    st.align = ALIGN_CENTER;
    st.color = withAlpha(kInk, alpha);
    text(cx, cy - st.size * 0.56f, s.c_str(), st);
    return w;
}

float drawPromptBar(float xRight, float y, const PromptItem* items, int n, bool pad, float h, float alpha) {
    TextStyle st;
    st.font = FONT_HEADING;
    st.size = h * 0.78f;
    st.color = withAlpha(kText, alpha);
    st.shadow = 1.f;
    float gap = h * 0.35f, sep = h * 1.1f;
    float total = 0;
    for (int i = 0; i < n; i++) {
        const char* key = pad ? items[i].pad : items[i].kb;
        if (!key || !*key) continue;
        std::string lab = upper(items[i].label);
        total += promptWidth(key, pad, h) + gap + textWidth(lab.c_str(), st) + (i + 1 < n ? sep : 0.f);
    }
    float x = xRight - total;
    for (int i = 0; i < n; i++) {
        const char* key = pad ? items[i].pad : items[i].kb;
        if (!key || !*key) continue;
        x += drawPrompt(x, y, key, pad, h, alpha) + gap;
        std::string lab = upper(items[i].label);
        x += text(x, y + h * 0.5f - st.size * 0.56f, lab.c_str(), st) + sep;
    }
    return total;
}

// ------------------------------------------------------------------------------------------------------------------
// Rich text
u32 codeColor(char code, u32 def) {
    switch (code) {
    case 'r': return kRed;
    case 'g': return kGreen;
    case 'b': return kBlue;
    case 'y': return kYellow;
    case 'o': return kOrange;
    case 'p': return kPink;
    case 'c': return kCyan;
    case 'm': return kPurple;
    case 'w': return kWhite;
    case 'l': return kTextDim;
    default: return def;
    }
}

namespace rich_detail {

enum TokType { T_WORD, T_SPACE, T_NEWLINE, T_PROMPT };
struct Tok {
    TokType type;
    std::string s;
    u32 color;
    bool pad;
    float w;
};

void tokenize(const char* str, const TextStyle& st, const RichOpts& o, std::vector<Tok>& out) {
    u32 col = st.color;
    std::string word;
    auto flushWord = [&]() {
        if (!word.empty()) {
            out.push_back({T_WORD, word, col, false, textWidth(word.c_str(), st)});
            word.clear();
        }
    };
    float promptH = st.size * 1.08f;
    for (const char* p = str; *p; p++) {
        if (*p == '~') {
            const char* e = strchr(p + 1, '~');
            if (e) {
                std::string code(p + 1, e);
                bool handled = true;
                if (code.size() == 1) {
                    char c = code[0];
                    if (c == 'n') { flushWord(); out.push_back({T_NEWLINE, "", col, false, 0}); }
                    else if (c == 's') { flushWord(); col = st.color; }
                    else if (c == 'h') { flushWord(); }
                    else if (strchr("rgbyopcmwl", c)) { flushWord(); col = withAlpha(codeColor(c, st.color), ((st.color >> 24) & 255) / 255.f); }
                    else handled = false;
                } else if (code.size() == 7 && code[0] == '#') {
                    flushWord();
                    unsigned rgb = (unsigned)strtoul(code.c_str() + 1, nullptr, 16);
                    col = C(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, ((st.color >> 24) & 255) / 255.f);
                } else if (code.size() > 2 && code[1] == ':' && (code[0] == 'k' || code[0] == 'p' || code[0] == 'i')) {
                    flushWord();
                    std::string key = code.substr(2);
                    bool pad = code[0] == 'p';
                    if (code[0] == 'i') {
                        size_t bar = key.find('|');
                        if (bar != std::string::npos) {
                            pad = o.pad;
                            key = o.pad ? key.substr(bar + 1) : key.substr(0, bar);
                        }
                    }
                    out.push_back({T_PROMPT, key, col, pad, promptWidth(key.c_str(), pad, promptH)});
                } else handled = false;
                if (handled) { p = e; continue; }
            }
        }
        if (*p == ' ') {
            flushWord();
            out.push_back({T_SPACE, " ", col, false, textWidth(" ", st)});
        } else if (*p == '\n') {
            flushWord();
            out.push_back({T_NEWLINE, "", col, false, 0});
        } else word.push_back(*p);
    }
    flushWord();
}

struct Line {
    int first = 0, last = 0;  // token range [first, last)
    float w = 0;
};

void layoutLines(std::vector<Tok>& toks, float maxW, std::vector<Line>& lines) {
    Line cur;
    cur.first = 0;
    float x = 0;
    for (int i = 0; i < (int)toks.size(); i++) {
        Tok& t = toks[i];
        if (t.type == T_NEWLINE) {
            cur.last = i;
            cur.w = x;
            lines.push_back(cur);
            cur = Line();
            cur.first = i + 1;
            x = 0;
            continue;
        }
        if (t.type == T_SPACE && x == 0) { continue; }
        if (maxW > 0 && x + t.w > maxW && x > 0 && t.type != T_SPACE) {
            // wrap before this token (drop trailing space)
            int end = i;
            float w = x;
            if (end > cur.first && toks[end - 1].type == T_SPACE) w -= toks[end - 1].w;
            cur.last = end;
            cur.w = w;
            lines.push_back(cur);
            cur = Line();
            cur.first = i;
            x = 0;
        }
        x += t.w;
    }
    cur.last = (int)toks.size();
    if (cur.last > cur.first && toks[cur.last - 1].type == T_SPACE) x -= toks[cur.last - 1].w;
    cur.w = x;
    lines.push_back(cur);
}

}  // namespace rich_detail

using namespace rich_detail;

vec2 richMeasure(const char* str, const TextStyle& st, float maxW, const RichOpts& o) {
    std::vector<Tok> toks;
    tokenize(str, st, o, toks);
    std::vector<Line> lines;
    layoutLines(toks, maxW, lines);
    float w = 0;
    for (auto& l : lines) w = Max(w, l.w);
    float h = st.size * (1.f + (lines.size() - 1) * o.lineSpacing);
    return vec2(w, h);
}

vec2 richDraw(float x, float y, const char* str, const TextStyle& st, float maxW, const RichOpts& o) {
    std::vector<Tok> toks;
    tokenize(str, st, o, toks);
    std::vector<Line> lines;
    layoutLines(toks, maxW, lines);
    float w = 0;
    TextStyle ts = st;
    ts.align = ALIGN_LEFT;
    float promptH = st.size * 1.08f;
    float ly = y;
    for (auto& l : lines) {
        w = Max(w, l.w);
        float lx = st.align == ALIGN_CENTER ? x - l.w * 0.5f : st.align == ALIGN_RIGHT ? x - l.w : x;
        bool lineStart = true;
        for (int i = l.first; i < l.last; i++) {
            Tok& t = toks[i];
            if (t.type == T_SPACE && lineStart) continue;
            lineStart = false;
            if (t.type == T_WORD) {
                ts.color = withAlpha(t.color, o.alpha * ((t.color >> 24) & 255) / 255.f);
                text(lx, ly, t.s.c_str(), ts);
            } else if (t.type == T_PROMPT) {
                drawPrompt(lx, ly + st.size * 0.52f - promptH * 0.5f, t.s.c_str(), t.pad, promptH, o.alpha);
            }
            lx += t.w;
        }
        ly += st.size * o.lineSpacing;
    }
    return vec2(w, st.size * (1.f + (lines.size() - 1) * o.lineSpacing));
}

std::string stripCodes(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '~') {
            size_t e = s.find('~', i + 1);
            if (e != std::string::npos) {
                std::string code = s.substr(i + 1, e - i - 1);
                if (code.size() == 1 || (code.size() == 7 && code[0] == '#') || (code.size() > 2 && code[1] == ':')) {
                    if (code.size() > 2 && code[1] == ':') {
                        std::string key = code.substr(2);
                        size_t bar = key.find('|');
                        if (bar != std::string::npos) key = key.substr(0, bar);
                        o += "[" + key + "]";
                    } else if (code == "n") o += "\n";
                    i = e;
                    continue;
                }
            }
        }
        o.push_back(s[i]);
    }
    return o;
}

}  // namespace uix
}  // namespace UI
