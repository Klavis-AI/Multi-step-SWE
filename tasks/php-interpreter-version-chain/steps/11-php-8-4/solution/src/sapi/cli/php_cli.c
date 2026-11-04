/* php_cli.c -- the PHP CLI SAPI.
 *
 * The contract that matters is php-src's own run-tests.php, which runs every
 * .phpt as
 *
 *     php -n -d <ini> ... -f <test>.php [-- <args>]      with 2>&1
 *
 * so -n, a repeatable -d, -f and a trailing argument list after -- are the
 * load-bearing options; -r, -v, -h and reading the script from stdin are here
 * because the CLI has always had them.
 *
 * Nothing is read from a php.ini file: -n is the only mode this SAPI has, and
 * the defaults it starts from are php's own built-in ones (main/main.c's
 * php_ini_install_defaults), so a bare `php -f x.php` behaves like the real
 * `php -n -f x.php`.
 */

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include "zend_object.h"
#include "zend_parse.h"
#include "zend_exceptions.h"
#include "php_main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern char **environ;

#ifndef PHP_VERSION_STRING
#define PHP_VERSION_STRING "5.6.40"
#endif
#ifndef PHP_TARGET_ID
#ifdef PHP_VERSION_ID
#define PHP_TARGET_ID PHP_VERSION_ID
#else
#define PHP_TARGET_ID 50640
#endif
#endif

/* ==========================================================================
 *  Command line
 * ========================================================================== */

typedef enum {
    CLI_MODE_STANDARD = 0,      /* run a file, or stdin when none was given */
    CLI_MODE_CODE,              /* -r */
    CLI_MODE_VERSION,           /* -v */
    CLI_MODE_HELP               /* -h */
} cli_mode;

typedef struct {
    cli_mode      mode;
    const char   *script_file;  /* NULL: read the script from stdin */
    const char   *code;         /* -r */
    int           read_stdin;   /* an explicit "-" */
    const char  **ini;          /* -d entries, in order */
    int           num_ini;
    char        **args;         /* what $argv gets after element 0 */
    int           num_args;
} cli_options;

static void cli_usage(void)
{
    php_printf(
        "Usage: php [options] [-f] <file> [--] [args...]\n"
        "   php [options] -r <code> [--] [args...]\n"
        "   php [options] [-B <begin_code>] -R <code> [-E <end_code>] [--] [args...]\n"
        "   php [options] [-B <begin_code>] -F <file> [-E <end_code>] [--] [args...]\n"
        "   php [options] -S <addr>:<port> [-t docroot]\n"
        "   php [options] -- [args...]\n"
        "   php [options] -a\n"
        "\n"
        "  -a               Run as interactive shell\n"
        "  -c <path>|<file> Look for php.ini file in this directory\n"
        "  -n               No php.ini file will be used\n"
        "  -d foo[=bar]     Define INI entry foo with value 'bar'\n"
        "  -e               Generate extended information for debugger/profiler\n"
        "  -f <file>        Parse and execute <file>.\n"
        "  -h               This help\n"
        "  -i               PHP information\n"
        "  -l               Syntax check only (lint)\n"
        "  -m               Show compiled in modules\n"
        "  -r <code>        Run PHP <code> without using script tags <?..?>\n"
        "  -B <begin_code>  Run PHP <begin_code> before processing input lines\n"
        "  -R <code>        Run PHP <code> for every input line\n"
        "  -F <file>        Parse and execute <file> for every input line\n"
        "  -E <end_code>    Run PHP <end_code> after processing all input lines\n"
        "  -H               Hide any passed arguments from external tools.\n"
        "  -S <addr>:<port> Run with built-in web server.\n"
        "  -t <docroot>     Specify document root <docroot> for built-in web server.\n"
        "  -s               Output HTML syntax highlighted source.\n"
        "  -v               Version number\n"
        "  -w               Output source with stripped comments and whitespace.\n"
        "  -z <file>        Load Zend extension <file>.\n"
        "\n"
        "  args...          Arguments passed to script. Use -- args when first argument\n"
        "                   starts with - or script is read from stdin\n"
        "\n");
}

