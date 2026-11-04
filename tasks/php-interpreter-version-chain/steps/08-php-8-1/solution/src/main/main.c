/* main.c -- engine start-up and shutdown, output, INI, error printing.
 *
 * Everything a running script sees that is not a value, a statement or a
 * library function is decided here: where bytes go, which diagnostics are
 * shown and in what words, and what the process exits with.
 *
 * The output of php-src is the specification.  With display_errors=1,
 * html_errors=0 and both error_prepend_string and error_append_string empty
 * -- the settings php-src's own run-tests.php forces on every .phpt -- a
 * diagnostic is one php_printf of
 *
 *     "\n" <Label> ": " <message> " in " <file> " on line " <line> "\n"
 *
 * (main/main.c's php_error_cb, the `!PG(html_errors)` arm).  The label for
 * E_RECOVERABLE_ERROR is the one thing in that table that moved: php 5.6 and
 * 7.0 say "Catchable fatal error", 7.1 onwards says "Recoverable fatal
 * error".  Everything else has read the same since 5.x.
 *
 * Errors travel through the same FILE* as echo, because php-src's CLI routes
 * them through the output layer rather than stderr; that is what keeps
 * "before\n" ahead of the fatal error that follows it.
 */

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include "zend_exceptions.h"
#include "php_main.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* EG_ itself is defined by Zend/zend_operators.c. */

/* ==========================================================================
 *  Output
 * ==========================================================================
 * One block-buffered stdout for the whole run.  php-src's CLI sets
 * implicit_flush=1 but still buffers through its own output layer; what the
 * tests actually depend on is only that echo and diagnostics interleave in
 * program order, which a single stream gives us for free.
 */

static int php_output_ready;

static void php_output_startup(void)
{
    if (!php_output_ready) {
        php_output_ready = 1;
        setvbuf(stdout, NULL, _IOFBF, 65536);
    }
}

void php_write(const char *s, size_t len)
{
    if (s == NULL || len == 0) {
        return;
    }
    php_output_startup();
    fwrite(s, 1, len, stdout);
}

void php_write_str(zend_string *s)
{
    if (s != NULL) {
        php_write(ZSTR_VAL(s), ZSTR_LEN(s));
    }
}

void php_output_flush(void)
{
    if (php_output_ready) {
        fflush(stdout);
    }
}

/* Plain C formatting -- the callers are C code, not PHP's sprintf(). */
void php_printf(const char *fmt, ...)
{
    char stackbuf[1024];
    char *buf = stackbuf;
    va_list ap, ap2;
    int len;

    if (fmt == NULL) {
        return;
    }
    va_start(ap, fmt);
    va_copy(ap2, ap);
    len = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);

    if (len < 0) {
        va_end(ap2);
        return;
    }
    if ((size_t) len >= sizeof(stackbuf)) {
        buf = emalloc((size_t) len + 1);
        vsnprintf(buf, (size_t) len + 1, fmt, ap2);
    }
    va_end(ap2);

    php_write(buf, (size_t) len);
    if (buf != stackbuf) {
        efree(buf);
    }
}

static zend_string *php_vstrpprintf(const char *fmt, va_list ap)
{
    va_list ap2;
    zend_string *ret;
    int len;

    if (fmt == NULL) {
        return zend_string_init("", 0);
    }
    va_copy(ap2, ap);
    len = vsnprintf(NULL, 0, fmt, ap);
    if (len < 0) {
        va_end(ap2);
        return zend_string_init("", 0);
    }
    ret = zend_string_alloc((size_t) len);
    vsnprintf(ZSTR_VAL(ret), (size_t) len + 1, fmt, ap2);
    va_end(ap2);
    return ret;
}

/* ==========================================================================
 *  Bailout
 * ==========================================================================
 * A fatal error must not return to the evaluator frame that raised it.
 * php-src longjmp()s out; so do we.  Nothing needs unwinding: efree() is a
 * no-op and the process is on its way out.
 */

jmp_buf php_bailout_env;
int     php_bailout_armed;

void php_bailout(void)
{
    if (php_bailout_armed) {
        php_bailout_armed = 0;
        longjmp(php_bailout_env, 1);
    }
    php_output_flush();
    exit(EG(exit_status));
}

/* ==========================================================================
 *  INI
 * ==========================================================================
 * EG(ini) is name -> zend_string.  php accepts any -d, known or not, so this
 * is a plain store with a table of defaults on top.  A default carries the
 * version window it applies in, because several of them moved:
 * serialize_precision went from 17 to -1 in 7.1, and zend.assertions did not
 * exist before 7.0.
 */

typedef struct {
    const char *name;
    const char *value;
    long        since;      /* 0 = always */
    long        until;      /* 0 = never removed */
} php_ini_default;

/* The directive table is taken from the reference php builds: every entry
 * `php -n` reports through ini_get_all(), for the directives this interpreter
 * could plausibly own (the Core and standard globals plus the assert.*,
 * date.*, mbstring.*, zend.* and friends groups), with the CLI SAPI's own
 * overrides already folded in because the dump came from the CLI.  A value
 * that changed between releases becomes two rows with adjoining version
 * windows; each gated row carries a VERSIONGATE marker.
 *
 * error_reporting is deliberately the empty string here, exactly as php
 * reports it when no php.ini was read: the live mask lives in
 * EG(error_reporting) and is seeded separately below. */
