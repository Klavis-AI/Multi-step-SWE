/* zend_alloc.h -- allocation helpers used by the whole interpreter.
 *
 * The interpreter runs one short lived script per process, so memory is never
 * handed back: efree() is deliberately a no-op and the operating system does
 * the reclaiming at exit.  That is what lets zend_string and HashTable values
 * be shared freely between modules without ownership rules.
 *
 * Every allocator here either succeeds or terminates the process, so callers
 * never have to test the returned pointer.
 */
#ifndef ZEND_ALLOC_H
#define ZEND_ALLOC_H

#include <stddef.h>

void *emalloc(size_t size);
void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *ptr, size_t size);
char *estrdup(const char *s);
void  efree(void *ptr);

#endif /* ZEND_ALLOC_H */