/* php-src's banner is
 *     "PHP %s (%s) (built: %s %s) %s\nCopyright (c) ... The PHP Group\n%s"
 * with the trailing %s being the Zend Engine line.  The build stamp is fixed
 * here; nothing compares it. */
static void cli_version(void)
{
    long v = EG(php_version);
    long major = v / 10000;
    long minor = (v / 100) % 100;
    long release = v % 100;
    char zend_version[32];
    const char *thread_safety;
    const char *php_copyright;
    const char *zend_copyright;

    /* VERSIONGATE:80100 the marker lost its inner spaces in 8.1 */
    if (EG(php_version) >= 80100) {
        thread_safety = " (NTS)";
    /* VERSIONGATE:70000 else-branch is the 5.6 banner: no thread-safety marker */
    } else if (EG(php_version) >= 70000) {
        thread_safety = " ( NTS )";
    } else {
        thread_safety = " ";
    }

    /* VERSIONGATE:70400 else-branch is the 5.6 copyright line */
    if (EG(php_version) >= 70400) {
        php_copyright  = "Copyright (c) The PHP Group";
        zend_copyright = "Copyright (c) Zend Technologies";
    } else if (EG(php_version) >= 70000) {
        php_copyright  = "Copyright (c) 1997-2017 The PHP Group";
        zend_copyright = "Copyright (c) 1998-2017 Zend Technologies";
    } else {
        php_copyright  = "Copyright (c) 1997-2016 The PHP Group";
        zend_copyright = "Copyright (c) 1998-2016 Zend Technologies";
    }

    /* VERSIONGATE:80000 else-branch is the 5.6/7.x Zend Engine numbering */
    if (major >= 8) {
        snprintf(zend_version, sizeof(zend_version), "%ld.%ld.%ld",
                 major - 4, minor, release);
    } else if (major == 7) {
        snprintf(zend_version, sizeof(zend_version), "3.%ld.0", minor);
    } else {
        snprintf(zend_version, sizeof(zend_version), "2.%ld.0", minor);
    }

    php_printf("PHP %s (cli) (built: Jan  1 2024 00:00:00)%s\n%s\nZend Engine v%s, %s\n",
               EG(php_version_str) != NULL ? EG(php_version_str) : PHP_VERSION_STRING,
               thread_safety, php_copyright, zend_version, zend_copyright);
}

/* php's own getopt accepts both "-d name=value" and "-dname=value". */
static const char *cli_opt_value(int argc, char **argv, int *i, const char *rest)
{
    if (rest != NULL && *rest != '\0') {
        return rest;
    }
    if (*i + 1 < argc) {
        (*i)++;
        return argv[*i];
    }
    return NULL;
}

static int cli_parse_args(int argc, char **argv, cli_options *opt)
{
    int i;

    opt->ini = ecalloc((size_t) argc + 1, sizeof(char *));

    for (i = 1; i < argc; i++) {
        char *a = argv[i];

        if (a[0] != '-' || a[1] == '\0') {
            /* A bare "-" asks for the script on stdin; anything else is the
             * script file, and everything after it belongs to $argv. */
            if (a[0] == '-' && a[1] == '\0') {
                opt->read_stdin = 1;
            } else {
                opt->script_file = a;
            }
            opt->args = &argv[i + 1];
            opt->num_args = argc - i - 1;
            return 1;
        }
        if (strcmp(a, "--") == 0) {
            opt->args = &argv[i + 1];
            opt->num_args = argc - i - 1;
            return 1;
        }
        if (strcmp(a, "--version") == 0) {
            opt->mode = CLI_MODE_VERSION;
            return 1;
        }
        if (strcmp(a, "--help") == 0) {
            opt->mode = CLI_MODE_HELP;
            return 1;
        }

        switch (a[1]) {
            case 'n':                       /* no php.ini: always the case */
                break;
            case 'd': {
                const char *val = cli_opt_value(argc, argv, &i, a + 2);

                if (val == NULL) {
                    return 0;
                }
                opt->ini[opt->num_ini++] = val;
                break;
            }
            case 'f': {
                const char *val = cli_opt_value(argc, argv, &i, a + 2);

                if (val == NULL) {
                    return 0;
                }
                opt->script_file = val;
                break;
            }
            case 'r': {
                const char *val = cli_opt_value(argc, argv, &i, a + 2);

                if (val == NULL) {
                    return 0;
                }
                opt->mode = CLI_MODE_CODE;
                opt->code = val;
                break;
            }
            case 'v':
                opt->mode = CLI_MODE_VERSION;
                return 1;
            case 'h':
            case '?':
                opt->mode = CLI_MODE_HELP;
                return 1;
            case 'c':                       /* -c <path>: nothing to read */
            case 'z':                       /* -z <ext>: no Zend extensions */
                (void) cli_opt_value(argc, argv, &i, a + 2);
                break;
            case 'e':                       /* extended debug info: no-op */
            case 'H':                       /* hide arguments: no-op */
                break;
            default:
                return 0;
        }
    }
    return 1;
}

