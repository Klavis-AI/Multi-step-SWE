/* zend_alloc.c -- the interpreter's allocator.
 *
 * A PHP script executed by this interpreter is short lived, so nothing is ever
 * freed: efree() is a no-op and the process exit reclaims everything.  Keeping
 * the policy in one place means the rest of the engine can share zend_string
 * and HashTable pointers without reference counting for liveness (the refcount
 * fields are still maintained, but only so copy-on-write decisions elsewhere
 * stay conservative).
 *
 * Running out of memory is not recoverable, so -- like php-src's
 * zend_out_of_memory() -- we print a message and leave.
 */
#include "zend_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void zend_out_of_memory(size_t size)
{
    fprintf(stderr, "Fatal error: Out of memory (tried to allocate %zu bytes)\n", size);
    fflush(stderr);
    exit(1);
}

void *emalloc(size_t size)
{
    void *ptr;

    if (size == 0) {
        size = 1;               /* malloc(0) is allowed to return NULL */
    }
    ptr = malloc(size);
    if (ptr == NULL) {
        zend_out_of_memory(size);
    }
    return ptr;
}

void *ecalloc(size_t nmemb, size_t size)
{
    void *ptr;

    if (nmemb == 0 || size == 0) {
        nmemb = 1;
        size = 1;
    } else if (nmemb > ((size_t) -1) / size) {
        zend_out_of_memory((size_t) -1);   /* nmemb * size would wrap around */
    }
    ptr = calloc(nmemb, size);
    if (ptr == NULL) {
        zend_out_of_memory(nmemb * size);
    }
    return ptr;
}

void *erealloc(void *ptr, size_t size)
{
    void *newptr;

    if (size == 0) {
        size = 1;               /* never return NULL for a live allocation */
    }
    newptr = realloc(ptr, size);
    if (newptr == NULL) {
        zend_out_of_memory(size);
    }
    return newptr;
}

char *estrdup(const char *s)
{
    size_t len;
    char *copy;

    if (s == NULL) {
        return NULL;
    }
    len = strlen(s);
    copy = emalloc(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

void efree(void *ptr)
{
    /* Deliberately empty: this interpreter never frees.  Keeping the call sites
     * is still worthwhile -- they document ownership and make it possible to
     * switch to a real free() (or a pool) in one place later on. */
    (void) ptr;
}
