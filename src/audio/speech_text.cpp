// Text normalization: raw UTF-8 text -> lowercase words with phrase-break and emphasis marks.
// Handles cardinals, ordinals, years, decimals, money ($, cents, k/M/B), percentages, times (10:30, pm),
// phone-number-like digit groups, common abbreviations (Mr., Dr., St., Ave., ...), ALL CAPS (shouting
// vs. acronyms), *emphasis*, contractions / apostrophes ("nothin'"), explicit phonemes {HH AH0 L OW1}
// and punctuation-driven phrasing.
#include "speech_internal.h"

namespace Speech {
namespace detail {

// ---------------------------------------------------------------------------------------------
// Number words

static const char* const kOnes[20] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
                                      "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
                                      "seventeen", "eighteen", "nineteen"};
static const char* const kTens[10] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

typedef std::vector<std::string> Words;

static void say(Words& w, const char* s) { w.push_back(s); }

static void cardinalBelow1000(int n, Words& w) {
    if (n >= 100) {
        say(w, kOnes[n / 100]);
        say(w, "hundred");
        n %= 100;
        if (n == 0) return;
    }
    if (n >= 20) {
        say(w, kTens[n / 10]);
        if (n % 10) say(w, kOnes[n % 10]);
    } else {
        say(w, kOnes[n]);
    }
}

static void cardinal(u64 n, Words& w) {
    if (n == 0) {
        say(w, "zero");
        return;
    }
    static const char* const kScale[] = {"", "thousand", "million", "billion", "trillion", "quadrillion"};
    int groups[7] = {0, 0, 0, 0, 0, 0, 0};
    int ng = 0;
    while (n > 0 && ng < 7) {
        groups[ng++] = (int)(n % 1000);
        n /= 1000;
    }
    for (int g = ng - 1; g >= 0; g--) {
        if (groups[g] == 0) continue;
        cardinalBelow1000(groups[g], w);
        if (g > 0 && g < 6) say(w, kScale[g]);
    }
}

static void ordinalize(Words& w) {
    if (w.empty()) return;
    std::string& s = w.back();
    static const char* const kIrr[][2] = {{"one", "first"},   {"two", "second"}, {"three", "third"},
                                         {"five", "fifth"},  {"eight", "eighth"}, {"nine", "ninth"},
                                         {"twelve", "twelfth"}};
    for (size_t i = 0; i < ARRAY_COUNT(kIrr); i++)
        if (s == kIrr[i][0]) {
            s = kIrr[i][1];
            return;
        }
    if (s.size() > 1 && s.back() == 'y') s = s.substr(0, s.size() - 1) + "ieth";
    else s += "th";
}

static void digitsSeq(const std::string& d, Words& w, bool ohForZero) {
    for (char c : d)
        if (c >= '0' && c <= '9') say(w, (c == '0' && ohForZero) ? "oh" : kOnes[c - '0']);
}

static void yearWords(int y, Words& w) {
    if (y >= 2000 && y <= 2009) {
        cardinal((u64)y, w);
        return;
    }
    int hi = y / 100, lo = y % 100;
    cardinalBelow1000(hi, w);
    if (lo == 0) say(w, "hundred");
    else if (lo < 10) {
        say(w, "oh");
        say(w, kOnes[lo]);
    } else cardinalBelow1000(lo, w);
}

static u64 parseDigits(const std::string& s) {
    u64 v = 0;
    for (char c : s)
        if (c >= '0' && c <= '9') v = v * 10 + (u64)(c - '0');
    return v;
}

// ---------------------------------------------------------------------------------------------
// Tokenizer

enum TokType { TK_WORD, TK_NUM, TK_PUNCT };
struct Tok {
    TokType type;
    std::string s;       // original text (ASCII-folded)
    bool spaceBefore;
};

static bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool isDigit(char c) { return c >= '0' && c <= '9'; }
static bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
static char toLower(char c) { return isUpper(c) ? (char)(c + 32) : c; }
static std::string lowerStr(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = toLower(c);
    return r;
}

// Folds UTF-8 to ASCII: accented Latin letters -> base letters, typographic punctuation -> ASCII.
// Currency signs: '$' plus private marker bytes for the euro, pound and yen signs (folded from UTF-8).
static const char kEuroMark = '\x03', kPoundMark = '\x04', kYenMark = '\x05';
static bool isCurrencyMark(char c) { return c == '$' || c == kEuroMark || c == kPoundMark || c == kYenMark; }
static const char* currencyName(char c, bool plural) {
    if (c == kEuroMark) return plural ? "euros" : "euro";
    if (c == kPoundMark) return plural ? "pounds" : "pound";
    if (c == kYenMark) return "yen";
    return plural ? "dollars" : "dollar";
}

static std::string foldUtf8(const char* text) {
    std::string out;
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        unsigned c = *p;
        if (c < 0x80) {
            out += (char)c;
            p++;
            continue;
        }
        unsigned cp = 0;
        int n = 0;
        if ((c & 0xE0) == 0xC0) cp = c & 0x1F, n = 1;
        else if ((c & 0xF0) == 0xE0) cp = c & 0x0F, n = 2;
        else if ((c & 0xF8) == 0xF0) cp = c & 0x07, n = 3;
        else {
            p++;
            continue;
        }
        p++;
        bool bad = false;
        for (int i = 0; i < n; i++) {
            if ((*p & 0xC0) != 0x80) {
                bad = true;
                break;
            }
            cp = (cp << 6) | (*p & 0x3F);
            p++;
        }
        if (bad) continue;
        if (cp == 0x2018 || cp == 0x2019 || cp == 0x02BC || cp == 0x2032) out += '\'';
        else if (cp == 0x201C || cp == 0x201D || cp == 0x00AB || cp == 0x00BB) out += '"';
        else if (cp == 0x2014 || cp == 0x2013 || cp == 0x2012 || cp == 0x2015) out += " -- ";
        else if (cp == 0x2026) out += "...";
        else if (cp == 0x00A0 || cp == 0x2009 || cp == 0x200A || cp == 0x2002 || cp == 0x2003) out += ' ';
        else if (cp == 0x20AC) out += kEuroMark;   // currency signs become marker bytes (see isCurrencyMark)
        else if (cp == 0x00A3) out += kPoundMark;
        else if (cp == 0x00A5) out += kYenMark;
        else if (cp == 0x00BF || cp == 0x00A1) out += ' ';
        else if (cp >= 0xC0 && cp <= 0x17F) {
            static const char kLatin1[] =  // 0xC0..0xFF
                "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
            char b = cp <= 0xFF ? kLatin1[cp - 0xC0] : 'e';
            if (cp == 0xDF) out += "ss";
            else if (cp == 0xC6) out += "AE";
            else if (cp == 0xE6) out += "ae";
            else if (cp == 0xD7) out += " times ";
            else if (cp == 0xF7) out += ' ';
            else if (cp > 0xFF) {
                // Latin Extended-A: map a few common ones, rest to a vowel-ish guess
                static const char kExtA[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
                size_t idx = cp - 0x100;
                out += idx < sizeof(kExtA) - 1 ? kExtA[idx] : 'e';
            } else out += b;
        } else out += ' ';
    }
    return out;
}

static void tokenize(const std::string& s, std::vector<Tok>& toks) {
    size_t i = 0, n = s.size();
    bool space = true;
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = true;
            i++;
            continue;
        }
        Tok t;
        t.spaceBefore = space;
        space = false;
        // explicit phonemes {HH AH0 L OW1}
        if (c == '{') {
            size_t e = s.find('}', i);
            if (e != std::string::npos) {
                t.type = TK_WORD;
                t.s = s.substr(i, e - i + 1);
                toks.push_back(t);
                i = e + 1;
                continue;
            }
            i++;
            continue;
        }
        bool numStart = isDigit(c) || (isCurrencyMark(c) && i + 1 < n && (isDigit(s[i + 1]) || (s[i + 1] == '.' && i + 2 < n && isDigit(s[i + 2])))) ||
                        (c == '.' && i + 1 < n && isDigit(s[i + 1]) && (i == 0 || !isDigit(s[i - 1]))) ||
                        ((c == '-' || c == '+') && i + 1 < n && isDigit(s[i + 1]) && t.spaceBefore);
        if (numStart) {
            size_t b = i;
            if (isCurrencyMark(s[i]) || s[i] == '-' || s[i] == '+') i++;
            while (i < n) {
                char d = s[i];
                if (isDigit(d)) i++;
                else if ((d == ',' || d == '.' || d == ':') && i + 1 < n && isDigit(s[i + 1])) i++;
                else if (d == '%') { i++; break; }
                else if (isAlpha(d)) {
                    // attached suffix: st nd rd th s k m b am pm
                    size_t e = i;
                    while (e < n && isAlpha(s[e])) e++;
                    std::string suf = lowerStr(s.substr(i, e - i));
                    if (suf == "st" || suf == "nd" || suf == "rd" || suf == "th" || suf == "s" || suf == "k" ||
                        suf == "m" || suf == "b" || suf == "am" || suf == "pm" || suf == "bn" || suf == "mm" ||
                        suf == "km" || suf == "mph" || suf == "lbs" || suf == "kg" || suf == "ft" || suf == "x") {
                        i = e;
                    }
                    break;
                } else break;
            }
            t.type = TK_NUM;
            t.s = s.substr(b, i - b);
            // amount followed by its currency sign ("5$", "4 \u20ac"): read like a prefixed amount
            if (!isCurrencyMark(t.s[0])) {
                size_t j = i < n && s[i] == ' ' ? i + 1 : i;
                if (j < n && isCurrencyMark(s[j]) && !(j + 1 < n && isDigit(s[j + 1]))) {
                    t.s = std::string(1, s[j]) + t.s;
                    i = j + 1;
                }
            }
            // number-letter-number combos like 4x4 handled as separate tokens
            toks.push_back(t);
            continue;
        }
        if (isAlpha(c) || (c == '\'' && i + 1 < n && isAlpha(s[i + 1]) && t.spaceBefore)) {
            size_t b = i;
            i++;
            while (i < n) {
                char d = s[i];
                if (isAlpha(d) || isDigit(d)) i++;
                else if (d == '\'' ) {
                    if (i + 1 < n && isAlpha(s[i + 1])) i++;
                    else {
                        // trailing apostrophe: nothin' / cops' / ol'
                        i++;
                        break;
                    }
                } else if (d == '.' && i + 1 < n && isAlpha(s[i + 1]) && (i + 2 >= n || !isAlpha(s[i + 2])) &&
                           (i - b == 1 || (i >= 2 && s[i - 2] == '.'))) {
                    // dotted abbreviation a.m. / u.s.a. / e.g.: consume single-letter "x." groups
                    i++;
                } else break;
            }
            // include a final '.' for single-letter dotted groups like "u.s." (x.y.)
            std::string w = s.substr(b, i - b);
            if (w.find('.') != std::string::npos && i < n && s[i] == '.') {
                w += '.';
                i++;
            }
            t.type = TK_WORD;
            t.s = w;
            toks.push_back(t);
            continue;
        }
        // punctuation
        t.type = TK_PUNCT;
        if (c == '.' && i + 2 < n && s[i + 1] == '.' && s[i + 2] == '.') {
            t.s = "...";
            i += 3;
            while (i < n && s[i] == '.') i++;
        } else if (c == '-' && i + 1 < n && s[i + 1] == '-') {
            t.s = "--";
            i += 2;
            while (i < n && s[i] == '-') i++;
        } else {
            t.s = std::string(1, c);
            i++;
        }
        toks.push_back(t);
    }
}