/* ==========================================================================
 *  Script source
 * ========================================================================== */

static char *cli_read_stream(FILE *fp, size_t *out_len)
{
    size_t cap = 65536;
    size_t len = 0;
    char *buf = emalloc(cap);

    for (;;) {
        size_t got;

        if (len == cap) {
            cap *= 2;
            buf = erealloc(buf, cap);
        }
        got = fread(buf + len, 1, cap - len, fp);
        len += got;
        if (got == 0) {
            break;
        }
    }
    buf[len < cap ? len : cap - 1] = '\0';
    *out_len = len;
    return buf;
}

/* ==========================================================================
 *  Superglobals
 * ========================================================================== */

static void cli_set_global(const char *name, zval v)
{
    zend_hash_str_update(EG(symbol_table), name, strlen(name), v);
}

static void cli_register_variables(const char *self, cli_options *opt)
{
    HashTable *argv_ht = zend_hash_new();
    HashTable *server = zend_hash_new();
    HashTable *env = zend_hash_new();
    zend_long argc;
    int i;

    zend_hash_next_index_insert(argv_ht, zval_string(self));
    for (i = 0; i < opt->num_args; i++) {
        zend_hash_next_index_insert(argv_ht, zval_string(opt->args[i]));
    }
    argc = (zend_long) zend_hash_num_elements(argv_ht);

    cli_set_global("argv", zval_array(argv_ht));
    cli_set_global("argc", zval_long(argc));

    if (environ != NULL) {
        char **e;

        for (e = environ; *e != NULL; e++) {
            const char *eq = strchr(*e, '=');

            if (eq == NULL) {
                continue;
            }
            {
                zend_string *key = zend_string_init(*e, (size_t) (eq - *e));
                zval value = zval_string(eq + 1);

                zend_hash_update(env, key, value);
                zend_hash_update(server, key, zval_copy(value));
            }
        }
    }

    argv_ht->refcount++;
    zend_hash_str_update(server, "argv", 4, zval_array(argv_ht));
    zend_hash_str_update(server, "argc", 4, zval_long(argc));
    zend_hash_str_update(server, "PHP_SELF", 8, zval_string(self));
    zend_hash_str_update(server, "SCRIPT_NAME", 11, zval_string(self));
    zend_hash_str_update(server, "SCRIPT_FILENAME", 15, zval_string(self));
    zend_hash_str_update(server, "PATH_TRANSLATED", 15, zval_string(self));
    zend_hash_str_update(server, "DOCUMENT_ROOT", 13, zval_string(""));
    zend_hash_str_update(server, "REQUEST_TIME", 12, zval_long((zend_long) time(NULL)));
    zend_hash_str_update(server, "REQUEST_TIME_FLOAT", 18,
                         zval_double((double) time(NULL)));

    cli_set_global("_SERVER", zval_array(server));
    cli_set_global("_ENV", zval_array(env));
    cli_set_global("_GET", zval_array(zend_hash_new()));
    cli_set_global("_POST", zval_array(zend_hash_new()));
    cli_set_global("_COOKIE", zval_array(zend_hash_new()));
    cli_set_global("_FILES", zval_array(zend_hash_new()));
    cli_set_global("_REQUEST", zval_array(zend_hash_new()));
}