static const php_ini_default php_ini_defaults[] = {
    { "SMTP", "localhost", 0, 0 },
    { "allow_url_fopen", "1", 0, 0 },
    { "allow_url_include", "0", 0, 0 },
    /* VERSIONGATE:70000 changed/removed in 70000 */
    { "always_populate_raw_post_data", "0", 0, 70000 },
    { "arg_separator.input", "&", 0, 0 },
    { "arg_separator.output", "&", 0, 0 },
    /* VERSIONGATE:70000 changed/removed in 70000 */
    { "asp_tags", "0", 0, 70000 },
    { "assert.active", "1", 0, 0 },
    { "assert.bail", "0", 0, 0 },
    { "assert.callback", "", 0, 0 },
    /* VERSIONGATE:70000 added in 70000; changed/removed in 80000 */
    { "assert.exception", "0", 70000, 80000 },
    /* VERSIONGATE:80000 added in 80000 */
    { "assert.exception", "1", 80000, 0 },
    /* VERSIONGATE:80000 changed/removed in 80000 */
    { "assert.quiet_eval", "0", 0, 80000 },
    { "assert.warning", "1", 0, 0 },
    { "auto_append_file", "", 0, 0 },
    { "auto_detect_line_endings", "0", 0, 0 },
    { "auto_globals_jit", "1", 0, 0 },
    { "auto_prepend_file", "", 0, 0 },
    { "browscap", "", 0, 0 },
    { "cli.pager", "", 0, 0 },
    { "cli.prompt", "\\b \\> ", 0, 0 },
    { "date.default_latitude", "31.7667", 0, 0 },
    { "date.default_longitude", "35.2333", 0, 0 },
    /* VERSIONGATE:80000 changed/removed in 80000 */
    { "date.sunrise_zenith", "90.583333", 0, 80000 },
    /* VERSIONGATE:80000 added in 80000 */
    { "date.sunrise_zenith", "90.833333", 80000, 0 },
    /* VERSIONGATE:80000 changed/removed in 80000 */
    { "date.sunset_zenith", "90.583333", 0, 80000 },
    /* VERSIONGATE:80000 added in 80000 */
    { "date.sunset_zenith", "90.833333", 80000, 0 },


    { "default_charset", "UTF-8", 0, 0 },
    { "default_mimetype", "text/html", 0, 0 },
    { "default_socket_timeout", "60", 0, 0 },
    { "disable_classes", "", 0, 0 },
    { "disable_functions", "", 0, 0 },
    { "display_errors", "1", 0, 0 },
    /* VERSIONGATE:80000 changed/removed in 80000 */
    { "display_startup_errors", "0", 0, 80000 },
    /* VERSIONGATE:80000 added in 80000 */
    { "display_startup_errors", "1", 80000, 0 },
    { "doc_root", "", 0, 0 },
    { "docref_ext", "", 0, 0 },
    { "docref_root", "", 0, 0 },
    { "enable_dl", "1", 0, 0 },
    { "enable_post_data_reading", "1", 0, 0 },
    { "error_append_string", "", 0, 0 },
    { "error_log", "", 0, 0 },

    { "error_prepend_string", "", 0, 0 },
    { "error_reporting", "", 0, 0 },
    /* VERSIONGATE:70100 changed/removed in 70100 */
    { "exit_on_timeout", "0", 0, 70100 },
    { "expose_php", "1", 0, 0 },
    /* VERSIONGATE:70000 changed/removed in 70000 */
    { "extension_dir", "/usr/local/lib/php/extensions/no-debug-non-zts-20131226", 0, 70000 },
    /* VERSIONGATE:70000 added in 70000; changed/removed in 70100 */
    { "extension_dir", "/usr/local/lib/php/extensions/no-debug-non-zts-20151012", 70000, 70100 },
    /* VERSIONGATE:70100 added in 70100; changed/removed in 70200 */
    { "extension_dir", "/usr/local/lib/php/extensions/no-debug-non-zts-20160303", 70100, 70200 },
    /* VERSIONGATE:70200 added in 70200; changed/removed in 70300 */
    { "extension_dir", "/usr/local/lib/php/extensions/no-debug-non-zts-20170718", 70200, 70300 },
    /* VERSIONGATE:70300 added in 70300; changed/removed in 70400 */
    { "extension_dir", "/usr/local/lib/php/extensions/no-debug-non-zts-20180731", 70300, 70400 },
    /* VERSIONGATE:70400 added in 70400; changed/removed in 80000 */
    { "extension_dir", "/usr/local/lib/php/extensions/no-debug-non-zts-20190902", 70400, 80000 },
    /* VERSIONGATE:80000 added in 80000; changed/removed in 80100 */
    { "extension_dir", "/usr/local/lib/php/extensions/no-debug-non-zts-20200930", 80000, 80100 },
    /* VERSIONGATE:80100 added in 80100; changed/removed in 80200 */
    { "extension_dir", "/usr/local/lib/php/extensions/no-debug-non-zts-20210902", 80100, 80200 },



    { "file_uploads", "1", 0, 0 },
    { "from", "", 0, 0 },
    /* VERSIONGATE:70100 added in 70100 */
    { "hard_timeout", "2", 70100, 0 },
    { "highlight.comment", "#FF8000", 0, 0 },
    { "highlight.default", "#0000BB", 0, 0 },
    { "highlight.html", "#000000", 0, 0 },
    { "highlight.keyword", "#007700", 0, 0 },
    { "highlight.string", "#DD0000", 0, 0 },
    { "html_errors", "0", 0, 0 },
    { "ignore_repeated_errors", "0", 0, 0 },
    { "ignore_repeated_source", "0", 0, 0 },
    { "ignore_user_abort", "0", 0, 0 },
    { "implicit_flush", "1", 0, 0 },
    { "include_path", ".:/usr/local/lib/php", 0, 0 },
    { "input_encoding", "", 0, 0 },
    { "internal_encoding", "", 0, 0 },
    { "log_errors", "0", 0, 0 },
    /* VERSIONGATE:80100 changed/removed in 80100 */
    { "log_errors_max_len", "1024", 0, 80100 },
    { "mail.add_x_header", "0", 0, 0 },
    { "mail.force_extra_parameters", "", 0, 0 },
    { "mail.log", "", 0, 0 },

    { "max_execution_time", "0", 0, 0 },
    { "max_file_uploads", "20", 0, 0 },
    { "max_input_nesting_level", "64", 0, 0 },
    { "max_input_time", "-1", 0, 0 },
    { "max_input_vars", "1000", 0, 0 },
    /* VERSIONGATE:80000 added in 80000 */
    { "max_multipart_body_parts", "-1", 80000, 0 },
    { "mbstring.detect_order", "", 0, 0 },
    { "mbstring.encoding_translation", "0", 0, 0 },
    /* VERSIONGATE:80000 changed/removed in 80000 */
    { "mbstring.func_overload", "0", 0, 80000 },
    { "mbstring.http_input", "", 0, 0 },
    { "mbstring.http_output", "", 0, 0 },
    { "mbstring.http_output_conv_mimetypes", "^(text/|application/xhtml\\+xml)", 0, 0 },
    { "mbstring.internal_encoding", "", 0, 0 },
    { "mbstring.language", "neutral", 0, 0 },
    /* VERSIONGATE:70400 added in 70400 */
    { "mbstring.regex_retry_limit", "1000000", 70400, 0 },
    /* VERSIONGATE:70300 added in 70300 */
    { "mbstring.regex_stack_limit", "100000", 70300, 0 },
    { "mbstring.strict_detection", "0", 0, 0 },
    { "mbstring.substitute_character", "", 0, 0 },
    { "memory_limit", "128M", 0, 0 },
    { "open_basedir", "", 0, 0 },
    { "output_buffering", "0", 0, 0 },
    { "output_encoding", "", 0, 0 },
    { "output_handler", "", 0, 0 },
    { "post_max_size", "8M", 0, 0 },
    { "precision", "14", 0, 0 },
    /* VERSIONGATE:70000 changed/removed in 70000 */
    { "realpath_cache_size", "16K", 0, 70000 },
    /* VERSIONGATE:70000 added in 70000 */
    { "realpath_cache_size", "4096K", 70000, 0 },
    { "realpath_cache_ttl", "120", 0, 0 },
    { "register_argc_argv", "1", 0, 0 },
    { "report_memleaks", "1", 0, 0 },
    { "report_zend_debug", "0", 0, 0 },
    { "request_order", "", 0, 0 },
    { "sendmail_from", "", 0, 0 },
    /* VERSIONGATE:70300 changed/removed in 70300 */
    { "sendmail_path", " -t -i ", 0, 70300 },
    /* VERSIONGATE:70300 added in 70300 */
    { "sendmail_path", "/usr/sbin/sendmail -t -i", 70300, 0 },
    /* VERSIONGATE:70100 changed/removed in 70100 */
    { "serialize_precision", "17", 0, 70100 },
    /* VERSIONGATE:70100 added in 70100 */
    { "serialize_precision", "-1", 70100, 0 },
    { "short_open_tag", "1", 0, 0 },
    { "smtp_port", "25", 0, 0 },
    { "sys_temp_dir", "", 0, 0 },
    /* VERSIONGATE:80000 changed/removed in 80000 */
    { "track_errors", "0", 0, 80000 },
    { "unserialize_callback_func", "", 0, 0 },
    /* VERSIONGATE:70400 added in 70400 */
    { "unserialize_max_depth", "4096", 70400, 0 },
    { "upload_max_filesize", "2M", 0, 0 },
    { "upload_tmp_dir", "", 0, 0 },
    /* VERSIONGATE:70100 added in 70100 */
    { "url_rewriter.hosts", "", 70100, 0 },
    /* VERSIONGATE:70100 changed/removed in 70100 */
    { "url_rewriter.tags", "a=href,area=href,frame=src,form=,fieldset=", 0, 70100 },
    /* VERSIONGATE:70100 added in 70100 */
    { "url_rewriter.tags", "form=", 70100, 0 },
    { "user_agent", "", 0, 0 },
    { "user_dir", "", 0, 0 },
    { "user_ini.cache_ttl", "300", 0, 0 },
    { "user_ini.filename", ".user.ini", 0, 0 },
    { "variables_order", "EGPCS", 0, 0 },
    { "xmlrpc_error_number", "0", 0, 0 },
    { "xmlrpc_errors", "0", 0, 0 },
    /* VERSIONGATE:70000 added in 70000 */
    { "zend.assertions", "1", 70000, 0 },
    { "zend.detect_unicode", "1", 0, 0 },
    { "zend.enable_gc", "1", 0, 0 },
    /* VERSIONGATE:70400 added in 70400 */
    { "zend.exception_ignore_args", "0", 70400, 0 },
    /* VERSIONGATE:80000 added in 80000 */
    { "zend.exception_string_param_max_len", "15", 80000, 0 },

    { "zend.multibyte", "0", 0, 0 },

    { "zend.script_encoding", "", 0, 0 },
    /* VERSIONGATE:70100 added in 70100 */
    { "zend.signal_check", "0", 70100, 0 },
    { NULL, NULL, 0, 0 }
};

