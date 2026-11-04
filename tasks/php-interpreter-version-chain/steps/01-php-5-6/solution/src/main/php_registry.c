/* php_registry.c -- the function, constant and class registration tables.
 *
 * Everything a script can reach without writing a declaration is put into
 * EG(function_table), EG(constants) and EG(class_table) here, at start-up,
 * from the tables the extensions publish.
 *
 * The interpreter emulates one PHP release at a time, so registration is a
 * filter as much as a copy: a function carries the release it appeared in and
 * the release it was removed in, and a constant that only exists from a later
 * release sits behind a runtime test on EG(php_version).  Those tests are
 * marked VERSIONGATE:<id> so that a build targeting an older release can drop
 * them mechanically.
 */

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include "zend_object.h"
#include "zend_exceptions.h"
#include "php_main.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 *  Function tables published by the extensions
 * ========================================================================== */

extern const php_function_entry php_functions_string[];
extern const php_function_entry php_functions_array[];
extern const php_function_entry php_functions_math[];
extern const php_function_entry php_functions_var[];
extern const php_function_entry php_functions_type[];
extern const php_function_entry php_functions_misc[];
extern const php_function_entry php_functions_json[];
extern const php_function_entry php_functions_mbstring[];

/* zend_API.h also declares php_builtin_functions[].  It is optional: an
 * extension may or may not define it, so the reference is weak and the table
 * is only walked when something actually provided one. */
extern const php_function_entry php_builtin_functions[] __attribute__((weak));

/* Constant blocks the two self-contained extensions own. */
void php_json_register_constants(void);
void php_mbstring_register_constants(void);

/* ==========================================================================
 *  Functions
 * ========================================================================== */

static zval ptr_zval(void *p)
{
    zval v;

    v.u1 = IS_LONG;
    v.value.lval = (zend_long) (intptr_t) p;
    return v;
}

/* "01" means argument index 1 is taken by reference.  A mask shorter than the
 * argument list leaves the rest by value. */
static void fill_arg_info(zend_function *fn, const char *ref_mask)
{
    size_t mask_len = (ref_mask != NULL) ? strlen(ref_mask) : 0;
    uint32_t i;

    if (fn->num_args == 0) {
        fn->args = NULL;
        return;
    }
    fn->args = ecalloc(fn->num_args, sizeof(zend_arg_info));
    for (i = 0; i < fn->num_args; i++) {
        fn->args[i].by_ref = (i < mask_len && ref_mask[i] == '1') ? 1 : 0;
    }
    if ((fn->flags & ZEND_ACC_VARIADIC) != 0) {
        fn->args[fn->num_args - 1].variadic = 1;
    }
}

uint32_t zend_register_function_table(const php_function_entry *tbl)
{
    uint32_t registered = 0;
    const php_function_entry *e;

    if (tbl == NULL) {
        return 0;
    }
    for (e = tbl; e->name != NULL; e++) {
        zend_function *fn;
        uint32_t num_args;

        /* The version window: `since` is the first release that has it,
         * a non-zero `until` is the first release that does not. */
        if (EG(php_version) < e->since) {
            continue;
        }
        if (e->until != 0 && EG(php_version) >= e->until) {
            continue;
        }

        fn = ecalloc(1, sizeof(zend_function));
        fn->name        = zend_string_initz(e->name);
        fn->lcname      = zend_string_tolower(fn->name);
        fn->handler     = e->handler;
        fn->min_args    = e->min_args;
        fn->is_internal = 1;
        fn->scope       = NULL;
        fn->static_vars = zend_hash_new();

        if (e->max_args == (uint32_t) -1) {
            fn->flags |= ZEND_ACC_VARIADIC;
            num_args = e->min_args;
            if (e->ref_mask != NULL && strlen(e->ref_mask) > num_args) {
                num_args = (uint32_t) strlen(e->ref_mask);
            }
            if (num_args == 0) {
                num_args = 1;       /* the variadic slot itself */
            }
        } else {
            num_args = e->max_args;
        }
        fn->num_args = num_args;
        fill_arg_info(fn, e->ref_mask);

        zend_hash_update(EG(function_table), fn->lcname, ptr_zval(fn));
        registered++;
    }
    return registered;
}

void zend_register_functions(void)
{
    const php_function_entry *optional = php_builtin_functions;

    zend_register_function_table(php_functions_string);
    zend_register_function_table(php_functions_array);
    zend_register_function_table(php_functions_math);
    zend_register_function_table(php_functions_var);
    zend_register_function_table(php_functions_type);
    zend_register_function_table(php_functions_misc);
    zend_register_function_table(php_functions_json);
    zend_register_function_table(php_functions_mbstring);

    if (optional != NULL) {
        zend_register_function_table(optional);
    }
}

zend_function *zend_lookup_function(const zend_string *lcname)
{
    zval *v;

    if (lcname == NULL) {
        return NULL;
    }
    /* A fully qualified name in the global namespace is the same function. */
    if (ZSTR_LEN(lcname) > 0 && ZSTR_VAL(lcname)[0] == '\\') {
        v = zend_hash_str_find(EG(function_table), ZSTR_VAL(lcname) + 1,
                               ZSTR_LEN(lcname) - 1);
    } else {
        v = zend_hash_find(EG(function_table), lcname);
    }
    return (v != NULL) ? (zend_function *) (intptr_t) v->value.lval : NULL;
}

/* ==========================================================================
 *  Constants
 * ========================================================================== */

void zend_register_stringl_constant(const char *name, const char *v, size_t len)
{
    zend_hash_str_update(EG(constants), name, strlen(name),
                         zval_zstr(zend_string_init(v, len)));
}

void zend_register_string_constant(const char *name, const char *v)
{
    zend_register_stringl_constant(name, v, (v != NULL) ? strlen(v) : 0);
}

void zend_register_long_constant(const char *name, zend_long v)
{
    zend_hash_str_update(EG(constants), name, strlen(name), zval_long(v));
}