/* ==========================================================================
 *  main
 * ========================================================================== */

int main(int argc, char **argv)
{
    cli_options opt;
    /* volatile: it is live across the bailout landing pad below, and C leaves
     * a non-volatile local indeterminate after a longjmp. */
    zend_ast * volatile ast = NULL;
    zend_string *parse_err = NULL;
    zend_string *filename;
    const char *self;
    int i;

    memset(&opt, 0, sizeof(opt));
    opt.args = &argv[argc];

    EG(php_version)     = PHP_TARGET_ID;
    EG(php_version_str) = PHP_VERSION_STRING;
    php_binary_path     = (argv[0] != NULL) ? argv[0] : "";

    php_engine_startup();

    if (!cli_parse_args(argc, argv, &opt)) {
        cli_usage();
        php_output_flush();
        return 1;
    }

    /* -d entries apply on top of the built-in defaults, in the order given. */
    for (i = 0; i < opt.num_ini; i++) {
        const char *entry = opt.ini[i];
        const char *eq = strchr(entry, '=');

        if (eq != NULL) {
            zend_string *name = zend_string_init(entry, (size_t) (eq - entry));

            php_ini_set(ZSTR_VAL(name), eq + 1);
        } else {
            php_ini_set(entry, "1");
        }
    }

    if (opt.mode == CLI_MODE_VERSION) {
        cli_version();
        php_output_flush();
        return 0;
    }
    if (opt.mode == CLI_MODE_HELP) {
        cli_usage();
        php_output_flush();
        return 0;
    }

    zend_register_functions();
    zend_register_constants();
    zend_register_classes();

    if (opt.mode == CLI_MODE_CODE) {
        /* -r runs the code with an implicit opening tag on the same line, so
         * that __LINE__ on the first line of the code is 1. */
        size_t code_len = strlen(opt.code);
        size_t total = code_len + 6;
        char *buf = emalloc(total + 1);

        memcpy(buf, "<?php ", 6);
        memcpy(buf + 6, opt.code, code_len);
        buf[total] = '\0';

        self = "-";
        filename = zend_string_initz("Command line code");
        EG(filename) = filename;
        EG(lineno) = 1;
        ast = php_parse_string(buf, total, filename, &parse_err);
    } else if (opt.script_file != NULL && !opt.read_stdin) {
        FILE *probe = fopen(opt.script_file, "rb");

        if (probe == NULL) {
            php_printf("Could not open input file: %s\n", opt.script_file);
            php_output_flush();
            return 1;
        }
        fclose(probe);

        self = opt.script_file;
        filename = zend_string_initz(opt.script_file);
        EG(filename) = filename;
        EG(lineno) = 1;
        ast = php_parse_file(opt.script_file, &parse_err);
    } else {
        size_t len = 0;
        char *buf = cli_read_stream(stdin, &len);

        /* VERSIONGATE:70200 else-branch is the 5.6 name for stdin: "-" */
        if (EG(php_version) >= 70200) {
            filename = zend_string_initz("Standard input code");
        } else {
            filename = zend_string_initz("-");
        }
        self = "-";
        EG(filename) = filename;
        EG(lineno) = 1;
        ast = php_parse_string(buf, len, filename, &parse_err);
    }

    if (parse_err != NULL || ast == NULL) {
        uint32_t line = (php_parse_error_lineno != 0) ? php_parse_error_lineno : EG(lineno);

        php_report_parse_error(parse_err != NULL ? parse_err
                                                 : zend_string_initz("syntax error"),
                               filename, line);
        php_output_flush();
        return 255;
    }

    cli_register_variables(self, &opt);

    /* A fatal error lands here rather than returning through the evaluator. */
    if (PHP_BAILOUT_ARM() == 0) {
        zend_execute_script(ast);
        PHP_BAILOUT_DISARM();
    }

    php_request_shutdown();
    return EG(exit_status);
}