zend_string *php_ini_get(const char *name)
{
    zval *v;

    if (name == NULL || EG(ini) == NULL) {
        return NULL;
    }
    v = zend_hash_str_find(EG(ini), name, strlen(name));
    return (v != NULL && Z_TYPE_P(v) == IS_STRING) ? Z_STR_P(v) : NULL;
}

static int php_ini_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void php_ini_set(const char *name, const char *value)
{
    const char *start, *end;
    size_t len;

    if (name == NULL || EG(ini) == NULL) {
        return;
    }
    if (value == NULL) {
        value = "";
    }

    /* php trims the directive name but leaves the value exactly as written:
     *     php -d "precision = 10"   ->   ini_get("precision") === " 10"
     * The numeric readers below skip the leading blank themselves, the way
     * strtol() and strtod() do, so the value never needs touching. */
    start = name;
    while (*start != '\0' && php_ini_is_space(*start)) {
        start++;
    }
    end = start + strlen(start);
    while (end > start && php_ini_is_space(end[-1])) {
        end--;
    }
    len = (size_t) (end - start);
    if (len == 0) {
        return;
    }

    zend_hash_str_update(EG(ini), start, len, zval_zstr(zend_string_initz(value)));

    /* error_reporting has a live copy in the globals, because every
     * diagnostic consults it and the `@` operator edits it. */
    if (len == sizeof("error_reporting") - 1 &&
        strncmp(start, "error_reporting", len) == 0) {
        EG(error_reporting) = php_parse_error_reporting(value, EG(error_reporting));
    }
}

