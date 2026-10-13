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
    {" ", "A", "WA", "AH0"},
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
    {"#:", "ARY", " ", "EH2 R IY0"},
    {"#:", "ANCY", " ", "AH0 N S IY0"},
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
    {" ", "CHARACT", "", "K EH R AH0 K T"},
    {" ", "CHEM", "", "K EH M"},
    {" ", "CHAOS", "", "K EY AA S"},
    {"", "CHR", "", "K R"},
    {"", "CH", "", "CH"},
    {"", "C", "Q", ""},
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
    {"", "DU", "A", "JH UW0"},
    {"", "DGE", "", "JH"},
    {"", "DG", "", "JH"},
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
    {"#:^", "ENCY", " ", "AH0 N S IY0"},
    {"#", "ENCY", " ", "AH0 N S IY0"},
    {"#:^", "ENT", " ", "AH0 N T"},
    {"#:^", "ENTS", " ", "AH0 N T S"},
    {"#:^", "ENCE", " ", "AH0 N S"},
    {" ", "EX", "#", "IH0 G Z"},
    {" ", "E", "^^", "EH"},
    {"", "EAR", "^", "ER"},
    {"", "EAR", "", "IH R"},
    {"", "EAU", "", "Y UW"},
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
    {"#:", "GUE", " ", "G"},
    {"#:", "GUES", " ", "G Z"},
    {" ", "GH", "", "G"},
    {"OU", "GH", " ", "F"},
    {"", "GH", "", ""},
    {" ", "G", "N", ""},
    {"", "G", "N ", ""},
    {"", "G", "NS ", ""},
    {"", "G", "NED ", ""},
    {"", "G", "NM", ""},
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
    {" ", "INN", "", "IH N"},
    {" ", "IN", "", "IH N"},
    {" ", "I", " ", "AY"},
    {"", "IGH", "", "AY"},
    {"", "I", "GN ", "AY"},
    {"", "I", "GNS ", "AY"},
    {"", "I", "GNED ", "AY"},
    {"", "I", "GNM", "AY"},
    {"", "I", "ND ", "AY"},
    {"", "I", "NDS ", "AY"},
    {"", "I", "LD", "AY"},
    {"^", "I", "ENT ", "IY0"},
    {"^", "I", "ENTS ", "IY0"},
    {"^", "I", "ENCE", "IY0"},
    {"^", "I", "ENCY", "IY0"},
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
    {"", "IOR", "", "IY0 ER0"},
    {"", "IUM", "", "IY0 AH0 M"},
    {"", "I", "ENT ", "IY0"},
    {"", "I", "ENTS ", "IY0"},
    {"", "I", "ENCE", "IY0"},
    {"#:", "IFY", "", "AH0 F AY2"},
    {"#:", "IFIED", "", "AH0 F AY2 D"},
    {"#:", "ICALLY", "", "IH0 K L IY0"},
    {"#:", "ICAL", "", "IH0 K AH0 L"},
    {"#:", "ICS", " ", "IH0 K S"},
    {"#:", "IC", " ", "IH0 K"},
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
    {"", "NGE", " ", "N JH"},
    {"", "NGES", " ", "N JH IH0 Z"},
    {"", "NGED", " ", "N JH D"},
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
    {"#:", "OUS", "", "AH0 S"},
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
    {"#:", "OR", "ED ", "ER0"},
    {"#:", "OR", "ING", "ER0"},
    {"#:", "ORY", " ", "AO2 R IY0"},
    {"#:", "ORS", " ", "ER0 Z"},
    {"", "OR", "", "AO R"},
    {"", "OLD", "", "OW L D"},
    {"", "OLT", "", "OW L T"},
    {"", "OST", " ", "OW S T"},
    {"#:^", "ON", " ", "AH0 N"},
    {"#:^", "ONS", " ", "AH0 N Z"},
    {"#:", "OM", " ", "AH0 M"},
    {"", "O", "TION", "OW"},
    {"", "O", "^%", "OW"},
    {"", "O", " ", "OW"},
    {"", "O", "", "AA"},
    {nullptr, nullptr, nullptr, nullptr}};