// ---------------------------------------------------------------------------------------------
// Abbreviations

struct Abbrev {
    const char* abbr;  // lowercase, without the period
    const char* exp;
    int kind;          // 0 generic, 1 title (precedes a name, never ends a sentence), 2 street type
};

static const Abbrev kAbbrevs[] = {
    {"mr", "mister", 1}, {"mrs", "missus", 1}, {"ms", "miz", 1}, {"dr", "doctor", 1}, {"prof", "professor", 1},
    {"sgt", "sergeant", 1}, {"lt", "lieutenant", 1}, {"capt", "captain", 1}, {"cpt", "captain", 1},
    {"det", "detective", 1}, {"gov", "governor", 1}, {"sen", "senator", 1}, {"rep", "representative", 1},
    {"gen", "general", 1}, {"col", "colonel", 1}, {"maj", "major", 1}, {"cpl", "corporal", 1}, {"pvt", "private", 1},
    {"off", "officer", 1}, {"insp", "inspector", 1}, {"rev", "reverend", 1}, {"mt", "mount", 1}, {"ft", "fort", 1},
    {"st", "saint", 1}, {"jr", "junior", 0}, {"sr", "senior", 0}, {"ave", "avenue", 2}, {"blvd", "boulevard", 2},
    {"rd", "road", 2}, {"ln", "lane", 2}, {"hwy", "highway", 2}, {"fwy", "freeway", 2}, {"pkwy", "parkway", 2},
    {"ct", "court", 2}, {"pl", "place", 2}, {"sq", "square", 2}, {"apt", "apartment", 0}, {"bldg", "building", 0},
    {"dept", "department", 0}, {"etc", "et cetera", 0}, {"vs", "versus", 0}, {"approx", "approximately", 0},
    {"inc", "incorporated", 0}, {"corp", "corporation", 0}, {"co", "company", 0}, {"ltd", "limited", 0},
    {"min", "minutes", 0}, {"mins", "minutes", 0}, {"hr", "hours", 0}, {"hrs", "hours", 0}, {"sec", "seconds", 0},
    {"lbs", "pounds", 0}, {"oz", "ounces", 0}, {"no", "number", 0}, {"tel", "telephone", 0}, {"ext", "extension", 0},
    {"jan", "january", 0}, {"feb", "february", 0}, {"mar", "march", 0}, {"apr", "april", 0}, {"jun", "june", 0},
    {"jul", "july", 0}, {"aug", "august", 0}, {"sep", "september", 0}, {"sept", "september", 0},
    {"oct", "october", 0}, {"nov", "november", 0}, {"dec", "december", 0}, {"mon", "monday", 0},
    {"tue", "tuesday", 0}, {"tues", "tuesday", 0}, {"wed", "wednesday", 0}, {"thu", "thursday", 0},
    {"thurs", "thursday", 0}, {"fri", "friday", 0}, {"sat", "saturday", 0}, {"sun", "sunday", 0},
};