zend_long php_ini_long(const char *name, zend_long dflt)
{
    zend_string *s = php_ini_get(name);
    char *end;
    long long v;

    if (s == NULL || ZSTR_LEN(s) == 0) {
        return dflt;
    }
    errno = 0;
    v = strtoll(ZSTR_VAL(s), &end, 10);
    if (end == ZSTR_VAL(s)) {
        return dflt;
    }
    /* php's INI shorthand: 1K / 2M / 3G */
    switch (*end) {
        case 'k': case 'K': v *= 1024LL; break;
        case 'm': case 'M': v *= 1024LL * 1024LL; break;
        case 'g': case 'G': v *= 1024LL * 1024LL * 1024LL; break;
        default: break;
    }
    return (zend_long) v;
}

double php_ini_double(const char *name, double dflt)
{
    zend_string *s = php_ini_get(name);
    char *end;
    double v;

    if (s == NULL || ZSTR_LEN(s) == 0) {
        return dflt;
    }
    v = strtod(ZSTR_VAL(s), &end);
    return (end == ZSTR_VAL(s)) ? dflt : v;
}

/* strcasecmp() is POSIX and -std=c11 hides it, so ASCII folding is done here. */
static int php_ascii_caseeq(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        unsigned char ca = (unsigned char) *a;
        unsigned char cb = (unsigned char) *b;

        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char) (ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char) (cb - 'A' + 'a');
        if (ca != cb) {
            return 0;
        }
    }
    return *a == *b;
}

int php_ini_bool(const char *name, int dflt)
{
    zend_string *s = php_ini_get(name);
    const char *p;

    if (s == NULL) {
        return dflt;
    }
    p = ZSTR_VAL(s);
    if (ZSTR_LEN(s) == 0) {
        return 0;
    }
    if (php_ascii_caseeq(p, "off") || php_ascii_caseeq(p, "no") ||
        php_ascii_caseeq(p, "false") || strcmp(p, "0") == 0) {
        return 0;
    }
    if (php_ascii_caseeq(p, "on") || php_ascii_caseeq(p, "yes") ||
        php_ascii_caseeq(p, "true")) {
        return 1;
    }
    return strtol(p, NULL, 10) != 0 ? 1 : 0;
}

/* ==========================================================================
 *  error_reporting text
 * ==========================================================================
 * run-tests.php passes the plain number 32767, but `-d error_reporting=E_ALL
 * & ~E_NOTICE` is equally legal, so the INI scanner's little constant
 * expression grammar is reproduced here:
 *
 *     expr  := xor ('|' xor)*
 *     xor   := and ('^' and)*
 *     and   := unary ('&' unary)*
 *     unary := '~' unary | '(' expr ')' | number | E_NAME
 *
 * E_ALL is version-dependent: E_STRICT was dropped from it in 8.4, taking the
 * value from 32767 down to 30719.
 */

static int php_e_all_value(void)
{

    return E_ALL;
}

typedef struct { const char *name; int value; } php_error_constant;

