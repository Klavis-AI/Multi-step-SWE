/*
 * ext/mbstring/php_mbstring.c -- a UTF-8 only subset of ext/mbstring.
 *
 * Covers mb_strlen, mb_substr, mb_strpos, mb_strrpos, mb_stripos,
 * mb_strtolower, mb_strtoupper, mb_str_split, mb_internal_encoding,
 * mb_check_encoding, mb_convert_case, mb_convert_encoding, mb_strwidth,
 * mb_substr_count and mb_str_pad.
 *
 * Scope notes (deliberate, and different from php-src):
 *   - Only UTF-8 (and ASCII, its subset) are really implemented.  Any other
 *     `to`/`from` encoding handed to mb_convert_encoding() returns the input
 *     unchanged instead of transcoding it.
 *   - Case mapping covers ASCII, Latin-1 Supplement, Latin Extended-A, Greek
 *     and Cyrillic, plus the full-mapping special cases PHP 7.3 introduced.
 *   - Malformed UTF-8: every byte that is not a well formed sequence counts
 *     as one character and converts to '?'.  That is the PHP 8.x rule;
 *     PHP 7.x's old mbfl filters sometimes swallowed a bad lead byte
 *     together with the byte after it (mb_strlen("\xC3\x28") is 1 on 7.4 and
 *     2 on 8.3).  Valid UTF-8 is unaffected.
 *
 * Everything else was checked against
 * oracle/php{5.6,7.0,7.1,7.2,7.3,7.4,8.0,8.1,8.2,8.3,8.4}.
 */

#include "zend.h"
#include "zend_API.h"
#include "zend_ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Provided by the engine's constant-registration layer.  It does not exist
 * yet; declaring it here keeps this module self contained until it does.
 */
extern void zend_register_long_constant(const char *name, zend_long val);

#define MB_CASE_UPPER          0
#define MB_CASE_LOWER          1
#define MB_CASE_TITLE          2
#define MB_CASE_FOLD           3
#define MB_CASE_UPPER_SIMPLE   4
#define MB_CASE_LOWER_SIMPLE   5
#define MB_CASE_TITLE_SIMPLE   6
#define MB_CASE_FOLD_SIMPLE    7

/* ext/standard: STR_PAD_* */
#define MB_STR_PAD_LEFT        0
#define MB_STR_PAD_RIGHT       1
#define MB_STR_PAD_BOTH        2

/*
 * The one piece of module state php-src also keeps
 * (MBSTRG(current_internal_encoding)).  It starts out unset so that the
 * first use can seed it from the mbstring.internal_encoding ini entry.
 */
static char mb_internal_enc[64];

static long mb_version(void)
{
    return EG(php_version) ? EG(php_version) : 80400;
}

/* ------------------------------------------------------------------ */
/* byte buffer                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    char   *s;
    size_t  len;
    size_t  cap;
    int     oom;
} mb_buf;

static void mbb_reserve(mb_buf *b, size_t extra)
{
    size_t need;
    char *n;

    if (b->oom) {
        return;
    }
    need = b->len + extra + 1;
    if (need <= b->cap) {
        return;
    }
    while (b->cap < need) {
        b->cap = b->cap ? b->cap << 1 : 128;
    }
    n = (char *) realloc(b->s, b->cap);
    if (!n) {
        b->oom = 1;
        return;
    }
    b->s = n;
}

static void mbb_char(mb_buf *b, char c)
{
    mbb_reserve(b, 1);
    if (!b->oom) {
        b->s[b->len++] = c;
    }
}

static void mbb_mem(mb_buf *b, const char *s, size_t n)
{
    if (!n) {
        return;
    }
    mbb_reserve(b, n);
    if (!b->oom) {
        memcpy(b->s + b->len, s, n);
        b->len += n;
    }
}

static zend_string *mbb_finish(mb_buf *b)
{
    zend_string *z = zend_string_init(b->s ? b->s : "", b->len);
    free(b->s);
    b->s = NULL;
    b->len = b->cap = 0;
    return z;
}

/* ------------------------------------------------------------------ */
/* UTF-8 primitives                                                     */
/* ------------------------------------------------------------------ */

