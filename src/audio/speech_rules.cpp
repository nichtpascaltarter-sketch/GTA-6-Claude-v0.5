// Pronunciation of words: dictionary lookup, apostrophe forms, morphological decomposition
// (suffixes, prefixes, compounds) and context-sensitive letter-to-sound rules with a stress assigner.
//
// Letter-to-sound rule format: left[match]right -> phonemes. Context symbols:
//   ' ' word boundary      '#' one or more vowels (AEIOUY)   '*' one or more consonants
//   ':' zero or more consonants   '^' one consonant   '.' one voiced consonant (BDVGJLMNRWZ)
//   '+' one front vowel (EIY)     '&' sibilant (S C G Z X J CH SH)
//   '@' T S R D L Z N J TH CH SH (consonants after which "u" is /u/ rather than /ju/)
//   '%' (right only) suffix E ER ES ED ING ELY EST ERS followed by the word end
// Vowels without a stress digit are stress candidates; the stress assigner decides afterwards.
#include "speech_internal.h"

namespace Speech {
namespace detail {

static const u8 kStressUnknown = 9;

struct LtsRule {
    const char* left;
    const char* match;
    const char* right;
    const char* out;
};

// clang-format off
static const LtsRule kRulesA[] = {
    {" ", "A", " ", "AH0"},
    {"", "ACHE", "", "EY K"},
    {"", "AGAIN", "", "AH0 G EH N"},
    {"", "AUGH", "T", "AO"},
    {"", "AUGH", "", "AE F"},
    {"", "AU", "", "AO"},
    {"", "AW", "", "AO"},
    {" ", "ANY", "", "EH N IY0"},
    {"M", "ANY", "", "EH N IY0"},
    {"", "AI", "R", "EH"},
    {"", "AI", "", "EY"},
    {"", "AY", "", "EY"},
    {"#:", "ALLY", " ", "AH0 L IY0"},
    {"", "ALK", "", "AO K"},
    {"", "ALL", "", "AO L"},
    {"", "AL", "T", "AO L"},
    {"", "ALM", "", "AA M"},
    {"", "ALF", "", "AE F"},
    {" ", "AL", "#", "AH0 L"},
    {"#:", "AL", " ", "AH0 L"},
    {"#:", "ALS", " ", "AH0 L Z"},
    {" :", "ABLE", "", "EY B AH0 L"},
    {"", "ABLE", "", "AH0 B AH0 L"},
    {"", "ABLY", "", "AH0 B L IY0"},
    {"", "ATION", "", "EY SH AH0 N"},
    {"", "ANGE", "", "EY N JH"},
    {" ", "ARR", "", "AH0 R"},
    {"", "ARR", "", "AE R"},
    {"W", "AR", "", "AO R"},
    {"QU", "AR", "", "AO R"},
    {"#:", "ARD", " ", "ER0 D"},
    {"#:", "AR", " ", "ER0"},
    {"#:", "ARS", " ", "ER0 Z"},
    {"", "AR", "#", "EH R"},
    {"", "AR", "", "AA R"},
    {"#:", "AGE", " ", "IH0 JH"},
    {"#:", "AGES", " ", "IH0 JH IH0 Z"},
    {"#:", "ANCE", " ", "AH0 N S"},
    {"#:", "ANT", " ", "AH0 N T"},
    {"#:", "ANTS", " ", "AH0 N T S"},
    {"#:", "AN", " ", "AH0 N"},
    {"#:", "ANS", " ", "AH0 N Z"},
    {"W", "A", "T", "AA"},
    {"W", "A", "SH", "AA"},
    {"W", "A", "N", "AA"},
    {"W", "A", "D", "AA"},
    {"W", "A", "S", "AA"},
    {"QU", "A", "", "AA"},
    {"", "A", "^%", "EY"},
    {"", "A", "^+#", "EY"},
    {" ", "A", "^#", "AH0"},
    {"#:", "A", " ", "AH0"},
    {"", "A", " ", "AA"},
    {"", "A", "", "AE"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesB[] = {
    {" ", "BE", "^#", "B IH0"},
    {"", "BEING", "", "B IY IH0 NG"},
    {" ", "BOTH", " ", "B OW TH"},
    {" ", "BUS", "#", "B IH Z"},
    {"", "BUIL", "", "B IH L"},
    {"M", "B", " ", ""},
    {"M", "B", "S ", ""},
    {"M", "B", "ED ", ""},
    {"M", "B", "ING", ""},
    {"", "B", "T ", ""},
    {"", "B", "TS ", ""},
    {"", "B", "TLE", ""},
    {"", "BB", "", "B"},
    {"", "B", "", "B"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesC[] = {
    {" ", "CH", "^", "K"},
    {"", "CHR", "", "K R"},
    {"", "CH", "", "CH"},
    {"S", "CI", "#", "S AY"},
    {"", "CI", "A", "SH"},
    {"", "CI", "O", "SH"},
    {"", "CI", "EN", "SH"},
    {"", "C", "+", "S"},
    {"", "CK", "", "K"},
    {"", "CC", "+", "K S"},
    {"", "CC", "", "K"},
    {"", "C", "", "K"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesD[] = {
    {"#:", "DED", " ", "D IH0 D"},
    {" ", "DE", "^#", "D IH0"},
    {" ", "DO", " ", "D UW"},
    {" ", "DOES", "", "D AH Z"},
    {" ", "DOW", "", "D AW"},
    {"", "DU", "A", "JH UW"},
    {"", "DGE", "", "JH"},
    {"", "DD", "", "D"},
    {"", "D", "", "D"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesE[] = {
    {"#:", "E", " ", ""},
    {" :", "E", " ", "IY"},
    {"#:T", "ED", " ", "IH0 D"},
    {"#:D", "ED", " ", "IH0 D"},
    {"#:P", "ED", " ", "T"},
    {"#:K", "ED", " ", "T"},
    {"#:F", "ED", " ", "T"},
    {"#:S", "ED", " ", "T"},
    {"#:C", "ED", " ", "T"},
    {"#:X", "ED", " ", "T"},
    {"#:H", "ED", " ", "T"},
    {"#:", "ED", " ", "D"},
    {"#:&", "ES", " ", "IH0 Z"},
    {"#:P", "ES", " ", "S"},
    {"#:T", "ES", " ", "S"},
    {"#:K", "ES", " ", "S"},
    {"#:F", "ES", " ", "S"},
    {"#:", "ES", " ", "Z"},
    {"#:^", "EN", " ", "AH0 N"},
    {"#:^", "EL", " ", "AH0 L"},
    {"I", "ENCE", "", "AH0 N S"},
    {"I", "ENT", "", "AH0 N T"},
    {"#:^", "ENT", " ", "AH0 N T"},
    {"#:^", "ENTS", " ", "AH0 N T S"},
    {"#:^", "ENCE", " ", "AH0 N S"},
    {" ", "EX", "#", "IH0 G Z"},
    {" ", "E", "^^", "EH"},
    {"", "EAR", "^", "ER"},
    {"", "EAR", "", "IH R"},
    {"", "EA", "D", "EH"},
    {"", "EA", "TH", "EH"},
    {"", "EA", "LTH", "EH"},
    {"", "EA", "SUR", "EH"},
    {"", "EA", "", "IY"},
    {"", "EER", "", "IH R"},
    {"", "EE", "", "IY"},
    {"C", "EI", "", "IY"},
    {"", "EIGH", "", "EY"},
    {"", "EI", "", "EY"},
    {"#:", "EY", " ", "IY0"},
    {"", "EY", "", "EY"},
    {"@", "EW", "", "UW"},
    {"", "EW", "", "Y UW"},
    {"@", "EU", "", "UW"},
    {"", "EU", "", "Y UW"},
    {"", "ERE", " ", "IH R"},
    {"", "ER", "#", "EH R"},
    {"", "ER", "", "ER"},
    {"", "E", "^E ", "IY"},
    {"", "E", "^ES ", "IY"},
    {"", "E", "^ED ", "IY"},
    {"", "E", "^ING", "IY"},
    {"", "E", "", "EH"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesF[] = {
    {"", "FULL", "", "F UH L"},
    {"#:", "FUL", "", "F AH0 L"},
    {"", "FF", "", "F"},
    {"", "F", "", "F"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesG[] = {
    {"", "GIV", "", "G IH V"},
    {" ", "G", "I^", "G"},
    {"", "GE", "T", "G EH"},
    {"SU", "GGES", "", "G JH EH S"},
    {"", "GG", "", "G"},
    {" ", "GU", "#", "G"},
    {"", "G", "+", "JH"},
    {" ", "GH", "", "G"},
    {"OU", "GH", " ", "F"},
    {"", "GH", "", ""},
    {" ", "G", "N", ""},
    {"", "G", "N ", ""},
    {"", "G", "NS ", ""},
    {"", "G", "NED ", ""},
    {"", "G", "", "G"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesH[] = {
    {" ", "HAV", "", "HH AE V"},
    {" ", "HERE", "", "HH IY R"},
    {" ", "HOUR", "", "AW ER0"},
    {" ", "HON", "EST", "AA N"},
    {" ", "HON", "OR", "AA N"},
    {"", "HOW", "", "HH AW"},
    {"#", "H", " ", ""},
    {"", "H", "#", "HH"},
    {"", "H", "", ""},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesI[] = {
    {" ", "IN", "", "IH N"},
    {" ", "I", " ", "AY"},
    {"", "IGH", "", "AY"},
    {"", "I", "GN ", "AY"},
    {"", "I", "GNS ", "AY"},
    {"", "I", "GNED ", "AY"},
    {"", "I", "ND ", "AY"},
    {"", "I", "NDS ", "AY"},
    {"", "I", "LD", "AY"},
    {"", "IEN", "", "AY AH0 N"},
    {"", "IE", "T", "AY AH0"},
    {"#:^", "IES", " ", "IY0 Z"},
    {"", "IES", " ", "AY Z"},
    {"#:^", "IED", " ", "IY0 D"},
    {"", "IED", " ", "AY D"},
    {"#:^", "IE", " ", "IY0"},
    {"", "IE", " ", "AY"},
    {"", "IER", "", "IY0 ER0"},
    {"", "IEF", "", "IY F"},
    {"", "IEV", "", "IY V"},
    {"", "IEL", "", "IY L"},
    {"", "IECE", "", "IY S"},
    {"", "IE", "", "IY"},
    {"", "IQUE", "", "IY K"},
    {"", "IRE", "", "AY ER0"},
    {"", "IR", "#", "AY R"},
    {"", "IR", "", "ER"},
    {"", "IOUS", "", "IY0 AH0 S"},
    {"L", "ION", "", "Y AH0 N"},
    {"N", "ION", "", "Y AH0 N"},
    {"", "ION", "", "IY0 AH0 N"},
    {"", "IAN", "", "IY0 AH0 N"},
    {"", "IAL", "", "IY0 AH0 L"},
    {"", "IA", "", "IY0 AH0"},
    {"", "IO", "", "IY0 OW"},
    {"#:", "ITY", " ", "IH0 T IY0"},
    {"#:^", "IVE", " ", "IH0 V"},
    {"#:", "ING", "", "IH0 NG"},
    {"", "IZE", "", "AY Z"},
    {"", "ISE", " ", "AY Z"},
    {"#:", "ISM", " ", "IH0 Z AH0 M"},
    {"#:", "IST", " ", "IH0 S T"},
    {"#:", "ISH", " ", "IH0 SH"},
    {"", "I", "^%", "AY"},
    {"#:^", "I", " ", "IY0"},
    {"", "I", " ", "AY"},
    {"", "I", "", "IH"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesJ[] = {
    {"", "J", "", "JH"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesK[] = {
    {" ", "K", "N", ""},
    {"", "K", "", "K"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesL[] = {
    {"", "LO", "C#", "L OW"},
    {"L", "L", "", ""},
    {"#:^", "LE", " ", "AH0 L"},
    {"#:^", "LES", " ", "AH0 L Z"},
    {"#:^", "LED", " ", "AH0 L D"},
    {"#:", "LESS", " ", "L AH0 S"},
    {"#:", "LY", " ", "L IY0"},
    {"", "L", "", "L"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesM[] = {
    {"", "MM", "", "M"},
    {"#:", "MENT", "", "M AH0 N T"},
    {"", "M", "", "M"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesN[] = {
    {"E", "NG", "+", "N JH"},
    {"", "NG", "R", "NG G"},
    {"", "NG", "#", "NG G"},
    {"", "NG", "L", "NG G"},
    {"", "NG", "", "NG"},
    {"", "NK", "", "NG K"},
    {"#:", "NESS", " ", "N AH0 S"},
    {"", "NN", "", "N"},
    {"", "N", "", "N"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesO[] = {
    {" ", "OF", " ", "AH V"},
    {"", "OUGHT", "", "AO T"},
    {"", "OUGH", " ", "AH F"},
    {"", "OUGH", "", "OW"},
    {"", "OULD", "", "UH D"},
    {"", "OUBLE", "", "AH B AH0 L"},
    {"", "OUNG", "", "AH NG"},
    {"", "OUP", "", "UW P"},
    {"", "OUR", "", "AO R"},
    {"", "OUS", "", "AH0 S"},
    {"", "OU", "", "AW"},
    {"", "OWN", " ", "AW N"},
    {"", "OWD", "", "AW D"},
    {"", "OWER", "", "AW ER0"},
    {"", "OWL", "", "AW L"},
    {"", "OW", "", "OW"},
    {"", "OY", "", "OY"},
    {"", "OI", "", "OY"},
    {"", "OOR", "", "AO R"},
    {"", "OOK", "", "UH K"},
    {"", "OOD", "", "UH D"},
    {"", "OO", "", "UW"},
    {"", "OA", "", "OW"},
    {"", "OE", " ", "OW"},
    {"W", "OR", "^", "ER"},
    {"#:", "OR", " ", "ER0"},
    {"#:", "ORS", " ", "ER0 Z"},
    {"", "OR", "", "AO R"},
    {"", "OLD", "", "OW L D"},
    {"", "OLT", "", "OW L T"},
    {"", "OST", " ", "OW S T"},
    {"#:^", "ON", " ", "AH0 N"},
    {"#:^", "ONS", " ", "AH0 N Z"},
    {"#:", "OM", " ", "AH0 M"},
    {"", "O", "^%", "OW"},
    {"", "O", " ", "OW"},
    {"", "O", "", "AA"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesP[] = {
    {"", "PH", "", "F"},
    {"", "PP", "", "P"},
    {" ", "PS", "", "S"},
    {" ", "PN", "", "N"},
    {"", "P", "", "P"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesQ[] = {
    {"", "QUE", " ", "K"},
    {"", "QU", "", "K W"},
    {"", "Q", "", "K"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesR[] = {
    {" ", "RE", "^#", "R IH0"},
    {"", "RR", "", "R"},
    {"", "R", "", "R"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesS[] = {
    {"", "SH", "", "SH"},
    {"", "SSION", "", "SH AH0 N"},
    {"#", "SION", "", "ZH AH0 N"},
    {"", "SION", "", "SH AH0 N"},
    {"", "SSURE", "", "SH ER0"},
    {"#", "SURE", "", "ZH ER0"},
    {"", "SURE", "", "SH UH R"},
    {"#", "SUAL", "", "ZH UW0 AH0 L"},
    {"#", "SIA", "", "ZH AH0"},
    {"", "SCH", "", "S K"},
    {"", "SC", "+", "S"},
    {"#", "S", "#", "Z"},
    {"", "SS", "", "S"},
    {".", "S", " ", "Z"},
    {"#:^A", "S", " ", "Z"},
    {"#:^O", "S", " ", "Z"},
    {"#:Y", "S", " ", "Z"},
    {"", "S", "", "S"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesT[] = {
    {"#", "THER", "", "DH ER0"},
    {"", "TH", "E ", "DH"},
    {"", "TH", "", "TH"},
    {"S", "TION", "", "CH AH0 N"},
    {"", "TION", "", "SH AH0 N"},
    {"", "TIAL", "", "SH AH0 L"},
    {"", "TIAN", "", "SH AH0 N"},
    {"", "TIENT", "", "SH AH0 N T"},
    {"", "TIOUS", "", "SH AH0 S"},
    {"", "TURE", "", "CH ER0"},
    {"", "TUAL", "", "CH UW0 AH0 L"},
    {"S", "T", "LE ", ""},
    {"S", "T", "EN ", ""},
    {"", "TT", "", "T"},
    {"", "T", "", "T"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesU[] = {
    {" ", "UN", "", "AH N"},
    {" ", "UP", "", "AH P"},
    {"", "URE", " ", "Y UH R"},
    {"@", "UR", "#", "UH R"},
    {"", "UR", "#", "Y UH R"},
    {"", "UR", "", "ER"},
    {"", "UY", "", "AY"},
    {"", "U", "^^", "AH"},
    {"", "U", "^ ", "AH"},
    {"@", "U", "^%", "UW"},
    {"", "U", "^%", "Y UW"},
    {" ", "U", "^#", "Y UW"},
    {"@", "U", "", "UW"},
    {"", "U", "", "Y UW"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesV[] = {
    {"", "V", "", "V"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesW[] = {
    {" ", "WERE", "", "W ER"},
    {"", "WH", "", "W"},
    {"", "WR", "", "R"},
    {"", "W", "", "W"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesX[] = {
    {" ", "X", "", "Z"},
    {"", "X", "", "K S"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesY[] = {
    {" ", "YOU", "", "Y UW"},
    {" ", "YES", "", "Y EH S"},
    {" ", "Y", "#", "Y"},
    {"#:^", "Y", "ING", "IY0"},
    {"", "Y", "ING", "AY"},
    {"#:^", "Y", " ", "IY0"},
    {"", "Y", " ", "AY"},
    {"", "Y", "^%", "AY"},
    {"", "Y", "#", "Y"},
    {"", "Y", "", "IH"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesZ[] = {
    {"", "ZZ", "", "Z"},
    {"", "Z", "", "Z"},
    {nullptr, nullptr, nullptr, nullptr}};
// clang-format on

static const LtsRule* const kRuleTable[26] = {kRulesA, kRulesB, kRulesC, kRulesD, kRulesE, kRulesF, kRulesG,
                                              kRulesH, kRulesI, kRulesJ, kRulesK, kRulesL, kRulesM, kRulesN,
                                              kRulesO, kRulesP, kRulesQ, kRulesR, kRulesS, kRulesT, kRulesU,
                                              kRulesV, kRulesW, kRulesX, kRulesY, kRulesZ};

static inline bool ltsLetter(char c) { return c >= 'A' && c <= 'Z'; }
static inline bool ltsVowel(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y'; }
static inline bool ltsCons(char c) { return ltsLetter(c) && c != 'A' && c != 'E' && c != 'I' && c != 'O' && c != 'U'; }
static inline bool ltsVoiced(char c) {
    return c == 'B' || c == 'D' || c == 'V' || c == 'G' || c == 'J' || c == 'L' || c == 'M' || c == 'N' || c == 'R' ||
           c == 'W' || c == 'Z';
}
static inline bool ltsFront(char c) { return c == 'E' || c == 'I' || c == 'Y'; }

// Left context is matched right-to-left, ending just before the match.
static bool ltsMatchLeft(const char* buf, int p, const char* ctx) {
    for (int k = (int)strlen(ctx) - 1; k >= 0; k--) {
        char c = ctx[k];
        char b = p >= 0 ? buf[p] : ' ';
        switch (c) {
            case ' ':
                if (ltsLetter(b)) return false;
                p--;
                break;
            case '#':
                if (!ltsVowel(b)) return false;
                while (p >= 0 && ltsVowel(buf[p])) p--;
                break;
            case ':':
                while (p >= 0 && ltsCons(buf[p])) p--;
                break;
            case '*':
                if (!ltsCons(b)) return false;
                while (p >= 0 && ltsCons(buf[p])) p--;
                break;
            case '^':
                if (!ltsCons(b)) return false;
                p--;
                break;
            case '.':
                if (!ltsVoiced(b)) return false;
                p--;
                break;
            case '+':
                if (!ltsFront(b)) return false;
                p--;
                break;
            case '&':
                if (p >= 1 && b == 'H' && (buf[p - 1] == 'C' || buf[p - 1] == 'S')) p -= 2;
                else if (b == 'S' || b == 'C' || b == 'G' || b == 'Z' || b == 'X' || b == 'J') p--;
                else return false;
                break;
            case '@':
                if (p >= 1 && b == 'H' && (buf[p - 1] == 'T' || buf[p - 1] == 'C' || buf[p - 1] == 'S')) p -= 2;
                else if (b == 'T' || b == 'S' || b == 'R' || b == 'D' || b == 'L' || b == 'Z' || b == 'N' || b == 'J') p--;
                else return false;
                break;
            default:
                if (b != c) return false;
                p--;
                break;
        }
    }
    return true;
}

static bool ltsMatchRight(const char* buf, int p, const char* ctx) {
    for (int k = 0; ctx[k]; k++) {
        char c = ctx[k];
        char b = buf[p];
        switch (c) {
            case ' ':
                if (ltsLetter(b)) return false;
                if (b) p++;
                break;
            case '#':
                if (!ltsVowel(b)) return false;
                while (ltsVowel(buf[p])) p++;
                break;
            case ':':
                while (ltsCons(buf[p])) p++;
                break;
            case '*':
                if (!ltsCons(b)) return false;
                while (ltsCons(buf[p])) p++;
                break;
            case '^':
                if (!ltsCons(b)) return false;
                p++;
                break;
            case '.':
                if (!ltsVoiced(b)) return false;
                p++;
                break;
            case '+':
                if (!ltsFront(b)) return false;
                p++;
                break;
            case '&':
                if ((b == 'C' || b == 'S') && buf[p + 1] == 'H') p += 2;
                else if (b == 'S' || b == 'C' || b == 'G' || b == 'Z' || b == 'X' || b == 'J') p++;
                else return false;
                break;
            case '@':
                if ((b == 'T' || b == 'C' || b == 'S') && buf[p + 1] == 'H') p += 2;
                else if (b == 'T' || b == 'S' || b == 'R' || b == 'D' || b == 'L' || b == 'Z' || b == 'N' || b == 'J') p++;
                else return false;
                break;
            case '%': {
                static const char* const kPctSuffixes[] = {"ING", "ELY", "EST", "ERS", "ES", "ED", "ER", "E"};
                bool ok = false;
                for (size_t s = 0; s < ARRAY_COUNT(kPctSuffixes) && !ok; s++) {
                    size_t n = strlen(kPctSuffixes[s]);
                    if (strncmp(buf + p, kPctSuffixes[s], n) == 0 && !ltsLetter(buf[p + n])) {
                        p += (int)n;
                        ok = true;
                    }
                }
                if (!ok) return false;
                break;
            }
            default:
                if (b != c) return false;
                p++;
                break;
        }
    }
    return true;
}

// Appends rule output phonemes; vowels without digit get kStressUnknown.
static void ltsAppend(const char* s, Pron& out) {
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        const char* b = s;
        while (*s && *s != ' ' && !(*s >= '0' && *s <= '9')) s++;
        int ph = phFromName(b, (int)(s - b));
        u8 st = kStressUnknown;
        if (*s >= '0' && *s <= '2') st = (u8)(*s++ - '0');
        while (*s && *s != ' ') s++;
        if (ph < 0) continue;
        PhS x;
        x.ph = (u8)ph;
        x.stress = isVowel(ph) ? st : 0;
        out.push_back(x);
    }
}

static bool isTenseVowel(int ph) { return hasFlag(ph, PF_TENSE) || ph == PH_IY || ph == PH_UW; }

static bool endsWith(const std::string& w, const char* suf) {
    size_t n = strlen(suf);
    return w.size() >= n && w.compare(w.size() - n, n, suf) == 0;
}
static bool startsWith(const std::string& w, const char* pre) {
    size_t n = strlen(pre);
    return w.size() > n && w.compare(0, n, pre) == 0;
}

// Assigns lexical stress to rule-derived pronunciations and reduces unstressed vowels.
static void assignStress(const std::string& w, Pron& p) {
    std::vector<int> vow;  // indices of vowels
    for (int i = 0; i < (int)p.size(); i++)
        if (isVowel(p[i].ph)) vow.push_back(i);
    if (vow.empty()) return;
    std::vector<int> cand;
    for (int i : vow)
        if (p[i].stress == kStressUnknown) cand.push_back(i);
    int n = (int)vow.size();
    int target = -1;
    if (cand.empty()) {
        target = vow[0];  // e.g. "king" -> K IH0 NG: promote
    } else if (cand.size() == 1) {
        target = cand[0];
    } else {
        // Suffix-driven placement.
        bool preStress = endsWith(w, "ic") || endsWith(w, "ics") || endsWith(w, "ical") || endsWith(w, "ically");
        bool finalStress = endsWith(w, "ee") || endsWith(w, "eer") || endsWith(w, "ese") || endsWith(w, "ette") ||
                           endsWith(w, "oon") || endsWith(w, "ique") || endsWith(w, "esque") || endsWith(w, "ade");
        int nc = (int)cand.size();
        if (finalStress) {
            target = cand[nc - 1];
        } else if (preStress) {
            // last candidate is the "ic" vowel; stress the one before it
            target = nc >= 2 ? cand[nc - 2] : cand[0];
        } else if ((endsWith(w, "ate") || endsWith(w, "ize") || endsWith(w, "ise") || endsWith(w, "ated") ||
                    endsWith(w, "izes") || endsWith(w, "ates")) &&
                   nc >= 3) {
            target = cand[nc - 3];
        } else {
            // Prefix check for two-syllable words: unstressed prefixes shift stress right.
            static const char* const kPrefixes[] = {"be", "de", "re", "pre", "pro", "con", "com", "dis", "ex", "mis",
                                                    "ob", "sub", "sur", "per", "ad", "ac", "ap", "at", "ef", "es",
                                                    "in", "im", "en", "em", "un", "for", "a"};
            int nsyl = n;
            // count trailing unstressed (marked) vowels as a neutral suffix
            int trail = 0;
            for (int k = n - 1; k >= 0 && p[vow[k]].stress != kStressUnknown; k--) trail++;
            int core = nsyl - trail;  // syllables before neutral suffix
            // candidates restricted to the core part
            std::vector<int> cc;
            for (int k = 0; k < core; k++)
                if (p[vow[k]].stress == kStressUnknown) cc.push_back(vow[k]);
            if (cc.empty()) cc = cand;
            if (core <= 1 || cc.size() == 1) {
                target = cc[0];
            } else if (core == 2) {
                bool pre = false;
                for (size_t k = 0; k < ARRAY_COUNT(kPrefixes); k++)
                    if (startsWith(w, kPrefixes[k]) && w.size() >= strlen(kPrefixes[k]) + 3) {
                        pre = true;
                        break;
                    }
                // "a" prefix only when followed by consonant + vowel (about, away)
                if (pre && w[0] == 'a' && w.size() > 2 && strchr("aeiouy", w[1])) pre = false;
                target = pre && cc.size() >= 2 ? cc[1] : cc[0];
            } else {
                // Latin-style rule over the core syllables: heavy penult attracts stress.
                int pen = vow[core - 2];
                int ante = vow[core - 3];
                int consAfter = 0;
                for (int k = pen + 1; k < vow[core - 1]; k++) consAfter++;
                bool heavy = isTenseVowel(p[pen].ph) || consAfter >= 2;
                target = heavy ? pen : ante;
                if (p[target].stress != kStressUnknown) target = (p[pen].stress == kStressUnknown) ? pen : cc[0];
            }
        }
    }
    // Apply stress: primary on target, secondary two syllables earlier in long words.
    int tv = -1;
    for (int k = 0; k < n; k++)
        if (vow[k] == target) tv = k;
    for (int k = 0; k < n; k++) {
        PhS& x = p[vow[k]];
        if (vow[k] == target) {
            x.stress = 1;
        } else if (x.stress == kStressUnknown) {
            x.stress = (tv >= 2 && k == tv - 2 && k == 0) ? 2 : 0;
        }
    }
    // Vowel reduction of unstressed full vowels.
    for (int k = 0; k < n; k++) {
        PhS& x = p[vow[k]];
        if (x.stress != 0) continue;
        switch (x.ph) {
            case PH_AE:
            case PH_AA:
            case PH_AO:
            case PH_EH:
            case PH_UH:
                x.ph = PH_AH;
                break;
            case PH_ER:
                break;
            default:
                break;
        }
    }
}

void letterToSound(const std::string& word, Pron& out) {
    std::string buf = " ";
    for (char c : word) {
        if (c >= 'a' && c <= 'z') buf += (char)(c - 32);
        else if (c >= 'A' && c <= 'Z') buf += c;
    }
    buf += "  ";
    if (buf.size() <= 3) return;
    Pron p;
    int len = (int)buf.size() - 2;
    int i = 1;
    while (i < len) {
        char c = buf[i];
        if (!ltsLetter(c)) {
            i++;
            continue;
        }
        const LtsRule* r = kRuleTable[c - 'A'];
        bool matched = false;
        for (; r->match; r++) {
            int ml = (int)strlen(r->match);
            if (strncmp(buf.c_str() + i, r->match, (size_t)ml) != 0) continue;
            if (!ltsMatchLeft(buf.c_str(), i - 1, r->left)) continue;
            if (!ltsMatchRight(buf.c_str(), i + ml, r->right)) continue;
            ltsAppend(r->out, p);
            i += ml;
            matched = true;
            break;
        }
        if (!matched) i++;
    }
    std::string lw;
    for (char c : word)
        if (c >= 'a' && c <= 'z') lw += c;
        else if (c >= 'A' && c <= 'Z') lw += (char)(c + 32);
    assignStress(lw, p);
    out.insert(out.end(), p.begin(), p.end());
}

// ---------------------------------------------------------------------------------------------
// Morphology

static bool pronFromEntry(const char* e, Pron& out, bool* function) {
    if (!e) return false;
    if (function) *function = e[0] == '~';
    parsePhonemes(e[0] == '~' ? e + 1 : e, out);
    return true;
}

static int lastPh(const Pron& p) { return p.empty() ? PH_SIL : p.back().ph; }

static bool isSibilantPh(int ph) { return ph == PH_S || ph == PH_Z || ph == PH_SH || ph == PH_ZH || ph == PH_CH || ph == PH_JH; }
static bool isVoicelessPh(int ph) {
    return ph == PH_P || ph == PH_T || ph == PH_K || ph == PH_F || ph == PH_TH || ph == PH_S || ph == PH_SH || ph == PH_CH;
}

static void addPlural(Pron& p) {
    int l = lastPh(p);
    if (isSibilantPh(l)) parsePhonemes("IH0 Z", p);
    else if (isVoicelessPh(l)) parsePhonemes("S", p);
    else parsePhonemes("Z", p);
}
static void addPast(Pron& p) {
    int l = lastPh(p);
    if (l == PH_T || l == PH_D) parsePhonemes("IH0 D", p);
    else if (isVoicelessPh(l)) parsePhonemes("T", p);
    else parsePhonemes("D", p);
}

static void demotePrimary(Pron& p) {
    for (PhS& x : p)
        if (x.stress == 1) x.stress = 2;
}

// Looks a stem up in the dictionary (optionally trying orthographic repairs).
static bool stemLookup(const std::string& stem, Pron& out) {
    if (stem.size() < 2) return false;
    return pronFromEntry(dictLookup(stem), out, nullptr);
}

static bool isConsChar(char c) { return c >= 'a' && c <= 'z' && !strchr("aeiou", c); }

// Tries stem candidates for a suffix-stripped word: stem, stem+e, undoubled stem, y-restored stem.
static bool findStem(const std::string& stem, bool yRestore, Pron& out, int depth);

static bool analyze(const std::string& w, Pron& out, int depth);

static bool findStem(const std::string& stem, bool yRestore, Pron& out, int depth) {
    if (stem.size() < 2) return false;
    Pron p;
    if (yRestore) {
        if (stemLookup(stem + "y", p)) { out = p; return true; }
        return false;
    }
    if (stem.size() >= 3 && stemLookup(stem, p)) { out = p; return true; }
    p.clear();
    if (stemLookup(stem + "e", p)) { out = p; return true; }
    p.clear();
    size_t n = stem.size();
    if (n >= 3 && stem[n - 1] == stem[n - 2] && isConsChar(stem[n - 1]) && stemLookup(stem.substr(0, n - 1), p)) {
        out = p;
        return true;
    }
    p.clear();
    if (stem.size() == 2 && stemLookup(stem, p)) { out = p; return true; }
    if (depth > 0) {
        p.clear();
        if (stem.size() >= 4 && analyze(stem, p, depth - 1)) { out = p; return true; }
    }
    return false;
}

struct SuffixDef {
    const char* suf;
    const char* ph;     // appended phonemes ("" for special handling)
    int kind;           // 0 = plain, 1 = plural -s, 2 = past -ed, 3 = y->i form
};

static const SuffixDef kSuffixes[] = {
    {"iness", "N AH0 S", 3}, {"ies", "", 3}, {"ied", "", 3}, {"ier", "ER0", 3}, {"iest", "IH0 S T", 3},
    {"ily", "L IY0", 3}, {"iful", "F AH0 L", 3},
    {"ings", "IH0 NG Z", 0}, {"ing", "IH0 NG", 0}, {"ed", "", 2}, {"es", "", 1}, {"'s", "", 1}, {"s'", "", 1},
    {"ers", "ER0 Z", 0}, {"er", "ER0", 0}, {"est", "IH0 S T", 0}, {"ly", "L IY0", 0}, {"ness", "N AH0 S", 0},
    {"ments", "M AH0 N T S", 0}, {"ment", "M AH0 N T", 0}, {"fully", "F AH0 L IY0", 0}, {"ful", "F AH0 L", 0},
    {"lessly", "L AH0 S L IY0", 0}, {"less", "L AH0 S", 0}, {"able", "AH0 B AH0 L", 0}, {"ably", "AH0 B L IY0", 0},
    {"ism", "IH0 Z AH0 M", 0}, {"ists", "IH0 S T S", 0}, {"ist", "IH0 S T", 0}, {"ish", "IH0 SH", 0},
    {"hood", "HH UH2 D", 0}, {"ship", "SH IH2 P", 0}, {"wards", "W ER0 D Z", 0}, {"ward", "W ER0 D", 0},
    {"y", "IY0", 0}, {"s", "", 1},
};

static bool analyzeSuffix(const std::string& w, Pron& out, int depth) {
    for (size_t k = 0; k < ARRAY_COUNT(kSuffixes); k++) {
        const SuffixDef& s = kSuffixes[k];
        if (!endsWith(w, s.suf)) continue;
        std::string stem = w.substr(0, w.size() - strlen(s.suf));
        if (stem.size() < 2) continue;
        Pron p;
        if (s.kind == 3) {
            if (!findStem(stem, true, p, depth)) continue;
            if (strcmp(s.suf, "ies") == 0) addPlural(p);
            else if (strcmp(s.suf, "ied") == 0) addPast(p);
            else {
                if (strcmp(s.suf, "ily") == 0 && !p.empty() && p.back().ph == PH_IY) {
                    p.back().ph = PH_AH;  // happily: HH AE1 P AH0 L IY0
                    p.back().stress = 0;
                }
                parsePhonemes(s.ph, p);
            }
        } else {
            // "-s" after a vowel letter could be a plural of a vowel-final word ("radios", "cameras").
            if (s.kind == 1 && strcmp(s.suf, "s") == 0 && endsWith(w, "ss")) continue;  // "boss", "miss"
            if (!findStem(stem, false, p, depth)) continue;
            if (s.kind == 1) addPlural(p);
            else if (s.kind == 2) addPast(p);
            else {
                // "-y" only after consonants ("dirty", "bloody"); avoid "ly" double counting
                if (strcmp(s.suf, "y") == 0 && !isConsChar(stem.back())) continue;
                parsePhonemes(s.ph, p);
            }
        }
        out.insert(out.end(), p.begin(), p.end());
        return true;
    }
    return false;
}

static bool analyzePrefix(const std::string& w, Pron& out, int depth) {
    struct PrefixDef {
        const char* pre;
        const char* ph;
    };
    static const PrefixDef kPrefixes[] = {
        {"under", "AH2 N D ER0"}, {"over", "OW2 V ER0"}, {"out", "AW2 T"}, {"un", "AH0 N"}, {"re", "R IY0"},
        {"dis", "D IH0 S"}, {"mis", "M IH0 S"}, {"non", "N AA2 N"}, {"pre", "P R IY0"}, {"anti", "AE2 N T IY0"},
        {"super", "S UW2 P ER0"}, {"sub", "S AH2 B"}, {"inter", "IH2 N T ER0"}, {"semi", "S EH2 M IY0"},
        {"mid", "M IH2 D"}, {"self", "S EH2 L F"}, {"ex", "EH2 K S"}, {"co", "K OW2"}, {"de", "D IY0"},
        {"up", "AH2 P"}, {"down", "D AW2 N"}, {"back", "B AE2 K"},
    };
    for (size_t k = 0; k < ARRAY_COUNT(kPrefixes); k++) {
        const PrefixDef& d = kPrefixes[k];
        if (!startsWith(w, d.pre)) continue;
        std::string rest = w.substr(strlen(d.pre));
        if (rest.size() < 3) continue;
        Pron p;
        bool ok = pronFromEntry(dictLookup(rest), p, nullptr);
        if (!ok && depth > 0) ok = analyzeSuffix(rest, p, depth - 1);
        if (!ok) continue;
        parsePhonemes(d.ph, out);
        out.insert(out.end(), p.begin(), p.end());
        return true;
    }
    return false;
}

static bool analyzeCompound(const std::string& w, Pron& out, int depth) {
    if (w.size() < 6) return false;
    // Prefer the longest first part.
    for (int split = (int)w.size() - 3; split >= 3; split--) {
        std::string a = w.substr(0, (size_t)split), b = w.substr((size_t)split);
        Pron pa, pb;
        const char* ea = dictLookup(a);
        if (!ea || ea[0] == '~') continue;  // function words rarely start compounds
        const char* eb = dictLookup(b);
        bool okb = false;
        if (eb) okb = pronFromEntry(eb, pb, nullptr);
        else if (depth > 0) okb = analyzeSuffix(b, pb, 0);
        if (!okb) continue;
        pronFromEntry(ea, pa, nullptr);
        // function-word entries carry reduced stress; give the first part primary stress
        bool hasPrimary = false;
        for (PhS& x : pa)
            if (x.stress == 1) hasPrimary = true;
        if (!hasPrimary)
            for (PhS& x : pa)
                if (isVowel(x.ph)) {
                    x.stress = 1;
                    break;
                }
        demotePrimary(pb);
        out.insert(out.end(), pa.begin(), pa.end());
        out.insert(out.end(), pb.begin(), pb.end());
        return true;
    }
    return false;
}

static bool analyze(const std::string& w, Pron& out, int depth) {
    if (analyzeSuffix(w, out, depth)) return true;
    if (analyzePrefix(w, out, depth)) return true;
    if (analyzeCompound(w, out, depth)) return true;
    return false;
}

static void makeGDrop(Pron& p) {
    if (p.size() >= 2 && p.back().ph == PH_NG) p.back().ph = PH_N;
}

void lookupWord(const TextWord& tw, WordPron& out) {
    out.ph.clear();
    out.function = false;
    if (tw.phon) {
        parsePhonemes(tw.w.c_str(), out.ph);
        return;
    }
    if (tw.spell) {
        spellWord(tw.w, out.ph);
        return;
    }
    std::string w = tw.w;
    if (w.empty()) return;
    if (tw.gDrop) {
        // "nothin'" -> "nothing" with final /n/
        std::string full = w + "g";
        WordPron tmp;
        TextWord t2 = tw;
        t2.gDrop = false;
        t2.w = full;
        lookupWord(t2, tmp);
        out = tmp;
        makeGDrop(out.ph);
        return;
    }
    const char* e = dictLookup(w);
    if (e) {
        pronFromEntry(e, out.ph, &out.function);
        return;
    }
    // Apostrophe forms.
    size_t ap = w.find('\'');
    if (ap != std::string::npos) {
        std::string base = w.substr(0, ap), tail = w.substr(ap + 1);
        if (!base.empty()) {
            TextWord tb = tw;
            tb.w = base;
            WordPron bp;
            if (tail == "s" || tail.empty()) {  // possessive / "is" / plural possessive
                lookupWord(tb, bp);
                out = bp;
                if (tail == "s") addPlural(out.ph);
                out.function = false;
                return;
            }
            const char* add = nullptr;
            if (tail == "ll") add = "AH0 L";
            else if (tail == "re") add = "ER0";
            else if (tail == "ve") add = "AH0 V";
            else if (tail == "d") add = "AH0 D";
            else if (tail == "t" && endsWith(base, "n")) add = "T";
            else if (tail == "m") add = "M";
            if (add) {
                lookupWord(tb, bp);
                out = bp;
                int l = lastPh(out.ph);
                bool afterVowel = isVowel(l);
                if (afterVowel && (tail == "ll" || tail == "ve" || tail == "d")) parsePhonemes(add + 4, out.ph);
                else parsePhonemes(add, out.ph);
                return;
            }
        }
        // Otherwise drop apostrophes ("'bout", "o'brien").
        std::string s;
        for (char c : w)
            if (c != '\'') s += c;
        TextWord t2 = tw;
        t2.w = s;
        lookupWord(t2, out);
        return;
    }
    if (analyze(w, out.ph, 1)) return;
    letterToSound(w, out.ph);
    if (out.ph.empty()) spellWord(w, out.ph);
}

}  // namespace detail
}  // namespace Speech