static int php_error_constant_value(const char *name, size_t len, int *out)
{
    static const php_error_constant tbl[] = {
        { "E_ERROR",             E_ERROR },
        { "E_WARNING",           E_WARNING },
        { "E_PARSE",             E_PARSE },
        { "E_NOTICE",            E_NOTICE },
        { "E_CORE_ERROR",        E_CORE_ERROR },
        { "E_CORE_WARNING",      E_CORE_WARNING },
        { "E_COMPILE_ERROR",     E_COMPILE_ERROR },
        { "E_COMPILE_WARNING",   E_COMPILE_WARNING },
        { "E_USER_ERROR",        E_USER_ERROR },
        { "E_USER_WARNING",      E_USER_WARNING },
        { "E_USER_NOTICE",       E_USER_NOTICE },
        { "E_STRICT",            E_STRICT },
        { "E_RECOVERABLE_ERROR", E_RECOVERABLE_ERROR },
        { "E_DEPRECATED",        E_DEPRECATED },
        { "E_USER_DEPRECATED",   E_USER_DEPRECATED },
        { NULL, 0 }
    };
    size_t i;

    if (len == 5 && strncmp(name, "E_ALL", 5) == 0) {
        *out = php_e_all_value();
        return 1;
    }
    for (i = 0; tbl[i].name != NULL; i++) {
        if (strlen(tbl[i].name) == len && strncmp(tbl[i].name, name, len) == 0) {
            *out = tbl[i].value;
            return 1;
        }
    }
    return 0;
}

typedef struct { const char *p; int ok; } php_er_parser;

static int php_er_expr(php_er_parser *s);

static void php_er_skip_ws(php_er_parser *s)
{
    while (*s->p == ' ' || *s->p == '\t') {
        s->p++;
    }
}

static int php_er_unary(php_er_parser *s)
{
    php_er_skip_ws(s);

    if (*s->p == '~') {
        s->p++;
        return ~php_er_unary(s);
    }
    if (*s->p == '(') {
        int v;
        s->p++;
        v = php_er_expr(s);
        php_er_skip_ws(s);
        if (*s->p == ')') {
            s->p++;
        } else {
            s->ok = 0;
        }
        return v;
    }
    if (*s->p == '-' || (*s->p >= '0' && *s->p <= '9')) {
        char *end;
        long v = strtol(s->p, &end, 0);
        if (end == s->p) {
            s->ok = 0;
            return 0;
        }
        s->p = end;
        return (int) v;
    }
    if ((*s->p >= 'A' && *s->p <= 'Z') || (*s->p >= 'a' && *s->p <= 'z') || *s->p == '_') {
        const char *start = s->p;
        int v = 0;
        while ((*s->p >= 'A' && *s->p <= 'Z') || (*s->p >= 'a' && *s->p <= 'z') ||
               (*s->p >= '0' && *s->p <= '9') || *s->p == '_') {
            s->p++;
        }
        if (!php_error_constant_value(start, (size_t) (s->p - start), &v)) {
            s->ok = 0;
        }
        return v;
    }
    s->ok = 0;
    return 0;
}

static int php_er_and(php_er_parser *s)
{
    int v = php_er_unary(s);

    for (;;) {
        php_er_skip_ws(s);
        if (*s->p != '&') {
            return v;
        }
        s->p++;
        v &= php_er_unary(s);
    }
}

static int php_er_xor(php_er_parser *s)
{
    int v = php_er_and(s);

    for (;;) {
        php_er_skip_ws(s);
        if (*s->p != '^') {
            return v;
        }
        s->p++;
        v ^= php_er_and(s);
    }
}

static int php_er_expr(php_er_parser *s)
{
    int v = php_er_xor(s);

    for (;;) {
        php_er_skip_ws(s);
        if (*s->p != '|') {
            return v;
        }
        s->p++;
        v |= php_er_xor(s);
    }
}

int php_parse_error_reporting(const char *str, int dflt)
{
    php_er_parser s;
    int v;

    if (str == NULL) {
        return dflt;
    }
    s.p = str;
    s.ok = 1;
    v = php_er_expr(&s);
    php_er_skip_ws(&s);
    if (!s.ok || *s.p != '\0') {
        return dflt;
    }
    return v;
}

/* ==========================================================================
 *  Diagnostics
 * ========================================================================== */

const char *php_error_type_name(int type)
{
    switch (type) {
        case E_ERROR:
        case E_CORE_ERROR:
        case E_COMPILE_ERROR:
        case E_USER_ERROR:
            return "Fatal error";
        case E_RECOVERABLE_ERROR:
            /* VERSIONGATE:70100 else-branch is the 5.6 label */
            if (EG(php_version) >= 70100) {
                return "Recoverable fatal error";
            }
            return "Catchable fatal error";
        case E_WARNING:
        case E_CORE_WARNING:
        case E_COMPILE_WARNING:
        case E_USER_WARNING:
            return "Warning";
        case E_PARSE:
            return "Parse error";
        case E_NOTICE:
        case E_USER_NOTICE:
            return "Notice";
        case E_STRICT:

            return "Strict Standards";
        case E_DEPRECATED:
        case E_USER_DEPRECATED:
            return "Deprecated";
        default:
            return "Unknown error";
    }
}