static const LtsRule kRulesP[] = {
    {"", "PH", "", "F"},
    {"", "PP", "", "P"},
    {" ", "PSYCH", "", "S AY K"},
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
    {" ", "UNI", "", "Y UW N IH"},
    {" ", "UN", "", "AH N"},
    {" ", "UPP", "", "AH P"},
    {" ", "UP", "", "AH P"},
    {"", "URE", " ", "Y UH R"},
    {"@", "UR", "#", "UH R"},
    {"", "UR", "#", "Y UH R"},
    {"", "UR", "", "ER"},
    {"", "UY", "", "AY"},
    {"#:", "UAL", "", "Y UW0 AH0 L"},
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
static inline bool ltsCons(char c) {
    return ltsLetter(c) && c != 'A' && c != 'E' && c != 'I' && c != 'O' && c != 'U' && c != 'Y';
}
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

// Number of trailing vowels that belong to a stress-neutral suffix (-ing, -ness, ...).
static int neutralSuffixVowels(const std::string& w, const Pron& p) {
    struct NS {
        const char* s;
        int v;
    };
    static const NS kNeutral[] = {{"ings", 1}, {"ing", 1},  {"ers", 1},    {"er", 1},   {"est", 1},  {"lessly", 2},
                                  {"less", 1}, {"ness", 1}, {"ments", 1},  {"ment", 1}, {"fully", 2}, {"ful", 1},
                                  {"ably", 2}, {"able", 2}, {"ism", 2},    {"ists", 1}, {"ist", 1},   {"ish", 1},
                                  {"ship", 1}, {"hood", 1}, {"wards", 1},  {"ward", 1}, {"ly", 1},
                                  {"aries", 2}, {"ories", 2}, {"ary", 2}, {"ory", 2}};
    for (size_t k = 0; k < ARRAY_COUNT(kNeutral); k++) {
        size_t n = strlen(kNeutral[k].s);
        bool ist = kNeutral[k].s[0] == 'i' && (kNeutral[k].s[1] == 's');  // -ist/-ism need a real stem
        if (w.size() >= n + (ist ? 5 : 3) && endsWith(w, kNeutral[k].s)) return kNeutral[k].v;
    }
    if ((endsWith(w, "ed") || endsWith(w, "es")) && p.size() >= 2 && p[p.size() - 2].ph == PH_IH &&
        p[p.size() - 2].stress == 0 && (p.back().ph == PH_D || p.back().ph == PH_Z))
        return 1;
    return 0;
}

// Assigns lexical stress to rule-derived pronunciations and reduces unstressed vowels.
static void assignStress(const std::string& w, Pron& p) {
    std::vector<int> vow;  // indices of vowels
    for (int i = 0; i < (int)p.size(); i++)
        if (isVowel(p[i].ph)) vow.push_back(i);
    if (vow.empty()) return;
    const int n = (int)vow.size();
    std::vector<int> cand;
    for (int i : vow)
        if (p[i].stress == kStressUnknown) cand.push_back(i);
    int target = -1;
    // Suffixes that put the stress on the syllable right before them (their own vowels are 0-marked by the rules).
    static const char* const kPreStress[] = {
        "tion", "tions", "sion", "sions", "cian", "cians", "tian", "cial", "cially", "tial", "tially", "cious",
        "tious", "xious", "gion", "gions", "gious", "ity", "ities", "ify", "ified", "ifies", "ical", "ically", "ic",
        "ics", "ial", "ially", "ian", "ians", "ious", "iously", "eous", "uous", "ual", "ually", "ia", "ias", "ient",
        "ients", "ience", "iency", "ior", "iors", "ium", "iums", "ssive", "ctive", "ctives", "nsive", "ptive",
        "ency", "ancy"};
    static const char* const kFinalStress[] = {"ee", "ees", "eer", "eers", "ese", "ette", "ettes", "oon", "oons",
                                               "ique", "esque", "aire"};
    bool preStress = false, finalStress = false;
    for (size_t k = 0; k < ARRAY_COUNT(kPreStress) && !preStress; k++)
        if (w.size() > strlen(kPreStress[k]) + 1 && endsWith(w, kPreStress[k])) preStress = true;
    for (size_t k = 0; k < ARRAY_COUNT(kFinalStress) && !finalStress; k++)
        if (w.size() > strlen(kFinalStress[k]) + 1 && endsWith(w, kFinalStress[k])) finalStress = true;
    if (cand.empty()) {
        target = vow[0];  // e.g. "king" -> K IH0 NG: promote
    } else if (cand.size() == 1) {
        target = cand[0];
    } else if (preStress) {
        target = cand.back();
    } else if (finalStress) {
        target = cand.back();
    } else {
        int core = std::max(1, n - neutralSuffixVowels(w, p));
        std::vector<int> cc;  // candidates inside the core
        for (int k = 0; k < core; k++)
            if (p[vow[k]].stress == kStressUnknown) cc.push_back(vow[k]);
        if (cc.empty()) cc = cand;
        bool verbAte = (endsWith(w, "ate") || endsWith(w, "ated") || endsWith(w, "ates") || endsWith(w, "ating") ||
                        endsWith(w, "ator") || endsWith(w, "ators") ||
                        endsWith(w, "ize") || endsWith(w, "ized") || endsWith(w, "izes") || endsWith(w, "izing") ||
                        endsWith(w, "ise") || endsWith(w, "ised"));
        if (verbAte && core >= 3 && p[vow[core - 3]].stress == kStressUnknown) {
            target = vow[core - 3];
        } else if (core <= 1 || cc.size() == 1) {
            target = cc[0];
        } else if (core == 2) {
            static const char* const kPrefixes[] = {"be", "de", "re", "pre", "pro", "con", "com", "dis", "ex", "mis",
                                                    "ob", "sub", "sur", "per", "ad", "ac", "ap", "at", "ef", "es",
                                                    "in", "im", "en", "em", "un", "for", "a"};
            bool pre = false;
            for (size_t k = 0; k < ARRAY_COUNT(kPrefixes) && !pre; k++) {
                size_t pl = strlen(kPrefixes[k]);
                if (!startsWith(w, kPrefixes[k]) || w.size() < pl + 3) continue;
                if (pl == 1) {
                    // a- prefix: a + consonant + vowel (about), doubled consonant (assist, attack) or an onset
                    // cluster (asleep, agree); not before other clusters (alter, active)
                    if (w.size() <= 3 || strchr("aeiouy", w[1])) continue;
                    char c1 = w[1], c2 = w[2];
                    bool vowel2 = strchr("aeiouy", c2) != nullptr;
                    bool doubled = c1 == c2;
                    bool onset = strchr("lrw", c2) != nullptr && strchr("bcdfgkpt", c1) != nullptr;
                    bool sOnset = c1 == 's' && strchr("lpt", c2) != nullptr;
                    pre = vowel2 || doubled || onset || sOnset;
                } else {
                    pre = true;
                }
            }
            if (p[cc[0]].stress != kStressUnknown) pre = true;
            target = pre && cc.size() >= 2 ? cc[1] : cc[0];
        } else {
            // Latin-style rule: a heavy penult (tense vowel or closed syllable) attracts stress, else antepenult.
            int pen = vow[core - 2], ante = vow[core - 3], last = vow[core - 1];
            int clusterLen = last - pen - 1;
            bool closed = false;
            if (clusterLen >= 2) {
                int c1 = p[pen + 2].ph;
                bool mutaCumLiquida = clusterLen == 2 && hasFlag(p[pen + 1].ph, PF_OBSTRUENT) &&
                                      (c1 == PH_L || c1 == PH_R || c1 == PH_W || c1 == PH_Y);
                closed = !mutaCumLiquida;
            }
            bool heavy = isTenseVowel(p[pen].ph) || p[pen].ph == PH_ER || closed;
            target = heavy ? pen : ante;
            if (p[target].stress != kStressUnknown) target = (p[pen].stress == kStressUnknown) ? pen : cc[0];
        }
    }
    // Apply stress: primary on target, secondary on the first syllable of long words.
    int tv = -1;
    for (int k = 0; k < n; k++)
        if (vow[k] == target) tv = k;
    for (int k = 0; k < n; k++) {
        PhS& x = p[vow[k]];
        if (vow[k] == target) x.stress = 1;
        else if (x.stress == kStressUnknown) x.stress = (tv >= 2 && k == 0) ? 2 : 0;
    }
    // Vowel reduction of unstressed full vowels.
    for (int k = 0; k < n; k++) {
        PhS& x = p[vow[k]];
        if (x.stress != 0) continue;
        if (x.ph == PH_AE || x.ph == PH_AA || x.ph == PH_AO || x.ph == PH_EH || x.ph == PH_UH) x.ph = PH_AH;
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
    // final -s after a consonant letter (or y / w) that follows a voiced sound is /z/ ("weighs", "lens", "laws")
    size_t n = lw.size();
    if (n >= 3 && lw[n - 1] == 's' && !strchr("aeious", lw[n - 2]) && p.size() >= 2 && p.back().ph == PH_S) {
        int pv = p[p.size() - 2].ph;
        if (hasFlag(pv, PF_VOICED) && !hasFlag(pv, PF_SIBILANT)) p.back().ph = PH_Z;
    }
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

static int lastPh(const Pron& p) { return p.empty() ? (int)PH_SIL : (int)p.back().ph; }

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

// Looks a stem up in the dictionary (content words only: "butter" is not "but" + "er").
static bool stemLookup(const std::string& stem, Pron& out) {
    if (stem.size() < 2) return false;
    const char* e = dictLookup(stem);
    if (!e || e[0] == '~') return false;
    return pronFromEntry(e, out, nullptr);
}

static bool isConsChar(char c) { return c >= 'a' && c <= 'z' && !strchr("aeiou", c); }
static bool isVowelChar(char c) { return c && strchr("aeiou", c) != nullptr; }

static bool analyze(const std::string& w, Pron& out, int depth);
static bool analyzeSuffix(const std::string& w, Pron& out, int depth);

// Finds the stem of a suffixed word. Vowel-initial suffixes allow silent-e restoration ("hated" -> hate; single
// consonant after a single vowel prefers the e-form) and consonant undoubling ("robbed" -> rob); consonant-initial
// suffixes need the exact stem ("movement" -> move).
static bool findStem(const std::string& stem, bool vowelSuffix, bool yRestore, Pron& out, int depth) {
    size_t n = stem.size();
    Pron p;
    if (yRestore) return n >= 2 && stemLookup(stem + "y", out);
    if (n < 2) return false;
    if (!vowelSuffix) {
        if (n >= 3 && stemLookup(stem, out)) return true;
        return depth > 0 && n >= 4 && analyzeSuffix(stem, out, depth - 1);
    }
    bool singleVC = isConsChar(stem[n - 1]) && isVowelChar(stem[n - 2]) && (n < 3 || !isVowelChar(stem[n - 3])) &&
                    stem[n - 1] != 'w' && stem[n - 1] != 'x' && stem[n - 1] != 'y';
    if (singleVC && stemLookup(stem + "e", p)) {
        out = p;
        return true;
    }
    p.clear();
    if (n >= 3 && stemLookup(stem, p)) {
        out = p;
        return true;
    }
    p.clear();
    if (n >= 4 && stem[n - 1] == stem[n - 2] && isConsChar(stem[n - 1]) && stemLookup(stem.substr(0, n - 1), p)) {
        out = p;
        return true;
    }
    p.clear();
    if (!singleVC && stemLookup(stem + "e", p)) {
        out = p;
        return true;
    }
    return false;
}

struct SuffixDef {
    const char* suf;
    const char* ph;  // appended phonemes ("" for special handling)
    int kind;        // 0 = plain, 1 = plural -s, 2 = past -ed, 3 = y->i form
    bool vowelInit;  // suffix starts with a vowel (stem may drop silent e / double its consonant)
    bool stack;      // the stem itself may carry another suffix ("killers", "powerfully")
};

static const SuffixDef kSuffixes[] = {
    {"iness", "N AH0 S", 3, false, false}, {"ies", "", 3, true, false}, {"ied", "", 3, true, false},
    {"ier", "ER0", 3, true, false}, {"iest", "IH0 S T", 3, true, false}, {"ily", "L IY0", 3, false, false},
    {"iful", "F AH0 L", 3, false, false}, {"ings", "IH0 NG Z", 0, true, false}, {"ing", "IH0 NG", 0, true, false},
    {"ed", "", 2, true, false}, {"es", "", 1, true, false}, {"'s", "", 1, false, true}, {"s'", "", 1, false, true},
    {"ers", "ER0 Z", 0, true, false}, {"er", "ER0", 0, true, false}, {"est", "IH0 S T", 0, true, false},
    {"ors", "ER0 Z", 0, true, false}, {"or", "ER0", 0, true, false}, {"als", "AH0 L Z", 0, true, false},
    {"al", "AH0 L", 0, true, false},
    {"ly", "L IY0", 0, false, true}, {"ness", "N AH0 S", 0, false, true}, {"ments", "M AH0 N T S", 0, false, false},
    {"ment", "M AH0 N T", 0, false, false}, {"fully", "F AH0 L IY0", 0, false, false},
    {"ful", "F AH0 L", 0, false, false}, {"lessly", "L AH0 S L IY0", 0, false, false},
    {"less", "L AH0 S", 0, false, false}, {"able", "AH0 B AH0 L", 0, true, false},
    {"ably", "AH0 B L IY0", 0, true, false}, {"ism", "IH0 Z AH0 M", 0, true, false},
    {"ists", "IH0 S T S", 0, true, false}, {"ist", "IH0 S T", 0, true, false}, {"ish", "IH0 SH", 0, true, false},
    {"hood", "HH UH2 D", 0, false, false}, {"ship", "SH IH2 P", 0, false, false},
    {"wards", "W ER0 D Z", 0, false, false}, {"ward", "W ER0 D", 0, false, false}, {"y", "IY0", 0, true, false},
    {"s", "", 1, false, true},
};

static bool analyzeSuffix(const std::string& w, Pron& out, int depth) {
    // "-ly" after "-le": simply = simple + ly
    if (endsWith(w, "ly") && w.size() >= 5) {
        Pron p;
        std::string stem = w.substr(0, w.size() - 2) + "le";
        if (stemLookup(stem, p) && p.size() >= 2 && p.back().ph == PH_L) {
            if (isVowel(p[p.size() - 2].ph) && p[p.size() - 2].stress == 0) p.erase(p.end() - 2);
            parsePhonemes("IY0", p);
            out.insert(out.end(), p.begin(), p.end());
            return true;
        }
    }
    for (size_t k = 0; k < ARRAY_COUNT(kSuffixes); k++) {
        const SuffixDef& s = kSuffixes[k];
        if (!endsWith(w, s.suf)) continue;
        std::string stem = w.substr(0, w.size() - strlen(s.suf));
        if (stem.size() < 2) continue;
        Pron p;
        if (s.kind == 3) {
            if (!findStem(stem, s.vowelInit, true, p, 0)) continue;
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
            if (strcmp(s.suf, "s") == 0 && (endsWith(w, "ss") || endsWith(w, "us") || endsWith(w, "is"))) continue;
            if (strcmp(s.suf, "es") == 0 && !(endsWith(stem, "s") || endsWith(stem, "x") || endsWith(stem, "z") ||
                                              endsWith(stem, "ch") || endsWith(stem, "sh") || endsWith(stem, "o")))
                continue;
            if (strcmp(s.suf, "y") == 0 && (stem.size() < 3 || !isConsChar(stem.back()))) continue;
            if ((strcmp(s.suf, "al") == 0 || strcmp(s.suf, "als") == 0 || strcmp(s.suf, "or") == 0 ||
                 strcmp(s.suf, "ors") == 0) && stem.size() < 4)
                continue;
            if (!findStem(stem, s.vowelInit, false, p, s.stack ? depth : 0)) continue;
            if (s.kind == 1) addPlural(p);
            else if (s.kind == 2) addPast(p);
            else if (strcmp(s.suf, "ly") == 0 && !p.empty() && p.back().ph == PH_L) parsePhonemes("IY0", p);
            else parsePhonemes(s.ph, p);
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
        {"mid", "M IH2 D"}, {"self", "S EH2 L F"}, {"co", "K OW2"}, {"de", "D IY0"},
    };
    for (size_t k = 0; k < ARRAY_COUNT(kPrefixes); k++) {
        const PrefixDef& d = kPrefixes[k];
        if (!startsWith(w, d.pre)) continue;
        std::string rest = w.substr(strlen(d.pre));
        if (rest.size() < 4) continue;
        bool shortPre = strcmp(d.pre, "co") == 0 || strcmp(d.pre, "de") == 0 || strcmp(d.pre, "re") == 0;
        if (shortPre && isVowelChar(rest[0]) && rest.size() < 5) continue;  // "co-" + vowel etc. rarely prefixes
        Pron p;
        bool ok = stemLookup(rest, p);
        if (!ok && depth > 0) ok = analyzeSuffix(rest, p, 0);
        if (!ok) continue;
        parsePhonemes(d.ph, out);
        out.insert(out.end(), p.begin(), p.end());
        return true;
    }
    return false;
}

static bool analyzeCompound(const std::string& w, Pron& out, int depth) {
    if (w.size() < 7) return false;
    static const char* const kBadSecond[] = {"age", "ate", "ant", "ent", "ion", "ice", "ive", "ism", "ist", "ize",
                                             "ess", "est", "ous", "ary", "ory", "ery", "ance", "ence", "able",
                                             "ible", "ity", "ure", "ing", "ness", "less", "ment", "ful", "ship",
                                             "hood", "ward", "ers", "ions", "age", "ages", "ates", "ants", "ents",
                                             "tion", "sion", "ted", "ted"};
    for (int split = (int)w.size() - 3; split >= 3; split--) {
        std::string a = w.substr(0, (size_t)split), b = w.substr((size_t)split);
        bool bad = false;
        for (size_t k = 0; k < ARRAY_COUNT(kBadSecond) && !bad; k++) bad = b == kBadSecond[k];
        if (bad) continue;
        Pron pa, pb;
        if (!stemLookup(a, pa)) continue;
        bool okb = stemLookup(b, pb);
        if (!okb && depth > 0 && b.size() >= 4) okb = analyzeSuffix(b, pb, 0);
        if (!okb) continue;
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