void zend_register_double_constant(const char *name, double v)
{
    zend_hash_str_update(EG(constants), name, strlen(name), zval_double(v));
}

void zend_register_bool_constant(const char *name, int v)
{
    zend_hash_str_update(EG(constants), name, strlen(name), zval_bool(v));
}

void zend_register_null_constant(const char *name)
{
    zend_hash_str_update(EG(constants), name, strlen(name), zval_null());
}

zval *zend_get_constant_str(const char *name, size_t len)
{
    zval *v;

    if (name == NULL) {
        return NULL;
    }
    if (len > 0 && name[0] == '\\') {       /* \FOO is FOO in the global scope */
        name++;
        len--;
    }
    v = zend_hash_str_find(EG(constants), name, len);
    if (v != NULL) {
        return v;
    }
    /* true / false / null are the only names php matches case-insensitively. */
    if (len == 4 || len == 5) {
        char folded[8];
        size_t i;

        for (i = 0; i < len; i++) {
            char c = name[i];

            folded[i] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
        }
        folded[len] = '\0';
        if (strcmp(folded, "true") == 0 || strcmp(folded, "false") == 0 ||
            strcmp(folded, "null") == 0) {
            return zend_hash_str_find(EG(constants), folded, len);
        }
    }
    return NULL;
}


static void register_version_constants(void)
{
    long v = EG(php_version);

    zend_register_string_constant("PHP_VERSION",
                                  EG(php_version_str) != NULL ? EG(php_version_str) : "");
    zend_register_long_constant("PHP_MAJOR_VERSION", (zend_long) (v / 10000));
    zend_register_long_constant("PHP_MINOR_VERSION", (zend_long) ((v / 100) % 100));
    zend_register_long_constant("PHP_RELEASE_VERSION", (zend_long) (v % 100));
    zend_register_string_constant("PHP_EXTRA_VERSION", "");
    zend_register_long_constant("PHP_VERSION_ID", (zend_long) v);

    zend_register_string_constant("PHP_OS", "Linux");

    zend_register_string_constant("PHP_SAPI", php_sapi_name);
    zend_register_string_constant("PHP_BINARY", php_binary_path);


        zend_register_long_constant("PHP_DEBUG", 0);
        zend_register_long_constant("PHP_ZTS", 0);

}

static void register_numeric_constants(void)
{
    zend_register_long_constant("PHP_INT_MAX", ZEND_LONG_MAX);
    zend_register_long_constant("PHP_INT_SIZE", (zend_long) sizeof(zend_long));




    zend_register_double_constant("NAN", (double) NAN);
    zend_register_double_constant("INF", (double) INFINITY);

}

static void register_error_constants(void)
{

        zend_register_long_constant("E_ALL", E_ALL);

}


/* ==========================================================================
 *  The Core and standard constant table
 * ==========================================================================
 * Names, values and version windows are taken from the reference php builds
 * (get_defined_constants(true), the "Core" and "standard" groups of every
 * release from 5.6 to 8.4), so a constant is registered exactly when the
 * release being emulated has it and with exactly the value that release
 * reports.  A row whose `since` is above the target, or whose non-zero
 * `until` is at or below it, is skipped; each such row carries a
 * VERSIONGATE marker so a build pinned to an older release can drop it.
 *
 * ext/json and ext/mbstring keep their own blocks, and the handful of
 * constants that depend on this build rather than on the release --
 * PHP_VERSION, PHP_INT_MAX, PHP_BINARY, E_ALL, ... -- are set above.
 * STDIN/STDOUT/STDERR are absent because this value model has no resource
 * type.
 */

typedef enum { C_L, C_D, C_S, C_B, C_N } php_const_kind;

typedef struct {
    const char    *name;
    php_const_kind kind;
    zend_long      lval;        /* also carries the boolean */
    double         dval;
    const char    *sval;
    long           since;       /* 0 = present in every release */
    long           until;       /* 0 = never removed */
} php_const_entry;