int php_effective_error_reporting(void)
{
    if (EG(silence) <= 0) {
        return EG(error_reporting);
    }
    /* The `@` operator.  Before 8.0 ZEND_BEGIN_SILENCE set error_reporting to
     * 0 and hid everything, fatal errors included; from 8.0 it only narrows,
     *     EG(error_reporting) &= E_FATAL_ERRORS;
     * so a fatal error is still displayed. */
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 80000) {
        return EG(error_reporting) & PHP_E_FATAL_ERRORS;
    }
    return 0;
}

void php_error_display(int type, zend_string *filename, uint32_t lineno,
                       zend_string *message)
{
    const char *file = (filename != NULL && ZSTR_LEN(filename) > 0)
                       ? ZSTR_VAL(filename) : "Unknown";

    /* php-src writes diagnostics through the output layer, so anything echoed
     * before this point has to be on its way out first. */
    php_output_flush();
    php_printf("\n%s: %s in %s on line %u\n",
               php_error_type_name(type),
               message != NULL ? ZSTR_VAL(message) : "",
               file,
               (unsigned) lineno);
    php_output_flush();
}

int (*php_user_error_handler)(int type, zend_string *message,
                              zend_string *filename, uint32_t lineno);

/* ext/standard owns set_error_handler(); it publishes this to hand a
 * diagnostic to the installed handler, and answers 1 when the handler took
 * it.  The reference is weak so that a build without ext/standard still
 * links -- the engine simply prints everything itself. */
extern int php_call_user_error_handler(int type, const char *message,
                                       const char *filename, uint32_t lineno)
    __attribute__((weak));

/* The six php-src refuses to route to user space: a script cannot safely
 * recover from them (Zend/zend.c's zend_error_va_list). */
#define PHP_E_NEVER_HANDLED \
    (E_ERROR | E_PARSE | E_CORE_ERROR | E_CORE_WARNING | \
     E_COMPILE_ERROR | E_COMPILE_WARNING)

/* Offer a diagnostic to set_error_handler().  php-src gates this on the mask
 * the handler was registered with and NOT on error_reporting(): a handler
 * still sees an error that error_reporting(0) would have hidden, which is what
 * lets a handler observe error_reporting() === 0 and decide for itself.  The
 * error_reporting() gate applies only to the engine's own printing, below. */
static int php_offer_to_user_handler(int type, zend_string *filename,
                                     uint32_t lineno, zend_string *message)
{
    const char *file = (filename != NULL) ? ZSTR_VAL(filename) : "";
    const char *msg = (message != NULL) ? ZSTR_VAL(message) : "";

    if ((type & PHP_E_NEVER_HANDLED) != 0) {
        return 0;
    }
    if (php_call_user_error_handler != NULL) {
        return php_call_user_error_handler(type, msg, file, lineno);
    }
    if (php_user_error_handler != NULL) {
        return php_user_error_handler(type, message, filename, lineno);
    }
    return 0;
}

static void php_error_raise(int type, zend_string *filename, uint32_t lineno,
                            zend_string *message)
{
    int fatal = (type & PHP_E_FATAL_ERRORS) != 0;
    int handled;

    if (filename == NULL) {
        filename = EG(filename);
    }

    handled = php_offer_to_user_handler(type, filename, lineno, message);

    if (!handled && (php_effective_error_reporting() & type) != 0) {
        php_error_display(type, filename, lineno, message);
    }

    /* A handler that took the error also took the decision to stop: php-src
     * never reaches php_error_cb for it, so even E_USER_ERROR carries on. */
    if (fatal && !handled) {
        EG(exit_status) = 255;
        EG(exiting) = 1;
        php_bailout();
    }
}

void zend_error(int type, const char *fmt, ...)
{
    zend_string *message;
    va_list ap;

    va_start(ap, fmt);
    message = php_vstrpprintf(fmt, ap);
    va_end(ap);

    php_error_raise(type, EG(filename), EG(lineno), message);
}

void zend_error_at(int type, zend_string *file, uint32_t line, const char *fmt, ...)
{
    zend_string *message;
    va_list ap;

    va_start(ap, fmt);
    message = php_vstrpprintf(fmt, ap);
    va_end(ap);

    php_error_raise(type, file, line, message);
}

void zend_fatal(const char *fmt, ...)
{
    zend_string *message;
    va_list ap;

    va_start(ap, fmt);
    message = php_vstrpprintf(fmt, ap);
    va_end(ap);

    php_error_raise(E_ERROR, EG(filename), EG(lineno), message);
    /* php_error_raise() does not come back for a fatal, but the compiler has
     * no way to know that through the function pointer above. */
    exit(255);
}

/* ==========================================================================
 *  Uncaught exceptions
 * ==========================================================================
 * php-src's zend_exception_error() turns the object into its __toString()
 * text and hands that to the ordinary fatal-error path with the message
 * "Uncaught %s\n  thrown", using the exception's own file and line:
 *
 *   5.6  Fatal error: Uncaught exception 'E' with message 'm' in FILE:LINE
 *        Stack trace:
 *        #0 {main}
 *          thrown in FILE on line LINE
 *
 *   7.0+ Fatal error: Uncaught E: m in FILE:LINE
 *        ...
 *
 * The wording difference lives entirely inside __toString(), which is why the
 * two spellings need no special case here.  The message text is passed as a
 * %s argument so that a '%' inside it stays literal.
 */

