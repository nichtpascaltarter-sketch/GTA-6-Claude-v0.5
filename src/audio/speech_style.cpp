// Speaking styles: emotion / delivery / accent parameter tables, inline markup parsing ("[angry]",
// "[accent:south:0.6]", "[pause:1.2]"), subtitle text cleanup and the cast personas.
#include "speech_internal.h"

namespace Speech {
namespace detail {

// ---------------------------------------------------------------------------------------------
// Parameter tables (intensity 1). Multiplicative fields scale around 1, additive ones around 0.

static StyleParams emotionTable(int e) {
    StyleParams p;
    switch (e) {
        case EMOTION_ANGRY:
            p.rate = 1.08f, p.pause = 0.75f, p.pitch = 2.f, p.range = 1.3f, p.decl = 1.2f, p.stressLen = 1.12f;
            p.loud = 3.f, p.rd = -0.22f, p.creak = 0.06f, p.jitter = 1.3f, p.tilt = -4.f, p.f1 = 1.05f;
            break;
        case EMOTION_SCARED:
            p.rate = 1.15f, p.pause = 0.7f, p.pitch = 4.f, p.range = 1.2f, p.decl = 0.6f, p.loud = 1.f;
            p.rd = 0.12f, p.breath = 0.18f, p.jitter = 2.2f, p.tremor = 0.55f, p.tilt = -1.f, p.f1 = 1.03f;
            p.endLift = 1.5f;
            break;
        case EMOTION_CALM:
            p.rate = 0.9f, p.pause = 1.3f, p.pitch = -1.f, p.range = 0.7f, p.decl = 0.9f, p.loud = -2.f;
            p.rd = 0.12f, p.breath = 0.05f, p.tilt = 2.f, p.f1 = 0.98f;
            break;
        case EMOTION_SAD:
            p.rate = 0.82f, p.pause = 1.5f, p.pitch = -2.5f, p.range = 0.55f, p.decl = 1.3f, p.stressLen = 1.08f;
            p.loud = -4.f, p.rd = 0.3f, p.breath = 0.15f, p.creak = 0.2f, p.tilt = 4.f, p.f1 = 0.95f, p.f23 = 0.98f;
            break;
        case EMOTION_HAPPY:
            p.rate = 1.06f, p.pause = 0.9f, p.pitch = 2.5f, p.range = 1.35f, p.decl = 0.8f, p.loud = 2.f;
            p.rd = -0.08f, p.breath = 0.03f, p.tilt = -2.f, p.f23 = 1.04f, p.endLift = 0.5f;
            break;
        case EMOTION_SHOUT:  // on top of the shouting rules shared with ALL-CAPS sentences
            p.rate = 0.95f, p.pause = 0.9f, p.pitch = 2.5f, p.range = 1.f, p.decl = 0.8f, p.stressLen = 1.12f;
            p.loud = 2.f, p.rd = -0.12f, p.jitter = 1.2f, p.tilt = -5.f, p.f1 = 1.1f, p.f23 = 1.02f;
            break;
        case EMOTION_WHISPER:
            p.rate = 0.92f, p.pause = 1.2f, p.range = 0.6f, p.whisper = 1.f, p.f1 = 1.08f;
            break;
        default: break;
    }
    return p;
}

static StyleParams deliveryTable(int d) {
    StyleParams p;
    switch (d) {
        case DELIVERY_DJ:
            p.rate = 1.12f, p.pause = 0.7f, p.pitch = 2.f, p.range = 1.45f, p.decl = 0.7f, p.stressLen = 1.1f;
            p.loud = 3.f, p.rd = -0.15f, p.tilt = -3.f, p.f23 = 1.03f, p.endLift = 0.8f, p.accentAll = 0.5f;
            break;
        case DELIVERY_AD:
            p.rate = 1.18f, p.pause = 0.65f, p.pitch = 1.5f, p.range = 1.4f, p.stressLen = 1.12f, p.loud = 4.f;
            p.rd = -0.2f, p.tilt = -4.f, p.f23 = 1.03f, p.accentAll = 0.6f;
            break;
        case DELIVERY_FINEPRINT:
            p.rate = 1.75f, p.pause = 0.35f, p.pitch = -1.f, p.range = 0.35f, p.stressLen = 0.9f, p.reducedLen = 0.85f;
            p.loud = -3.f, p.rd = 0.05f;
            break;
        case DELIVERY_NEWS:
            p.rate = 1.04f, p.pause = 0.95f, p.pitch = -0.5f, p.range = 1.15f, p.decl = 1.1f, p.reducedLen = 1.1f;
            p.loud = 1.f, p.rd = -0.08f, p.tilt = -1.f;
            break;
        case DELIVERY_DISPATCH:
            p.rate = 1.15f, p.pause = 0.6f, p.range = 0.45f, p.decl = 0.5f, p.rd = -0.05f, p.tilt = -1.f;
            p.endLift = 0.8f;
            break;
        default: break;
    }
    return p;
}

static StyleParams accentTable(int a) {
    StyleParams p;
    switch (a) {
        case ACCENT_SOUTH: p.rate = 0.9f, p.stressLen = 1.18f, p.range = 1.12f, p.reducedLen = 0.95f; break;
        case ACCENT_NEWYORK: p.rate = 1.08f, p.range = 1.25f, p.pause = 0.85f; break;
        case ACCENT_LATINO: p.rate = 1.02f, p.reducedLen = 1.45f, p.stressLen = 0.92f, p.range = 1.2f, p.vot = 0.4f; break;
        case ACCENT_CARIBBEAN:
            p.reducedLen = 1.35f, p.stressLen = 0.95f, p.range = 1.35f, p.pitch = 1.f, p.endLift = 0.5f, p.vot = 0.6f;
            break;
        case ACCENT_BRITISH: p.rate = 0.97f, p.range = 1.2f, p.rd = -0.04f; break;
        default: break;
    }
    return p;
}

// Scales a parameter set's deviation from neutral by k.
static StyleParams scaled(const StyleParams& p, float k) {
    auto m = [k](float v) { return 1.f + (v - 1.f) * k; };
    StyleParams r;
    r.rate = m(p.rate), r.pause = m(p.pause), r.range = m(p.range), r.decl = m(p.decl);
    r.stressLen = m(p.stressLen), r.reducedLen = m(p.reducedLen), r.jitter = m(p.jitter);
    r.f1 = m(p.f1), r.f23 = m(p.f23), r.vot = m(p.vot);
    r.pitch = p.pitch * k, r.loud = p.loud * k, r.rd = p.rd * k, r.breath = p.breath * k, r.creak = p.creak * k;
    r.tremor = p.tremor * k, r.tilt = p.tilt * k, r.endLift = p.endLift * k, r.accentAll = p.accentAll * k;
    r.whisper = Saturate(p.whisper * k);
    return r;
}

static void combine(StyleParams& a, const StyleParams& b) {
    a.rate *= b.rate, a.pause *= b.pause, a.range *= b.range, a.decl *= b.decl;
    a.stressLen *= b.stressLen, a.reducedLen *= b.reducedLen, a.jitter *= b.jitter;
    a.f1 *= b.f1, a.f23 *= b.f23, a.vot *= b.vot;
    a.pitch += b.pitch, a.loud += b.loud, a.rd += b.rd, a.breath += b.breath, a.creak += b.creak;
    a.tremor += b.tremor, a.tilt += b.tilt, a.endLift += b.endLift, a.accentAll += b.accentAll;
    a.whisper = std::max(a.whisper, b.whisper);
}

StyleParams styleParams(const Style& s) {
    StyleParams p = scaled(emotionTable(s.emotion), Clamp(s.intensity, 0.f, 1.5f));
    combine(p, deliveryTable(s.delivery));
    combine(p, scaled(accentTable(s.accent), Clamp(s.accentStrength, 0.f, 1.f)));
    p.rate = Clamp(p.rate, 0.5f, 2.2f);
    p.pause = Clamp(p.pause, 0.2f, 3.f);
    p.accentAll = Saturate(p.accentAll);
    return p;
}

bool styleEqual(const Style& a, const Style& b) {
    return a.emotion == b.emotion && a.delivery == b.delivery && a.accent == b.accent && a.intensity == b.intensity &&
           a.accentStrength == b.accentStrength;
}

// ---------------------------------------------------------------------------------------------
// Markup

struct NameId {
    const char* name;
    u8 id;
};
static const NameId kEmotionNames[] = {
    {"neutral", EMOTION_NEUTRAL}, {"normal", EMOTION_NEUTRAL}, {"angry", EMOTION_ANGRY}, {"mad", EMOTION_ANGRY},
    {"furious", EMOTION_ANGRY}, {"annoyed", EMOTION_ANGRY}, {"scared", EMOTION_SCARED}, {"afraid", EMOTION_SCARED},
    {"fear", EMOTION_SCARED}, {"fearful", EMOTION_SCARED}, {"panic", EMOTION_SCARED}, {"panicked", EMOTION_SCARED},
    {"nervous", EMOTION_SCARED}, {"terrified", EMOTION_SCARED}, {"calm", EMOTION_CALM}, {"relaxed", EMOTION_CALM},
    {"gentle", EMOTION_CALM}, {"soft", EMOTION_CALM}, {"sad", EMOTION_SAD}, {"crying", EMOTION_SAD},
    {"grieving", EMOTION_SAD}, {"tired", EMOTION_SAD}, {"happy", EMOTION_HAPPY}, {"excited", EMOTION_HAPPY},
    {"cheerful", EMOTION_HAPPY}, {"joyful", EMOTION_HAPPY}, {"shout", EMOTION_SHOUT}, {"shouting", EMOTION_SHOUT},
    {"yell", EMOTION_SHOUT}, {"yelling", EMOTION_SHOUT}, {"scream", EMOTION_SHOUT}, {"screaming", EMOTION_SHOUT},
    {"whisper", EMOTION_WHISPER}, {"whispering", EMOTION_WHISPER}, {"whispered", EMOTION_WHISPER},
};
static const NameId kDeliveryNames[] = {
    {"talk", DELIVERY_TALK}, {"conversation", DELIVERY_TALK}, {"dj", DELIVERY_DJ}, {"host", DELIVERY_DJ},
    {"ad", DELIVERY_AD}, {"advert", DELIVERY_AD}, {"commercial", DELIVERY_AD}, {"announcer", DELIVERY_AD},
    {"fineprint", DELIVERY_FINEPRINT}, {"disclaimer", DELIVERY_FINEPRINT}, {"legal", DELIVERY_FINEPRINT},
    {"news", DELIVERY_NEWS}, {"newsreader", DELIVERY_NEWS}, {"anchor", DELIVERY_NEWS},
    {"dispatch", DELIVERY_DISPATCH}, {"dispatcher", DELIVERY_DISPATCH},
};
static const NameId kAccentNames[] = {
    {"general", ACCENT_GENERAL}, {"american", ACCENT_GENERAL}, {"standard", ACCENT_GENERAL}, {"none", ACCENT_GENERAL},
    {"south", ACCENT_SOUTH}, {"southern", ACCENT_SOUTH}, {"redneck", ACCENT_SOUTH}, {"country", ACCENT_SOUTH},
    {"newyork", ACCENT_NEWYORK}, {"ny", ACCENT_NEWYORK}, {"brooklyn", ACCENT_NEWYORK}, {"bronx", ACCENT_NEWYORK},
    {"jersey", ACCENT_NEWYORK}, {"latino", ACCENT_LATINO}, {"latin", ACCENT_LATINO}, {"spanish", ACCENT_LATINO},
    {"hispanic", ACCENT_LATINO}, {"cuban", ACCENT_LATINO}, {"mexican", ACCENT_LATINO},
    {"caribbean", ACCENT_CARIBBEAN}, {"island", ACCENT_CARIBBEAN}, {"jamaican", ACCENT_CARIBBEAN},
    {"haitian", ACCENT_CARIBBEAN}, {"british", ACCENT_BRITISH}, {"english", ACCENT_BRITISH}, {"uk", ACCENT_BRITISH},
    {"rp", ACCENT_BRITISH}, {"posh", ACCENT_BRITISH},
};
static const char* const kEmotionTag[EMOTION_COUNT] = {"neutral", "angry", "scared", "calm", "sad", "happy", "shout", "whisper"};
static const char* const kDeliveryTag[DELIVERY_COUNT] = {"talk", "dj", "ad", "fineprint", "news", "dispatch"};
static const char* const kAccentTag[ACCENT_COUNT] = {"general", "south", "newyork", "latino", "caribbean", "british"};

static int findName(const NameId* t, size_t n, const std::string& w) {
    for (size_t i = 0; i < n; i++)
        if (w == t[i].name) return t[i].id;
    return -1;
}

static bool parseNumber(const std::string& s, float& v) {
    if (s.empty() || s.size() > 12) return false;
    int dots = 0;
    for (char c : s) {
        if (c == '.') dots++;
        else if (c < '0' || c > '9') return false;
    }
    if (dots > 1 || s == ".") return false;
    v = (float)atof(s.c_str());
    return true;
}

bool parseStyleTag(const std::string& tagIn, Style& st, float& pauseSec) {
    // lowercase, drop spaces / hyphens / underscores ("New York" -> "newyork", "fine print" -> "fineprint")
    std::string tag;
    for (char c : tagIn) {
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c == ' ' || c == '-' || c == '_' || c == '\t') continue;
        tag += c;
    }
    std::vector<std::string> f;
    size_t b = 0;
    while (b <= tag.size()) {
        size_t e = tag.find_first_of(":=", b);
        if (e == std::string::npos) e = tag.size();
        f.push_back(tag.substr(b, e - b));
        b = e + 1;
    }
    if (f.empty() || f[0].empty()) return false;
    float num = 0.f;
    if (f[0] == "pause" || f[0] == "break") {
        float sec = 0.5f;
        if (f.size() >= 2 && !parseNumber(f[1], sec)) return false;
        pauseSec = Clamp(sec, 0.05f, 5.f);
        return true;
    }
    if (f[0] == "accent") {
        if (f.size() < 2) return false;
        int a = findName(kAccentNames, ARRAY_COUNT(kAccentNames), f[1]);
        if (a < 0) return false;
        st.accent = (u8)a;
        st.accentStrength = 1.f;
        if (f.size() >= 3 && parseNumber(f[2], num)) st.accentStrength = Saturate(num);
        return true;
    }
    size_t k = 0;
    if ((f[0] == "emotion" || f[0] == "mood" || f[0] == "delivery" || f[0] == "style") && f.size() >= 2) k = 1;
    int e = findName(kEmotionNames, ARRAY_COUNT(kEmotionNames), f[k]);
    if (e >= 0) {
        st.emotion = (u8)e;
        st.intensity = 1.f;
        if (f.size() > k + 1 && parseNumber(f[k + 1], num)) st.intensity = Clamp(num, 0.f, 1.5f);
        return true;
    }
    int d = findName(kDeliveryNames, ARRAY_COUNT(kDeliveryNames), f[k]);
    if (d >= 0) {
        st.delivery = (u8)d;
        return true;
    }
    int a = findName(kAccentNames, ARRAY_COUNT(kAccentNames), f[k]);
    if (a >= 0 && a != ACCENT_GENERAL && k == 0) {  // bare accent names: "[south]"
        st.accent = (u8)a;
        st.accentStrength = 1.f;
        if (f.size() >= 2 && parseNumber(f[1], num)) st.accentStrength = Saturate(num);
        return true;
    }
    return false;
}

static std::string fmtAmount(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

}  // namespace detail

std::string styleTags(const Style& s) {
    using namespace detail;
    std::string r;
    if (s.accent != ACCENT_GENERAL && s.accent < ACCENT_COUNT && s.accentStrength > 0.f) {
        r += "[accent:";
        r += kAccentTag[s.accent];
        if (s.accentStrength < 0.995f) r += ":" + fmtAmount(Saturate(s.accentStrength));
        r += "]";
    }
    if (s.delivery != DELIVERY_TALK && s.delivery < DELIVERY_COUNT) r += std::string("[") + kDeliveryTag[s.delivery] + "]";
    if (s.emotion != EMOTION_NEUTRAL && s.emotion < EMOTION_COUNT) {
        r += "[";
        r += kEmotionTag[s.emotion];
        if (fabsf(s.intensity - 1.f) > 0.005f) r += ":" + fmtAmount(Clamp(s.intensity, 0.f, 1.5f));
        r += "]";
    }
    return r;
}

std::string displayText(const char* text) {
    std::string out;
    if (!text) return out;
    const char* p = text;
    while (*p) {
        if (*p == '[') {
            const char* e = strchr(p, ']');
            if (e && e - p <= 80) {  // tag or stage direction: drop it
                p = e + 1;
                continue;
            }
        }
        if (*p == '*') {
            bool letterNext = isalpha((unsigned char)p[1]) != 0;
            bool letterPrev = !out.empty() && isalpha((unsigned char)out.back()) != 0;
            if (letterNext || letterPrev) {
                p++;
                continue;
            }
        }
        out += *p++;
    }
    // tidy whitespace: collapse runs, no space before punctuation, trim
    std::string r;
    for (size_t i = 0; i < out.size(); i++) {
        char c = out[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (r.empty() || r.back() == ' ') continue;
            size_t j = i + 1;
            while (j < out.size() && (out[j] == ' ' || out[j] == '\t' || out[j] == '\n' || out[j] == '\r')) j++;
            if (j < out.size() && strchr(",.!?;:", out[j])) continue;
            r += ' ';
            continue;
        }
        r += c;
    }
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
}

// ---------------------------------------------------------------------------------------------
// Personas

namespace detail {

struct PersonaDef {
    const char* key;
    float pitch, fs, speed, breath, rough, expr;
    u8 accent;
    float accentStrength;
    u8 emotion;
    float intensity;
    u8 delivery;
};

// Timbre, accent and default manner of the story cast and stock roles.
static const PersonaDef kPersonas[] = {
    // protagonists
    {"mari", 198.f, 1.14f, 1.0f, 0.16f, 0.04f, 1.15f, ACCENT_LATINO, 0.3f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"marisol", 198.f, 1.14f, 1.0f, 0.16f, 0.04f, 1.15f, ACCENT_LATINO, 0.3f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"dex", 106.f, 0.98f, 0.97f, 0.12f, 0.14f, 0.9f, ACCENT_GENERAL, 1.f, EMOTION_CALM, 0.3f, DELIVERY_TALK},
    // story cast
    {"cast_tomas", 134.f, 1.04f, 1.1f, 0.14f, 0.02f, 1.35f, ACCENT_LATINO, 0.45f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_lucha", 180.f, 1.08f, 0.93f, 0.22f, 0.3f, 1.25f, ACCENT_LATINO, 0.8f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_rook", 86.f, 0.92f, 0.93f, 0.08f, 0.42f, 0.9f, ACCENT_NEWYORK, 0.7f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_kit", 216.f, 1.18f, 1.12f, 0.12f, 0.f, 1.35f, ACCENT_GENERAL, 1.f, EMOTION_HAPPY, 0.3f, DELIVERY_TALK},
    {"cast_jonah", 98.f, 0.97f, 0.86f, 0.22f, 0.48f, 1.05f, ACCENT_SOUTH, 0.9f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_sandoval", 102.f, 0.98f, 0.9f, 0.1f, 0.08f, 0.85f, ACCENT_BRITISH, 0.45f, EMOTION_CALM, 0.6f, DELIVERY_TALK},
    {"cast_holt", 168.f, 1.07f, 1.03f, 0.05f, 0.12f, 0.75f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_cuervo", 92.f, 0.95f, 0.95f, 0.1f, 0.34f, 1.15f, ACCENT_CARIBBEAN, 0.6f, EMOTION_CALM, 0.4f, DELIVERY_TALK},
    {"cast_thug_a", 118.f, 1.01f, 1.06f, 0.1f, 0.18f, 1.2f, ACCENT_LATINO, 0.6f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_thug_b", 104.f, 0.97f, 1.0f, 0.08f, 0.3f, 1.1f, ACCENT_CARIBBEAN, 0.55f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_thug_c", 126.f, 1.02f, 1.1f, 0.12f, 0.1f, 1.3f, ACCENT_NEWYORK, 0.5f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_thug_d", 204.f, 1.15f, 1.08f, 0.14f, 0.12f, 1.25f, ACCENT_LATINO, 0.55f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_guard_a", 100.f, 0.95f, 0.95f, 0.06f, 0.2f, 0.7f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_guard_b", 112.f, 0.97f, 1.0f, 0.08f, 0.15f, 0.75f, ACCENT_BRITISH, 0.35f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_cop_a", 110.f, 0.97f, 1.02f, 0.08f, 0.16f, 0.85f, ACCENT_SOUTH, 0.45f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_cop_b", 190.f, 1.12f, 1.05f, 0.1f, 0.06f, 0.9f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_bouncer", 84.f, 0.92f, 0.9f, 0.08f, 0.4f, 0.8f, ACCENT_NEWYORK, 0.8f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_docker", 96.f, 0.95f, 0.98f, 0.1f, 0.35f, 1.05f, ACCENT_NEWYORK, 0.6f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"cast_reporter", 200.f, 1.15f, 1.04f, 0.1f, 0.f, 1.15f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_NEWS},
    {"cast_banker", 114.f, 0.99f, 0.95f, 0.08f, 0.06f, 0.8f, ACCENT_BRITISH, 0.35f, EMOTION_CALM, 0.4f, DELIVERY_TALK},
    {"cast_pilot", 188.f, 1.13f, 1.05f, 0.1f, 0.04f, 0.95f, ACCENT_GENERAL, 1.f, EMOTION_CALM, 0.3f, DELIVERY_TALK},
    {"cast_mechanic", 104.f, 0.97f, 0.92f, 0.14f, 0.28f, 1.1f, ACCENT_SOUTH, 0.65f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    // stock roles
    {"cop", 108.f, 0.97f, 1.03f, 0.08f, 0.14f, 0.85f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"dispatcher", 196.f, 1.13f, 1.05f, 0.08f, 0.02f, 0.8f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_DISPATCH},
    {"dj", 116.f, 1.0f, 1.04f, 0.1f, 0.06f, 1.3f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_DJ},
    {"dj_female", 206.f, 1.16f, 1.04f, 0.12f, 0.02f, 1.3f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_DJ},
    {"announcer", 100.f, 0.96f, 1.0f, 0.06f, 0.08f, 1.2f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_AD},
    {"announcer_female", 196.f, 1.14f, 1.0f, 0.1f, 0.f, 1.2f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_AD},
    {"newsreader", 112.f, 0.98f, 1.0f, 0.08f, 0.04f, 1.0f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_NEWS},
    {"newsreader_female", 192.f, 1.13f, 1.0f, 0.1f, 0.f, 1.05f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_NEWS},
    {"redneck", 104.f, 0.97f, 0.9f, 0.14f, 0.3f, 1.1f, ACCENT_SOUTH, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"tourist", 124.f, 1.01f, 1.0f, 0.12f, 0.04f, 1.25f, ACCENT_BRITISH, 0.8f, EMOTION_HAPPY, 0.4f, DELIVERY_TALK},
    {"gangster", 100.f, 0.96f, 1.0f, 0.1f, 0.25f, 1.1f, ACCENT_LATINO, 0.6f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"old_woman", 172.f, 1.1f, 0.88f, 0.24f, 0.3f, 1.05f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"old_man", 96.f, 0.95f, 0.86f, 0.2f, 0.45f, 0.95f, ACCENT_GENERAL, 1.f, EMOTION_NEUTRAL, 1.f, DELIVERY_TALK},
    {"kid", 265.f, 1.3f, 1.12f, 0.12f, 0.f, 1.4f, ACCENT_GENERAL, 1.f, EMOTION_HAPPY, 0.3f, DELIVERY_TALK},
};

}  // namespace detail

Persona persona(const char* keyIn, bool femaleIfUnknown) {
    using namespace detail;
    std::string key;
    for (const char* p = keyIn ? keyIn : ""; *p; p++) key += (char)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
    Persona r;
    for (const PersonaDef& d : kPersonas) {
        if (key != d.key) continue;
        r.voice.pitch = d.pitch;
        r.voice.formantScale = d.fs;
        r.voice.speed = d.speed;
        r.voice.breathiness = d.breath;
        r.voice.roughness = d.rough;
        r.voice.expressiveness = d.expr;
        r.style.accent = d.accent;
        r.style.accentStrength = d.accentStrength;
        r.style.emotion = d.emotion;
        r.style.intensity = d.intensity;
        r.style.delivery = d.delivery;
        return r;
    }
    // Unknown key: a stable preset voice and a mild accent chosen from the key's hash.
    u32 h = hashString(key.c_str());
    r.voice = presetVoice(femaleIfUnknown, h);
    u32 a = hash32(h ^ 0x5bd1e995u) % 10u;
    static const u8 kAcc[10] = {ACCENT_GENERAL, ACCENT_GENERAL, ACCENT_GENERAL, ACCENT_GENERAL, ACCENT_SOUTH,
                                ACCENT_NEWYORK, ACCENT_LATINO, ACCENT_LATINO, ACCENT_CARIBBEAN, ACCENT_BRITISH};
    r.style.accent = kAcc[a];
    r.style.accentStrength = 0.35f + 0.4f * hashToFloat(hash32(h + 17u));
    return r;
}

}  // namespace Speech