/* Decodes one character; returns -1 for a malformed byte (*used == 1). */
static int mb_u8_decode(const unsigned char *s, size_t len, size_t *used)
{
    unsigned int c = s[0];

    *used = 1;
    if (c < 0x80) {
        return (int) c;
    }
    if (c < 0xC2) {
        return -1;
    }
    if (c < 0xE0) {
        if (len < 2 || (s[1] & 0xC0) != 0x80) {
            return -1;
        }
        *used = 2;
        return (int) (((c & 0x1F) << 6) | (s[1] & 0x3F));
    }
    if (c < 0xF0) {
        int cp;
        if (len < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
            return -1;
        }
        cp = (int) (((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return -1;
        }
        *used = 3;
        return cp;
    }
    if (c < 0xF5) {
        int cp;
        if (len < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 ||
                (s[3] & 0xC0) != 0x80) {
            return -1;
        }
        cp = (int) (((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                    ((s[2] & 0x3F) << 6) | (s[3] & 0x3F));
        if (cp < 0x10000 || cp > 0x10FFFF) {
            return -1;
        }
        *used = 4;
        return cp;
    }
    return -1;
}

static void mb_u8_encode(mb_buf *b, unsigned int cp)
{
    if (cp < 0x80) {
        mbb_char(b, (char) cp);
    } else if (cp < 0x800) {
        mbb_char(b, (char) (0xC0 | (cp >> 6)));
        mbb_char(b, (char) (0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        mbb_char(b, (char) (0xE0 | (cp >> 12)));
        mbb_char(b, (char) (0x80 | ((cp >> 6) & 0x3F)));
        mbb_char(b, (char) (0x80 | (cp & 0x3F)));
    } else {
        mbb_char(b, (char) (0xF0 | (cp >> 18)));
        mbb_char(b, (char) (0x80 | ((cp >> 12) & 0x3F)));
        mbb_char(b, (char) (0x80 | ((cp >> 6) & 0x3F)));
        mbb_char(b, (char) (0x80 | (cp & 0x3F)));
    }
}

/* Character count: every byte that is not a UTF-8 continuation byte. */
static size_t mb_u8_strlen(const char *s, size_t len)
{
    size_t i, n = 0;

    for (i = 0; i < len; i++) {
        if (((unsigned char) s[i] & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

/* Byte offset of character number `chars` (clamped to len). */
static size_t mb_u8_offset(const char *s, size_t len, size_t chars)
{
    size_t i = 0, n = 0;

    while (i < len && n < chars) {
        i++;
        while (i < len && ((unsigned char) s[i] & 0xC0) == 0x80) {
            i++;
        }
        n++;
    }
    return i;
}

static int mb_u8_valid(const char *s, size_t len)
{
    size_t i = 0;

    while (i < len) {
        size_t used;
        if (mb_u8_decode((const unsigned char *) s + i, len - i, &used) < 0) {
            return 0;
        }
        i += used;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* case mapping                                                         */
/* ------------------------------------------------------------------ */
/*
 * Simple (1:1) mappings for ASCII, Latin-1 Supplement, Latin Extended-A,
 * Greek and Cyrillic.  The multi-character full mappings PHP 7.3 added live
 * in mb_case_full_upper() below.
 */
static unsigned int mb_toupper_cp(unsigned int c)
{
    if (c < 0x80) {
        return (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    if (c == 0xB5) {                      /* MICRO SIGN -> GREEK CAPITAL MU */
        return 0x39C;
    }
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
        return c - 0x20;
    }
    if (c == 0xFF) {                      /* y with diaeresis -> U+0178 */
        return 0x178;
    }
    if (c >= 0x100 && c <= 0x137) {
        if (c == 0x131) {                 /* dotless i -> I */
            return 'I';
        }
        if (c == 0x130) {
            return c;                     /* I with dot above is already upper */
        }
        return (c & 1) ? c - 1 : c;
    }
    if (c >= 0x139 && c <= 0x148) {
        return (c & 1) ? c : c - 1;
    }
    if (c >= 0x14A && c <= 0x177) {
        return (c & 1) ? c - 1 : c;
    }
    if (c >= 0x179 && c <= 0x17E) {
        return (c & 1) ? c : c - 1;
    }
    if (c == 0x17F) {                     /* long s -> S */
        return 'S';
    }
    /* Greek */
    if (c == 0x3AC) { return 0x386; }
    if (c >= 0x3AD && c <= 0x3AF) { return c - 0x25; }
    if (c == 0x3C2) { return 0x3A3; }     /* final sigma -> SIGMA */
    if ((c >= 0x3B1 && c <= 0x3C1) || (c >= 0x3C3 && c <= 0x3CB)) {
        return c - 0x20;
    }
    if (c == 0x3CC) { return 0x38C; }
    if (c >= 0x3CD && c <= 0x3CE) { return c - 0x3F; }
    /* Cyrillic */
    if (c >= 0x430 && c <= 0x44F) { return c - 0x20; }
    if (c >= 0x450 && c <= 0x45F) { return c - 0x50; }
    if (c >= 0x460 && c <= 0x481) { return (c & 1) ? c - 1 : c; }
    if (c == 0x4CF) { return 0x4C0; }
    if (c >= 0x48A && c <= 0x4BF) { return (c & 1) ? c - 1 : c; }
    if (c >= 0x4C1 && c <= 0x4CE) { return (c & 1) ? c : c - 1; }
    if (c >= 0x4D0 && c <= 0x527) { return (c & 1) ? c - 1 : c; }
    /*
     * U+0528..U+052F (Cyrillic Extended) only reached mbstring's tables
     * after PHP 5.6.  Oracle: php5.6 leaves them unchanged, php7.0 maps them.
     */
    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
    if (c >= 0x528 && c <= 0x52F && mb_version() >= 70000) {
        return (c & 1) ? c - 1 : c;
    }
    /* Halfwidth and Fullwidth Forms */
    if (c >= 0xFF41 && c <= 0xFF5A) { return c - 0x20; }
    return c;
}

static unsigned int mb_tolower_cp(unsigned int c)
{
    if (c < 0x80) {
        return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    }
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) {
        return c + 0x20;
    }
    if (c == 0x178) {                     /* U+0178 -> y with diaeresis */
        return 0xFF;
    }
    if (c >= 0x100 && c <= 0x137) {
        if (c == 0x130) {                 /* handled by the full mapping */
            return 'i';
        }
        if (c == 0x131) {
            return c;
        }
        return (c & 1) ? c : c + 1;
    }
    if (c >= 0x139 && c <= 0x148) {
        return (c & 1) ? c + 1 : c;
    }
    if (c >= 0x14A && c <= 0x177) {
        return (c & 1) ? c : c + 1;
    }
    if (c >= 0x179 && c <= 0x17E) {
        return (c & 1) ? c + 1 : c;
    }
    /* Greek */
    if (c == 0x386) { return 0x3AC; }
    if (c >= 0x388 && c <= 0x38A) { return c + 0x25; }
    if (c == 0x38C) { return 0x3CC; }
    if (c >= 0x38E && c <= 0x38F) { return c + 0x3F; }
    if (c >= 0x391 && c <= 0x3A1) { return c + 0x20; }
    if (c >= 0x3A3 && c <= 0x3AB) { return c + 0x20; }
    /* Cyrillic */
    if (c >= 0x400 && c <= 0x40F) { return c + 0x50; }
    if (c >= 0x410 && c <= 0x42F) { return c + 0x20; }
    if (c >= 0x460 && c <= 0x481) { return (c & 1) ? c : c + 1; }
    if (c == 0x4C0) { return 0x4CF; }
    if (c >= 0x48A && c <= 0x4BF) { return (c & 1) ? c : c + 1; }
    if (c >= 0x4C1 && c <= 0x4CE) { return (c & 1) ? c + 1 : c; }
    if (c >= 0x4D0 && c <= 0x527) { return (c & 1) ? c : c + 1; }
    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
    if (c >= 0x528 && c <= 0x52F && mb_version() >= 70000) {
        return (c & 1) ? c : c + 1;
    }
    if (c >= 0xFF21 && c <= 0xFF3A) { return c + 0x20; }
    return c;
}

/*
 * Full (1:N) uppercase mappings.  Returns the number of code points written
 * to `out`, or 0 when the simple mapping applies.
 */
static int mb_case_full_upper(unsigned int c, unsigned int *out)
{
    switch (c) {
        case 0xDF:                          /* sharp s -> SS */
            out[0] = 'S'; out[1] = 'S';
            return 2;
        case 0x149:                         /* n preceded by apostrophe */
            out[0] = 0x2BC; out[1] = 'N';
            return 2;
        default:
            return 0;
    }
}

/* Full (1:N) lowercase mappings. */
static int mb_case_full_lower(unsigned int c, unsigned int *out)
{
    if (c == 0x130) {                       /* I with dot above -> i + U+0307 */
        out[0] = 'i'; out[1] = 0x307;
        return 2;
    }
    return 0;
}

/*
 * Full (1:N) *title* mappings, which differ from the uppercase ones: the
 * sharp s title-cases to "Ss" but uppercases to "SS".  Oracle:
 *   php7.4 -r 'echo mb_convert_case("\xC3\x9F", MB_CASE_TITLE);' => Ss
 *   php7.4 -r 'echo mb_strtoupper("\xC3\x9F");'                  => SS
 */
static int mb_case_full_title(unsigned int c, unsigned int *out)
{
    switch (c) {
        case 0xDF:
            out[0] = 'S'; out[1] = 's';
            return 2;
        case 0x149:
            out[0] = 0x2BC; out[1] = 'N';
            return 2;
        default:
            return 0;
    }
}

/*
 * Full (1:N) case *folding*.  Folding is lowercase-like but maps the sharp s
 * to "ss", which is what distinguishes MB_CASE_FOLD from MB_CASE_LOWER.
 * Oracle (php8.0):
 *   mb_convert_case("spa\xC3\x9F", MB_CASE_FOLD)        => spass
 *   mb_convert_case("spa\xC3\x9F", MB_CASE_FOLD_SIMPLE) => spa + sharp s
 *   mb_convert_case("spa\xC3\x9F", MB_CASE_LOWER)       => spa + sharp s
 */
static int mb_case_full_fold(unsigned int c, unsigned int *out)
{
    if (c == 0xDF) {
        out[0] = 's'; out[1] = 's';
        return 2;
    }
    return mb_case_full_lower(c, out);
}

/*
 * Unicode's Cased property.  Most cased characters have a simple mapping,
 * but a handful do not (the sharp s, kra, the apostrophe-n ligature, the
 * ordinal indicators, the Greek iota/upsilon with tonos and diaeresis), and
 * they still keep a title-cased word going.
 */
static int mb_is_cased(unsigned int c)
{
    switch (c) {
        case 0xAA: case 0xBA: case 0xDF:
        case 0x138: case 0x149:
        case 0x390: case 0x3B0:
            return 1;
        default:
            break;
    }
    return mb_toupper_cp(c) != c || mb_tolower_cp(c) != c;
}

/*
 * The pre-7.3 title-case word test.  php_unicode_convert_case() asked
 * php_unicode_is_prop(c, UC_MN|UC_ME|UC_CF|UC_LM|UC_SK|UC_LU|UC_LL|UC_LT|
 * UC_PO|UC_OS): a character in one of those general categories both starts
 * and continues a word, which is why "other punctuation" such as '"' and
 * "'" behaved differently from a dash or an underscore.  Oracle:
 *   php7.2 -r 'echo mb_convert_case("a\"b", MB_CASE_TITLE);'  => A"b
 *   php7.4 -r 'echo mb_convert_case("a\"b", MB_CASE_TITLE);'  => A"B
 */
static int mb_title_prop_pre73(unsigned int c)
{
    if (mb_is_cased(c)) {                   /* Lu | Ll | Lt */
        return 1;
    }
    switch (c) {
        /* Po */
        case 0x21: case 0x22: case 0x23: case 0x25: case 0x26: case 0x27:
        case 0x2A: case 0x2C: case 0x2E: case 0x2F: case 0x3A: case 0x3B:
        case 0x3F: case 0x40: case 0x5C:
        case 0xA1: case 0xA7: case 0xB6: case 0xB7: case 0xBF:
        case 0x37E: case 0x387:
        /* Sk */
        case 0x5E: case 0x60: case 0xA8: case 0xAF: case 0xB4: case 0xB8:
        case 0x375: case 0x384: case 0x385:
        /* Cf */
        case 0xAD:
        /* Lm */
        case 0x37A: case 0x559: case 0x640:
            return 1;
        default:
            break;
    }
    if (c >= 0x2B0 && c <= 0x2FF) { return 1; }    /* Lm and Sk block */
    if (c >= 0x300 && c <= 0x36F) { return 1; }    /* Mn */
    if (c >= 0x483 && c <= 0x489) { return 1; }    /* Mn and Me */
    if (c >= 0x200B && c <= 0x200F) { return 1; }  /* Cf */
    if (c >= 0x202A && c <= 0x202E) { return 1; }  /* Cf */
    if (c >= 0x2060 && c <= 0x2064) { return 1; }  /* Cf */
    return 0;
}

/*
 * A practical subset of Unicode's Case_Ignorable: the Word_Break
 * MidLetter / MidNumLet / Single_Quote characters plus combining marks,
 * modifier letters and modifier symbols.  These do not end a title-cased
 * word, which is why mb_convert_case("o'neil", MB_CASE_TITLE) is "O'neil"
 * and not "O'Neil".
 */
static int mb_is_case_ignorable(unsigned int c)
{
    switch (c) {
        case 0x27:      /* APOSTROPHE */
        case 0x2E:      /* FULL STOP */
        case 0x3A:      /* COLON */
        case 0x5E: case 0x60:
        case 0xA8: case 0xAD: case 0xAF: case 0xB4: case 0xB7: case 0xB8:
        case 0x2018: case 0x2019: case 0x2024: case 0x2027:
        case 0xFE52: case 0xFE55: case 0xFF07: case 0xFF0E:
            return 1;
        default:
            break;
    }
    if (c >= 0x2B0 && c <= 0x36F) {         /* modifier letters + combining */
        return 1;
    }
    if (c >= 0x200B && c <= 0x200F) {
        return 1;
    }
    if (c >= 0x2060 && c <= 0x2064) {
        return 1;
    }
    return 0;
}

/*
 * The workhorse behind mb_strtoupper/mb_strtolower/mb_convert_case.
 * `mode` is one of MB_CASE_*; the *_SIMPLE variants suppress the 1:N
 * mappings.
 */
static zend_string *mb_convert_case_str(const char *s, size_t len, int mode)
{
    mb_buf b = { NULL, 0, 0, 0 };
    size_t i = 0;
    int title_mode = 0;
    int prev_cased = 0;
    int simple = (mode >= MB_CASE_UPPER_SIMPLE);
    int base = simple ? mode - MB_CASE_UPPER_SIMPLE : mode;
    int use_full;

    /*
     * PHP 7.3 replaced mbstring's simple case tables with the full Unicode
     * mappings, so "sharp s" started uppercasing to "SS".  Oracle:
     *   php7.2 -r 'echo mb_strtoupper("gr\xC3\xBC\xC3\x9Fe");' => GRUESSE? no:
     *   php7.2 -r 'echo mb_strtoupper("\xC3\x9F");' => the sharp s unchanged
     *   php7.3 -r 'echo mb_strtoupper("\xC3\x9F");' => SS
     *   php7.2 -r 'echo mb_strtolower("\xC4\xB0");' => i
     *   php7.3 -r 'echo mb_strtolower("\xC4\xB0");' => i + U+0307
     */

        use_full = 0;


    while (i < len) {
        size_t used;
        int cp = mb_u8_decode((const unsigned char *) s + i, len - i, &used);
        unsigned int full[4];
        int n = 0, k;
        unsigned int mapped;
        int want_upper;

        if (cp < 0) {
            /* mbstring substitutes '?' for malformed input */
            mbb_char(&b, '?');
            i += used;
            title_mode = 0;
            prev_cased = 0;
            continue;
        }
        i += used;

        if (base == MB_CASE_TITLE) {

                if (!mb_title_prop_pre73((unsigned int) cp)) {
                    mb_u8_encode(&b, (unsigned int) cp);
                    title_mode = 0;
                    continue;
                }

            want_upper = !title_mode;
            title_mode = 1;
        } else if (base == MB_CASE_UPPER) {
            want_upper = 1;
        } else {
            /* MB_CASE_LOWER and MB_CASE_FOLD both fold towards lowercase */
            want_upper = 0;
        }

        if (use_full) {
            if (base == MB_CASE_FOLD) {
                n = mb_case_full_fold((unsigned int) cp, full);
            } else if (!want_upper) {
                n = mb_case_full_lower((unsigned int) cp, full);
            } else if (base == MB_CASE_TITLE) {
                n = mb_case_full_title((unsigned int) cp, full);
            } else {
                n = mb_case_full_upper((unsigned int) cp, full);
            }
        }
        if (n > 0) {
            for (k = 0; k < n; k++) {
                mb_u8_encode(&b, full[k]);
            }
            prev_cased = mb_is_cased((unsigned int) cp);
            continue;
        }

        mapped = want_upper ? mb_toupper_cp((unsigned int) cp)
                            : mb_tolower_cp((unsigned int) cp);

        /*
         * PHP 8.3 taught mb_strtolower() the final-sigma rule: a capital
         * sigma at the end of a word lowercases to U+03C2 rather than
         * U+03C3.  Oracle (two capital sigmas):
         *   php8.2 -r 'echo mb_strtolower("\xCE\xA3\xCE\xA3");' => sigma sigma
         *   php8.3 -r 'echo mb_strtolower("\xCE\xA3\xCE\xA3");' => sigma final
         */


        if (!mb_is_case_ignorable((unsigned int) cp)) {
            prev_cased = mb_is_cased((unsigned int) cp);
        }
        mb_u8_encode(&b, mapped);
    }

    return mbb_finish(&b);
}

/* ------------------------------------------------------------------ */
/* East Asian Width                                                     */
/* ------------------------------------------------------------------ */
typedef struct { int begin, end; } mb_eaw_range;

/* php-src 5.6 .. 8.0: ext/mbstring/libmbfl/mbfl/eaw_table.h */
static const mb_eaw_range mb_eaw_table_legacy[] = {
    { 0x1100, 0x115f }, { 0x11a3, 0x11a7 }, { 0x11fa, 0x11ff },
    { 0x2329, 0x232a }, { 0x2e80, 0x2e99 }, { 0x2e9b, 0x2ef3 },
    { 0x2f00, 0x2fd5 }, { 0x2ff0, 0x2ffb }, { 0x3000, 0x303e },
    { 0x3041, 0x3096 }, { 0x3099, 0x30ff }, { 0x3105, 0x312d },
    { 0x3131, 0x318e }, { 0x3190, 0x31ba }, { 0x31c0, 0x31e3 },
    { 0x31f0, 0x321e }, { 0x3220, 0x3247 }, { 0x3250, 0x32fe },
    { 0x3300, 0x4dbf }, { 0x4e00, 0xa48c }, { 0xa490, 0xa4c6 },
    { 0xa960, 0xa97c }, { 0xac00, 0xd7a3 }, { 0xd7b0, 0xd7c6 },
    { 0xd7cb, 0xd7fb }, { 0xf900, 0xfaff }, { 0xfe10, 0xfe19 },
    { 0xfe30, 0xfe52 }, { 0xfe54, 0xfe66 }, { 0xfe68, 0xfe6b },
    { 0xff01, 0xff60 }, { 0xffe0, 0xffe6 }, { 0x1b000, 0x1b001 },
    { 0x1f200, 0x1f202 }, { 0x1f210, 0x1f23a }, { 0x1f240, 0x1f248 },
    { 0x1f250, 0x1f251 }, { 0x20000, 0x2fffd }, { 0x30000, 0x3fffd }
};

/* php-src 8.1+: the regenerated table, which finally makes emoji wide */
static const mb_eaw_range mb_eaw_table_modern[] = {
    { 0x1100, 0x115f }, { 0x231a, 0x231b }, { 0x2329, 0x232a },
    { 0x23e9, 0x23ec }, { 0x23f0, 0x23f0 }, { 0x23f3, 0x23f3 },
    { 0x25fd, 0x25fe }, { 0x2614, 0x2615 }, { 0x2648, 0x2653 },
    { 0x267f, 0x267f }, { 0x2693, 0x2693 }, { 0x26a1, 0x26a1 },
    { 0x26aa, 0x26ab }, { 0x26bd, 0x26be }, { 0x26c4, 0x26c5 },
    { 0x26ce, 0x26ce }, { 0x26d4, 0x26d4 }, { 0x26ea, 0x26ea },
    { 0x26f2, 0x26f3 }, { 0x26f5, 0x26f5 }, { 0x26fa, 0x26fa },
    { 0x26fd, 0x26fd }, { 0x2705, 0x2705 }, { 0x270a, 0x270b },
    { 0x2728, 0x2728 }, { 0x274c, 0x274c }, { 0x274e, 0x274e },
    { 0x2753, 0x2755 }, { 0x2757, 0x2757 }, { 0x2795, 0x2797 },
    { 0x27b0, 0x27b0 }, { 0x27bf, 0x27bf }, { 0x2b1b, 0x2b1c },
    { 0x2b50, 0x2b50 }, { 0x2b55, 0x2b55 }, { 0x2e80, 0x2e99 },
    { 0x2e9b, 0x2ef3 }, { 0x2f00, 0x2fd5 }, { 0x2ff0, 0x2ffb },
    { 0x3000, 0x303e }, { 0x3041, 0x3096 }, { 0x3099, 0x30ff },
    { 0x3105, 0x312f }, { 0x3131, 0x318e }, { 0x3190, 0x31e3 },
    { 0x31f0, 0x321e }, { 0x3220, 0x3247 }, { 0x3250, 0x4dbf },
    { 0x4e00, 0xa48c }, { 0xa490, 0xa4c6 }, { 0xa960, 0xa97c },
    { 0xac00, 0xd7a3 }, { 0xf900, 0xfaff }, { 0xfe10, 0xfe19 },
    { 0xfe30, 0xfe52 }, { 0xfe54, 0xfe66 }, { 0xfe68, 0xfe6b },
    { 0xff01, 0xff60 }, { 0xffe0, 0xffe6 }, { 0x16fe0, 0x16fe4 },
    { 0x16ff0, 0x16ff1 }, { 0x17000, 0x187f7 }, { 0x18800, 0x18cd5 },
    { 0x18d00, 0x18d08 }, { 0x1aff0, 0x1aff3 }, { 0x1aff5, 0x1affb },
    { 0x1affd, 0x1affe }, { 0x1b000, 0x1b122 }, { 0x1b150, 0x1b152 },
    { 0x1b164, 0x1b167 }, { 0x1b170, 0x1b2fb }, { 0x1f004, 0x1f004 },
    { 0x1f0cf, 0x1f0cf }, { 0x1f18e, 0x1f18e }, { 0x1f191, 0x1f19a },
    { 0x1f200, 0x1f202 }, { 0x1f210, 0x1f23b }, { 0x1f240, 0x1f248 },
    { 0x1f250, 0x1f251 }, { 0x1f260, 0x1f265 }, { 0x1f300, 0x1f320 },
    { 0x1f32d, 0x1f335 }, { 0x1f337, 0x1f37c }, { 0x1f37e, 0x1f393 },
    { 0x1f3a0, 0x1f3ca }, { 0x1f3cf, 0x1f3d3 }, { 0x1f3e0, 0x1f3f0 },
    { 0x1f3f4, 0x1f3f4 }, { 0x1f3f8, 0x1f43e }, { 0x1f440, 0x1f440 },
    { 0x1f442, 0x1f4fc }, { 0x1f4ff, 0x1f53d }, { 0x1f54b, 0x1f54e },
    { 0x1f550, 0x1f567 }, { 0x1f57a, 0x1f57a }, { 0x1f595, 0x1f596 },
    { 0x1f5a4, 0x1f5a4 }, { 0x1f5fb, 0x1f64f }, { 0x1f680, 0x1f6c5 },
    { 0x1f6cc, 0x1f6cc }, { 0x1f6d0, 0x1f6d2 }, { 0x1f6d5, 0x1f6d7 },
    { 0x1f6dd, 0x1f6df }, { 0x1f6eb, 0x1f6ec }, { 0x1f6f4, 0x1f6fc },
    { 0x1f7e0, 0x1f7eb }, { 0x1f7f0, 0x1f7f0 }, { 0x1f90c, 0x1f93a },
    { 0x1f93c, 0x1f945 }, { 0x1f947, 0x1f9ff }, { 0x1fa70, 0x1fa74 },
    { 0x1fa78, 0x1fa7c }, { 0x1fa80, 0x1fa86 }, { 0x1fa90, 0x1faac },
    { 0x1fab0, 0x1faba }, { 0x1fac0, 0x1fac5 }, { 0x1fad0, 0x1fad9 },
    { 0x1fae0, 0x1fae7 }, { 0x1faf0, 0x1faf6 }, { 0x20000, 0x2fffd },
    { 0x30000, 0x3fffd }
};

static int mb_is_fullwidth(int cp)
{
    const mb_eaw_range *tbl;
    size_t n, i;

    if (cp < 0x1100) {
        return 0;
    }
    /*
     * The East Asian Width table was regenerated for PHP 8.1, which is when
     * emoji became double width.  Oracle, mb_strwidth of U+1F600:
     *   php8.0 => 1      php8.1 => 2
     */

        tbl = mb_eaw_table_legacy;
        n = sizeof mb_eaw_table_legacy / sizeof mb_eaw_table_legacy[0];

    for (i = 0; i < n; i++) {
        if (cp >= tbl[i].begin && cp <= tbl[i].end) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* encoding names                                                       */
/* ------------------------------------------------------------------ */
/*
 * mbstring recognises a fixed set of encoding names and warns about anything
 * else.  Only UTF-8 and ASCII are really processed here, but the whole list
 * has to be *recognised*, because passing a name such as "EUC-JP" or
 * "HTML-ENTITIES" is not an error and a spurious "Unknown encoding" warning
 * breaks every test that does so.  The list is php 5.6's
 * mb_list_encodings() output, plus the aliases the tests use.
 */
typedef enum {
    MB_ENC_UTF8 = 0,      /* real UTF-8 handling                         */
    MB_ENC_ASCII,         /* 7-bit: every byte must be < 0x80            */
    MB_ENC_HTML,          /* HTML-ENTITIES: 7-bit and entity free        */
    MB_ENC_SINGLE,        /* single byte: every byte sequence is valid   */
    MB_ENC_OTHER          /* recognised, processed as UTF-8 (best effort)*/
} mb_enc_kind;

typedef struct {
    const char *name;     /* canonical spelling, as php-src reports it   */
    unsigned char kind;
    unsigned char alias;  /* 1 = accepted spelling, not listed by
                             mb_list_encodings()                         */
} mb_encoding;

static const mb_encoding mb_encodings[] = {
    { "UTF-8",        MB_ENC_UTF8, 0 }, { "UTF8",         MB_ENC_UTF8,   1 },
    { "ASCII",        MB_ENC_ASCII, 0 }, { "US-ASCII",     MB_ENC_ASCII,  1 },
    { "7bit",         MB_ENC_ASCII, 0 }, { "HTML-ENTITIES", MB_ENC_HTML, 0 },
    { "HTML",         MB_ENC_HTML,   1 }, { "8bit",         MB_ENC_SINGLE, 0 },
    { "pass",         MB_ENC_SINGLE, 0 }, { "auto",         MB_ENC_OTHER, 0 },
    { "wchar",        MB_ENC_OTHER, 0 }, { "byte2be",      MB_ENC_OTHER, 0 },
    { "byte2le",      MB_ENC_OTHER, 0 }, { "byte4be",      MB_ENC_OTHER, 0 },
    { "byte4le",      MB_ENC_OTHER, 0 }, { "BASE64",       MB_ENC_OTHER, 0 },
    { "UUENCODE",     MB_ENC_OTHER, 0 }, { "Quoted-Printable", MB_ENC_OTHER, 0 },
    { "UCS-4",        MB_ENC_OTHER, 0 }, { "UCS-4BE",      MB_ENC_OTHER, 0 },
    { "UCS-4LE",      MB_ENC_OTHER, 0 }, { "UCS-2",        MB_ENC_OTHER, 0 },
    { "UCS-2BE",      MB_ENC_OTHER, 0 }, { "UCS-2LE",      MB_ENC_OTHER, 0 },
    { "UTF-32",       MB_ENC_OTHER, 0 }, { "UTF-32BE",     MB_ENC_OTHER, 0 },
    { "UTF-32LE",     MB_ENC_OTHER, 0 }, { "UTF-16",       MB_ENC_OTHER, 0 },
    { "UTF-16BE",     MB_ENC_OTHER, 0 }, { "UTF-16LE",     MB_ENC_OTHER, 0 },
    { "UTF-7",        MB_ENC_OTHER, 0 }, { "UTF7-IMAP",    MB_ENC_OTHER, 0 },
    { "EUC-JP",       MB_ENC_OTHER, 0 }, { "SJIS",         MB_ENC_OTHER, 0 },
    { "Shift_JIS",    MB_ENC_OTHER,  1 }, { "eucJP-win",    MB_ENC_OTHER, 0 },
    { "EUC-JP-2004",  MB_ENC_OTHER, 0 }, { "SJIS-win",     MB_ENC_OTHER, 0 },
    { "SJIS-mac",     MB_ENC_OTHER, 0 }, { "SJIS-2004",    MB_ENC_OTHER, 0 },
    { "CP932",        MB_ENC_OTHER, 0 }, { "CP51932",      MB_ENC_OTHER, 0 },
    { "JIS",          MB_ENC_OTHER, 0 }, { "JIS-ms",       MB_ENC_OTHER, 0 },
    { "ISO-2022-JP",  MB_ENC_OTHER, 0 }, { "ISO-2022-JP-MS", MB_ENC_OTHER, 0 },
    { "ISO-2022-JP-2004", MB_ENC_OTHER, 0 }, { "GB18030",   MB_ENC_OTHER, 0 },
    { "CP50220",      MB_ENC_OTHER, 0 }, { "CP50220raw",   MB_ENC_OTHER, 0 },
    { "CP50221",      MB_ENC_OTHER, 0 }, { "CP50222",      MB_ENC_OTHER, 0 },
    { "Windows-1251", MB_ENC_SINGLE, 0 }, { "Windows-1252", MB_ENC_SINGLE, 0 },
    { "Windows-1254", MB_ENC_SINGLE, 0 }, { "CP1251",       MB_ENC_SINGLE, 1 },
    { "CP1252",       MB_ENC_SINGLE, 1 }, { "CP1254",       MB_ENC_SINGLE, 1 },
    { "ISO-8859-1",   MB_ENC_SINGLE, 0 }, { "ISO-8859-2",   MB_ENC_SINGLE, 0 },
    { "ISO-8859-3",   MB_ENC_SINGLE, 0 }, { "ISO-8859-4",   MB_ENC_SINGLE, 0 },
    { "ISO-8859-5",   MB_ENC_SINGLE, 0 }, { "ISO-8859-6",   MB_ENC_SINGLE, 0 },
    { "ISO-8859-7",   MB_ENC_SINGLE, 0 }, { "ISO-8859-8",   MB_ENC_SINGLE, 0 },
    { "ISO-8859-9",   MB_ENC_SINGLE, 0 }, { "ISO-8859-10",  MB_ENC_SINGLE, 0 },
    { "ISO-8859-13",  MB_ENC_SINGLE, 0 }, { "ISO-8859-14",  MB_ENC_SINGLE, 0 },
    { "ISO-8859-15",  MB_ENC_SINGLE, 0 }, { "ISO-8859-16",  MB_ENC_SINGLE, 0 },
    { "EUC-CN",       MB_ENC_OTHER, 0 }, { "CP936",        MB_ENC_OTHER, 0 },
    { "HZ",           MB_ENC_OTHER, 0 }, { "EUC-TW",       MB_ENC_OTHER, 0 },
    { "BIG-5",        MB_ENC_OTHER, 0 }, { "BIG5",         MB_ENC_OTHER,  1 },
    { "CP950",        MB_ENC_OTHER, 0 }, { "EUC-KR",       MB_ENC_OTHER, 0 },
    { "UHC",          MB_ENC_OTHER, 0 }, { "ISO-2022-KR",  MB_ENC_OTHER, 0 },
    { "CP866",        MB_ENC_SINGLE, 0 }, { "CP850",        MB_ENC_SINGLE, 0 },
    { "KOI8-R",       MB_ENC_SINGLE, 0 }, { "KOI8-U",       MB_ENC_SINGLE, 0 },
    { "ArmSCII-8",    MB_ENC_SINGLE, 0 },
    { "SJIS-Mobile#DOCOMO",   MB_ENC_OTHER, 0 },
    { "SJIS-Mobile#KDDI",     MB_ENC_OTHER, 0 },
    { "SJIS-Mobile#SOFTBANK", MB_ENC_OTHER, 0 },
    { "UTF-8-Mobile#DOCOMO",  MB_ENC_OTHER, 0 },
    { "UTF-8-Mobile#KDDI-A",  MB_ENC_OTHER, 0 },
    { "UTF-8-Mobile#KDDI-B",  MB_ENC_OTHER, 0 },
    { "UTF-8-Mobile#SOFTBANK", MB_ENC_OTHER, 0 },
    { "ISO-2022-JP-MOBILE#KDDI", MB_ENC_OTHER, 0 },
    { "GB18030-2022", MB_ENC_OTHER, 0 }
};

/*
 * A handful of encoding names come and go, and a name can stop being *listed*
 * by mb_list_encodings() while still being accepted.  Oracle:
 *   "auto"         accepted through 7.3, listed through 7.3
 *   "pass"         accepted through 7.4, listed through 7.4
 *   "wchar"        accepted through 8.0, listed through 8.0
 *   "byte2be"/"byte2le"/"byte4be"/"byte4le"/"JIS-ms"/"CP50220raw"
 *                  still accepted at 8.4, but delisted in 8.1
 *   "GB18030-2022" new in 8.4
 * php8.0 -r 'mb_internal_encoding("auto");'
 *     => ValueError: ... must be a valid encoding, "auto" given
 * php8.1 -r 'var_dump(in_array("wchar", mb_list_encodings()));' => bool(false)
 */
typedef struct {
    const char *name;
    long valid_since, valid_until;   /* 0 = no bound */
    long list_since,  list_until;
} mb_enc_window;

static const mb_enc_window mb_enc_windows[] = {
    { "auto",         0, 70400, 0,     70400 },
    { "pass",         0, 80000, 0,     80000 },
    { "wchar",        0, 80100, 0,     80100 },
    { "byte2be",      0,     0, 0,     80100 },
    { "byte2le",      0,     0, 0,     80100 },
    { "byte4be",      0,     0, 0,     80100 },
    { "byte4le",      0,     0, 0,     80100 },
    { "JIS-ms",       0,     0, 0,     80100 },
    { "CP50220raw",   0,     0, 0,     80100 },
    { "GB18030-2022", 80400, 0, 80400, 0     }
};

static const mb_enc_window *mb_enc_window_for(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof mb_enc_windows / sizeof mb_enc_windows[0]; i++) {
        if (strcmp(mb_enc_windows[i].name, name) == 0) {
            return &mb_enc_windows[i];
        }
    }
    return NULL;
}

/* VERSIONGATE:70400 else-branch is the 5.6 behaviour */
static int mb_enc_valid_now(const mb_encoding *e)
{
    const mb_enc_window *w = mb_enc_window_for(e->name);
    long v = mb_version();

    if (!w) {
        return 1;
    }
    if (w->valid_since && v < w->valid_since) {
        return 0;
    }
    if (w->valid_until && v >= w->valid_until) {
        return 0;
    }
    return 1;
}

/* VERSIONGATE:80100 else-branch is the 5.6 behaviour */
static int mb_enc_listed_now(const mb_encoding *e)
{
    const mb_enc_window *w;
    long v = mb_version();

    if (e->alias) {
        return 0;
    }
    w = mb_enc_window_for(e->name);
    if (!w) {
        return 1;
    }
    if (w->list_since && v < w->list_since) {
        return 0;
    }
    if (w->list_until && v >= w->list_until) {
        return 0;
    }
    return 1;
}

/* Uppercase and drop '-' and '_' so name matching is as loose as mbfl's. */
static void mb_normalize_name(const char *s, char *out, size_t outlen)
{
    size_t n = 0;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    for (; *s && n + 1 < outlen; s++) {
        char c = *s;
        if (c == '-' || c == '_' || c == ' ') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char) (c - 32);
        }
        out[n++] = c;
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
}

static const mb_encoding *mb_lookup_encoding(const char *name)
{
    char want[96], have[96];
    size_t i;

    if (!name || !*name) {
        return NULL;
    }
    mb_normalize_name(name, want, sizeof want);
    for (i = 0; i < sizeof mb_encodings / sizeof mb_encodings[0]; i++) {
        mb_normalize_name(mb_encodings[i].name, have, sizeof have);
        if (strcmp(want, have) == 0) {
            return mb_enc_valid_now(&mb_encodings[i]) ? &mb_encodings[i] : NULL;
        }
    }
    return NULL;
}

static int mb_is_ascii_name(const char *n)
{
    const mb_encoding *e = mb_lookup_encoding(n);
    return e && e->kind == MB_ENC_ASCII;
}

static int mb_is_known_name(const char *n)
{
    return mb_lookup_encoding(n) != NULL;
}

/* The mbstring.internal_encoding ini entry seeds the internal encoding. */
static const char *mb_current_internal(void)
{
    if (mb_internal_enc[0] == '\0') {
        const mb_encoding *e = NULL;
        zval *v = EG(ini) ? zend_hash_str_find(EG(ini),
                    "mbstring.internal_encoding",
                    sizeof("mbstring.internal_encoding") - 1) : NULL;

        if (v && Z_TYPE_P(v) == IS_STRING && Z_STR_P(v)) {
            e = mb_lookup_encoding(ZSTR_VAL(Z_STR_P(v)));
        }
        snprintf(mb_internal_enc, sizeof mb_internal_enc, "%s",
                 e ? e->name : "UTF-8");
    }
    return mb_internal_enc;
}

/* ------------------------------------------------------------------ */
/* argument helpers                                                     */
/* ------------------------------------------------------------------ */
static zval *mb_arg(uint32_t argc, zval *argv, uint32_t i)
{
    if (i >= argc) {
        return NULL;
    }
    return zval_deref(&argv[i]);
}

static int mb_arg_given(uint32_t argc, zval *argv, uint32_t i)
{
    zval *z = mb_arg(argc, argv, i);
    return z && Z_TYPE_P(z) != IS_NULL && Z_TYPE_P(z) != IS_UNDEF;
}

static zend_string *mb_arg_str(uint32_t argc, zval *argv, uint32_t i)
{
    zval *z = mb_arg(argc, argv, i);
    return z ? zval_get_string(z) : zend_string_initz("");
}

static zend_long mb_arg_long(uint32_t argc, zval *argv, uint32_t i,
                             zend_long dflt)
{
    zval *z = mb_arg(argc, argv, i);

    if (!z || Z_TYPE_P(z) == IS_NULL || Z_TYPE_P(z) == IS_UNDEF) {
        return dflt;
    }
    return zval_get_long(z);
}

/*
 * Returns 0 when the encoding argument names an encoding mbstring knows.
 * Anything outside the table is the "Unknown encoding" warning php-src
 * raises; a known-but-unimplemented encoding is accepted and then handled on
 * a best-effort basis.
 */
static int mb_check_enc_arg(uint32_t argc, zval *argv, uint32_t i,
                            const char *fname)
{
    zend_string *enc;

    if (!mb_arg_given(argc, argv, i)) {
        return 0;
    }
    enc = mb_arg_str(argc, argv, i);
    if (mb_is_known_name(ZSTR_VAL(enc))) {
        return 0;
    }
    zend_error(E_WARNING, "%s(): Unknown encoding \"%s\"", fname,
               ZSTR_VAL(enc));
    return -1;
}

/* Validity of `s` when read as `kind`. */
static int mb_valid_in(const char *s, size_t len, unsigned char kind)
{
    size_t i;

    switch (kind) {
        case MB_ENC_UTF8:
            return mb_u8_valid(s, len);

        case MB_ENC_ASCII:
            for (i = 0; i < len; i++) {
                if ((unsigned char) s[i] >= 0x80) {
                    return 0;
                }
            }
            return 1;

        case MB_ENC_HTML:
            /*
             * HTML-ENTITIES is 7-bit, and mbstring's round-trip check also
             * rejects text that already contains an entity (decoding it
             * would change the string).  Oracle:
             *   mb_check_encoding("abc","HTML-ENTITIES")   => true
             *   mb_check_encoding("a&b","HTML-ENTITIES")   => true
             *   mb_check_encoding("&x;","HTML-ENTITIES")   => true
             *   mb_check_encoding("&amp;","HTML-ENTITIES") => false
             *   mb_check_encoding("&#65;","HTML-ENTITIES") => false
             */
            for (i = 0; i < len; i++) {
                if ((unsigned char) s[i] >= 0x80) {
                    return 0;
                }
            }
            for (i = 0; i + 1 < len; i++) {
                static const char *named[] = { "amp;", "lt;", "gt;", "quot;",
                                               "apos;", "nbsp;", NULL };
                size_t k;
                if (s[i] != '&') {
                    continue;
                }
                if (s[i + 1] == '#') {
                    size_t j = i + 2;
                    if (j < len && (s[j] == 'x' || s[j] == 'X')) {
                        j++;
                    }
                    while (j < len && ((s[j] >= '0' && s[j] <= '9') ||
                                       (s[j] >= 'a' && s[j] <= 'f') ||
                                       (s[j] >= 'A' && s[j] <= 'F'))) {
                        j++;
                    }
                    if (j > i + 2 && j < len && s[j] == ';') {
                        return 0;
                    }
                }
                for (k = 0; named[k]; k++) {
                    size_t nl = strlen(named[k]);
                    if (len - i - 1 >= nl &&
                            memcmp(s + i + 1, named[k], nl) == 0) {
                        return 0;
                    }
                }
            }
            return 1;

        case MB_ENC_SINGLE:
            return 1;                   /* every byte sequence is legal */

        default:
            return mb_u8_valid(s, len);
    }
}

/* ------------------------------------------------------------------ */
/* substring search (byte level; UTF-8 is self synchronising)           */
/* ------------------------------------------------------------------ */
static const char *mb_memmem(const char *h, size_t hlen, const char *n,
                             size_t nlen)
{
    size_t i;

    if (nlen == 0) {
        return h;
    }
    if (nlen > hlen) {
        return NULL;
    }
    for (i = 0; i + nlen <= hlen; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, nlen) == 0) {
            return h + i;
        }
    }
    return NULL;
}

static const char *mb_memrmem(const char *h, size_t hlen, const char *n,
                              size_t nlen)
{
    size_t i;

    if (nlen == 0) {
        return h + hlen;
    }
    if (nlen > hlen) {
        return NULL;
    }
    for (i = hlen - nlen + 1; i > 0; i--) {
        if (h[i - 1] == n[0] && memcmp(h + i - 1, n, nlen) == 0) {
            return h + i - 1;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* builtins                                                             */
/* ------------------------------------------------------------------ */
static void zif_mb_strlen(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *s;

    if (mb_check_enc_arg(argc, argv, 1, "mb_strlen") < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    s = mb_arg_str(argc, argv, 0);
    ZVAL_LONG(ret, (zend_long) mb_u8_strlen(ZSTR_VAL(s), ZSTR_LEN(s)));
}

static void zif_mb_substr(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *s;
    zend_long start, length;
    size_t nchars, from, to, boff, eoff;

    if (mb_check_enc_arg(argc, argv, 3, "mb_substr") < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    s = mb_arg_str(argc, argv, 0);
    start = mb_arg_long(argc, argv, 1, 0);
    nchars = mb_u8_strlen(ZSTR_VAL(s), ZSTR_LEN(s));

    if (start < 0) {
        start += (zend_long) nchars;
        if (start < 0) {
            start = 0;
        }
    }
    if ((size_t) start > nchars) {
        ZVAL_STR(ret, zend_string_initz(""));
        return;
    }
    from = (size_t) start;

    if (!mb_arg_given(argc, argv, 2)) {
        to = nchars;
    } else {
        length = mb_arg_long(argc, argv, 2, 0);
        if (length < 0) {
            zend_long end = (zend_long) nchars + length;
            to = (end < (zend_long) from) ? from : (size_t) end;
        } else if ((size_t) length > nchars - from) {
            to = nchars;
        } else {
            to = from + (size_t) length;
        }
    }

    boff = mb_u8_offset(ZSTR_VAL(s), ZSTR_LEN(s), from);
    eoff = mb_u8_offset(ZSTR_VAL(s), ZSTR_LEN(s), to);
    ZVAL_STR(ret, zend_string_init(ZSTR_VAL(s) + boff, eoff - boff));
}

/* shared by mb_strpos and mb_stripos */
static void mb_do_strpos(zval *ret, uint32_t argc, zval *argv, int ci,
                         const char *fname)
{
    zend_string *h, *n;
    zend_long offset;
    size_t hchars, start_off;
    const char *found;

    if (mb_check_enc_arg(argc, argv, 3, fname) < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    h = mb_arg_str(argc, argv, 0);
    n = mb_arg_str(argc, argv, 1);
    offset = mb_arg_long(argc, argv, 2, 0);

    if (ci) {
        h = mb_convert_case_str(ZSTR_VAL(h), ZSTR_LEN(h), MB_CASE_LOWER);
        n = mb_convert_case_str(ZSTR_VAL(n), ZSTR_LEN(n), MB_CASE_LOWER);
    }
    hchars = mb_u8_strlen(ZSTR_VAL(h), ZSTR_LEN(h));

    if (offset < 0) {
        /*
         * A negative offset counts back from the end of the haystack; before
         * PHP 7.1 it was simply rejected.  Oracle:
         *   php7.0 -r 'var_dump(mb_strpos("hello","l",-3));'
         *       => Warning: mb_strpos(): Offset not contained in string
         *          bool(false)
         *   php7.1 -r 'var_dump(mb_strpos("hello","l",-3));'  => int(2)
         */

            zend_error(E_WARNING, "%s(): Offset not contained in string",
                       fname);
            ZVAL_BOOL(ret, 0);
            return;

    }
    if ((size_t) offset > hchars) {
        zend_error(E_WARNING, "%s(): Offset not contained in string", fname);
        ZVAL_BOOL(ret, 0);
        return;
    }

    if (ZSTR_LEN(n) == 0) {
        /*
         * An empty needle was an error before PHP 8.0 and matches at the
         * offset from 8.0 on.  Oracle:
         *   php7.4 -r 'var_dump(mb_strpos("abc",""));' => bool(false)
         *   php8.0 -r 'var_dump(mb_strpos("abc",""));' => int(0)
         */

            zend_error(E_WARNING, "%s(): Empty delimiter", fname);
            ZVAL_BOOL(ret, 0);

        return;
    }

    start_off = mb_u8_offset(ZSTR_VAL(h), ZSTR_LEN(h), (size_t) offset);
    found = mb_memmem(ZSTR_VAL(h) + start_off, ZSTR_LEN(h) - start_off,
                      ZSTR_VAL(n), ZSTR_LEN(n));
    if (!found) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_LONG(ret, (zend_long) mb_u8_strlen(ZSTR_VAL(h),
                                            (size_t) (found - ZSTR_VAL(h))));
}

static void zif_mb_strpos(zval *ret, uint32_t argc, zval *argv)
{
    mb_do_strpos(ret, argc, argv, 0, "mb_strpos");
}

static void zif_mb_stripos(zval *ret, uint32_t argc, zval *argv)
{
    mb_do_strpos(ret, argc, argv, 1, "mb_stripos");
}

static void mb_do_strrpos(zval *ret, uint32_t argc, zval *argv, int ci,
                          const char *fname)
{
    zend_string *h, *n;
    zend_long offset;
    size_t hchars, lo_off, hi_off;
    const char *found;

    if (mb_check_enc_arg(argc, argv, 3, fname) < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    h = mb_arg_str(argc, argv, 0);
    n = mb_arg_str(argc, argv, 1);
    offset = mb_arg_long(argc, argv, 2, 0);
    if (ci) {
        h = mb_convert_case_str(ZSTR_VAL(h), ZSTR_LEN(h), MB_CASE_LOWER);
        n = mb_convert_case_str(ZSTR_VAL(n), ZSTR_LEN(n), MB_CASE_LOWER);
    }
    hchars = mb_u8_strlen(ZSTR_VAL(h), ZSTR_LEN(h));

    if (ZSTR_LEN(n) == 0) {

            zend_error(E_WARNING, "%s(): Empty delimiter", fname);
            ZVAL_BOOL(ret, 0);

        return;
    }

    /*
     * A non-negative offset starts the search there; a negative one caps how
     * far into the haystack a match may start.  Oracle:
     *   mb_strrpos("hello","l",3)  => 3
     *   mb_strrpos("hello","l",-2) => 3
     */
    if (offset >= 0) {
        if ((size_t) offset > hchars) {
            zend_error(E_WARNING, "%s(): Offset not contained in string", fname);
            ZVAL_BOOL(ret, 0);
            return;
        }
        lo_off = mb_u8_offset(ZSTR_VAL(h), ZSTR_LEN(h), (size_t) offset);
        hi_off = ZSTR_LEN(h);
    } else {
        zend_long cap = (zend_long) hchars + offset;
        if (cap < 0) {
            zend_error(E_WARNING, "%s(): Offset not contained in string", fname);
            ZVAL_BOOL(ret, 0);
            return;
        }
        lo_off = 0;
        hi_off = mb_u8_offset(ZSTR_VAL(h), ZSTR_LEN(h), (size_t) cap)
                 + ZSTR_LEN(n);
        if (hi_off > ZSTR_LEN(h)) {
            hi_off = ZSTR_LEN(h);
        }
    }

    if (hi_off < lo_off) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    found = mb_memrmem(ZSTR_VAL(h) + lo_off, hi_off - lo_off, ZSTR_VAL(n),
                       ZSTR_LEN(n));
    if (!found) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_LONG(ret, (zend_long) mb_u8_strlen(ZSTR_VAL(h),
                                            (size_t) (found - ZSTR_VAL(h))));
}

static void zif_mb_strrpos(zval *ret, uint32_t argc, zval *argv)
{
    mb_do_strrpos(ret, argc, argv, 0, "mb_strrpos");
}

static void zif_mb_strripos(zval *ret, uint32_t argc, zval *argv)
{
    mb_do_strrpos(ret, argc, argv, 1, "mb_strripos");
}

/*
 * mb_strstr / mb_stristr / mb_strrchr / mb_strrichr all return a slice of the
 * *original* haystack, so a case-insensitive search folds a copy and maps the
 * hit back by character index.
 */
static void mb_do_strstr(zval *ret, uint32_t argc, zval *argv, int ci, int last,
                         const char *fname)
{
    zend_string *h, *n, *sh, *sn;
    int before;
    const char *found;
    size_t cidx, boff;

    if (mb_check_enc_arg(argc, argv, 3, fname) < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    h = mb_arg_str(argc, argv, 0);
    n = mb_arg_str(argc, argv, 1);
    before = mb_arg_given(argc, argv, 2)
           ? zval_is_true(mb_arg(argc, argv, 2)) : 0;

    sh = h;
    sn = n;
    if (ci) {
        sh = mb_convert_case_str(ZSTR_VAL(h), ZSTR_LEN(h), MB_CASE_LOWER);
        sn = mb_convert_case_str(ZSTR_VAL(n), ZSTR_LEN(n), MB_CASE_LOWER);
    }

    if (ZSTR_LEN(sn) == 0) {
        /*
         * Oracle: php7.4 -r 'var_dump(mb_strstr("abcabc",""));'
         *     => Warning: mb_strstr(): Empty delimiter   bool(false)
         *   php8.4 same => string(6) "abcabc"   (and mb_strrchr gives "")
         */

    }

    found = last ? mb_memrmem(ZSTR_VAL(sh), ZSTR_LEN(sh), ZSTR_VAL(sn),
                              ZSTR_LEN(sn))
                 : mb_memmem(ZSTR_VAL(sh), ZSTR_LEN(sh), ZSTR_VAL(sn),
                             ZSTR_LEN(sn));
    if (!found) {
        ZVAL_BOOL(ret, 0);
        return;
    }

    cidx = mb_u8_strlen(ZSTR_VAL(sh), (size_t) (found - ZSTR_VAL(sh)));
    boff = mb_u8_offset(ZSTR_VAL(h), ZSTR_LEN(h), cidx);

    if (before) {
        ZVAL_STR(ret, zend_string_init(ZSTR_VAL(h), boff));
    } else {
        ZVAL_STR(ret, zend_string_init(ZSTR_VAL(h) + boff, ZSTR_LEN(h) - boff));
    }
}

static void zif_mb_strstr(zval *ret, uint32_t argc, zval *argv)
{
    mb_do_strstr(ret, argc, argv, 0, 0, "mb_strstr");
}

static void zif_mb_stristr(zval *ret, uint32_t argc, zval *argv)
{
    mb_do_strstr(ret, argc, argv, 1, 0, "mb_stristr");
}

static void zif_mb_strrchr(zval *ret, uint32_t argc, zval *argv)
{
    mb_do_strstr(ret, argc, argv, 0, 1, "mb_strrchr");
}

static void zif_mb_strrichr(zval *ret, uint32_t argc, zval *argv)
{
    mb_do_strstr(ret, argc, argv, 1, 1, "mb_strrichr");
}

static void zif_mb_strtolower(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *s;

    if (mb_check_enc_arg(argc, argv, 1, "mb_strtolower") < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    s = mb_arg_str(argc, argv, 0);
    ZVAL_STR(ret, mb_convert_case_str(ZSTR_VAL(s), ZSTR_LEN(s), MB_CASE_LOWER));
}

static void zif_mb_strtoupper(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *s;

    if (mb_check_enc_arg(argc, argv, 1, "mb_strtoupper") < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    s = mb_arg_str(argc, argv, 0);
    ZVAL_STR(ret, mb_convert_case_str(ZSTR_VAL(s), ZSTR_LEN(s), MB_CASE_UPPER));
}

static void zif_mb_convert_case(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *s;
    zend_long mode;

    if (mb_check_enc_arg(argc, argv, 2, "mb_convert_case") < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    s = mb_arg_str(argc, argv, 0);
    mode = mb_arg_long(argc, argv, 1, MB_CASE_UPPER);

    if (mode < MB_CASE_UPPER || mode > MB_CASE_FOLD_SIMPLE) {
        /*
         * Oracle: php7.4 -r 'var_dump(mb_convert_case("a", 99));'
         *     => Warning: mb_convert_case(): Invalid case mode  bool(false)
         *   php8.0 same
         *     => ValueError: mb_convert_case(): Argument #2 ($mode) must be
         *        one of the MB_CASE_* constants
         */

            zend_error(E_WARNING, "mb_convert_case(): Invalid case mode");

        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_STR(ret, mb_convert_case_str(ZSTR_VAL(s), ZSTR_LEN(s), (int) mode));
}



static void zif_mb_internal_encoding(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *enc;
    const mb_encoding *e;

    if (!mb_arg_given(argc, argv, 0)) {
        ZVAL_STR(ret, zend_string_initz(mb_current_internal()));
        return;
    }
    enc = mb_arg_str(argc, argv, 0);
    e = mb_lookup_encoding(ZSTR_VAL(enc));
    if (!e) {
        zend_error(E_WARNING, "mb_internal_encoding(): Unknown encoding \"%s\"",
                   ZSTR_VAL(enc));
        ZVAL_BOOL(ret, 0);
        return;
    }
    /* php-src reports back the canonical spelling, not what was passed in */
    snprintf(mb_internal_enc, sizeof mb_internal_enc, "%s", e->name);
    ZVAL_BOOL(ret, 1);
}

static void zif_mb_check_encoding(zval *ret, uint32_t argc, zval *argv)
{
    zval *v;
    unsigned char kind = MB_ENC_UTF8;

    /* mb_check_encoding() with no value has nothing to check on the CLI */
    if (argc == 0 || !mb_arg_given(argc, argv, 0)) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    if (mb_check_enc_arg(argc, argv, 1, "mb_check_encoding") < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }

    v = mb_arg(argc, argv, 0);
    {
        const mb_encoding *e = mb_arg_given(argc, argv, 1)
            ? mb_lookup_encoding(ZSTR_VAL(mb_arg_str(argc, argv, 1)))
            : mb_lookup_encoding(mb_current_internal());
        kind = e ? e->kind : MB_ENC_UTF8;
    }
    if (Z_TYPE_P(v) == IS_ARRAY) {
        /*
         * Passing an array was rejected by PHP 5.6 and accepted from PHP 7.0
         * on.  Oracle:
         *   php5.6 -r 'var_dump(mb_check_encoding(["a"],"UTF-8"));'
         *       => bool(false)
         *   php7.4 same => bool(true)
         */
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (mb_version() >= 70000) {
            int ok = 1;
            /* php-src validates the string keys as well as the values */
            ZEND_HASH_FOREACH(Z_ARR_P(v), bkt) {
                zend_string *e = zval_get_string(zval_deref(&bkt->val));
                if (!mb_valid_in(ZSTR_VAL(e), ZSTR_LEN(e), kind)) {
                    ok = 0;
                }
                if (bkt->key &&
                        !mb_valid_in(ZSTR_VAL(bkt->key), ZSTR_LEN(bkt->key),
                                     kind)) {
                    ok = 0;
                }
            } ZEND_HASH_FOREACH_END();
            ZVAL_BOOL(ret, ok);
        } else {
            ZVAL_BOOL(ret, 0);
        }
        return;
    }

    {
        zend_string *s = zval_get_string(v);
        ZVAL_BOOL(ret, mb_valid_in(ZSTR_VAL(s), ZSTR_LEN(s), kind));
    }
}

static void zif_mb_convert_encoding(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *s, *to;
    mb_buf b = { NULL, 0, 0, 0 };
    size_t i;

    s = mb_arg_str(argc, argv, 0);
    to = mb_arg_given(argc, argv, 1) ? mb_arg_str(argc, argv, 1)
                                     : zend_string_initz(mb_current_internal());

    if (!mb_is_known_name(ZSTR_VAL(to))) {
        zend_error(E_WARNING, "mb_convert_encoding(): Unknown encoding \"%s\"",
                   ZSTR_VAL(to));
        ZVAL_BOOL(ret, 0);
        return;
    }
    if (mb_arg_given(argc, argv, 2)) {
        zend_string *from = mb_arg_str(argc, argv, 2);
        if (!mb_is_known_name(ZSTR_VAL(from))) {
            zend_error(E_WARNING,
                       "mb_convert_encoding(): Unknown encoding \"%s\"",
                       ZSTR_VAL(from));
            ZVAL_BOOL(ret, 0);
            return;
        }
    }

    /*
     * Only UTF-8 <-> UTF-8 and UTF-8 <-> ASCII are implemented; any other
     * target encoding hands the input straight back (php-src would really
     * transcode).
     */
    if (mb_is_ascii_name(ZSTR_VAL(to))) {
        for (i = 0; i < ZSTR_LEN(s); ) {
            size_t used;
            int cp = mb_u8_decode((const unsigned char *) ZSTR_VAL(s) + i,
                                  ZSTR_LEN(s) - i, &used);
            if (cp >= 0 && cp < 0x80) {
                mbb_char(&b, (char) cp);
            } else {
                mbb_char(&b, '?');      /* mbstring's substitute character */
            }
            i += used;
        }
        ZVAL_STR(ret, mbb_finish(&b));
        return;
    }

    ZVAL_STR(ret, zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s)));
}

static void zif_mb_strwidth(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *s;
    size_t i;
    zend_long width = 0;

    if (mb_check_enc_arg(argc, argv, 1, "mb_strwidth") < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    s = mb_arg_str(argc, argv, 0);
    for (i = 0; i < ZSTR_LEN(s); ) {
        size_t used;
        int cp = mb_u8_decode((const unsigned char *) ZSTR_VAL(s) + i,
                              ZSTR_LEN(s) - i, &used);
        /* a malformed byte becomes the replacement marker, width 1 */
        width += (cp >= 0 && mb_is_fullwidth(cp)) ? 2 : 1;
        i += used;
    }
    ZVAL_LONG(ret, width);
}

static void zif_mb_substr_count(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *h, *n;
    size_t pos = 0;
    zend_long count = 0;

    if (mb_check_enc_arg(argc, argv, 2, "mb_substr_count") < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    h = mb_arg_str(argc, argv, 0);
    n = mb_arg_str(argc, argv, 1);

    if (ZSTR_LEN(n) == 0) {
        /*
         * Oracle:
         *   php7.4 -r 'var_dump(@mb_substr_count("abc",""));' => bool(false)
         *   php8.0 => ValueError: mb_substr_count(): Argument #2 ($needle)
         *             must not be empty
         */

            zend_error(E_WARNING, "mb_substr_count(): Empty substring");

        ZVAL_BOOL(ret, 0);
        return;
    }

    while (pos + ZSTR_LEN(n) <= ZSTR_LEN(h)) {
        const char *f = mb_memmem(ZSTR_VAL(h) + pos, ZSTR_LEN(h) - pos,
                                  ZSTR_VAL(n), ZSTR_LEN(n));
        if (!f) {
            break;
        }
        count++;
        pos = (size_t) (f - ZSTR_VAL(h)) + ZSTR_LEN(n);   /* non overlapping */
    }
    ZVAL_LONG(ret, count);
}









/* Case-converts only the first character and leaves the rest alone. */
static void mb_do_firstcase(zval *ret, uint32_t argc, zval *argv, int mode,
                            const char *fname)
{
    zend_string *s, *head;
    size_t split;

    if (mb_check_enc_arg(argc, argv, 1, fname) < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    s = mb_arg_str(argc, argv, 0);
    if (ZSTR_LEN(s) == 0) {
        ZVAL_STR(ret, zend_string_initz(""));
        return;
    }
    split = mb_u8_offset(ZSTR_VAL(s), ZSTR_LEN(s), 1);
    head = mb_convert_case_str(ZSTR_VAL(s), split, mode);
    {
        mb_buf b = { NULL, 0, 0, 0 };
        mbb_mem(&b, ZSTR_VAL(head), ZSTR_LEN(head));
        mbb_mem(&b, ZSTR_VAL(s) + split, ZSTR_LEN(s) - split);
        ZVAL_STR(ret, mbb_finish(&b));
    }
}

/*
 * VERSIONGATE:80400
 * mb_ucfirst() applies the *title* mapping, which is why the sharp s becomes
 * "Ss" and not "SS".  Oracle (php8.4):
 *   mb_ucfirst("\xC3\x9F")   => Ss
 *   mb_ucfirst("\xC3\xA9lan") => Élan
 *   mb_lcfirst("\xC4\xB0")   => i + U+0307
 */
static void zif_mb_ucfirst(zval *ret, uint32_t argc, zval *argv)
{
    mb_do_firstcase(ret, argc, argv, MB_CASE_TITLE, "mb_ucfirst");
}



/*
 * VERSIONGATE:80400
 * mb_trim()/mb_ltrim()/mb_rtrim().  The default character list is php-src's
 * trim_default_chars[] -- ASCII whitespace plus NUL and the Unicode space
 * separators -- and an explicit list is simply the set of characters in it
 * (no ranges).  Oracle (php8.4):
 *   mb_trim("  a b  ")            => "a b"
 *   mb_trim("xxaxx", "x")         => "a"
 *   mb_trim(U+3000 . "a" . U+3000) => "a"
 */
static int mb_cp_in_set(unsigned int cp, const unsigned int *set, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (set[i] == cp) {
            return 1;
        }
    }
    return 0;
}

static void mb_do_trim(zval *ret, uint32_t argc, zval *argv, int left,
                       int right, const char *fname)
{
    static const unsigned int trim_default_chars[] = {
        0x20, 0x0C, 0x0A, 0x0D, 0x09, 0x0B, 0x00, 0xA0, 0x1680,
        0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007,
        0x2008, 0x2009, 0x200A, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000,
        0x85, 0x180E
    };
    const unsigned int *set = trim_default_chars;
    size_t nset = sizeof trim_default_chars / sizeof trim_default_chars[0];
    unsigned int *owned = NULL;
    zend_string *s, *what = NULL;
    size_t start, end, i;

    if (mb_check_enc_arg(argc, argv, 2, fname) < 0) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    s = mb_arg_str(argc, argv, 0);

    if (mb_arg_given(argc, argv, 1)) {
        size_t n = 0;
        what = mb_arg_str(argc, argv, 1);
        owned = (unsigned int *) malloc((ZSTR_LEN(what) + 1) *
                                        sizeof(unsigned int));
        if (!owned) {
            ZVAL_BOOL(ret, 0);
            return;
        }
        for (i = 0; i < ZSTR_LEN(what); ) {
            size_t used;
            int cp = mb_u8_decode((const unsigned char *) ZSTR_VAL(what) + i,
                                  ZSTR_LEN(what) - i, &used);
            if (cp >= 0) {
                owned[n++] = (unsigned int) cp;
            }
            i += used;
        }
        set = owned;
        nset = n;
    }

    start = 0;
    end = ZSTR_LEN(s);

    if (left) {
        while (start < end) {
            size_t used;
            int cp = mb_u8_decode((const unsigned char *) ZSTR_VAL(s) + start,
                                  end - start, &used);
            if (cp < 0 || !mb_cp_in_set((unsigned int) cp, set, nset)) {
                break;
            }
            start += used;
        }
    }
    if (right) {
        while (end > start) {
            /* step back to the start of the last character */
            size_t b = end - 1, used;
            int cp;
            while (b > start && ((unsigned char) ZSTR_VAL(s)[b] & 0xC0) == 0x80) {
                b--;
            }
            cp = mb_u8_decode((const unsigned char *) ZSTR_VAL(s) + b,
                              end - b, &used);
            if (cp < 0 || used != end - b ||
                    !mb_cp_in_set((unsigned int) cp, set, nset)) {
                break;
            }
            end = b;
        }
    }

    free(owned);
    ZVAL_STR(ret, zend_string_init(ZSTR_VAL(s) + start, end - start));
}







static void zif_mb_list_encodings(zval *ret, uint32_t argc, zval *argv)
{
    HashTable *ht = zend_hash_new();
    size_t i;

    for (i = 0; i < sizeof mb_encodings / sizeof mb_encodings[0]; i++) {
        zval v;
        if (!mb_enc_listed_now(&mb_encodings[i])) {
            continue;               /* alias, or outside its release window */
        }
        ZVAL_STR(&v, zend_string_initz(mb_encodings[i].name));
        zend_hash_next_index_insert(ht, v);
    }
    ZVAL_ARR(ret, ht);
}

const php_function_entry php_functions_mbstring[] = {
    { "mb_strlen",            zif_mb_strlen,            1, 2, NULL, 0,     0 },
    { "mb_substr",            zif_mb_substr,            2, 4, NULL, 0,     0 },
    { "mb_strpos",            zif_mb_strpos,            2, 4, NULL, 0,     0 },
    { "mb_strrpos",           zif_mb_strrpos,           2, 4, NULL, 0,     0 },
    { "mb_stripos",           zif_mb_stripos,           2, 4, NULL, 0,     0 },
    { "mb_strripos",          zif_mb_strripos,          2, 4, NULL, 0,     0 },
    { "mb_strstr",            zif_mb_strstr,            2, 4, NULL, 0,     0 },
    { "mb_stristr",           zif_mb_stristr,           2, 4, NULL, 0,     0 },
    { "mb_strrchr",           zif_mb_strrchr,           2, 4, NULL, 0,     0 },
    { "mb_strrichr",          zif_mb_strrichr,          2, 4, NULL, 0,     0 },
    { "mb_list_encodings",    zif_mb_list_encodings,    0, 0, NULL, 0,     0 },








    { "mb_strtolower",        zif_mb_strtolower,        1, 2, NULL, 0,     0 },
    { "mb_strtoupper",        zif_mb_strtoupper,        1, 2, NULL, 0,     0 },

    { "mb_internal_encoding", zif_mb_internal_encoding, 0, 1, NULL, 0,     0 },
    { "mb_check_encoding",    zif_mb_check_encoding,    0, 2, NULL, 0,     0 },
    { "mb_convert_case",      zif_mb_convert_case,      2, 3, NULL, 0,     0 },
    { "mb_convert_encoding",  zif_mb_convert_encoding,  1, 3, NULL, 0,     0 },
    { "mb_strwidth",          zif_mb_strwidth,          1, 2, NULL, 0,     0 },
    { "mb_substr_count",      zif_mb_substr_count,      2, 3, NULL, 0,     0 },

    { NULL, NULL, 0, 0, NULL, 0, 0 }
};

/* ------------------------------------------------------------------ */
/* constants                                                            */
/* ------------------------------------------------------------------ */
/* main/php_registry.c */
extern void zend_register_string_constant(const char *name, const char *v);

void php_mbstring_register_constants(void)
{
    zend_register_long_constant("MB_CASE_UPPER", MB_CASE_UPPER);
    zend_register_long_constant("MB_CASE_LOWER", MB_CASE_LOWER);
    zend_register_long_constant("MB_CASE_TITLE", MB_CASE_TITLE);

    /*
     * MB_CASE_FOLD and the *_SIMPLE variants came with the PHP 7.3 case
     * mapping rewrite.  Oracle: on 7.2 only the first three constants exist.
     */
    /*
     * MB_ONIGURUMA_VERSION reports the oniguruma the build links against; it
     * appeared with the mbregex rework in PHP 7.4.  The official builds carry
     * 6.9.6 on the 7.4 and 8.0 branches and 6.9.8 from 8.1 on.  Oracle:
     *   php7.2 -r 'var_dump(defined("MB_ONIGURUMA_VERSION"));' => bool(false)
     *   php7.4 -r 'echo MB_ONIGURUMA_VERSION;'                 => 6.9.6
     *   php8.1 -r 'echo MB_ONIGURUMA_VERSION;'                 => 6.9.8
     */


    /*
     * mbstring function overloading (mbstring.func_overload) was removed in
     * PHP 8.0 together with its three constants.  Oracle:
     *   php7.4 -r 'var_dump(defined("MB_OVERLOAD_STRING"));' => bool(true)
     *   php8.0 same => bool(false)
     */



}
