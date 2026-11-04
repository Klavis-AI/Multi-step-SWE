/* php_misc.h -- the parts of ext/standard/php_misc.c the engine has to reach.
 *
 * set_error_handler()/restore_error_handler() keep their stack inside
 * php_misc.c because the frozen zend_globals has no slot for it.  zend_error()
 * has to offer every diagnostic to that stack before printing it itself, which
 * is what php_call_user_error_handler() is for.
 */
#ifndef PHP_MISC_H
#define PHP_MISC_H

#include "zend.h"

/* Offer one diagnostic to the user error handler installed by
 * set_error_handler().
 *
 *   type  one of the E_* constants
 *   msg   the already formatted message (no trailing newline)
 *   file  the script the error is attributed to, or NULL
 *   line  the line the error is attributed to
 *
 * Returns 1 when a user handler consumed the error, in which case zend_error()
 * must NOT print its own "Warning: ..." line.  Returns 0 when it did not:
 *   - no handler is installed,
 *   - the handler's error_levels mask does not cover `type`,
 *   - `type` is one of the levels php-src never hands to userland
 *     (E_ERROR, E_PARSE, E_CORE_ERROR, E_CORE_WARNING, E_COMPILE_ERROR,
 *      E_COMPILE_WARNING),
 *   - a handler is already running (php-src does not re-enter), or
 *   - the handler returned exactly false, which php-src defines as "fall back
 *     to the standard handler".
 *
 * Safe to call before any handler has ever been installed.
 */
int php_call_user_error_handler(int type, const char *msg,
                                const char *file, uint32_t line);

#endif /* PHP_MISC_H */
