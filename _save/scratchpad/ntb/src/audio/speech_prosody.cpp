// Phonetic and prosodic processing: phrasing, post-lexical rules (flapping, dark /l/, "the" before vowels,
// aspiration, geminate merging), syllabification, segment durations (Klatt 1979 / MITalk duration rules)
// and the F0 contour (declination, pitch accents with downstep, boundary tones, emphasis).
#include "speech_internal.h"

namespace Speech {
namespace detail {

static bool isWhWord(const std::string& w) {
    return w == "what" || w == "where" || w == "who" || w == "whom" || w == "whose" || w == "why" || w == "when" ||
           w == "how" || w == "which" || w == "what's" || w == "where's" || w == "who's" || w == "how's" ||
           w == "why's" || w == "when's" || w == "whatcha";
}

static bool isPhraseSplitWord(const std::string& w) {
    static const char* const kSplit[] = {"and", "but", "or", "because", "that", "which", "who", "when", "where",
                                         "while", "if", "so", "since", "until", "unless", "although", "though",
                                         "before", "after", "with", "for", "from", "to", "in", "on", "at", "into",
                                         "like", "than", "whether", "cause"};
    for (size_t i = 0; i < ARRAY_COUNT(kSplit); i++)
        if (w == kSplit[i]) return true;
    return false;
}

static bool isSentenceBreak(u8 b) { return b == BRK_PERIOD || b == BRK_QUESTION || b == BRK_EXCLAIM; }

// Legal English syllable onsets (maximal onset principle).
static bool legalOnset(const Seg* c, int n) {
    if (n == 0) return true;
    if (n == 1) return c[0].ph != PH_NG && !isVowel(c[0].ph);
    int a = c[0].ph, b = c[1].ph;
    if (n == 2) {
        if (b == PH_L || b == PH_LX) return a == PH_P || a == PH_B || a == PH_K || a == PH_G || a == PH_F || a == PH_S || a == PH_SH;
        if (b == PH_R) return a == PH_P || a == PH_B || a == PH_T || a == PH_D || a == PH_K || a == PH_G || a == PH_F || a == PH_TH || a == PH_SH;
        if (b == PH_W) return a == PH_T || a == PH_D || a == PH_K || a == PH_G || a == PH_S || a == PH_TH || a == PH_SH;
        if (b == PH_Y) return a == PH_P || a == PH_B || a == PH_K || a == PH_G || a == PH_F || a == PH_V || a == PH_M || a == PH_HH;
        if (a == PH_S) return b == PH_P || b == PH_T || b == PH_K || b == PH_M || b == PH_N || b == PH_F;
        return false;
    }
    if (n == 3) {
        if (a != PH_S) return false;
        int d = c[2].ph;
        if (b == PH_P) return d == PH_L || d == PH_R || d == PH_Y;
        if (b == PH_T) return d == PH_R || d == PH_Y;
        if (b == PH_K) return d == PH_R || d == PH_W || d == PH_L || d == PH_Y;
        return false;
    }
    return false;
}

static float pauseMs(u8 brk, bool last) {
    if (last) return 90.f;
    switch (brk) {
        case BRK_MINOR: return 35.f;
        case BRK_COMMA: return 210.f;
        case BRK_CLAUSE: return 300.f;
        case BRK_DASH: return 280.f;
        case BRK_ELLIPSIS: return 550.f;
        case BRK_PERIOD: return 420.f;
        case BRK_QUESTION: return 450.f;
        case BRK_EXCLAIM: return 380.f;
        default: return 0.f;
    }
}

struct WordBuild {
    TextWord tw;
    WordPron wp;
};

static void splitLongPhrases(std::vector<WordBuild>& ws) {
    size_t b = 0;
    while (b < ws.size()) {
        size_t e = b;
        while (e < ws.size() && !(ws[e].tw.brk >= BRK_MINOR)) e++;
        if (e >= ws.size()) e = ws.size() - 1;
        // phrase = [b, e]
        std::vector<std::pair<size_t, size_t>> stack;
        stack.push_back(std::make_pair(b, e));
        while (!stack.empty()) {
            std::pair<size_t, size_t> r = stack.back();
            stack.pop_back();
            size_t n = r.second - r.first + 1;
            if (n <= 8) continue;
            size_t mid = r.first + n / 2, best = 0;
            long bestDist = 1 << 30;
            for (size_t j = r.first + 3; j + 2 <= r.second; j++) {
                if (!isPhraseSplitWord(ws[j].tw.w)) continue;
                long d = (long)j - (long)mid;
                if (d < 0) d = -d;
                if (d < bestDist) bestDist = d, best = j;
            }
            if (best == 0 && n > 12) {
                // no conjunction: split before a function word near the middle
                for (size_t j = r.first + 4; j + 3 <= r.second; j++) {
                    if (!ws[j].wp.function || ws[j - 1].wp.function) continue;
                    long d = (long)j - (long)mid;
                    if (d < 0) d = -d;
                    if (d < bestDist) bestDist = d, best = j;
                }
            }
            if (best == 0) continue;
            ws[best - 1].tw.brk = BRK_MINOR;
            stack.push_back(std::make_pair(r.first, best - 1));
            stack.push_back(std::make_pair(best, r.second));
        }
        b = e + 1;
    }
}

static void syllabifyWord(Utterance& u, int ws, int we, int& sylCounter) {
    std::vector<int> nuc;
    for (int i = ws; i <= we; i++)
        if (isVowel(u.segs[i].ph)) nuc.push_back(i);
    UWord& w = u.words[u.segs[ws].word];
    w.nSyl = (int)nuc.size();
    if (nuc.empty()) {
        // no vowel (e.g. "hmm"): treat as one unstressed syllable
        for (int i = ws; i <= we; i++) {
            u.segs[i].syl = sylCounter;
            u.segs[i].flags |= SF_WORD_FINAL_SYL;
        }
        sylCounter++;
        return;
    }
    int firstSyl = sylCounter;
    std::vector<int> sylStart(nuc.size()), sylEnd(nuc.size());
    sylStart[0] = ws;
    for (size_t k = 0; k + 1 < nuc.size(); k++) {
        int a = nuc[k] + 1, b = nuc[k + 1];
        int s = a;
        for (; s <= b; s++)
            if (legalOnset(&u.segs[s], b - s)) break;
        sylEnd[k] = s - 1;
        sylStart[k + 1] = s;
    }
    sylEnd[nuc.size() - 1] = we;
    for (size_t k = 0; k < nuc.size(); k++) {
        u8 st = u.segs[nuc[k]].stress;
        for (int i = sylStart[k]; i <= sylEnd[k]; i++) {
            Seg& s = u.segs[i];
            s.syl = firstSyl + (int)k;
            if (!isVowel(s.ph)) {
                s.stress = st;
                s.flags |= i < nuc[k] ? SF_ONSET : SF_CODA;
            }
            if (st >= 1) s.flags |= SF_STRESSED;
            if (st == 1) s.flags |= SF_PRIMARY;
            if (k + 1 == nuc.size()) s.flags |= SF_WORD_FINAL_SYL;
            if (nuc.size() > 1) s.flags |= SF_POLYSYL;
        }
    }
    sylCounter += (int)nuc.size();
}

static bool isStopPh(int ph) { return hasFlag(ph, PF_STOP); }
static bool voicelessObstruent(int ph) {
    return hasFlag(ph, PF_OBSTRUENT) && !hasFlag(ph, PF_VOICED);
}

void buildUtterance(const char* text, const Audio::VoiceParams& voice, Utterance& u) {
    u = Utterance();
    std::vector<TextWord> tws;
    normalizeText(text, tws);
    std::vector<WordBuild> ws;
    ws.reserve(tws.size());
    for (size_t i = 0; i < tws.size(); i++) {
        WordBuild b;
        b.tw = tws[i];
        lookupWord(b.tw, b.wp);
        if (b.wp.ph.empty()) {
            if (!ws.empty() && tws[i].brk > ws.back().tw.brk) ws.back().tw.brk = tws[i].brk;
            continue;
        }
        ws.push_back(b);
    }
    if (ws.empty()) return;
    if (ws.back().tw.brk < BRK_COMMA) ws.back().tw.brk = BRK_PERIOD;
    splitLongPhrases(ws);

    // Sentences: type and wh-question.
    std::vector<u8> sentType(ws.size(), BRK_PERIOD);
    std::vector<bool> whq(ws.size(), false), sentStart(ws.size(), false);
    {
        size_t b = 0;
        while (b < ws.size()) {
            size_t e = b;
            while (e < ws.size() - 1 && !isSentenceBreak(ws[e].tw.brk)) e++;
            u8 t = ws[e].tw.brk;
            if (!isSentenceBreak(t)) t = BRK_PERIOD;
            bool wh = false;
            for (size_t k = b; k <= e && k < b + 2; k++) {
                if (isWhWord(ws[k].tw.w)) {
                    wh = true;
                    break;
                }
                if (!(ws[k].tw.w == "so" || ws[k].tw.w == "and" || ws[k].tw.w == "but" || ws[k].tw.w == "well" ||
                      ws[k].tw.w == "then" || ws[k].tw.w == "okay" || ws[k].tw.w == "hey"))
                    break;
            }
            for (size_t k = b; k <= e; k++) {
                sentType[k] = t;
                whq[k] = wh && t == BRK_QUESTION;
            }
            sentStart[b] = true;
            b = e + 1;
        }
    }

    // Segments.
    const float speed = Clamp(voice.speed, 0.4f, 2.5f);
    Seg lead;
    lead.ph = PH_SIL;
    lead.dur = 0.04f;
    u.segs.push_back(lead);
    UPhrase cur;
    cur.firstSeg = 1;
    bool phraseOpen = false;
    for (size_t wi = 0; wi < ws.size(); wi++) {
        const WordBuild& b = ws[wi];
        if (!phraseOpen) {
            cur = UPhrase();
            cur.firstSeg = (int)u.segs.size();
            cur.sentenceStart = sentStart[wi] || wi == 0;
            cur.sentType = sentType[wi];
            cur.whQuestion = whq[wi];
            cur.shout = b.tw.shout;
            phraseOpen = true;
        }
        UWord uw;
        uw.firstSeg = (int)u.segs.size();
        uw.phrase = (int)u.phrases.size();
        uw.function = b.wp.function;
        uw.emph = b.tw.emph;
        int widx = (int)u.words.size();
        bool isThe = b.tw.w == "the";
        // Glottal onset before a vowel-initial stressed content word following a consonant (clear word
        // boundary: "all [q]units", "is [q]often").
        if (!b.wp.ph.empty() && isVowel(b.wp.ph[0].ph) && u.segs.size() > 1 && u.segs.back().ph != PH_SIL &&
            !isVowel(u.segs.back().ph) && b.wp.ph[0].stress == 1 && !b.wp.function) {
            Seg q;
            q.ph = PH_Q;
            q.stress = b.wp.ph[0].stress;
            q.word = widx;
            q.flags = SF_WORD_START | (b.wp.function ? (u32)SF_FUNCTION : 0u);
            u.segs.push_back(q);
        }
        for (size_t k = 0; k < b.wp.ph.size(); k++) {
            Seg s;
            s.ph = b.wp.ph[k].ph;
            s.stress = b.wp.ph[k].stress;
            s.word = widx;
            if (b.wp.function) s.flags |= SF_FUNCTION;
            if (b.tw.emph) s.flags |= SF_EMPH;
            if (b.tw.shout) s.flags |= SF_SHOUT;
            if (k == 0 && u.segs.back().ph != PH_Q) s.flags |= SF_WORD_START;
            if (k + 1 == b.wp.ph.size()) s.flags |= SF_WORD_END;
            u.segs.push_back(s);
        }
        // "and" before a vowel drops its /d/ in connected speech ("and a" -> [@n@], not "and the")
        if (b.tw.w == "and" && b.tw.brk == BRK_NONE && wi + 1 < ws.size() && !ws[wi + 1].wp.ph.empty() &&
            isVowel(ws[wi + 1].wp.ph[0].ph) && u.segs.back().ph == PH_D) {
            u.segs.pop_back();
            u.segs.back().flags |= SF_WORD_END;
        }
        // "the" before a vowel -> DH IY
        if (isThe && wi + 1 < ws.size() && !ws[wi + 1].wp.ph.empty() && isVowel(ws[wi + 1].wp.ph[0].ph) &&
            b.tw.brk == BRK_NONE) {
            Seg& v = u.segs.back();
            if (isVowel(v.ph)) v.ph = PH_IY;
        }
        uw.lastSeg = (int)u.segs.size() - 1;
        u.words.push_back(uw);
        cur.shout = cur.shout && b.tw.shout;
        u8 brk = b.tw.brk;
        bool last = wi + 1 == ws.size();
        if (brk >= BRK_MINOR || last) {
            cur.lastSeg = (int)u.segs.size() - 1;
            cur.brk = brk == BRK_NONE ? (u8)BRK_PERIOD : brk;
            u.phrases.push_back(cur);
            phraseOpen = false;
            Seg p;
            p.ph = PH_SIL;
            p.dur = pauseMs(brk, last) * 0.001f / (brk == BRK_MINOR ? 1.f : (0.5f + 0.5f * speed));
            if (brk == BRK_MINOR && !last) p.dur = 0.f;  // minor break: lengthening only (no silence)
            u.segs.push_back(p);
        }
    }
    // Remove zero-length pauses (minor breaks) but remember phrase-final marks.
    {
        std::vector<Seg> out;
        std::vector<int> remap(u.segs.size(), -1);
        for (size_t i = 0; i < u.segs.size(); i++) {
            if (u.segs[i].ph == PH_SIL && u.segs[i].dur <= 0.f) continue;
            remap[i] = (int)out.size();
            out.push_back(u.segs[i]);
        }
        auto fix = [&](int idx) {
            while (idx >= 0 && idx < (int)remap.size() && remap[idx] < 0) idx--;
            return idx < 0 ? 0 : remap[idx];
        };
        for (UWord& w : u.words) w.firstSeg = fix(w.firstSeg), w.lastSeg = fix(w.lastSeg);
        for (UPhrase& p : u.phrases) p.firstSeg = fix(p.firstSeg), p.lastSeg = fix(p.lastSeg);
        u.segs.swap(out);
    }

    // Syllabification.
    int sylCounter = 0;
    for (size_t w = 0; w < u.words.size(); w++) syllabifyWord(u, u.words[w].firstSeg, u.words[w].lastSeg, sylCounter);
    std::vector<Seg>& S = u.segs;
    const int N = (int)S.size();

    // Phrase-final syllable flags and utterance/pause adjacency.
    for (UPhrase& p : u.phrases) {
        int lastSyl = S[p.lastSeg].syl;
        for (int i = p.lastSeg; i >= p.firstSeg && S[i].syl == lastSyl; i--) S[i].flags |= SF_PHRASE_FINAL;
    }
    for (int i = 0; i < N; i++) {
        if (S[i].ph == PH_SIL) continue;
        if (i == 0 || S[i - 1].ph == PH_SIL) S[i].flags |= SF_UTT_START;
        if (i + 1 >= N || S[i + 1].ph == PH_SIL) S[i].flags |= SF_PREPAUSE;
    }

    // The article "a" keeps a fuller [^] quality and some length (otherwise it is easily heard as "the").
    for (size_t w = 0; w < u.words.size(); w++) {
        const UWord& uw = u.words[w];
        if (uw.firstSeg == uw.lastSeg && ws.size() > w && ws[w].tw.w == "a" && S[uw.firstSeg].ph == PH_AH)
            S[uw.firstSeg].flags |= SF_ARTICLE_A;
    }
    // Vowel allophones for unstressed vowels.
    for (int i = 0; i < N; i++) {
        Seg& s = S[i];
        if (!isVowel(s.ph) || s.stress != 0 || (s.flags & SF_ARTICLE_A)) continue;
        if (s.ph == PH_AH) s.ph = PH_AX;
        else if (s.ph == PH_ER) s.ph = PH_AXR;
        else if (s.ph == PH_IH && !(s.flags & SF_WORD_START)) s.ph = PH_IX;
    }

    // Flapping (within words only, for clarity): T/D between a vowel (or R) and an unstressed vowel -> DX.
    for (int i = 1; i + 1 < N; i++) {
        Seg& s = S[i];
        if (s.ph != PH_T && s.ph != PH_D) continue;
        if (s.flags & (SF_WORD_START | SF_WORD_END | SF_EMPH)) continue;
        int pv = S[i - 1].ph, nv = S[i + 1].ph;
        if (!(isVowel(pv) || pv == PH_R) || !isVowel(nv)) continue;
        if (S[i - 1].word != s.word || S[i + 1].word != s.word) continue;
        if (S[i + 1].stress != 0) continue;
        s.ph = PH_DX;
    }
    // Dark /l/ in codas; syllabic reductions.
    for (int i = 0; i < N; i++) {
        Seg& s = S[i];
        if (s.ph == PH_L) {
            bool vowelNext = i + 1 < N && isVowel(S[i + 1].ph) && S[i + 1].word == s.word;
            if (!vowelNext) s.ph = PH_LX;
        }
        if (s.ph == PH_AX && i > 0 && i + 1 < N && !isVowel(S[i - 1].ph) && S[i - 1].ph != PH_SIL &&
            S[i - 1].word == s.word) {
            int nx = S[i + 1].ph;
            bool wordEndNext = (S[i + 1].flags & SF_WORD_END) != 0;
            if ((nx == PH_LX && (wordEndNext || !isVowel(i + 2 < N ? (int)S[i + 2].ph : (int)PH_SIL))) ||
                (nx == PH_N && wordEndNext && (S[i - 1].ph == PH_T || S[i - 1].ph == PH_D)))
                s.flags |= SF_SYLLABIC;
        }
    }
    // Identical consonants across a word boundary: keep one, longer (handled in durations).
    std::vector<bool> gem(N, false);
    for (int i = 0; i + 1 < N; i++) {
        if (isVowel(S[i].ph) || S[i].ph == PH_SIL) continue;
        if (S[i].ph == S[i + 1].ph && (S[i].flags & SF_WORD_END) && (S[i + 1].flags & SF_WORD_START) &&
            S[i].ph != PH_HH) {
            gem[i + 1] = true;
            S[i].dur = -1.f;  // mark for removal
        }
    }
    {
        std::vector<Seg> out;
        std::vector<bool> g2;
        std::vector<int> remap(N, -1);
        for (int i = 0; i < N; i++) {
            if (S[i].dur < 0.f) continue;
            remap[i] = (int)out.size();
            out.push_back(S[i]);
            g2.push_back(gem[i]);
        }
        auto fixFwd = [&](int idx) {
            while (idx < N && remap[idx] < 0) idx++;
            return idx >= N ? (int)out.size() - 1 : remap[idx];
        };
        auto fixBack = [&](int idx) {
            while (idx >= 0 && remap[idx] < 0) idx--;
            return idx < 0 ? 0 : remap[idx];
        };
        for (UWord& w : u.words) w.firstSeg = fixFwd(w.firstSeg), w.lastSeg = std::max(w.firstSeg, fixBack(w.lastSeg));
        for (UPhrase& p : u.phrases) p.firstSeg = fixFwd(p.firstSeg), p.lastSeg = fixBack(p.lastSeg);
        u.segs.swap(out);
        gem.swap(g2);
    }
    const int M = (int)S.size();

    // Stop release classification and aspiration.
    for (int i = 0; i < M; i++) {
        Seg& s = S[i];
        if (!isStopPh(s.ph) && s.ph != PH_CH && s.ph != PH_JH) continue;
        int nx = i + 1 < M ? (int)S[i + 1].ph : (int)PH_SIL;
        if (isStopPh(s.ph) && (isStopPh(nx) || nx == PH_CH || nx == PH_JH)) s.flags |= SF_UNRELEASED;
        if ((s.ph == PH_P || s.ph == PH_T || s.ph == PH_K) && (s.flags & SF_ONSET) &&
            (isVowel(nx) || hasFlag(nx, PF_LIQUID) || hasFlag(nx, PF_GLIDE))) {
            bool afterS = i > 0 && S[i - 1].ph == PH_S && S[i - 1].syl == s.syl;
            if (!afterS) s.flags |= SF_ASPIRATED;
        }
    }

    // Durations (Klatt rules).
    for (int i = 0; i < M; i++) {
        Seg& s = S[i];
        if (s.ph == PH_SIL) continue;
        const PhInfo& pi = phInfo(s.ph);
        bool vowel = isVowel(s.ph);
        float inh = pi.inh, mn = pi.mn, pr = 1.f;
        int prev = i > 0 ? (int)S[i - 1].ph : (int)PH_SIL, next = i + 1 < M ? (int)S[i + 1].ph : (int)PH_SIL;
        bool phraseFinal = (s.flags & SF_PHRASE_FINAL) != 0;
        bool prePause = phraseFinal && (i + 1 >= M || [&]() {
                            for (int k = i + 1; k < M; k++) {
                                if (S[k].ph == PH_SIL) return true;
                                if (S[k].syl != s.syl) return false;
                            }
                            return true;
                        }());
        if (vowel) {
            if (prePause) pr *= 1.4f;       // clause-final lengthening
            else if (!phraseFinal) pr *= 0.6f;  // non-phrase-final shortening
            if (!(s.flags & SF_WORD_FINAL_SYL)) pr *= 0.85f;
            if (s.flags & SF_POLYSYL) pr *= 0.8f;
            if (s.stress == 0) {
                mn *= 0.5f;
                pr *= 0.7f;
            } else if (s.stress == 2) {
                pr *= 0.9f;
            }
            // postvocalic context within the word
            float f = 1.f;
            if (i + 1 < M && S[i + 1].word == s.word && !isVowel(next)) {
                if (hasFlag(next, PF_FRIC) && hasFlag(next, PF_VOICED)) f = 1.6f;
                else if (isStopPh(next) && hasFlag(next, PF_VOICED)) f = 1.2f;
                else if (hasFlag(next, PF_NASAL)) f = 0.85f;
                else if (voicelessObstruent(next)) f = 0.7f;
            } else if (s.flags & SF_WORD_END) {
                f = 1.1f;
            }
            pr *= prePause ? f : 1.f + (f - 1.f) * 0.75f;
            if (isVowel(next)) pr *= 1.2f;
            if (s.flags & SF_SYLLABIC) pr *= 0.45f;
            if (s.flags & SF_SHOUT) pr *= 1.1f;
        } else {
            if (!(s.flags & SF_WORD_START)) pr *= 0.85f;
            if (s.stress == 0) pr *= 0.8f;
            bool pc = !isVowel(prev) && prev != PH_SIL, nc = !isVowel(next) && next != PH_SIL;
            // nasal before another nasal and sibilant before a stop keep more of their length (clear cues)
            float cl = 0.7f;
            if (nc && hasFlag(s.ph, PF_NASAL) && hasFlag(next, PF_NASAL)) cl = 1.f;
            else if (nc && hasFlag(s.ph, PF_SIBILANT) && hasFlag(s.ph, PF_FRIC) && hasFlag(next, PF_STOP)) cl = 0.85f;
            if (pc && nc) pr *= 0.5f * cl / 0.7f;
            else if (nc) pr *= cl;
            else if (pc) pr *= 0.7f;
            if (prePause && (s.flags & SF_CODA)) pr *= 1.4f;
            if (s.ph == PH_DX) pr = 1.f;
            if (s.ph == PH_HH && isVowel(prev)) pr *= 0.7f;  // intervocalic /h/ is short
        }
        if (s.flags & SF_EMPH) pr *= 1.35f;
        if (s.flags & SF_FUNCTION) pr *= 0.85f;
        if (s.flags & SF_UNRELEASED) pr *= 0.6f;
        if ((s.ph == PH_CH || s.ph == PH_JH) && i > 0 && (S[i - 1].flags & SF_UNRELEASED)) pr *= 0.6f;
        if (vowel && (s.flags & SF_UTT_START) && (s.flags & SF_WORD_END) && (s.flags & SF_FUNCTION)) pr *= 1.6f;
        if (s.flags & SF_ARTICLE_A) pr *= 1.35f;
        float d = mn + (inh - mn) * pr;
        if (s.flags & SF_UNRELEASED) d *= 0.8f;
        if (gem[i]) d *= 1.5f;
        float floor = vowel ? ((s.flags & SF_SYLLABIC) ? 22.f : 38.f) : (isStopPh(s.ph) ? 40.f : 25.f);
        if (s.flags & SF_UNRELEASED) floor = 30.f;
        if (s.ph == PH_Q) floor = 22.f;
        if (s.ph == PH_DX) floor = 16.f;
        d = std::max(d, floor);
        s.dur = d * 0.001f / speed;
    }

    // Voice onset time: voiceless (aspiration) portion at the start of the segment after a released stop.
    for (int i = 1; i < M; i++) {
        Seg& s = S[i];
        const Seg& p = S[i - 1];
        if (!isStopPh(p.ph) || (p.flags & SF_UNRELEASED)) continue;
        bool onsetSonorant = hasFlag(s.ph, PF_LIQUID) || hasFlag(s.ph, PF_GLIDE);
        if (!(isVowel(s.ph) || (onsetSonorant && (p.flags & SF_ONSET)))) continue;
        const PhInfo& pi = phInfo(p.ph);
        float vot;
        bool voicedBefore = i >= 2 && (isVowel(S[i - 2].ph) || hasFlag(S[i - 2].ph, PF_SONOR));
        if (hasFlag(p.ph, PF_VOICED)) vot = voicedBefore ? 0.f : (onsetSonorant ? 6.f : pi.vot * 0.5f);
        else if (p.flags & SF_ASPIRATED) vot = pi.vot * (p.stress == 1 ? 1.f : p.stress == 2 ? 0.85f : 0.6f);
        else vot = 15.f;
        vot = vot * 0.001f / std::sqrt(speed);
        if (vot <= 0.f) continue;
        if (isVowel(s.ph) && (p.flags & SF_ASPIRATED)) s.dur += 0.55f * vot;
        s.vot = std::min(vot, 0.65f * s.dur);
    }

    // Timing.
    float t = 0.f;
    for (int i = 0; i < M; i++) {
        S[i].t0 = t;
        t += S[i].dur;
    }
    u.total = t;

    // Pitch accents: primary-stressed vowel of content words (emphasis overrides).
    for (size_t p = 0; p < u.phrases.size(); p++) {
        UPhrase& ph = u.phrases[p];
        int count = 0;
        bool anyEmph = false;
        for (int i = ph.firstSeg; i <= ph.lastSeg && i < M; i++)
            if (S[i].flags & SF_EMPH) anyEmph = true;
        int lastCandidate = -1;
        for (int i = ph.firstSeg; i <= ph.lastSeg && i < M; i++) {
            Seg& s = S[i];
            if (!isVowel(s.ph)) continue;
            if (s.stress == 1) lastCandidate = i;
            if (s.stress != 1 || (s.flags & SF_FUNCTION)) continue;
            float a = count == 0 ? 1.f : std::max(0.55f, 1.f - 0.12f * (float)count);
            if (anyEmph) a *= (s.flags & SF_EMPH) ? 1.6f : 0.6f;
            s.flags |= SF_ACCENT;
            s.accent = a;
            count++;
        }
        if (count == 0 && lastCandidate >= 0) {
            S[lastCandidate].flags |= SF_ACCENT;
            S[lastCandidate].accent = 0.7f;
        } else if (count == 0) {
            for (int i = ph.lastSeg; i >= ph.firstSeg; i--)
                if (isVowel(S[i].ph)) {
                    S[i].flags |= SF_ACCENT;
                    S[i].accent = 0.6f;
                    break;
                }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// F0 contour

void buildF0(const Utterance& u, const Audio::VoiceParams& voice, std::vector<F0Point>& out) {
    out.clear();
    const std::vector<Seg>& S = u.segs;
    const float expr = Clamp(voice.expressiveness, 0.f, 3.f);
    const float R = 4.5f * expr;  // accent range (semitones)
    for (size_t pi = 0; pi < u.phrases.size(); pi++) {
        const UPhrase& ph = u.phrases[pi];
        if (ph.firstSeg >= (int)S.size()) continue;
        float t0 = S[ph.firstSeg].t0;
        float t1 = S[ph.lastSeg].t0 + S[ph.lastSeg].dur;
        float dur = std::max(0.05f, t1 - t0);
        float top = ph.sentenceStart ? 1.0f : 0.4f;
        if (ph.shout) top += 3.f;
        if (ph.sentType == BRK_EXCLAIM) top += 1.f;
        float decl = std::min(2.8f, 1.1f * dur + 0.4f);
        float bEnd = top - decl;
        auto base = [&](float t) { return top + (bEnd - top) * Clamp((t - t0) / dur, 0.f, 1.f); };
        float rangeMul = 1.f;
        if (ph.sentType == BRK_EXCLAIM) rangeMul = 1.35f;
        if (ph.shout) rangeMul *= 1.2f;
        bool question = ph.brk == BRK_QUESTION && !ph.whQuestion;
        bool whq = ph.brk == BRK_QUESTION && ph.whQuestion;
        bool statementEnd = ph.brk == BRK_PERIOD || ph.brk == BRK_EXCLAIM || ph.brk == BRK_CLAUSE || whq;
        bool continuation = ph.brk == BRK_COMMA || ph.brk == BRK_MINOR || ph.brk == BRK_DASH;

        // collect accents
        std::vector<int> acc;
        for (int i = ph.firstSeg; i <= ph.lastSeg; i++)
            if (S[i].flags & SF_ACCENT) acc.push_back(i);
        out.push_back(F0Point{t0, base(t0) - 0.6f});
        for (size_t k = 0; k < acc.size(); k++) {
            const Seg& v = S[acc[k]];
            bool nuclear = k + 1 == acc.size();
            float h = R * rangeMul * v.accent;
            // syllable onset start (first segment of the vowel's syllable)
            int so = acc[k];
            while (so > ph.firstSeg && S[so - 1].syl == v.syl && S[so - 1].ph != PH_SIL) so--;
            float ts = S[so].t0, tv0 = v.t0 + v.vot, tvd = std::max(0.02f, v.dur - v.vot);
            if (!nuclear || continuation) {
                out.push_back(F0Point{ts, base(ts) + 0.1f * h});
                float peakT = tv0 + (nuclear ? 0.5f : 0.7f) * tvd;
                float hh = nuclear ? 0.85f * h : h;
                out.push_back(F0Point{peakT, base(peakT) + hh});
                if (!nuclear) {
                    float nextT = k + 1 < acc.size() ? S[acc[k + 1]].t0 : t1;
                    float dropT = std::min(v.t0 + v.dur + 0.1f, std::max(peakT + 0.03f, nextT - 0.05f));
                    out.push_back(F0Point{dropT, base(dropT) + 0.3f * hh});
                } else {
                    // continuation: fall then rise (L-H%) or plain rise for minor breaks
                    float dipT = peakT + 0.55f * (t1 - peakT);
                    if (dipT > peakT + 0.03f) out.push_back(F0Point{dipT, base(dipT) - 0.3f});
                    float endLift = ph.brk == BRK_MINOR ? 0.25f * R : 0.45f * R + 0.8f;
                    out.push_back(F0Point{t1, base(t1) + endLift});
                }
            } else if (question) {
                // L* H-H%: low nuclear syllable then steep final rise
                out.push_back(F0Point{ts, base(ts)});
                out.push_back(F0Point{tv0 + 0.4f * tvd, base(tv0) - 1.0f});
                float riseStart = std::min(t1 - 0.03f, tv0 + 0.6f * tvd);
                out.push_back(F0Point{riseStart, base(riseStart) - 0.6f});
                out.push_back(F0Point{t1, base(t1) + std::max(6.f, 1.9f * R)});
            } else {
                // H* L-L%: peak then fall to the bottom of the range
                float hh = (whq ? 1.0f : 0.9f) * h;
                out.push_back(F0Point{ts, base(ts) + 0.15f * hh});
                float peakT = tv0 + 0.35f * tvd;
                out.push_back(F0Point{peakT, base(peakT) + hh});
                float fallT = std::min(t1 - 0.01f, v.t0 + v.dur + 0.06f);
                if (fallT > peakT + 0.02f) out.push_back(F0Point{fallT, base(fallT) - 0.8f});
                float endLow = ph.brk == BRK_EXCLAIM ? -3.2f : -2.4f;
                if (ph.brk == BRK_ELLIPSIS) endLow = -1.2f;
                if (!statementEnd && ph.brk != BRK_ELLIPSIS) endLow = -1.5f;
                out.push_back(F0Point{t1, bEnd + endLow * std::max(0.6f, std::min(1.3f, expr))});
            }
        }
        if (acc.empty()) out.push_back(F0Point{t1, question ? base(t1) + 4.f : bEnd - 1.5f});
    }
    // sort and enforce increasing times
    std::stable_sort(out.begin(), out.end(), [](const F0Point& a, const F0Point& b) { return a.t < b.t; });
    std::vector<F0Point> clean;
    for (const F0Point& p : out) {
        if (!clean.empty() && p.t < clean.back().t + 0.008f) {
            clean.back().st = 0.5f * (clean.back().st + p.st);
            continue;
        }
        clean.push_back(p);
    }
    out.swap(clean);
}

std::string debugPhonemes(const char* text) {
    std::vector<TextWord> tws;
    normalizeText(text, tws);
    std::string s;
    for (size_t i = 0; i < tws.size(); i++) {
        WordPron wp;
        lookupWord(tws[i], wp);
        if (!s.empty()) s += " | ";
        s += tws[i].w;
        if (tws[i].spell) s += "(sp)";
        if (tws[i].emph) s += "(!)";
        s += wp.function ? " ~" : " ";
        for (size_t k = 0; k < wp.ph.size(); k++) {
            s += phInfo(wp.ph[k].ph).name;
            if (isVowel(wp.ph[k].ph)) s += (char)('0' + wp.ph[k].stress);
            if (k + 1 < wp.ph.size()) s += ' ';
        }
        switch (tws[i].brk) {
            case BRK_MINOR: s += " /"; break;
            case BRK_COMMA: s += " ,"; break;
            case BRK_CLAUSE: s += " ;"; break;
            case BRK_DASH: s += " -"; break;
            case BRK_ELLIPSIS: s += " ..."; break;
            case BRK_PERIOD: s += " ."; break;
            case BRK_QUESTION: s += " ?"; break;
            case BRK_EXCLAIM: s += " !"; break;
            default: break;
        }
    }
    return s;
}

}  // namespace detail
}  // namespace Speech
