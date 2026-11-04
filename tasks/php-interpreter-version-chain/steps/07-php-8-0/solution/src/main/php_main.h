/* php_main.h -- what main/main.c and main/php_registry.c publish to the rest
 * of the interpreter.
 *
 * The frozen contracts (Zend/zend.h, Zend/zend_ast.h, Zend/zend_API.h) already
 * declare the pieces every module needs: zend_error(), php_write(),
 * php_printf(), zend_register_functions() and friends.  This header only adds
 * the engine start-up / shutdown / INI surface that has no home there.
 */
#ifndef PHP_MAIN_H
#define PHP_MAIN_H

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"

/* ------------------------------------------------------------- start-up */

/* Bring the engine up: globals, hash tables, INI defaults.  Must run before
 * zend_register_functions() / _constants() / _classes().  EG(php_version) and
 * EG(php_version_str) have to be set by the SAPI first, because the INI
 * defaults and the registered symbol set both depend on them. */
void php_engine_startup(void);

/* Flush output and settle EG(exit_status).  Safe to call more than once. */
void php_request_shutdown(void);

/* ---------------------------------------------------------------- output */

void php_output_flush(void);

/* ------------------------------------------------------------------- INI */

/* EG(ini) maps an INI name to a zend_string.  Unknown names are accepted and
 * stored, exactly as php's -d does. */
zend_string *php_ini_get(const char *name);
void         php_ini_set(const char *name, const char *value);
zend_long    php_ini_long(const char *name, zend_long dflt);
double       php_ini_double(const char *name, double dflt);
int          php_ini_bool(const char *name, int dflt);

/* "E_ALL & ~E_NOTICE" or "32767"; returns the mask, or `dflt` when the text
 * cannot be read at all. */
int php_parse_error_reporting(const char *s, int dflt);

/* -------------------------------------------------------- error handling */

/* The error mask that is actually in force right now: EG(error_reporting),
 * narrowed by the `@` operator (EG(silence)).  Before PHP 8 `@` hides
 * everything; from PHP 8 it keeps the fatal classes visible. */
int php_effective_error_reporting(void);

/* E_ERROR | E_CORE_ERROR | E_COMPILE_ERROR | E_USER_ERROR | E_RECOVERABLE_ERROR
 * | E_PARSE -- php-src's E_FATAL_ERRORS. */
#define PHP_E_FATAL_ERRORS \
    (E_ERROR | E_CORE_ERROR | E_COMPILE_ERROR | E_USER_ERROR | \
     E_RECOVERABLE_ERROR | E_PARSE)

/* Optional hook for ext/standard's set_error_handler().  When it is set,
 * zend_error() offers every non-fatal diagnostic to it first; returning
 * non-zero means "handled, do not print".  NULL (the default) means the engine
 * handles everything itself. */
extern int (*php_user_error_handler)(int type, zend_string *message,
                                     zend_string *filename, uint32_t lineno);

/* ext/standard's set_error_handler() entry point, called by zend_error()
 * before the engine prints anything.  Returns 1 when a user handler consumed
 * the diagnostic, in which case nothing is printed and even a fatal type does
 * not stop the script.  Weakly referenced: absent means "no handlers". */
int php_call_user_error_handler(int type, const char *message,
                                const char *filename, uint32_t lineno);

/* php-src's display form, with error_prepend_string / error_append_string
 * empty and html_errors off:
 *     "\n" <Label> ": " <message> " in " <file> " on line " <line> "\n" */
void php_error_display(int type, zend_string *filename, uint32_t lineno,
                       zend_string *message);

/* "Warning", "Fatal error", ... for an E_* type, as php-src spells it for the
 * emulated version (E_RECOVERABLE_ERROR is "Catchable fatal error" before
 * 7.1 and "Recoverable fatal error" from 7.1). */
const char *php_error_type_name(int type);

/* Report an exception as php-src's uncaught-exception fatal error and set
 * EG(exit_status) to 255.  Clears EG(exception). */
void php_report_uncaught(zend_object *ex);

/* The parse-error path: "\nParse error: <msg> in <file> on line <n>\n". */
void php_report_parse_error(zend_string *message, zend_string *filename,
                            uint32_t lineno);

/* ---------------------------------------------------------- superglobals */

/* True for a name the SAPI publishes as a superglobal ($_SERVER, $_GET,
 * $GLOBALS, ...).  php resolves these out of the global symbol table from any
 * scope, without a `global` declaration, so a variable lookup that misses the
 * local table must try EG(symbol_table) when this says yes. */
int php_is_auto_global(const zend_string *name);

/* --------------------------------------------------------------- bailout */

/* php-src unwinds a fatal error with longjmp() rather than returning through
 * every evaluator frame.  The SAPI arms the landing pad once; zend_fatal() and
 * zend_error(E_ERROR) jump to it.  Nothing needs unwinding because efree() is
 * a no-op and the process is about to leave. */
extern jmp_buf php_bailout_env;
extern int     php_bailout_armed;

#define PHP_BAILOUT_ARM()    (php_bailout_armed = 1, setjmp(php_bailout_env))
#define PHP_BAILOUT_DISARM() (php_bailout_armed = 0)

void php_bailout(void);     /* never returns */

/* ------------------------------------------------------------- constants */

void zend_register_long_constant(const char *name, zend_long v);
void zend_register_string_constant(const char *name, const char *v);
void zend_register_stringl_constant(const char *name, const char *v, size_t len);
void zend_register_double_constant(const char *name, double v);
void zend_register_bool_constant(const char *name, int v);
void zend_register_null_constant(const char *name);

/* Constant lookup honouring php's one case-insensitive corner (true/false/
 * null).  Returns NULL when the name is not defined. */
zval *zend_get_constant_str(const char *name, size_t len);

/* Register one internal function table (a {NULL}-terminated
 * php_function_entry[]), honouring `since`/`until` against EG(php_version).
 * Returns how many entries were actually registered. */
uint32_t zend_register_function_table(const php_function_entry *tbl);

/* --------------------------------------------------------------- the SAPI */

extern const char *php_sapi_name;       /* "cli" */
extern const char *php_binary_path;     /* argv[0] resolved, for PHP_BINARY */

#endif /* PHP_MAIN_H */
