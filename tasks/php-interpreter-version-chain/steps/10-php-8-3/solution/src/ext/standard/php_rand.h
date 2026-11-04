/* php_rand.h -- Mersenne Twister shared with the other extension modules.
 *
 * This is a byte-faithful re-implementation of php-src's ext/standard/mt_rand.c
 * (and, before 7.1, ext/standard/rand.c).  Seeded sequences therefore match the
 * oracle exactly:
 *     php7.4 -r 'mt_srand(42); echo mt_rand();'   ==  804318771
 *     php5.6 -r 'mt_srand(42); echo mt_rand();'   == 1354439493
 * (the difference is the twist() macro bug fixed in 7.1, see php_mt_reload()).
 */
#ifndef PHP_RAND_H
#define PHP_RAND_H

#include "zend.h"

#define PHP_MT_N            624
#define PHP_MT_RAND_MAX     ((zend_long) 0x7FFFFFFF)   /* (1<<31) - 1 */
#define PHP_RAND_MAX        PHP_MT_RAND_MAX

#define MT_RAND_MT19937     0
#define MT_RAND_PHP         1

/* Seed the generator.  Also selects the "fixed" (MT19937) twist for
 * EG(php_version) >= 70100 and the historical PHP twist below that. */
void       php_mt_srand(uint32_t seed);

/* Raw 32-bit output of the generator (auto-seeds when never seeded). */
uint32_t   php_mt_rand(void);

/* Uniform value in [min, max] using php-src's rejection sampling. */
zend_long  php_mt_rand_range(zend_long min, zend_long max);

/* mt_rand()/rand() range helper: honours the MT_RAND_PHP legacy scaling. */
zend_long  php_mt_rand_common(zend_long min, zend_long max);

/* mt_srand()'s second parameter (7.1+); MT_RAND_MT19937 or MT_RAND_PHP. */
void       php_mt_set_mode(int mode);
int        php_mt_get_mode(void);

#endif /* PHP_RAND_H */