static const Abbrev* findAbbrev(const std::string& lw) {
    for (size_t i = 0; i < ARRAY_COUNT(kAbbrevs); i++)
        if (lw == kAbbrevs[i].abbr) return &kAbbrevs[i];
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Normalizer

struct Normalizer {
    std::vector<TextWord>* out;
    bool shoutSentence = false;

    void addWord(const std::string& w, u8 emph = 0, bool spell = false) {
        if (w.empty()) return;
        TextWord t;
        t.w = w;
        t.emph = emph;
        t.spell = spell;
        t.shout = shoutSentence;
        out->push_back(t);
    }
    void addWords(const Words& ws, u8 emph = 0) {
        for (const std::string& s : ws) {
            // multi-word strings ("et cetera")
            size_t b = 0;
            while (b < s.size()) {
                size_t e = s.find(' ', b);
                if (e == std::string::npos) e = s.size();
                if (e > b) addWord(s.substr(b, e - b), emph);
                b = e + 1;
            }
        }
    }
    void setBreak(u8 brk) {
        if (out->empty()) return;
        u8& b = out->back().brk;
        // keep the strongest break (sentence-final types dominate)
        if (brk > b) b = brk;
    }
};

static bool looksLikeAcronym(const std::string& w) {
    // all caps, letters only; no vowels -> spell; short -> spell
    bool vowel = false;
    for (char c : w)
        if (strchr("AEIOUY", c)) vowel = true;
    return !vowel || w.size() <= 3;
}

static void expandNumber(Normalizer& nz, const std::string& tokIn, const std::vector<Tok>& toks, size_t ti,
                         const std::string& prevWord) {
    std::string t = tokIn;
    bool money = false, negative = false, percent = false;
    char currency = '$';
    if (!t.empty() && isCurrencyMark(t[0])) {
        money = true;
        currency = t[0];
        t = t.substr(1);
    } else if (!t.empty() && (t[0] == '-' || t[0] == '+')) {
        negative = t[0] == '-';
        t = t.substr(1);
    }
    if (!t.empty() && t.back() == '%') {
        percent = true;
        t.pop_back();
    }
    // split alphabetic suffix
    size_t sp = t.size();
    while (sp > 0 && isAlpha(t[sp - 1])) sp--;
    std::string suf = lowerStr(t.substr(sp));
    t = t.substr(0, sp);
    Words w;
    if (negative) say(w, "minus");

    // time 10:30
    size_t colon = t.find(':');
    if (colon != std::string::npos) {
        int h = (int)parseDigits(t.substr(0, colon));
        std::string ms = t.substr(colon + 1);
        size_t colon2 = ms.find(':');
        if (colon2 != std::string::npos) ms = ms.substr(0, colon2);
        int m = (int)parseDigits(ms);
        cardinal((u64)h, w);
        bool ampm = suf == "am" || suf == "pm";
        if (!ampm && ti + 1 < toks.size() && toks[ti + 1].type == TK_WORD) {
            std::string nx = lowerStr(toks[ti + 1].s);
            ampm = nx == "am" || nx == "pm" || nx == "a.m." || nx == "p.m." || nx == "a.m" || nx == "p.m";
        }
        if (m == 0) {
            if (!ampm) say(w, "o'clock");
        } else if (m < 10) {
            say(w, "oh");
            say(w, kOnes[m]);
        } else {
            cardinalBelow1000(m, w);
        }
        if (suf == "am" || suf == "pm") {
            nz.addWords(w);
            nz.addWord(suf.substr(0, 1), 0, true);
            nz.addWord("m", 0, true);
            return;
        }
        nz.addWords(w);
        return;
    }

    // phone-number-like / code groups: "555-0123" arrives as separate tokens; long digit strings are read digitwise
    std::string digitsOnly;
    for (char c : t)
        if (isDigit(c)) digitsOnly += c;
    size_t dot = t.find('.');
    std::string intPart = dot == std::string::npos ? t : t.substr(0, dot);
    std::string fracPart = dot == std::string::npos ? "" : t.substr(dot + 1);
    bool hasComma = intPart.find(',') != std::string::npos;
    std::string intDigits;
    for (char c : intPart)
        if (isDigit(c)) intDigits += c;

    // scale suffixes k / m / b
    const char* scale = nullptr;
    if (suf == "k" && money) scale = "thousand";
    else if (suf == "m" && money) scale = "million";
    else if ((suf == "b" || suf == "bn") && money) scale = "billion";
    // following scale word ("$2.5 million")
    std::string nextWord;
    if (ti + 1 < toks.size() && toks[ti + 1].type == TK_WORD) nextWord = lowerStr(toks[ti + 1].s);
    bool nextScale = nextWord == "million" || nextWord == "billion" || nextWord == "thousand" || nextWord == "trillion" ||
                     nextWord == "hundred";

    if (!hasComma && dot == std::string::npos && intDigits.size() >= 6 && !money && !scale) {
        digitsSeq(intDigits, w, false);  // long codes: digit by digit
        nz.addWords(w);
        return;
    }
    if (!hasComma && dot == std::string::npos && !money && suf.empty() && !percent &&
        (intDigits == "911" || intDigits == "411" || intDigits == "311" || intDigits == "211")) {
        digitsSeq(intDigits, w, false);  // emergency / service numbers
        nz.addWords(w);
        return;
    }
    if (!hasComma && intDigits.size() > 15 && !money) {
        digitsSeq(intDigits, w, false);
        nz.addWords(w);
        return;
    }
    if (!hasComma && intDigits.size() > 1 && intDigits[0] == '0' && dot == std::string::npos && !money) {
        digitsSeq(intDigits, w, true);  // "007", "0123"
        nz.addWords(w);
        return;
    }

    u64 iv = parseDigits(intDigits);
    bool ordinal = suf == "st" || suf == "nd" || suf == "rd" || suf == "th";
    bool decade = suf == "s" && !money;
    bool year = !money && !percent && !hasComma && dot == std::string::npos && intDigits.size() == 4 && !ordinal &&
                !scale && ((iv >= 1100 && iv <= 1999) ||
                           (iv >= 2000 && iv <= 2099 &&
                            (prevWord == "in" || prevWord == "since" || prevWord == "of" || prevWord == "from" ||
                             prevWord == "until" || prevWord == "till" || prevWord == "by" || prevWord == "year" ||
                             prevWord == "back" || prevWord == "around" || prevWord == "summer" ||
                             prevWord == "winter" || prevWord == "spring" || prevWord == "fall" || decade)));
    if (year) {
        yearWords((int)iv, w);
        if (decade) {
            std::string& l = w.back();
            if (l.back() == 'y') l = l.substr(0, l.size() - 1) + "ies";
            else l += "s";
        }
        nz.addWords(w);
        return;
    }
    if (decade && intDigits.size() == 2) {  // 90s -> nineties
        cardinal(iv, w);
        std::string& l = w.back();
        if (l.back() == 'y') l = l.substr(0, l.size() - 1) + "ies";
        else l += "s";
        nz.addWords(w);
        return;
    }
    if (money) {
        u64 cents = 0;
        bool hasCents = false;
        if (!fracPart.empty() && !scale && !nextScale) {
            std::string f = fracPart.substr(0, 2);
            if (f.size() == 1) f += "0";
            cents = parseDigits(f);
            hasCents = true;
        }
        if (scale || nextScale) {
            cardinal(iv, w);
            if (!fracPart.empty()) {
                say(w, "point");
                digitsSeq(fracPart, w, false);
            }
            if (scale) say(w, scale);
            else {
                // "$2.5 million": the scale word follows as its own token; add "dollars" after it
                nz.addWords(w);
                nz.addWord(std::string("\x01") + currencyName(currency, true));  // emitted after the scale word
                return;
            }
            say(w, currencyName(currency, true));
            nz.addWords(w);
            return;
        }
        if (currency == kYenMark) hasCents = false;
        if (iv > 0 || !hasCents || cents == 0) {
            cardinal(iv, w);
            say(w, currencyName(currency, iv != 1));
        }
        if (hasCents && cents > 0) {
            if (iv > 0) say(w, "and");
            cardinal(cents, w);
            if (currency == kPoundMark) say(w, cents == 1 ? "penny" : "pence");
            else say(w, cents == 1 ? "cent" : "cents");
        }
        nz.addWords(w);
        return;
    }
    cardinal(iv, w);
    if (ordinal) ordinalize(w);
    if (!fracPart.empty()) {
        say(w, "point");
        digitsSeq(fracPart, w, false);
    }
    if (scale) say(w, scale);
    if (percent) say(w, "percent");
    else if (suf == "mph") {
        say(w, "miles");
        say(w, "per");
        say(w, "hour");
    } else if (suf == "km") say(w, "kilometers");
    else if (suf == "mm") say(w, "millimeter");
    else if (suf == "lbs") say(w, "pounds");
    else if (suf == "kg") say(w, "kilograms");
    else if (suf == "ft") say(w, "feet");
    else if (suf == "x") say(w, (ti + 1 < toks.size() && toks[ti + 1].type == TK_NUM && !toks[ti + 1].spaceBefore) ? "by" : "times");
    else if (suf == "m" && !money) say(w, "meters");
    else if (suf == "k" || suf == "b") {
        nz.addWords(w);  // "5K run", "exit 12B": the letter is spelled
        nz.addWord(suf, 0, true);
        return;
    }
    else if (suf == "am" || suf == "pm") {
        nz.addWords(w);
        nz.addWord(suf.substr(0, 1), 0, true);
        nz.addWord("m", 0, true);
        return;
    }
    nz.addWords(w);
}

static u8 punctBreak(const std::string& p) {
    if (p == ",") return BRK_COMMA;
    if (p == ";" || p == ":") return BRK_CLAUSE;
    if (p == ".") return BRK_PERIOD;
    if (p == "?") return BRK_QUESTION;
    if (p == "!") return BRK_EXCLAIM;
    if (p == "...") return BRK_ELLIPSIS;
    if (p == "--") return BRK_DASH;
    if (p == "(" || p == ")" || p == "[" || p == "]") return BRK_COMMA;
    return BRK_NONE;
}

// ---------------------------------------------------------------------------------------------
// Homographs: the pronunciation is chosen from the neighbouring words (noun/verb stress pairs and a few
// others that matter in game dialogue: "the suspect" / "we suspect", "close the door" / "too close").

static bool inList(const std::string& w, const char* const* list, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (w == list[i]) return true;
    return false;
}
#define SPEECH_IN(w, list) inList(w, list, ARRAY_COUNT(list))

// Words after which a noun/verb homograph is a verb ("to record", "I suspect", "don't convict").
static const char* const kVerbCtx[] = {
    "to", "will", "would", "can", "could", "should", "must", "might", "may", "shall", "i", "we", "you", "they", "he",
    "she", "don't", "didn't", "doesn't", "won't", "can't", "cannot", "couldn't", "wouldn't", "shouldn't", "let's",
    "please", "gonna", "wanna", "gotta", "i'll", "we'll", "you'll", "they'll", "he'll", "she'll", "never", "always",
    "also", "not", "who", "just"};
// Words that follow a verb used as an imperative at the start of a phrase ("Record everything", "Escort him").
static const char* const kObjStart[] = {"the", "a", "an", "this", "that", "these", "those", "him", "her", "them",
                                        "it", "me", "us", "your", "my", "his", "our", "their", "everything",
                                        "everyone", "everybody", "all", "every", "some"};

struct NounVerb {
    const char* w;
    const char* noun;
    const char* verb;
};
static const NounVerb kNounVerb[] = {
    {"suspect", "S AH1 S P EH2 K T", "S AH0 S P EH1 K T"}, {"record", "R EH1 K ER0 D", "R IH0 K AO1 R D"},
    {"convict", "K AA1 N V IH0 K T", "K AH0 N V IH1 K T"}, {"escort", "EH1 S K AO2 R T", "IH0 S K AO1 R T"},
    {"permit", "P ER1 M IH2 T", "P ER0 M IH1 T"}, {"contract", "K AA1 N T R AE2 K T", "K AH0 N T R AE1 K T"},
    {"present", "P R EH1 Z AH0 N T", "P R IY0 Z EH1 N T"}, {"object", "AA1 B JH EH0 K T", "AH0 B JH EH1 K T"},
    {"project", "P R AA1 JH EH0 K T", "P R AH0 JH EH1 K T"}, {"protest", "P R OW1 T EH2 S T", "P R AH0 T EH1 S T"},
    {"produce", "P R OW1 D UW0 S", "P R AH0 D UW1 S"}, {"progress", "P R AA1 G R EH2 S", "P R AH0 G R EH1 S"},
    {"conduct", "K AA1 N D AH0 K T", "K AH0 N D AH1 K T"}, {"conflict", "K AA1 N F L IH0 K T", "K AH0 N F L IH1 K T"},
    {"contest", "K AA1 N T EH0 S T", "K AH0 N T EH1 S T"}, {"rebel", "R EH1 B AH0 L", "R IH0 B EH1 L"},
    {"insult", "IH1 N S AH0 L T", "IH0 N S AH1 L T"}, {"increase", "IH1 N K R IY2 S", "IH0 N K R IY1 S"},
    {"decrease", "D IY1 K R IY2 S", "D IH0 K R IY1 S"}, {"import", "IH1 M P AO2 R T", "IH0 M P AO1 R T"},
    {"export", "EH1 K S P AO2 R T", "IH0 K S P AO1 R T"},
    {"transport", "T R AE1 N S P AO2 R T", "T R AE0 N S P AO1 R T"}, {"survey", "S ER1 V EY2", "S ER0 V EY1"},
    {"desert", "D EH1 Z ER0 T", "D IH0 Z ER1 T"}, {"combat", "K AA1 M B AE0 T", "K AH0 M B AE1 T"},
    {"compound", "K AA1 M P AW0 N D", "K AH0 M P AW1 N D"}, {"extract", "EH1 K S T R AE0 K T", "IH0 K S T R AE1 K T"},
};

// Appends an inflection to an ARPAbet string according to its last phoneme (0 = -s, 1 = -ed, 2 = -ing).
static std::string inflectPron(const std::string& pron, int kind) {
    size_t sp = pron.find_last_of(' ');
    std::string last = sp == std::string::npos ? pron : pron.substr(sp + 1);
    while (!last.empty() && isDigit(last.back())) last.pop_back();
    bool sib = last == "S" || last == "Z" || last == "SH" || last == "ZH" || last == "CH" || last == "JH";
    bool voiceless = last == "P" || last == "T" || last == "K" || last == "F" || last == "TH" || last == "S" ||
                     last == "SH" || last == "CH";
    if (kind == 0) return pron + (sib ? " IH0 Z" : voiceless ? " S" : " Z");
    if (kind == 1) return pron + ((last == "T" || last == "D") ? " IH0 D" : voiceless ? " T" : " D");
    return pron + " IH0 NG";
}

// Returns the pronunciation of a homograph in this context, or "" when the word is not one.
static std::string resolveHomograph(const std::string& w, const std::string& prev, const std::string& next,
                                    const std::string& next2) {
    for (const NounVerb& h : kNounVerb) {
        std::string b = h.w;
        size_t n = b.size();
        if (w.compare(0, std::min(w.size(), n - 1), b, 0, n - 1) != 0) continue;
        bool eEnd = b.back() == 'e';
        int form = -1;  // 0 base, 1 -s, 2 -ed, 3 -ing
        if (w == b) form = 0;
        else if (w == b + "s") form = 1;
        else if (w == b + "ed" || (eEnd && w == b + "d")) form = 2;
        else if (w == b + "ing" || (eEnd && w == b.substr(0, n - 1) + "ing")) form = 3;
        if (form < 0) continue;
        if (form == 2) return inflectPron(h.verb, 1);
        if (form == 3) return inflectPron(h.verb, 2);
        bool verb = prev.empty() ? SPEECH_IN(next, kObjStart) : SPEECH_IN(prev, kVerbCtx);
        std::string pr = verb ? h.verb : h.noun;
        return form == 1 ? inflectPron(pr, 0) : pr;
    }
    if (w == "close") {
        static const char* const kAdjNext[] = {"to", "call", "calls", "range", "by", "enough", "friend", "friends",
                                               "behind", "together", "one", "shave", "quarters", "contact"};
        static const char* const kVerbNext[] = {"up", "down", "off", "in"};
        if (SPEECH_IN(next, kAdjNext)) return "K L OW1 S";
        if (!prev.empty() && SPEECH_IN(prev, kVerbCtx)) return "K L OW1 Z";
        if (SPEECH_IN(next, kObjStart) || SPEECH_IN(next, kVerbNext)) return "K L OW1 Z";
        return "K L OW1 S";
    }
    if (w == "live") {
        static const char* const kAdjNext[] = {
            "from", "music", "broadcast", "broadcasting", "coverage", "feed", "footage", "concert", "performance",
            "ammo", "round", "rounds", "wire", "wires", "bait", "stream", "audience", "tv", "television", "grenade",
            "update", "updates", "radio", "report", "reporting", "show", "shows", "band", "action", "event"};
        static const char* const kAdjPrev[] = {"the", "a", "are", "is", "we're", "you're", "they're", "it's", "i'm",
                                               "go", "going", "went", "be", "being", "been", "was", "were", "still"};
        if (SPEECH_IN(next, kAdjNext)) return "L AY1 V";
        if (!prev.empty() && SPEECH_IN(prev, kVerbCtx)) return "L IH1 V";
        if (SPEECH_IN(prev, kAdjPrev)) return "L AY1 V";
        return "L IH1 V";
    }
    if (w == "lives") {
        static const char* const kNounPrev[] = {
            "their", "our", "your", "my", "his", "her", "its", "the", "innocent", "many", "save", "saved", "saves",
            "saving", "risk", "risked", "risking", "lost", "lose", "losing", "ruin", "ruined", "cost", "costs", "nine",
            "two", "three", "four", "five", "hundreds", "thousands", "millions", "whose", "human", "other", "private",
            "double", "secret", "own", "real", "entire", "whole", "personal", "these", "those", "all", "extra"};
        static const char* const kVerbPrev[] = {"he", "she", "it", "who", "that", "which", "still", "also", "just",
                                                "now", "never", "only", "really", "actually"};
        static const char* const kVerbNext[] = {"in", "at", "with", "here", "there", "near", "alone", "next",
                                                "downtown", "across", "upstairs", "nearby", "around", "by", "on"};
        if (SPEECH_IN(prev, kNounPrev)) return "L AY1 V Z";
        if (SPEECH_IN(prev, kVerbPrev)) return "L IH1 V Z";
        if (SPEECH_IN(next, kVerbNext) && !(next == "on" && next2 == "the")) return "L IH1 V Z";
        return "L AY1 V Z";
    }
    if (w == "use") {
        static const char* const kNounPrev[] = {
            "the", "no", "any", "of", "for", "in", "a", "much", "good", "some", "its", "what's", "there's", "into",
            "full", "personal", "drug", "military", "police", "public", "daily", "illegal", "excessive", "future",
            "fair", "common", "own", "my", "your", "his", "her", "our", "their", "whose", "proper", "medical",
            "private", "official", "commercial", "heavy"};
        return SPEECH_IN(prev, kNounPrev) ? "Y UW1 S" : "Y UW1 Z";
    }
    if (w == "used") return next == "to" ? "Y UW1 S T" : "Y UW1 Z D";
    if (w == "lead") {
        static const char* const kMetalNext[] = {"pipe", "pipes", "poisoning", "paint", "bullet", "bullets",
                                                 "weight", "weights", "pencil", "balloon", "shot"};
        return (SPEECH_IN(next, kMetalNext) || prev == "of" || prev == "with") ? "L EH1 D" : "L IY1 D";
    }
    if (w == "wind") return (next == "up" || next == "down") ? "W AY1 N D" : "W IH1 N D";
    if (w == "wound") return (next == "up" || next == "down" || next == "around") ? "W AW1 N D" : "W UW1 N D";
    if (w == "tear") {
        static const char* const kDropNext[] = {"gas", "drop", "drops", "duct", "ducts"};
        return SPEECH_IN(next, kDropNext) ? "T IH1 R" : "T EH1 R";
    }
    if (w == "tears") {
        static const char* const kRipPrev[] = {"he", "she", "it", "who", "which", "that"};
        static const char* const kRipNext[] = {"up", "down", "off", "apart", "through", "into", "open", "it", "them"};
        return (SPEECH_IN(prev, kRipPrev) || SPEECH_IN(next, kRipNext)) ? "T EH1 R Z" : "T IH1 R Z";
    }
    if (w == "read") {
        static const char* const kPastPrev[] = {"have", "has", "had", "i've", "you've", "we've", "they've", "he's",
                                                "she's", "it's", "been", "was", "were", "is", "are", "already", "being"};
        return SPEECH_IN(prev, kPastPrev) ? "R EH1 D" : "R IY1 D";
    }
    if (w == "bass") {
        static const char* const kFishPrev[] = {"caught", "catch", "catching", "largemouth", "smallmouth", "striped",
                                                "sea"};
        static const char* const kFishNext[] = {"fishing", "boat", "pro", "tournament"};
        return (SPEECH_IN(prev, kFishPrev) || SPEECH_IN(next, kFishNext)) ? "B AE1 S" : "B EY1 S";
    }
    return "";
}
#undef SPEECH_IN

void normalizeText(const char* text, std::vector<TextWord>& out) {
    out.clear();
    if (!text) return;
    std::string s = foldUtf8(text);
    std::vector<Tok> toks;
    tokenize(s, toks);
    Normalizer nz;
    nz.out = &out;

    // Sentence-level shouting detection: a sentence whose letters are all upper case (>= 2 words or >= 4 letters).
    std::vector<bool> shout(toks.size(), false);
    {
        size_t b = 0;
        while (b < toks.size()) {
            size_t e = b;
            int words = 0, letters = 0, upper = 0;
            while (e < toks.size()) {
                const Tok& t = toks[e];
                if (t.type == TK_WORD && t.s[0] != '{') {
                    words++;
                    for (char c : t.s)
                        if (isAlpha(c)) {
                            letters++;
                            if (isUpper(c)) upper++;
                        }
                }
                e++;
                if (t.type == TK_PUNCT && (t.s == "." || t.s == "!" || t.s == "?")) break;
            }
            bool sh = letters >= 4 && upper == letters && (words >= 2 || letters >= 5);
            for (size_t k = b; k < e; k++) shout[k] = sh;
            b = e;
        }
    }

    std::string prevWord;
    for (size_t i = 0; i < toks.size(); i++) {
        const Tok& t = toks[i];
        nz.shoutSentence = shout[i];
        if (t.type == TK_PUNCT) {
            const std::string& p = t.s;
            if (p == "-") {
                // spaced hyphen acts as a dash; attached hyphen joins words (no pause)
                bool spacedAfter = i + 1 < toks.size() && toks[i + 1].spaceBefore;
                if (t.spaceBefore && spacedAfter) nz.setBreak(BRK_DASH);
                continue;
            }
            if (p == "&") {
                nz.addWord("and");
                continue;
            }
            if (p.size() == 1 && isCurrencyMark(p[0]) && p[0] != '$') {
                nz.addWord(currencyName(p[0], false));  // a lone euro / pound / yen sign
                continue;
            }
            if (p == "@") {
                nz.addWord("at");
                continue;
            }
            if (p == "+") {
                nz.addWord("plus");
                continue;
            }
            if (p == "=") {
                nz.addWord("equals");
                continue;
            }
            if (p == "#") {
                if (i + 1 < toks.size() && toks[i + 1].type == TK_NUM) nz.addWord("number");
                continue;
            }
            if (p == "%") {
                nz.addWord("percent");
                continue;
            }
            if (p == "*") continue;  // emphasis markers handled on words
            u8 b = punctBreak(p);
            if (b == BRK_PERIOD && i + 1 < toks.size() && toks[i + 1].type == TK_PUNCT &&
                (toks[i + 1].s == "?" || toks[i + 1].s == "!"))
                continue;
            if (b != BRK_NONE) nz.setBreak(b);
            continue;
        }
        if (t.type == TK_NUM) {
            // digit groups joined by hyphens: phone numbers (555-0123) or codes (10-4)
            if (i + 2 < toks.size() && toks[i + 1].type == TK_PUNCT && toks[i + 1].s == "-" && !toks[i + 1].spaceBefore &&
                toks[i + 2].type == TK_NUM && !toks[i + 2].spaceBefore) {
                // gather the whole chain
                std::vector<std::string> groups;
                size_t k = i;
                groups.push_back(toks[k].s);
                while (k + 2 < toks.size() && toks[k + 1].type == TK_PUNCT && toks[k + 1].s == "-" &&
                       !toks[k + 1].spaceBefore && toks[k + 2].type == TK_NUM && !toks[k + 2].spaceBefore) {
                    groups.push_back(toks[k + 2].s);
                    k += 2;
                }
                size_t totalDigits = 0;
                for (auto& g : groups) totalDigits += g.size();
                if (totalDigits >= 7) {
                    for (size_t g = 0; g < groups.size(); g++) {
                        Words w;
                        digitsSeq(groups[g], w, true);
                        nz.addWords(w);
                        if (g + 1 < groups.size()) nz.setBreak(BRK_COMMA);
                    }
                } else {
                    // "3-5 inches", "ages 18-25": an ascending pair (or one followed by a plural noun) is a range;
                    // otherwise a code or score ("10-4", "102-98")
                    bool pluralNext = k + 1 < toks.size() && toks[k + 1].type == TK_WORD && toks[k + 1].s.size() > 2 &&
                                      toks[k + 1].s.back() == 's';
                    bool ascending = false;
                    if (groups.size() == 2) {
                        std::string a, b;
                        for (char c : groups[0])
                            if (isDigit(c)) a += c;
                        for (char c : groups[1])
                            if (isDigit(c)) b += c;
                        ascending = !a.empty() && !b.empty() && a.size() <= 6 && b.size() <= 6 &&
                                    parseDigits(a) < parseDigits(b);
                    }
                    bool range = groups.size() == 2 && (pluralNext || ascending);
                    if (range && isCurrencyMark(groups[0][0]) && !isCurrencyMark(groups[1][0])) {
                        groups[1] = groups[0].substr(0, 1) + groups[1];  // "$10-20" -> "ten to twenty dollars"
                        groups[0] = groups[0].substr(1);
                    }
                    for (size_t g = 0; g < groups.size(); g++) {
                        expandNumber(nz, groups[g], toks, g + 1 < groups.size() ? i : k, prevWord);
                        if (range && g == 0) nz.addWord("to");
                    }
                }
                i = k;
                prevWord = "";
                continue;
            }
            // "24/7"
            expandNumber(nz, t.s, toks, i, prevWord);
            if (i + 2 < toks.size() && toks[i + 1].type == TK_PUNCT && toks[i + 1].s == "/" && toks[i + 2].type == TK_NUM) {
                i++;  // skip slash; next number is read on its own
            }
            prevWord = "";
            continue;
        }
        // Word token.
        std::string w = t.s;
        if (w[0] == '{') {
            TextWord tw;
            tw.w = w.substr(1, w.size() - 2);
            tw.phon = true;
            tw.shout = nz.shoutSentence;
            out.push_back(tw);
            prevWord = "";
            continue;
        }
        // emphasis with *word*
        u8 emph = 0;
        if (i > 0 && toks[i - 1].type == TK_PUNCT && toks[i - 1].s == "*") emph = 1;
        std::string lw = lowerStr(w);
        // dotted abbreviations: u.s., a.m., e.g., i.e., l.a.
        if (lw.find('.') != std::string::npos) {
            std::string letters;
            for (char c : lw)
                if (isAlpha(c)) letters += c;
            if (letters == "eg") nz.addWords(Words{"for", "example"});
            else if (letters == "ie") nz.addWords(Words{"that", "is"});
            else if (letters == "vs") nz.addWord("versus");
            else if (letters == "etc") nz.addWords(Words{"et", "cetera"});
            else
                for (char c : letters) nz.addWord(std::string(1, c), emph, true);
            prevWord = letters;
            continue;
        }
        // abbreviation followed by a period
        bool nextIsPeriod = i + 1 < toks.size() && toks[i + 1].type == TK_PUNCT && toks[i + 1].s == "." &&
                            !toks[i + 1].spaceBefore;
        if (nextIsPeriod) {
            const Abbrev* ab = findAbbrev(lw);
            bool nextCap = i + 2 < toks.size() && toks[i + 2].type == TK_WORD && isUpper(toks[i + 2].s[0]);
            bool prevCapOrNum = i > 0 && ((toks[i - 1].type == TK_WORD && isUpper(toks[i - 1].s[0])) || toks[i - 1].type == TK_NUM);
            if (ab && lw == "no" && !(i + 2 < toks.size() && toks[i + 2].type == TK_NUM)) ab = nullptr;
            if (ab && (lw == "sat" || lw == "sun" || lw == "mar" || lw == "jun" || lw == "min" || lw == "sec" ||
                       lw == "co" || lw == "off" || lw == "gen" || lw == "col" || lw == "rep" || lw == "wed" ||
                       lw == "mon" || lw == "dec" || lw == "sen" || lw == "pl" || lw == "fri") && !isUpper(w[0]))
                ab = nullptr;  // ordinary words at sentence end ("I sat.")
            if (ab) {
                const char* exp = ab->exp;
                if (lw == "dr" && !nextCap && prevCapOrNum) exp = "drive";
                if (lw == "st" && (prevCapOrNum || !nextCap)) exp = "street";
                if (lw == "ft" && (!nextCap || (i > 0 && toks[i - 1].type == TK_NUM))) exp = "feet";
                nz.addWords(Words{exp});
                // does the period end the sentence?
                bool endsSentence = false;
                if (i + 2 >= toks.size()) endsSentence = true;
                else if (ab->kind == 1 && !(lw == "st" && !strcmp(exp, "street")) && !(lw == "dr" && !strcmp(exp, "drive")))
                    endsSentence = false;
                else endsSentence = nextCap && ab->kind != 1;
                i++;  // consume the period
                if (endsSentence) nz.setBreak(BRK_PERIOD);
                prevWord = lw;
                continue;
            }
        }
        // mixed letters and digits: K9, AK47, M16, 3D
        bool hasDigit = false, hasAlpha = false;
        for (char c : w) {
            if (isDigit(c)) hasDigit = true;
            if (isAlpha(c)) hasAlpha = true;
        }
        if (hasDigit && hasAlpha) {
            size_t b = 0;
            while (b < w.size()) {
                size_t e = b;
                if (isDigit(w[b])) {
                    while (e < w.size() && isDigit(w[e])) e++;
                    Words ww;
                    cardinal(parseDigits(w.substr(b, e - b)), ww);
                    nz.addWords(ww, emph);
                } else {
                    while (e < w.size() && !isDigit(w[e])) e++;
                    std::string part = lowerStr(w.substr(b, e - b));
                    std::string clean;
                    for (char c : part)
                        if (isAlpha(c)) clean += c;
                    if (clean.size() <= 3 || !dictLookup(clean)) {
                        for (char c : clean) nz.addWord(std::string(1, c), emph, true);
                    } else nz.addWord(clean, emph);
                }
                b = e;
            }
            prevWord = lw;
            continue;
        }
        // strip surrounding quotes/apostrophes used as quotes
        bool gDrop = false;
        if (lw.size() >= 3 && lw.back() == '\'' && lw[lw.size() - 2] == 'n' && lw[lw.size() - 3] == 'i') {
            gDrop = true;  // nothin', somethin', runnin'
            lw.pop_back();
        } else if (lw.back() == '\'' && lw != "ol'") {
            // plural possessive "cops'" -> "cops"
            lw.pop_back();
        }
        if (lw.size() >= 2 && lw[0] == '\'' && !dictLookup(lw)) lw = lw.substr(1);
        // capitalization analysis
        int upper = 0, letters = 0;
        for (char c : w)
            if (isAlpha(c)) {
                letters++;
                if (isUpper(c)) upper++;
            }
        bool allCaps = letters >= 2 && upper == letters;
        if (allCaps && !nz.shoutSentence) {
            static const char* const kSpellCaps[] = {"us", "uk", "la", "ny", "dc", "pd", "un", "eu", "ai", "hq",
                                                     "er", "tv", "dj", "pc", "ok", "id", "ss", "bbq", "gps", "suv"};
            bool spellIt = false;
            for (size_t k = 0; k < ARRAY_COUNT(kSpellCaps); k++)
                if (lw == kSpellCaps[k]) spellIt = true;
            if (lw == "ok") {
                nz.addWord("okay", emph);
            } else if (spellIt) {
                const char* e = dictLookup(lw);
                if (e && lw != "us" && lw != "la" && lw != "er" && lw != "ai" && lw != "id") nz.addWord(lw, emph);
                else
                    for (char c : lw) nz.addWord(std::string(1, c), emph, true);
            } else if (lw == "swat" || lw == "nasa" || lw == "nato" || lw == "laser" || lw == "radar" ||
                       lw == "scuba" || lw == "fema" || lw == "unicef" || lw == "awol") {
                nz.addWord(lw, emph);  // acronyms pronounced as words: no shouting emphasis
            } else if (dictLookup(lw)) {
                const char* e = dictLookup(lw);
                bool letterSpelling = strlen(lw.c_str()) <= 5 && e[0] != '~' && lw.size() >= 2 && [&]() {
                    // dictionary acronym entries (fbi, atm) have one stressed vowel per letter
                    int v = 0;
                    Pron tmp;
                    parsePhonemes(e, tmp);
                    for (auto& x : tmp)
                        if (isVowel(x.ph)) v++;
                    return v >= (int)lw.size() && lw.size() <= 4;
                }();
                nz.addWord(lw, letterSpelling ? emph : (u8)(emph | 1));
            } else if (looksLikeAcronym(w)) {
                for (char c : lw)
                    if (isAlpha(c)) nz.addWord(std::string(1, c), emph, true);
            } else {
                nz.addWord(lw, (u8)(emph | 1));
            }
            prevWord = lw;
            continue;
        }
        if (lw == "mph" && prevWord.empty() && i > 0 && toks[i - 1].type == TK_NUM) {
            nz.addWords(Words{"miles", "per", "hour"});
            prevWord = lw;
            continue;
        }
        // single letters other than a / i are spelled
        if (lw.size() == 1 && lw != "a" && lw != "i") {
            if (lw == "u") nz.addWord("you", emph);
            else nz.addWord(lw, emph, true);
            prevWord = lw;
            continue;
        }
        if (lw == "a" && allCaps) {
            nz.addWord("a", emph);
            prevWord = lw;
            continue;
        }
        // remove any characters that are not letters/apostrophes
        std::string clean;
        for (char c : lw)
            if (isAlpha(c) || c == '\'') clean += c;
        if (clean.empty()) continue;
        TextWord tw;
        tw.w = clean;
        tw.emph = emph;
        tw.shout = nz.shoutSentence;
        tw.gDrop = gDrop;
        {
            // context-dependent pronunciations
            std::string pw = (i > 0 && toks[i - 1].type == TK_WORD) ? prevWord : std::string();
            std::string nw, nw2;
            if (i + 1 < toks.size() && toks[i + 1].type == TK_WORD) nw = lowerStr(toks[i + 1].s);
            if (i + 2 < toks.size() && toks[i + 2].type == TK_WORD) nw2 = lowerStr(toks[i + 2].s);
            std::string hp = resolveHomograph(clean, pw, nw, nw2);
            if (!hp.empty()) {
                tw.w = hp;
                tw.phon = true;
            }
        }
        if (clean == "may") {
            // the month (a stressed content word) rather than the modal verb: "in May", "May 5th"
            bool nextNum = i + 1 < toks.size() && toks[i + 1].type == TK_NUM;
            bool prevAdj = i > 0 && toks[i - 1].type == TK_WORD;
            static const char* const kBefore[] = {"in", "of", "on", "since", "until", "till", "from", "to", "through",
                                                  "last", "next", "early", "late", "mid", "during", "by"};
            bool prevMonth = false;
            for (size_t k = 0; k < ARRAY_COUNT(kBefore); k++)
                if (prevAdj && prevWord == kBefore[k]) prevMonth = true;
            if (nextNum || prevMonth) {
                tw.w = "M EY1";
                tw.phon = true;
            }
        }
        out.push_back(tw);
        prevWord = clean;
    }
    // Resolve deferred currency markers ("$2.5 million" -> "two point five million dollars").
    for (size_t k = 0; k < out.size(); k++) {
        if (!out[k].w.empty() && out[k].w[0] == '\x01') {
            std::string cur = out[k].w.substr(1);
            TextWord marker = out[k];
            out.erase(out.begin() + (long)k);
            // insert after the next word (the scale word)
            size_t pos = k + 1 <= out.size() ? k + 1 : out.size();
            TextWord tw;
            tw.w = cur;
            tw.shout = marker.shout;
            if (pos > 0 && pos <= out.size()) {
                tw.brk = out[pos - 1].brk;
                out[pos - 1].brk = BRK_NONE;
            }
            out.insert(out.begin() + (long)pos, tw);
        }
    }
    if (!out.empty() && out.back().brk == BRK_NONE) out.back().brk = BRK_PERIOD;
}

}  // namespace detail
}  // namespace Speech