void php_report_uncaught(zend_object *ex)
{
    zend_string *str, *file;
    zend_long line;

    if (ex == NULL) {
        return;
    }

    /* php-src clears the exception before reporting, so that __toString() can
     * run without tripping over a pending throw. */
    EG(exception) = NULL;

    str  = zend_exception_to_string(ex);
    file = zend_exception_get_file(ex);
    line = zend_exception_get_line(ex);

    EG(exit_status) = 255;
    zend_error_at(E_ERROR, file, (uint32_t) line, "Uncaught %s\n  thrown",
                  str != NULL ? ZSTR_VAL(str) : "");
}

void php_report_parse_error(zend_string *message, zend_string *filename,
                            uint32_t lineno)
{
    EG(exit_status) = 255;
    /* A parse error is not routed through the bailout: the SAPI has nothing
     * running yet and simply stops. */
    if ((php_effective_error_reporting() & E_PARSE) != 0) {
        php_error_display(E_PARSE, filename, lineno, message);
    }
}

/* ==========================================================================
 *  Start-up and shutdown
 * ========================================================================== */

const char *php_sapi_name   = "cli";
const char *php_binary_path = "";

static void php_ini_install_defaults(void)
{
    size_t i;
    int default_er;

    /* php's built-in error_reporting when no php.ini is read: E_ALL without
     * the three quiet levels before 8.0, plain E_ALL from 8.0 (which itself
     * loses E_STRICT in 8.4).  Measured: 22519 / 32767 / 30719. */
    /* VERSIONGATE:80000 else-branch is the 5.6 default */
    if (EG(php_version) >= 80000) {
        default_er = php_e_all_value();
    } else {
        default_er = E_ALL & ~E_NOTICE & ~E_STRICT & ~E_DEPRECATED;
    }
    EG(error_reporting) = default_er;

    for (i = 0; php_ini_defaults[i].name != NULL; i++) {
        const php_ini_default *d = &php_ini_defaults[i];

        if (d->since != 0 && EG(php_version) < d->since) {
            continue;
        }
        if (d->until != 0 && EG(php_version) >= d->until) {
            continue;
        }
        php_ini_set(d->name, d->value);
    }
}

void php_engine_startup(void)
{
    php_output_startup();

    EG(function_table)  = zend_hash_new();
    EG(class_table)     = zend_hash_new();
    EG(constants)       = zend_hash_new();
    EG(symbol_table)    = zend_hash_new();
    EG(included_files)  = zend_hash_new();
    EG(ini)             = zend_hash_new();
    EG(autoloaders)     = zend_hash_new();
    EG(interned)        = zend_hash_new();

    EG(current)         = NULL;
    EG(exception)       = NULL;
    EG(filename)        = NULL;
    EG(lineno)          = 0;
    EG(silence)         = 0;
    EG(exit_status)     = 0;
    EG(exiting)         = 0;
    EG(next_obj_handle) = 0;
    EG(in_shutdown)     = 0;
    EG(strict_types)    = 0;
    EG(suppress_deprecations) = 0;

    php_ini_install_defaults();
}

void php_request_shutdown(void)
{
    if (EG(in_shutdown)) {
        php_output_flush();
        return;
    }
    EG(in_shutdown) = 1;

    /* An exception that made it all the way out is reported here, exactly as
     * php_execute_script() does after zend_execute_scripts() returns. */
    if (EG(exception) != NULL) {
        zend_object *ex = EG(exception);
        EG(exception) = NULL;
        EG(exit_status) = 255;
        php_report_uncaught(ex);
    }

    php_output_flush();
}

/* ==========================================================================
 *  Superglobals
 * ==========================================================================
 * The SAPI creates these in the global symbol table; php makes them visible
 * in every scope without a `global` declaration, which is a property of the
 * *name*, not of the table.  The evaluator asks here rather than carrying its
 * own copy of the list.
 */