static const php_const_entry php_core_constants[] = {
    { "ABDAY_1", C_L, 131072, 0.0, NULL, 0, 0 },
    { "ABDAY_2", C_L, 131073, 0.0, NULL, 0, 0 },
    { "ABDAY_3", C_L, 131074, 0.0, NULL, 0, 0 },
    { "ABDAY_4", C_L, 131075, 0.0, NULL, 0, 0 },
    { "ABDAY_5", C_L, 131076, 0.0, NULL, 0, 0 },
    { "ABDAY_6", C_L, 131077, 0.0, NULL, 0, 0 },
    { "ABDAY_7", C_L, 131078, 0.0, NULL, 0, 0 },
    { "ABMON_1", C_L, 131086, 0.0, NULL, 0, 0 },
    { "ABMON_10", C_L, 131095, 0.0, NULL, 0, 0 },
    { "ABMON_11", C_L, 131096, 0.0, NULL, 0, 0 },
    { "ABMON_12", C_L, 131097, 0.0, NULL, 0, 0 },
    { "ABMON_2", C_L, 131087, 0.0, NULL, 0, 0 },
    { "ABMON_3", C_L, 131088, 0.0, NULL, 0, 0 },
    { "ABMON_4", C_L, 131089, 0.0, NULL, 0, 0 },
    { "ABMON_5", C_L, 131090, 0.0, NULL, 0, 0 },
    { "ABMON_6", C_L, 131091, 0.0, NULL, 0, 0 },
    { "ABMON_7", C_L, 131092, 0.0, NULL, 0, 0 },
    { "ABMON_8", C_L, 131093, 0.0, NULL, 0, 0 },
    { "ABMON_9", C_L, 131094, 0.0, NULL, 0, 0 },
    { "ALT_DIGITS", C_L, 131119, 0.0, NULL, 0, 0 },
    { "AM_STR", C_L, 131110, 0.0, NULL, 0, 0 },
    { "ARRAY_FILTER_USE_BOTH", C_L, 1, 0.0, NULL, 0, 0 },
    { "ARRAY_FILTER_USE_KEY", C_L, 2, 0.0, NULL, 0, 0 },
    { "ASSERT_ACTIVE", C_L, 1, 0.0, NULL, 0, 0 },
    { "ASSERT_BAIL", C_L, 3, 0.0, NULL, 0, 0 },
    { "ASSERT_CALLBACK", C_L, 2, 0.0, NULL, 0, 0 },



    { "ASSERT_WARNING", C_L, 4, 0.0, NULL, 0, 0 },
    { "CASE_LOWER", C_L, 0, 0.0, NULL, 0, 0 },
    { "CASE_UPPER", C_L, 1, 0.0, NULL, 0, 0 },
    { "CHAR_MAX", C_L, 127, 0.0, NULL, 0, 0 },
    { "CODESET", C_L, 14, 0.0, NULL, 0, 0 },
    { "CONNECTION_ABORTED", C_L, 1, 0.0, NULL, 0, 0 },
    { "CONNECTION_NORMAL", C_L, 0, 0.0, NULL, 0, 0 },
    { "CONNECTION_TIMEOUT", C_L, 2, 0.0, NULL, 0, 0 },
    { "COUNT_NORMAL", C_L, 0, 0.0, NULL, 0, 0 },
    { "COUNT_RECURSIVE", C_L, 1, 0.0, NULL, 0, 0 },
    { "CREDITS_ALL", C_L, 4294967295, 0.0, NULL, 0, 0 },
    { "CREDITS_DOCS", C_L, 16, 0.0, NULL, 0, 0 },
    { "CREDITS_FULLPAGE", C_L, 32, 0.0, NULL, 0, 0 },
    { "CREDITS_GENERAL", C_L, 2, 0.0, NULL, 0, 0 },
    { "CREDITS_GROUP", C_L, 1, 0.0, NULL, 0, 0 },
    { "CREDITS_MODULES", C_L, 8, 0.0, NULL, 0, 0 },
    { "CREDITS_QA", C_L, 64, 0.0, NULL, 0, 0 },
    { "CREDITS_SAPI", C_L, 4, 0.0, NULL, 0, 0 },
    { "CRNCYSTR", C_L, 262159, 0.0, NULL, 0, 0 },
    { "CRYPT_BLOWFISH", C_L, 1, 0.0, NULL, 0, 0 },
    { "CRYPT_EXT_DES", C_L, 1, 0.0, NULL, 0, 0 },
    { "CRYPT_MD5", C_L, 1, 0.0, NULL, 0, 0 },
    { "CRYPT_SALT_LENGTH", C_L, 123, 0.0, NULL, 0, 0 },
    { "CRYPT_SHA256", C_L, 1, 0.0, NULL, 0, 0 },
    { "CRYPT_SHA512", C_L, 1, 0.0, NULL, 0, 0 },
    { "CRYPT_STD_DES", C_L, 1, 0.0, NULL, 0, 0 },

    { "DAY_1", C_L, 131079, 0.0, NULL, 0, 0 },
    { "DAY_2", C_L, 131080, 0.0, NULL, 0, 0 },
    { "DAY_3", C_L, 131081, 0.0, NULL, 0, 0 },
    { "DAY_4", C_L, 131082, 0.0, NULL, 0, 0 },
    { "DAY_5", C_L, 131083, 0.0, NULL, 0, 0 },
    { "DAY_6", C_L, 131084, 0.0, NULL, 0, 0 },
    { "DAY_7", C_L, 131085, 0.0, NULL, 0, 0 },
    { "DEBUG_BACKTRACE_IGNORE_ARGS", C_L, 2, 0.0, NULL, 0, 0 },
    { "DEBUG_BACKTRACE_PROVIDE_OBJECT", C_L, 1, 0.0, NULL, 0, 0 },

    { "DEFAULT_INCLUDE_PATH", C_S, 0, 0.0, ".:/usr/local/lib/php", 0, 0 },
    { "DIRECTORY_SEPARATOR", C_S, 0, 0.0, "/", 0, 0 },
    { "DNS_A", C_L, 1, 0.0, NULL, 0, 0 },
    { "DNS_A6", C_L, 16777216, 0.0, NULL, 0, 0 },
    { "DNS_AAAA", C_L, 134217728, 0.0, NULL, 0, 0 },


    { "DNS_ANY", C_L, 268435456, 0.0, NULL, 0, 0 },

    { "DNS_CNAME", C_L, 16, 0.0, NULL, 0, 0 },
    { "DNS_HINFO", C_L, 4096, 0.0, NULL, 0, 0 },
    { "DNS_MX", C_L, 16384, 0.0, NULL, 0, 0 },
    { "DNS_NAPTR", C_L, 67108864, 0.0, NULL, 0, 0 },
    { "DNS_NS", C_L, 2, 0.0, NULL, 0, 0 },
    { "DNS_PTR", C_L, 2048, 0.0, NULL, 0, 0 },
    { "DNS_SOA", C_L, 32, 0.0, NULL, 0, 0 },
    { "DNS_SRV", C_L, 33554432, 0.0, NULL, 0, 0 },
    { "DNS_TXT", C_L, 32768, 0.0, NULL, 0, 0 },
    { "D_FMT", C_L, 131113, 0.0, NULL, 0, 0 },
    { "D_T_FMT", C_L, 131112, 0.0, NULL, 0, 0 },
    { "ENT_COMPAT", C_L, 2, 0.0, NULL, 0, 0 },
    { "ENT_DISALLOWED", C_L, 128, 0.0, NULL, 0, 0 },
    { "ENT_HTML401", C_L, 0, 0.0, NULL, 0, 0 },
    { "ENT_HTML5", C_L, 48, 0.0, NULL, 0, 0 },
    { "ENT_IGNORE", C_L, 4, 0.0, NULL, 0, 0 },
    { "ENT_NOQUOTES", C_L, 0, 0.0, NULL, 0, 0 },
    { "ENT_QUOTES", C_L, 3, 0.0, NULL, 0, 0 },
    { "ENT_SUBSTITUTE", C_L, 8, 0.0, NULL, 0, 0 },
    { "ENT_XHTML", C_L, 32, 0.0, NULL, 0, 0 },
    { "ENT_XML1", C_L, 16, 0.0, NULL, 0, 0 },
    { "ERA", C_L, 131116, 0.0, NULL, 0, 0 },
    { "ERA_D_FMT", C_L, 131118, 0.0, NULL, 0, 0 },
    { "ERA_D_T_FMT", C_L, 131120, 0.0, NULL, 0, 0 },
    { "ERA_T_FMT", C_L, 131121, 0.0, NULL, 0, 0 },

    { "EXTR_IF_EXISTS", C_L, 6, 0.0, NULL, 0, 0 },
    { "EXTR_OVERWRITE", C_L, 0, 0.0, NULL, 0, 0 },
    { "EXTR_PREFIX_ALL", C_L, 3, 0.0, NULL, 0, 0 },
    { "EXTR_PREFIX_IF_EXISTS", C_L, 5, 0.0, NULL, 0, 0 },
    { "EXTR_PREFIX_INVALID", C_L, 4, 0.0, NULL, 0, 0 },
    { "EXTR_PREFIX_SAME", C_L, 2, 0.0, NULL, 0, 0 },
    { "EXTR_REFS", C_L, 256, 0.0, NULL, 0, 0 },
    { "EXTR_SKIP", C_L, 1, 0.0, NULL, 0, 0 },
    { "E_COMPILE_ERROR", C_L, 64, 0.0, NULL, 0, 0 },
    { "E_COMPILE_WARNING", C_L, 128, 0.0, NULL, 0, 0 },
    { "E_CORE_ERROR", C_L, 16, 0.0, NULL, 0, 0 },
    { "E_CORE_WARNING", C_L, 32, 0.0, NULL, 0, 0 },
    { "E_DEPRECATED", C_L, 8192, 0.0, NULL, 0, 0 },
    { "E_ERROR", C_L, 1, 0.0, NULL, 0, 0 },
    { "E_NOTICE", C_L, 8, 0.0, NULL, 0, 0 },
    { "E_PARSE", C_L, 4, 0.0, NULL, 0, 0 },
    { "E_RECOVERABLE_ERROR", C_L, 4096, 0.0, NULL, 0, 0 },
    { "E_STRICT", C_L, 2048, 0.0, NULL, 0, 0 },
    { "E_USER_DEPRECATED", C_L, 16384, 0.0, NULL, 0, 0 },
    { "E_USER_ERROR", C_L, 256, 0.0, NULL, 0, 0 },
    { "E_USER_NOTICE", C_L, 1024, 0.0, NULL, 0, 0 },
    { "E_USER_WARNING", C_L, 512, 0.0, NULL, 0, 0 },
    { "E_WARNING", C_L, 2, 0.0, NULL, 0, 0 },
    { "FALSE", C_B, 0, 0.0, NULL, 0, 0 },
    { "FILE_APPEND", C_L, 8, 0.0, NULL, 0, 0 },
    { "FILE_BINARY", C_L, 0, 0.0, NULL, 0, 0 },
    { "FILE_IGNORE_NEW_LINES", C_L, 2, 0.0, NULL, 0, 0 },
    { "FILE_NO_DEFAULT_CONTEXT", C_L, 16, 0.0, NULL, 0, 0 },
    { "FILE_SKIP_EMPTY_LINES", C_L, 4, 0.0, NULL, 0, 0 },
    { "FILE_TEXT", C_L, 0, 0.0, NULL, 0, 0 },
    { "FILE_USE_INCLUDE_PATH", C_L, 1, 0.0, NULL, 0, 0 },
    { "FNM_CASEFOLD", C_L, 16, 0.0, NULL, 0, 0 },
    { "FNM_NOESCAPE", C_L, 2, 0.0, NULL, 0, 0 },
    { "FNM_PATHNAME", C_L, 1, 0.0, NULL, 0, 0 },
    { "FNM_PERIOD", C_L, 4, 0.0, NULL, 0, 0 },

    { "False", C_B, 0, 0.0, NULL, 0, 0 },
    { "GLOB_AVAILABLE_FLAGS", C_L, 9303, 0.0, NULL, 0, 0 },
    { "GLOB_BRACE", C_L, 1024, 0.0, NULL, 0, 0 },
    { "GLOB_ERR", C_L, 1, 0.0, NULL, 0, 0 },
    { "GLOB_MARK", C_L, 2, 0.0, NULL, 0, 0 },
    { "GLOB_NOCHECK", C_L, 16, 0.0, NULL, 0, 0 },
    { "GLOB_NOESCAPE", C_L, 64, 0.0, NULL, 0, 0 },
    { "GLOB_NOSORT", C_L, 4, 0.0, NULL, 0, 0 },
    { "GLOB_ONLYDIR", C_L, 8192, 0.0, NULL, 0, 0 },

    { "HTML_ENTITIES", C_L, 1, 0.0, NULL, 0, 0 },
    { "HTML_SPECIALCHARS", C_L, 0, 0.0, NULL, 0, 0 },

    { "IMAGETYPE_BMP", C_L, 6, 0.0, NULL, 0, 0 },



    { "IMAGETYPE_GIF", C_L, 1, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_ICO", C_L, 17, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_IFF", C_L, 14, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_JB2", C_L, 12, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_JP2", C_L, 10, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_JPC", C_L, 9, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_JPEG", C_L, 2, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_JPEG2000", C_L, 9, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_JPX", C_L, 11, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_PNG", C_L, 3, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_PSD", C_L, 5, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_SWC", C_L, 13, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_SWF", C_L, 4, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_TIFF_II", C_L, 7, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_TIFF_MM", C_L, 8, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_UNKNOWN", C_L, 0, 0.0, NULL, 0, 0 },
    { "IMAGETYPE_WBMP", C_L, 15, 0.0, NULL, 0, 0 },

    { "IMAGETYPE_XBM", C_L, 16, 0.0, NULL, 0, 0 },
    { "INFO_ALL", C_L, 4294967295, 0.0, NULL, 0, 0 },
    { "INFO_CONFIGURATION", C_L, 4, 0.0, NULL, 0, 0 },
    { "INFO_CREDITS", C_L, 2, 0.0, NULL, 0, 0 },
    { "INFO_ENVIRONMENT", C_L, 16, 0.0, NULL, 0, 0 },
    { "INFO_GENERAL", C_L, 1, 0.0, NULL, 0, 0 },
    { "INFO_LICENSE", C_L, 64, 0.0, NULL, 0, 0 },
    { "INFO_MODULES", C_L, 8, 0.0, NULL, 0, 0 },
    { "INFO_VARIABLES", C_L, 32, 0.0, NULL, 0, 0 },
    { "INI_ALL", C_L, 7, 0.0, NULL, 0, 0 },
    { "INI_PERDIR", C_L, 2, 0.0, NULL, 0, 0 },
    { "INI_SCANNER_NORMAL", C_L, 0, 0.0, NULL, 0, 0 },
    { "INI_SCANNER_RAW", C_L, 1, 0.0, NULL, 0, 0 },
    { "INI_SCANNER_TYPED", C_L, 2, 0.0, NULL, 0, 0 },
    { "INI_SYSTEM", C_L, 4, 0.0, NULL, 0, 0 },
    { "INI_USER", C_L, 1, 0.0, NULL, 0, 0 },


    { "LC_ALL", C_L, 6, 0.0, NULL, 0, 0 },
    { "LC_COLLATE", C_L, 3, 0.0, NULL, 0, 0 },
    { "LC_CTYPE", C_L, 0, 0.0, NULL, 0, 0 },
    { "LC_MESSAGES", C_L, 5, 0.0, NULL, 0, 0 },
    { "LC_MONETARY", C_L, 4, 0.0, NULL, 0, 0 },
    { "LC_NUMERIC", C_L, 1, 0.0, NULL, 0, 0 },
    { "LC_TIME", C_L, 2, 0.0, NULL, 0, 0 },
    { "LOCK_EX", C_L, 2, 0.0, NULL, 0, 0 },
    { "LOCK_NB", C_L, 4, 0.0, NULL, 0, 0 },
    { "LOCK_SH", C_L, 1, 0.0, NULL, 0, 0 },
    { "LOCK_UN", C_L, 3, 0.0, NULL, 0, 0 },
    { "LOG_ALERT", C_L, 1, 0.0, NULL, 0, 0 },
    { "LOG_AUTH", C_L, 32, 0.0, NULL, 0, 0 },
    { "LOG_AUTHPRIV", C_L, 80, 0.0, NULL, 0, 0 },
    { "LOG_CONS", C_L, 2, 0.0, NULL, 0, 0 },
    { "LOG_CRIT", C_L, 2, 0.0, NULL, 0, 0 },
    { "LOG_CRON", C_L, 72, 0.0, NULL, 0, 0 },
    { "LOG_DAEMON", C_L, 24, 0.0, NULL, 0, 0 },
    { "LOG_DEBUG", C_L, 7, 0.0, NULL, 0, 0 },
    { "LOG_EMERG", C_L, 0, 0.0, NULL, 0, 0 },
    { "LOG_ERR", C_L, 3, 0.0, NULL, 0, 0 },
    { "LOG_INFO", C_L, 6, 0.0, NULL, 0, 0 },
    { "LOG_KERN", C_L, 0, 0.0, NULL, 0, 0 },
    { "LOG_LOCAL0", C_L, 128, 0.0, NULL, 0, 0 },
    { "LOG_LOCAL1", C_L, 136, 0.0, NULL, 0, 0 },
    { "LOG_LOCAL2", C_L, 144, 0.0, NULL, 0, 0 },
    { "LOG_LOCAL3", C_L, 152, 0.0, NULL, 0, 0 },
    { "LOG_LOCAL4", C_L, 160, 0.0, NULL, 0, 0 },
    { "LOG_LOCAL5", C_L, 168, 0.0, NULL, 0, 0 },
    { "LOG_LOCAL6", C_L, 176, 0.0, NULL, 0, 0 },
    { "LOG_LOCAL7", C_L, 184, 0.0, NULL, 0, 0 },
    { "LOG_LPR", C_L, 48, 0.0, NULL, 0, 0 },
    { "LOG_MAIL", C_L, 16, 0.0, NULL, 0, 0 },
    { "LOG_NDELAY", C_L, 8, 0.0, NULL, 0, 0 },
    { "LOG_NEWS", C_L, 56, 0.0, NULL, 0, 0 },
    { "LOG_NOTICE", C_L, 5, 0.0, NULL, 0, 0 },
    { "LOG_NOWAIT", C_L, 16, 0.0, NULL, 0, 0 },
    { "LOG_ODELAY", C_L, 4, 0.0, NULL, 0, 0 },
    { "LOG_PERROR", C_L, 32, 0.0, NULL, 0, 0 },
    { "LOG_PID", C_L, 1, 0.0, NULL, 0, 0 },
    { "LOG_SYSLOG", C_L, 40, 0.0, NULL, 0, 0 },
    { "LOG_USER", C_L, 8, 0.0, NULL, 0, 0 },
    { "LOG_UUCP", C_L, 64, 0.0, NULL, 0, 0 },
    { "LOG_WARNING", C_L, 4, 0.0, NULL, 0, 0 },
    { "MON_1", C_L, 131098, 0.0, NULL, 0, 0 },
    { "MON_10", C_L, 131107, 0.0, NULL, 0, 0 },
    { "MON_11", C_L, 131108, 0.0, NULL, 0, 0 },
    { "MON_12", C_L, 131109, 0.0, NULL, 0, 0 },
    { "MON_2", C_L, 131099, 0.0, NULL, 0, 0 },
    { "MON_3", C_L, 131100, 0.0, NULL, 0, 0 },
    { "MON_4", C_L, 131101, 0.0, NULL, 0, 0 },
    { "MON_5", C_L, 131102, 0.0, NULL, 0, 0 },
    { "MON_6", C_L, 131103, 0.0, NULL, 0, 0 },
    { "MON_7", C_L, 131104, 0.0, NULL, 0, 0 },
    { "MON_8", C_L, 131105, 0.0, NULL, 0, 0 },
    { "MON_9", C_L, 131106, 0.0, NULL, 0, 0 },





    { "M_1_PI", C_D, 0, 0.31830988618379069, NULL, 0, 0 },
    { "M_2_PI", C_D, 0, 0.63661977236758138, NULL, 0, 0 },
    { "M_2_SQRTPI", C_D, 0, 1.1283791670955126, NULL, 0, 0 },
    { "M_E", C_D, 0, 2.7182818284590451, NULL, 0, 0 },
    { "M_EULER", C_D, 0, 0.57721566490153287, NULL, 0, 0 },
    { "M_LN10", C_D, 0, 2.3025850929940459, NULL, 0, 0 },
    { "M_LN2", C_D, 0, 0.69314718055994529, NULL, 0, 0 },
    { "M_LNPI", C_D, 0, 1.1447298858494002, NULL, 0, 0 },
    { "M_LOG10E", C_D, 0, 0.43429448190325182, NULL, 0, 0 },
    { "M_LOG2E", C_D, 0, 1.4426950408889634, NULL, 0, 0 },
    { "M_PI", C_D, 0, 3.1415926535897931, NULL, 0, 0 },
    { "M_PI_2", C_D, 0, 1.5707963267948966, NULL, 0, 0 },
    { "M_PI_4", C_D, 0, 0.78539816339744828, NULL, 0, 0 },
    { "M_SQRT1_2", C_D, 0, 0.70710678118654757, NULL, 0, 0 },
    { "M_SQRT2", C_D, 0, 1.4142135623730951, NULL, 0, 0 },
    { "M_SQRT3", C_D, 0, 1.7320508075688772, NULL, 0, 0 },
    { "M_SQRTPI", C_D, 0, 1.7724538509055161, NULL, 0, 0 },

    { "NOEXPR", C_L, 327681, 0.0, NULL, 0, 0 },

    { "NULL", C_N, 0, 0.0, NULL, 0, 0 },



    { "Null", C_N, 0, 0.0, NULL, 0, 0 },















    { "PATHINFO_BASENAME", C_L, 2, 0.0, NULL, 0, 0 },
    { "PATHINFO_DIRNAME", C_L, 1, 0.0, NULL, 0, 0 },
    { "PATHINFO_EXTENSION", C_L, 4, 0.0, NULL, 0, 0 },
    { "PATHINFO_FILENAME", C_L, 8, 0.0, NULL, 0, 0 },
    { "PATH_SEPARATOR", C_S, 0, 0.0, ":", 0, 0 },











    { "PEAR_INSTALL_DIR", C_S, 0, 0.0, "/usr/local/lib/php", 0, 0 },
    { "PHP_BINDIR", C_S, 0, 0.0, "/usr/local/bin", 0, 0 },

    { "PHP_CONFIG_FILE_PATH", C_S, 0, 0.0, "/usr/local/etc/php", 0, 0 },
    { "PHP_CONFIG_FILE_SCAN_DIR", C_S, 0, 0.0, "/usr/local/etc/php/conf.d", 0, 0 },
    { "PHP_DATADIR", C_S, 0, 0.0, "/usr/local/share/php", 0, 0 },
    { "PHP_EOL", C_S, 0, 0.0, "\n", 0, 0 },












    { "PHP_LIBDIR", C_S, 0, 0.0, "/usr/local/lib/php", 0, 0 },
    { "PHP_LOCALSTATEDIR", C_S, 0, 0.0, "/usr/local/var", 0, 0 },
    { "PHP_MANDIR", C_S, 0, 0.0, "/usr/local/php/man", 0, 0 },
    { "PHP_MAXPATHLEN", C_L, 4096, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_CLEAN", C_L, 2, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_CLEANABLE", C_L, 16, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_CONT", C_L, 0, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_DISABLED", C_L, 8192, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_END", C_L, 8, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_FINAL", C_L, 8, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_FLUSH", C_L, 4, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_FLUSHABLE", C_L, 32, 0.0, NULL, 0, 0 },

    { "PHP_OUTPUT_HANDLER_REMOVABLE", C_L, 64, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_START", C_L, 1, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_STARTED", C_L, 4096, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_STDFLAGS", C_L, 112, 0.0, NULL, 0, 0 },
    { "PHP_OUTPUT_HANDLER_WRITE", C_L, 0, 0.0, NULL, 0, 0 },
    { "PHP_PREFIX", C_S, 0, 0.0, "/usr/local", 0, 0 },
    { "PHP_QUERY_RFC1738", C_L, 1, 0.0, NULL, 0, 0 },
    { "PHP_QUERY_RFC3986", C_L, 2, 0.0, NULL, 0, 0 },
    { "PHP_ROUND_HALF_DOWN", C_L, 2, 0.0, NULL, 0, 0 },
    { "PHP_ROUND_HALF_EVEN", C_L, 3, 0.0, NULL, 0, 0 },
    { "PHP_ROUND_HALF_ODD", C_L, 4, 0.0, NULL, 0, 0 },
    { "PHP_ROUND_HALF_UP", C_L, 1, 0.0, NULL, 0, 0 },

    { "PHP_SHLIB_SUFFIX", C_S, 0, 0.0, "so", 0, 0 },
    { "PHP_SYSCONFDIR", C_S, 0, 0.0, "/usr/local/etc", 0, 0 },
    { "PHP_URL_FRAGMENT", C_L, 7, 0.0, NULL, 0, 0 },
    { "PHP_URL_HOST", C_L, 1, 0.0, NULL, 0, 0 },
    { "PHP_URL_PASS", C_L, 4, 0.0, NULL, 0, 0 },
    { "PHP_URL_PATH", C_L, 5, 0.0, NULL, 0, 0 },
    { "PHP_URL_PORT", C_L, 2, 0.0, NULL, 0, 0 },
    { "PHP_URL_QUERY", C_L, 6, 0.0, NULL, 0, 0 },
    { "PHP_URL_SCHEME", C_L, 0, 0.0, NULL, 0, 0 },
    { "PHP_URL_USER", C_L, 3, 0.0, NULL, 0, 0 },
    { "PM_STR", C_L, 131111, 0.0, NULL, 0, 0 },

    { "PSFS_ERR_FATAL", C_L, 0, 0.0, NULL, 0, 0 },
    { "PSFS_FEED_ME", C_L, 1, 0.0, NULL, 0, 0 },
    { "PSFS_FLAG_FLUSH_CLOSE", C_L, 2, 0.0, NULL, 0, 0 },
    { "PSFS_FLAG_FLUSH_INC", C_L, 1, 0.0, NULL, 0, 0 },
    { "PSFS_FLAG_NORMAL", C_L, 0, 0.0, NULL, 0, 0 },
    { "PSFS_PASS_ON", C_L, 2, 0.0, NULL, 0, 0 },



    { "RADIXCHAR", C_L, 65536, 0.0, NULL, 0, 0 },
    { "SCANDIR_SORT_ASCENDING", C_L, 0, 0.0, NULL, 0, 0 },
    { "SCANDIR_SORT_DESCENDING", C_L, 1, 0.0, NULL, 0, 0 },
    { "SCANDIR_SORT_NONE", C_L, 2, 0.0, NULL, 0, 0 },
    { "SEEK_CUR", C_L, 1, 0.0, NULL, 0, 0 },
    { "SEEK_END", C_L, 2, 0.0, NULL, 0, 0 },
    { "SEEK_SET", C_L, 0, 0.0, NULL, 0, 0 },
    { "SORT_ASC", C_L, 4, 0.0, NULL, 0, 0 },
    { "SORT_DESC", C_L, 3, 0.0, NULL, 0, 0 },
    { "SORT_FLAG_CASE", C_L, 8, 0.0, NULL, 0, 0 },
    { "SORT_LOCALE_STRING", C_L, 5, 0.0, NULL, 0, 0 },
    { "SORT_NATURAL", C_L, 6, 0.0, NULL, 0, 0 },
    { "SORT_NUMERIC", C_L, 1, 0.0, NULL, 0, 0 },
    { "SORT_REGULAR", C_L, 0, 0.0, NULL, 0, 0 },
    { "SORT_STRING", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_BUFFER_FULL", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_BUFFER_LINE", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_BUFFER_NONE", C_L, 0, 0.0, NULL, 0, 0 },
    { "STREAM_CAST_AS_STREAM", C_L, 0, 0.0, NULL, 0, 0 },
    { "STREAM_CAST_FOR_SELECT", C_L, 3, 0.0, NULL, 0, 0 },
    { "STREAM_CLIENT_ASYNC_CONNECT", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_CLIENT_CONNECT", C_L, 4, 0.0, NULL, 0, 0 },
    { "STREAM_CLIENT_PERSISTENT", C_L, 1, 0.0, NULL, 0, 0 },




    { "STREAM_CRYPTO_METHOD_SSLv23_CLIENT", C_L, 57, 0.0, NULL, 0, 0 },


    { "STREAM_CRYPTO_METHOD_SSLv2_CLIENT", C_L, 3, 0.0, NULL, 0, 0 },
    { "STREAM_CRYPTO_METHOD_SSLv2_SERVER", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_CRYPTO_METHOD_SSLv3_CLIENT", C_L, 5, 0.0, NULL, 0, 0 },
    { "STREAM_CRYPTO_METHOD_SSLv3_SERVER", C_L, 4, 0.0, NULL, 0, 0 },






    { "STREAM_CRYPTO_METHOD_TLSv1_0_CLIENT", C_L, 9, 0.0, NULL, 0, 0 },
    { "STREAM_CRYPTO_METHOD_TLSv1_0_SERVER", C_L, 8, 0.0, NULL, 0, 0 },
    { "STREAM_CRYPTO_METHOD_TLSv1_1_CLIENT", C_L, 17, 0.0, NULL, 0, 0 },
    { "STREAM_CRYPTO_METHOD_TLSv1_1_SERVER", C_L, 16, 0.0, NULL, 0, 0 },
    { "STREAM_CRYPTO_METHOD_TLSv1_2_CLIENT", C_L, 33, 0.0, NULL, 0, 0 },
    { "STREAM_CRYPTO_METHOD_TLSv1_2_SERVER", C_L, 32, 0.0, NULL, 0, 0 },







    { "STREAM_FILTER_ALL", C_L, 3, 0.0, NULL, 0, 0 },
    { "STREAM_FILTER_READ", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_FILTER_WRITE", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_IGNORE_URL", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_IPPROTO_ICMP", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_IPPROTO_IP", C_L, 0, 0.0, NULL, 0, 0 },
    { "STREAM_IPPROTO_RAW", C_L, 255, 0.0, NULL, 0, 0 },
    { "STREAM_IPPROTO_TCP", C_L, 6, 0.0, NULL, 0, 0 },
    { "STREAM_IPPROTO_UDP", C_L, 17, 0.0, NULL, 0, 0 },
    { "STREAM_IS_URL", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_META_ACCESS", C_L, 6, 0.0, NULL, 0, 0 },
    { "STREAM_META_GROUP", C_L, 5, 0.0, NULL, 0, 0 },
    { "STREAM_META_GROUP_NAME", C_L, 4, 0.0, NULL, 0, 0 },
    { "STREAM_META_OWNER", C_L, 3, 0.0, NULL, 0, 0 },
    { "STREAM_META_OWNER_NAME", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_META_TOUCH", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_MKDIR_RECURSIVE", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_MUST_SEEK", C_L, 16, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_AUTH_REQUIRED", C_L, 3, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_AUTH_RESULT", C_L, 10, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_COMPLETED", C_L, 8, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_CONNECT", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_FAILURE", C_L, 9, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_FILE_SIZE_IS", C_L, 5, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_MIME_TYPE_IS", C_L, 4, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_PROGRESS", C_L, 7, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_REDIRECTED", C_L, 6, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_RESOLVE", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_SEVERITY_ERR", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_SEVERITY_INFO", C_L, 0, 0.0, NULL, 0, 0 },
    { "STREAM_NOTIFY_SEVERITY_WARN", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_OOB", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_OPTION_BLOCKING", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_OPTION_READ_BUFFER", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_OPTION_READ_TIMEOUT", C_L, 4, 0.0, NULL, 0, 0 },
    { "STREAM_OPTION_WRITE_BUFFER", C_L, 3, 0.0, NULL, 0, 0 },
    { "STREAM_PEEK", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_PF_INET", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_PF_INET6", C_L, 10, 0.0, NULL, 0, 0 },
    { "STREAM_PF_UNIX", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_REPORT_ERRORS", C_L, 8, 0.0, NULL, 0, 0 },
    { "STREAM_SERVER_BIND", C_L, 4, 0.0, NULL, 0, 0 },
    { "STREAM_SERVER_LISTEN", C_L, 8, 0.0, NULL, 0, 0 },
    { "STREAM_SHUT_RD", C_L, 0, 0.0, NULL, 0, 0 },
    { "STREAM_SHUT_RDWR", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_SHUT_WR", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_SOCK_DGRAM", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_SOCK_RAW", C_L, 3, 0.0, NULL, 0, 0 },
    { "STREAM_SOCK_RDM", C_L, 4, 0.0, NULL, 0, 0 },
    { "STREAM_SOCK_SEQPACKET", C_L, 5, 0.0, NULL, 0, 0 },
    { "STREAM_SOCK_STREAM", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_URL_STAT_LINK", C_L, 1, 0.0, NULL, 0, 0 },
    { "STREAM_URL_STAT_QUIET", C_L, 2, 0.0, NULL, 0, 0 },
    { "STREAM_USE_PATH", C_L, 1, 0.0, NULL, 0, 0 },
    { "STR_PAD_BOTH", C_L, 2, 0.0, NULL, 0, 0 },
    { "STR_PAD_LEFT", C_L, 0, 0.0, NULL, 0, 0 },
    { "STR_PAD_RIGHT", C_L, 1, 0.0, NULL, 0, 0 },

    { "THOUSEP", C_L, 65537, 0.0, NULL, 0, 0 },
    { "TRUE", C_B, 1, 0.0, NULL, 0, 0 },
    { "T_FMT", C_L, 131114, 0.0, NULL, 0, 0 },
    { "T_FMT_AMPM", C_L, 131115, 0.0, NULL, 0, 0 },
    { "True", C_B, 1, 0.0, NULL, 0, 0 },
    { "UPLOAD_ERR_CANT_WRITE", C_L, 7, 0.0, NULL, 0, 0 },
    { "UPLOAD_ERR_EXTENSION", C_L, 8, 0.0, NULL, 0, 0 },
    { "UPLOAD_ERR_FORM_SIZE", C_L, 2, 0.0, NULL, 0, 0 },
    { "UPLOAD_ERR_INI_SIZE", C_L, 1, 0.0, NULL, 0, 0 },
    { "UPLOAD_ERR_NO_FILE", C_L, 4, 0.0, NULL, 0, 0 },
    { "UPLOAD_ERR_NO_TMP_DIR", C_L, 6, 0.0, NULL, 0, 0 },
    { "UPLOAD_ERR_OK", C_L, 0, 0.0, NULL, 0, 0 },
    { "UPLOAD_ERR_PARTIAL", C_L, 3, 0.0, NULL, 0, 0 },
    { "YESEXPR", C_L, 327680, 0.0, NULL, 0, 0 },

    { "ZEND_DEBUG_BUILD", C_B, 0, 0.0, NULL, 0, 0 },
    { "ZEND_THREAD_SAFE", C_B, 0, 0.0, NULL, 0, 0 },
    { "false", C_B, 0, 0.0, NULL, 0, 0 },
    { "null", C_N, 0, 0.0, NULL, 0, 0 },
    { "true", C_B, 1, 0.0, NULL, 0, 0 },
    { NULL, C_L, 0, 0.0, NULL, 0, 0 }
};

static void register_table_constants(void)
{
    size_t i;

    for (i = 0; php_core_constants[i].name != NULL; i++) {
        const php_const_entry *c = &php_core_constants[i];

        if (EG(php_version) < c->since) {
            continue;
        }
        if (c->until != 0 && EG(php_version) >= c->until) {
            continue;
        }
        switch (c->kind) {
            case C_L: zend_register_long_constant(c->name, c->lval); break;
            case C_D: zend_register_double_constant(c->name, c->dval); break;
            case C_S: zend_register_string_constant(c->name, c->sval); break;
            case C_B: zend_register_bool_constant(c->name, (int) c->lval); break;
            case C_N: zend_register_null_constant(c->name); break;
        }
    }
}

void zend_register_constants(void)
{
    register_table_constants();
    register_version_constants();
    register_numeric_constants();
    register_error_constants();

    /* ext/json and ext/mbstring own their own constant blocks; both of them
     * carry version gates of their own for the options that arrived later
     * (JSON_THROW_ON_ERROR in 7.3, MB_CASE_FOLD in 7.3, ...). */
    php_json_register_constants();
    php_mbstring_register_constants();
}

/* ==========================================================================
 *  Classes
 * ========================================================================== */

void zend_register_classes(void)
{
    zend_register_default_classes();
}
