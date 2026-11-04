/* zend.h — core value model for the interpreter. */
#ifndef ZEND_H
#define ZEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>

typedef int64_t zend_long;
typedef uint64_t zend_ulong;
#define ZEND_LONG_MAX INT64_MAX
#define ZEND_LONG_MIN INT64_MIN

typedef enum {
    IS_UNDEF = 0,
    IS_NULL,
    IS_FALSE,
    IS_TRUE,
    IS_LONG,
    IS_DOUBLE,
    IS_STRING,
    IS_ARRAY,
    IS_OBJECT,
    IS_REFERENCE
} zend_type_t;

typedef struct _zend_string zend_string;
typedef struct _HashTable   HashTable;
typedef struct _zend_object zend_object;
typedef struct _zend_reference zend_reference;
typedef struct _zval zval;
typedef struct _zend_class_entry zend_class_entry;
typedef struct _zend_function zend_function;
typedef struct _zend_ast zend_ast;

struct _zval {
    uint8_t u1;                 /* zend_type_t */
    union {
        zend_long      lval;
        double         dval;
        zend_string   *str;
        HashTable     *arr;
        zend_object   *obj;
        zend_reference*ref;
    } value;
};

#define Z_TYPE(z)      ((z).u1)
#define Z_TYPE_P(z)    ((z)->u1)
#define Z_LVAL(z)      ((z).value.lval)
#define Z_LVAL_P(z)    ((z)->value.lval)
#define Z_DVAL(z)      ((z).value.dval)
#define Z_DVAL_P(z)    ((z)->value.dval)
#define Z_STR(z)       ((z).value.str)
#define Z_STR_P(z)     ((z)->value.str)
#define Z_ARR(z)       ((z).value.arr)
#define Z_ARR_P(z)     ((z)->value.arr)
#define Z_OBJ(z)       ((z).value.obj)
#define Z_OBJ_P(z)     ((z)->value.obj)
#define Z_REF(z)       ((z).value.ref)
#define Z_REF_P(z)     ((z)->value.ref)

/* ---------------------------------------------------------------- strings */
struct _zend_string {
    uint32_t refcount;
    size_t   len;
    zend_ulong h;      /* cached hash, 0 = not computed */
    char     val[1];
};

zend_string *zend_string_init(const char *s, size_t len);
zend_string *zend_string_initz(const char *s);       /* NUL-terminated */
zend_string *zend_string_alloc(size_t len);          /* uninitialised, NUL-terminated */
zend_string *zend_string_concat(const zend_string *a, const zend_string *b);
zend_string *zend_strpprintf(const char *fmt, ...);
int   zend_string_equals(const zend_string *a, const zend_string *b);
int   zend_string_equals_ci(const zend_string *a, const zend_string *b);
zend_string *zend_string_tolower(const zend_string *s);
#define ZSTR_VAL(s) ((s)->val)
#define ZSTR_LEN(s) ((s)->len)

/* ------------------------------------------------------------ hash tables */
typedef struct _Bucket {
    zval        val;         /* IS_UNDEF for a deleted slot */
    zend_ulong  h;           /* numeric key, or hash of key */
    zend_string *key;        /* NULL for an integer key */
} Bucket;

struct _HashTable {
    uint32_t refcount;
    uint32_t nNumUsed;       /* slots used in arData, including tombstones */
    uint32_t nNumOfElements; /* live elements */
    uint32_t nTableSize;
    zend_long nNextFreeElement;
    uint32_t nInternalPointer;
    Bucket  *arData;
    uint32_t *arHash;        /* open addressing index, size nTableSize*2 */
    uint32_t nHashSize;
    unsigned char protect;   /* recursion guard for printing */
};

HashTable *zend_hash_new(void);
HashTable *zend_hash_dup(const HashTable *src);
void  zend_hash_destroy(HashTable *ht);
zval *zend_hash_find(const HashTable *ht, const zend_string *key);
zval *zend_hash_str_find(const HashTable *ht, const char *k, size_t len);
zval *zend_hash_index_find(const HashTable *ht, zend_long idx);
zval *zend_hash_update(HashTable *ht, zend_string *key, zval val);
zval *zend_hash_str_update(HashTable *ht, const char *k, size_t len, zval val);
zval *zend_hash_index_update(HashTable *ht, zend_long idx, zval val);
zval *zend_hash_next_index_insert(HashTable *ht, zval val);
int   zend_hash_del(HashTable *ht, const zend_string *key);
int   zend_hash_index_del(HashTable *ht, zend_long idx);
void  zend_hash_clean(HashTable *ht);
uint32_t zend_hash_num_elements(const HashTable *ht);
void  zend_hash_renumber(HashTable *ht);   /* reindex integer keys from 0 */
void  zend_hash_sort(HashTable *ht, int (*cmp)(const Bucket *, const Bucket *), int renumber);
zend_ulong zend_string_hash(zend_string *s);

/* iteration: skips tombstones */
#define ZEND_HASH_FOREACH(ht, _b) do { \
    HashTable *__ht = (ht); uint32_t __i; \
    for (__i = 0; __i < __ht->nNumUsed; __i++) { \
        Bucket *_b = &__ht->arData[__i]; \
        if (Z_TYPE(_b->val) == IS_UNDEF) continue;
#define ZEND_HASH_FOREACH_END() } } while (0)

/* ------------------------------------------------------------- references */
struct _zend_reference {
    uint32_t refcount;
    zval val;
};

/* ---------------------------------------------------------------- objects */
typedef struct _zend_property_info {
    zend_string *name;          /* declared name */
    uint32_t flags;
    zend_ast *default_ast;
    zend_class_entry *ce;       /* declaring class */
    zend_string *mangled;       /* storage key */
    struct _zend_type_hint *type; /* NULL when untyped (7.4+) */
    zend_string *doc;
    uint32_t set_flags;         /* 8.4 asymmetric visibility, 0 when symmetric */
} zend_property_info;

struct _zend_object {
    uint32_t   handle;
    uint32_t   refcount;
    zend_class_entry *ce;
    HashTable *properties;
    void      *internal;        /* Closure/Generator/ArrayObject payload */
    unsigned char destructed;
    unsigned char in_destruct;
};

/* flags */
#define ZEND_ACC_PUBLIC     (1<<0)
#define ZEND_ACC_PROTECTED  (1<<1)
#define ZEND_ACC_PRIVATE    (1<<2)
#define ZEND_ACC_STATIC     (1<<3)
#define ZEND_ACC_ABSTRACT   (1<<4)
#define ZEND_ACC_FINAL      (1<<5)
#define ZEND_ACC_INTERFACE  (1<<6)
#define ZEND_ACC_TRAIT      (1<<7)
#define ZEND_ACC_CTOR       (1<<8)
#define ZEND_ACC_RETURN_REF (1<<9)
#define ZEND_ACC_VARIADIC   (1<<10)
#define ZEND_ACC_GENERATOR  (1<<11)
#define ZEND_ACC_READONLY   (1<<12)
#define ZEND_ACC_ENUM       (1<<13)
#define ZEND_ACC_PROMOTED   (1<<14)
#define ZEND_ACC_VISIBILITY (ZEND_ACC_PUBLIC|ZEND_ACC_PROTECTED|ZEND_ACC_PRIVATE)

#endif /* ZEND_H */