int php_is_auto_global(const zend_string *name)
{
    static const char *const names[] = {
        "GLOBALS", "_SERVER", "_GET", "_POST", "_COOKIE", "_FILES",
        "_ENV", "_REQUEST", "_SESSION", NULL
    };
    size_t i;

    if (name == NULL) {
        return 0;
    }
    for (i = 0; names[i] != NULL; i++) {
        size_t len = strlen(names[i]);

        if (ZSTR_LEN(name) == len && memcmp(ZSTR_VAL(name), names[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ==========================================================================
 *  Argument diagnostics for internal functions
 * ==========================================================================
 * Zend/zend_API.h declares these four next to the ARG() macros but no module
 * claimed them, and every extension needs them, so they live with the rest of
 * the error printing.
 *
 * Return convention follows php-src's zend_parse_arg_*(): non-zero on
 * success, zero when the argument could not be used -- and in that case the
 * diagnostic has already been issued, so the caller only has to return.
 *
 * Both wordings are load-bearing and both changed in 8.0, when an argument
 * mistake stopped being a warning and became a thrown Error:
 *
 *   5.6  Warning: strlen() expects exactly 1 parameter, 0 given
 *        Warning: strlen() expects parameter 1 to be string, array given
 *   8.0  ArgumentCountError: strlen() expects exactly 1 argument, 0 given
 *        TypeError: strlen(): Argument #1 ($string) must be of type string, array given
 *
 * The 8.0 TypeError names the parameter, which a php_function_entry does not
 * carry; that form is emitted without the "($name)" part.
 */

/* php-src's pre-8.0 zend_zval_type_name(): the gettype() spelling, except
 * that null is lower case here. */
static const char *php_arg_type_name(const zval *v)
{
    /* VERSIONGATE:80000 else-branch is the 5.6 spelling (integer/double/boolean) */
    if (EG(php_version) >= 80000) {
        return zend_zval_type_name(v);
    }
    switch (Z_TYPE_P(v)) {
        case IS_UNDEF:
        case IS_NULL:   return "null";
        case IS_FALSE:
        case IS_TRUE:   return "boolean";
        case IS_LONG:   return "integer";
        case IS_DOUBLE: return "double";
        case IS_STRING: return "string";
        case IS_ARRAY:  return "array";
        case IS_OBJECT: return "object";
        default:        return "unknown";
    }
}

void zend_wrong_param_count(const char *fname, uint32_t given, uint32_t min, uint32_t max)
{
    const char *relation = (min == max) ? "exactly"
                                        : ((given < min) ? "at least" : "at most");
    uint32_t shown = (given < min) ? min : max;
    const char *plural = (shown == 1) ? "" : "s";

    /* VERSIONGATE:80000 else-branch is the 5.6 form: a warning, not a throw */
    if (EG(php_version) >= 80000) {
        zend_throw_error("ArgumentCountError", "%s() expects %s %u argument%s, %u given",
                         fname != NULL ? fname : "?", relation, (unsigned) shown,
                         plural, (unsigned) given);
        return;
    }
    zend_error(E_WARNING, "%s() expects %s %u parameter%s, %u given",
               fname != NULL ? fname : "?", relation, (unsigned) shown,
               plural, (unsigned) given);
}

static int php_arg_type_error(const zval *v, int arg_num, const char *fname,
                              const char *expected)
{
    /* VERSIONGATE:80000 else-branch is the 5.6 form: a warning, not a throw */
    if (EG(php_version) >= 80000) {
        zend_throw_error("TypeError", "%s(): Argument #%d must be of type %s, %s given",
                         fname != NULL ? fname : "?", arg_num, expected,
                         php_arg_type_name(v));
    } else {
        zend_error(E_WARNING, "%s() expects parameter %d to be %s, %s given",
                   fname != NULL ? fname : "?", arg_num, expected,
                   php_arg_type_name(v));
    }
    return 0;
}

int zend_parse_arg_long(zval *v, zend_long *dest, int arg_num, const char *fname)
{
    zval *a = zval_deref(v);

    switch (Z_TYPE_P(a)) {
        case IS_LONG:
            *dest = Z_LVAL_P(a);
            return 1;
        case IS_UNDEF:
        case IS_NULL:
        case IS_FALSE:
        case IS_TRUE:
        case IS_DOUBLE:
            *dest = zval_get_long(a);
            return 1;
        case IS_STRING: {
            zend_long lval;
            double dval;
            int kind = is_numeric_string(ZSTR_VAL(Z_STR_P(a)), ZSTR_LEN(Z_STR_P(a)),
                                         &lval, &dval, 1);

            if (kind == IS_LONG) {
                *dest = lval;
                return 1;
            }
            if (kind == IS_DOUBLE) {
                *dest = (zend_long) dval;
                return 1;
            }
            return php_arg_type_error(a, arg_num, fname, "int");
        }
        default:
            return php_arg_type_error(a, arg_num, fname, "int");
    }
}

int zend_parse_arg_double(zval *v, double *dest, int arg_num, const char *fname)
{
    zval *a = zval_deref(v);

    switch (Z_TYPE_P(a)) {
        case IS_DOUBLE:
            *dest = Z_DVAL_P(a);
            return 1;
        case IS_UNDEF:
        case IS_NULL:
        case IS_FALSE:
        case IS_TRUE:
        case IS_LONG:
            *dest = zval_get_double(a);
            return 1;
        case IS_STRING: {
            zend_long lval;
            double dval;
            int kind = is_numeric_string(ZSTR_VAL(Z_STR_P(a)), ZSTR_LEN(Z_STR_P(a)),
                                         &lval, &dval, 1);

            if (kind == IS_LONG) {
                *dest = (double) lval;
                return 1;
            }
            if (kind == IS_DOUBLE) {
                *dest = dval;
                return 1;
            }
            return php_arg_type_error(a, arg_num, fname, "float");
        }
        default:
            return php_arg_type_error(a, arg_num, fname, "float");
    }
}

int zend_parse_arg_string(zval *v, zend_string **dest, int arg_num, const char *fname)
{
    zval *a = zval_deref(v);

    switch (Z_TYPE_P(a)) {
        case IS_STRING:
            *dest = Z_STR_P(a);
            return 1;
        case IS_UNDEF:
        case IS_NULL:
        case IS_FALSE:
        case IS_TRUE:
        case IS_LONG:
        case IS_DOUBLE:
            *dest = zval_get_string(a);
            return 1;
        case IS_OBJECT:
            /* An object is only acceptable when it can speak for itself. */
            if (Z_OBJ_P(a)->ce != NULL && Z_OBJ_P(a)->ce->__tostring != NULL) {
                if (zend_object_to_string(Z_OBJ_P(a), dest) == 0) {
                    return 1;
                }
                return 0;       /* the conversion already threw */
            }
            return php_arg_type_error(a, arg_num, fname, "string");
        default:
            return php_arg_type_error(a, arg_num, fname, "string");
    }
}
