/* zend_hash.c -- PHP's ordered hash table.
 *
 * The layout is php-src's: arData is an append-only array of Bucket in
 * insertion order and arHash is a throwaway index from hash -> position in
 * arData.  Everything observable about a PHP array comes from that split:
 *
 *   - iteration follows arData, so elements come back in insertion order;
 *   - deleting only turns a bucket into a tombstone (val = IS_UNDEF), so the
 *     order of the surviving elements never changes and positions stay stable;
 *   - arHash is pure acceleration and is thrown away and rebuilt whenever
 *     arData moves (grow or compact).
 *
 * arHash is open addressed with linear probing and holds nTableSize * 2 slots,
 * so it is at most half full (arData can never hold more than nTableSize
 * buckets) and probe runs stay short.  A slot holding the position of a
 * tombstoned bucket acts as the usual "deleted" marker: lookups step over it
 * and keep probing, and the next rebuild drops it.
 *
 * Keys: an integer key has key == NULL and h == (zend_ulong) the integer; a
 * string key has key != NULL and h == zend_string_hash(key).  Decimal integer
 * strings are NOT normalised here -- array_key_normalize() in the evaluator
 * does that before calling in, so "10" and 10 are different keys at this level.
 *
 * This file only needs zend.h, zend_alloc.h and libc.
 */
#include "zend.h"
#include "zend_alloc.h"

#include <stddef.h>
#include <string.h>

#define HT_MIN_SIZE     8
#define HT_INVALID_IDX  ((uint32_t) -1)
#define HT_MAX_SIZE     ((uint32_t) 0x40000000)  /* keeps nTableSize * 2 valid */

/* ------------------------------------------------------------------ hashing */

/* Must stay byte-for-byte identical to zend_string.c's hash: this table mixes
 * keys hashed here (zend_hash_str_find) with keys hashed there. */
static zend_ulong zend_hash_bytes(const char *str, size_t len)
{
    zend_ulong h = (zend_ulong) 5381;
    size_t i;

    for (i = 0; i < len; i++) {
        h = ((h << 5) + h) + (zend_ulong) (unsigned char) str[i];
    }
    return h | (zend_ulong) 0x8000000000000000ULL;
}

/* --------------------------------------------------------------- hash index */

static uint32_t ht_hash_slot(const HashTable *ht, zend_ulong h)
{
    return (uint32_t) (h & (zend_ulong) (ht->nHashSize - 1));
}

static void ht_hash_reset(HashTable *ht)
{
    /* HT_INVALID_IDX is all-ones, so one memset marks every slot empty. */
    memset(ht->arHash, 0xFF, (size_t) ht->nHashSize * sizeof(uint32_t));
}

static void ht_hash_add(HashTable *ht, zend_ulong h, uint32_t idx)
{
    uint32_t slot = ht_hash_slot(ht, h);
    uint32_t probes = 0;

    while (ht->arHash[slot] != HT_INVALID_IDX) {
        slot = (slot + 1) & (ht->nHashSize - 1);
        if (++probes >= ht->nHashSize) {
            return;             /* unreachable: the index is at most half full */
        }
    }
    ht->arHash[slot] = idx;
}

/* Rebuild the index from arData; tombstones are simply not re-indexed. */
static void ht_hash_rebuild(HashTable *ht)
{
    uint32_t i;

    ht_hash_reset(ht);
    for (i = 0; i < ht->nNumUsed; i++) {
        if (Z_TYPE(ht->arData[i].val) == IS_UNDEF) {
            continue;
        }
        ht_hash_add(ht, ht->arData[i].h, i);
    }
}

/* ----------------------------------------------------------------- lookups */

static Bucket *ht_find_index_bucket(HashTable *ht, zend_ulong h, uint32_t *pos)
{
    uint32_t slot, probes = 0, idx;

    if (ht->nNumOfElements == 0) {
        return NULL;
    }
    slot = ht_hash_slot(ht, h);
    while ((idx = ht->arHash[slot]) != HT_INVALID_IDX) {
        if (idx < ht->nNumUsed) {
            Bucket *b = &ht->arData[idx];

            if (b->key == NULL && b->h == h && Z_TYPE(b->val) != IS_UNDEF) {
                if (pos != NULL) {
                    *pos = idx;
                }
                return b;
            }
        }
        slot = (slot + 1) & (ht->nHashSize - 1);
        if (++probes >= ht->nHashSize) {
            break;
        }
    }
    return NULL;
}

static Bucket *ht_find_str_bucket(HashTable *ht, zend_ulong h,
                                  const char *str, size_t len, uint32_t *pos)
{
    uint32_t slot, probes = 0, idx;

    if (ht->nNumOfElements == 0) {
        return NULL;
    }
    slot = ht_hash_slot(ht, h);
    while ((idx = ht->arHash[slot]) != HT_INVALID_IDX) {
        if (idx < ht->nNumUsed) {
            Bucket *b = &ht->arData[idx];

            if (b->key != NULL && b->h == h && Z_TYPE(b->val) != IS_UNDEF
             && b->key->len == len
             && (len == 0 || memcmp(b->key->val, str, len) == 0)) {
                if (pos != NULL) {
                    *pos = idx;
                }
                return b;
            }
        }
        slot = (slot + 1) & (ht->nHashSize - 1);
        if (++probes >= ht->nHashSize) {
            break;
        }
    }
    return NULL;
}

/* --------------------------------------------------------- size maintenance */

static uint32_t ht_size_for(uint32_t nelems)
{
    uint32_t size = HT_MIN_SIZE;

    while (size < nelems && size < HT_MAX_SIZE) {
        size <<= 1;
    }
    return size;
}

/* Allocate arData/arHash for nTableSize == size.  Buckets are left
 * uninitialised; every bucket is fully written before it becomes used. */
static void ht_alloc_data(HashTable *ht, uint32_t size)
{
    ht->nTableSize = size;
    ht->nHashSize = size * 2;
    ht->arData = emalloc((size_t) size * sizeof(Bucket));
    ht->arHash = emalloc((size_t) ht->nHashSize * sizeof(uint32_t));
    ht_hash_reset(ht);
}

/* Move the live buckets to the front of arData, dropping tombstones and
 * keeping insertion order.  The internal pointer follows its bucket; if it
 * pointed at a dead (or past-the-end) slot it lands on the next live element.
 * The caller rebuilds arHash afterwards -- positions have changed. */
static void ht_compact_data(HashTable *ht)
{
    uint32_t i, j = 0;
    uint32_t old_ptr = ht->nInternalPointer;
    uint32_t new_ptr = HT_INVALID_IDX;

    for (i = 0; i < ht->nNumUsed; i++) {
        if (Z_TYPE(ht->arData[i].val) == IS_UNDEF) {
            continue;
        }
        if (new_ptr == HT_INVALID_IDX && i >= old_ptr) {
            new_ptr = j;
        }
        if (i != j) {
            ht->arData[j] = ht->arData[i];
        }
        j++;
    }
    ht->nNumUsed = j;
    ht->nInternalPointer = (new_ptr == HT_INVALID_IDX) ? j : new_ptr;
}

static void ht_resize(HashTable *ht, uint32_t newsize)
{
    ht->arData = erealloc(ht->arData, (size_t) newsize * sizeof(Bucket));
    ht->arHash = erealloc(ht->arHash, (size_t) newsize * 2 * sizeof(uint32_t));
    ht->nTableSize = newsize;
    ht->nHashSize = newsize * 2;
    ht_hash_rebuild(ht);
}

/* Make room for one more bucket.  php-src's rule (zend_hash_do_resize): when a
 * worthwhile part of arData is tombstones, compact in place instead of
 * doubling; the small slack term stops a single tombstone from triggering a
 * full compaction on every insert. */
static void ht_check_capacity(HashTable *ht)
{
    if (ht->nNumUsed < ht->nTableSize) {
        return;
    }
    if (ht->nNumOfElements + (ht->nNumOfElements >> 5) < ht->nNumUsed) {
        ht_compact_data(ht);
        ht_hash_rebuild(ht);
        return;
    }
    ht_resize(ht, ht->nTableSize * 2);
}

/* --------------------------------------------------------------- insertion */

static void ht_update_next_free(HashTable *ht, zend_ulong h)
{
    if ((zend_long) h >= ht->nNextFreeElement) {
        ht->nNextFreeElement = ((zend_long) h < ZEND_LONG_MAX)
                             ? (zend_long) h + 1 : ZEND_LONG_MAX;
    }
}

/* Append a brand new bucket.  Any bucket pointer taken before this call may be
 * stale afterwards, exactly as in php-src. */
static zval *ht_append(HashTable *ht, zend_ulong h, zend_string *key, zval val)
{
    Bucket *b;
    uint32_t idx;

    ht_check_capacity(ht);
    idx = ht->nNumUsed++;
    b = &ht->arData[idx];
    b->h = h;
    b->key = key;
    b->val = val;
    ht->nNumOfElements++;
    ht_hash_add(ht, h, idx);
    return &b->val;
}

/* ---------------------------------------------------------------- deletion */

static void ht_delete_bucket(HashTable *ht, uint32_t pos)
{
    Bucket *b = &ht->arData[pos];

    /* Tombstone: the slot keeps its place in arData so the order and the
     * positions of every other element are untouched.  h/key are left alone --
     * lookups ignore dead buckets and the next rebuild drops them. */
    Z_TYPE(b->val) = IS_UNDEF;
    ht->nNumOfElements--;

    if (ht->nInternalPointer == pos) {
        uint32_t i = pos + 1;

        while (i < ht->nNumUsed && Z_TYPE(ht->arData[i].val) == IS_UNDEF) {
            i++;
        }
        ht->nInternalPointer = i;       /* == nNumUsed means "past the end" */
    }
}

/* ---------------------------------------------------------------- renumber */

/* Give the integer-keyed elements the indices 0..n-1 in their current order;
 * string keys are left alone.  arHash must be rebuilt by the caller because the
 * integer hashes change. */
static void ht_renumber_keys(HashTable *ht)
{
    zend_long next = 0;
    uint32_t i;

    for (i = 0; i < ht->nNumUsed; i++) {
        Bucket *b = &ht->arData[i];

        if (Z_TYPE(b->val) == IS_UNDEF || b->key != NULL) {
            continue;
        }
        b->h = (zend_ulong) next;
        if (next < ZEND_LONG_MAX) {
            next++;
        }
    }
    ht->nNextFreeElement = next;
}

/* ------------------------------------------------------------- value copies */

/* The value-semantics copy used by zend_hash_dup().  Kept local so this file
 * stays independent of the evaluator's zval_copy(): arrays and objects are
 * shared with a bumped refcount (arrays separate later, copy-on-write), strings
 * are shared because they are immutable, and an IS_REFERENCE bucket keeps
 * pointing at the very same zend_reference -- php-src does not break
 * references apart when an array is copied. */
static zval ht_copy_zval(zval val)
{
    switch (Z_TYPE(val)) {
        case IS_STRING:
            if (Z_STR(val) != NULL) {
                Z_STR(val)->refcount++;
            }
            break;
        case IS_ARRAY:
            if (Z_ARR(val) != NULL) {
                Z_ARR(val)->refcount++;
            }
            break;
        case IS_OBJECT:
            if (Z_OBJ(val) != NULL) {
                Z_OBJ(val)->refcount++;
            }
            break;
        case IS_REFERENCE:
            if (Z_REF(val) != NULL) {
                Z_REF(val)->refcount++;
            }
            break;
        default:
            break;              /* null/bool/long/double are self contained */
    }
    return val;
}

/* ------------------------------------------------------------ construction */

HashTable *zend_hash_new(void)
{
    HashTable *ht = ecalloc(1, sizeof(HashTable));

    ht->refcount = 1;
    ht->nNumUsed = 0;
    ht->nNumOfElements = 0;
    ht->nNextFreeElement = 0;
    ht->nInternalPointer = 0;
    ht->protect = 0;
    ht_alloc_data(ht, HT_MIN_SIZE);
    return ht;
}

HashTable *zend_hash_dup(const HashTable *src)
{
    HashTable *ht = ecalloc(1, sizeof(HashTable));
    uint32_t i, j = 0;

    ht->refcount = 1;
    ht->nInternalPointer = 0;           /* a copy starts at its first element */
    ht->protect = 0;

    if (src == NULL) {
        ht_alloc_data(ht, HT_MIN_SIZE);
        return ht;
    }

    ht_alloc_data(ht, ht_size_for(src->nNumOfElements));
    for (i = 0; i < src->nNumUsed; i++) {
        const Bucket *sb = &src->arData[i];
        Bucket *db;

        if (Z_TYPE(sb->val) == IS_UNDEF) {
            continue;                   /* tombstones are not carried over */
        }
        db = &ht->arData[j];
        db->h = sb->h;
        db->key = sb->key;
        if (db->key != NULL) {
            db->key->refcount++;        /* the copy holds the key too */
        }
        db->val = ht_copy_zval(sb->val);
        ht_hash_add(ht, db->h, j);
        j++;
    }
    ht->nNumUsed = j;
    ht->nNumOfElements = j;
    ht->nNextFreeElement = src->nNextFreeElement;
    return ht;
}

void zend_hash_destroy(HashTable *ht)
{
    if (ht == NULL) {
        return;
    }
    if (ht->refcount > 1) {
        ht->refcount--;                 /* still shared by somebody else */
        return;
    }
    ht->refcount = 0;
    /* Nothing is actually released: efree() is a no-op and the buckets stay
     * readable, which keeps any pointer the evaluator still holds valid. */
    efree(ht->arHash);
    efree(ht->arData);
    efree(ht);
}

/* ----------------------------------------------------------------- lookups */

zval *zend_hash_find(const HashTable *ht, const zend_string *key)
{
    Bucket *b;

    if (ht == NULL || key == NULL) {
        return NULL;
    }
    /* The cast only feeds the lazily filled hash cache inside the key. */
    b = ht_find_str_bucket((HashTable *) ht, zend_string_hash((zend_string *) key),
                           key->val, key->len, NULL);
    return (b != NULL) ? &b->val : NULL;
}

zval *zend_hash_str_find(const HashTable *ht, const char *k, size_t len)
{
    Bucket *b;

    if (ht == NULL || k == NULL) {
        return NULL;
    }
    b = ht_find_str_bucket((HashTable *) ht, zend_hash_bytes(k, len), k, len, NULL);
    return (b != NULL) ? &b->val : NULL;
}

zval *zend_hash_index_find(const HashTable *ht, zend_long idx)
{
    Bucket *b;

    if (ht == NULL) {
        return NULL;
    }
    b = ht_find_index_bucket((HashTable *) ht, (zend_ulong) idx, NULL);
    return (b != NULL) ? &b->val : NULL;
}

/* ----------------------------------------------------------------- updates */

zval *zend_hash_update(HashTable *ht, zend_string *key, zval val)
{
    zend_ulong h;
    Bucket *b;

    if (ht == NULL || key == NULL) {
        return NULL;
    }
    h = zend_string_hash(key);
    b = ht_find_str_bucket(ht, h, key->val, key->len, NULL);
    if (b != NULL) {
        b->val = val;                   /* replacing keeps the original position */
        return &b->val;
    }
    key->refcount++;                    /* the table keeps its own reference */
    return ht_append(ht, h, key, val);
}

zval *zend_hash_str_update(HashTable *ht, const char *k, size_t len, zval val)
{
    zend_ulong h;
    Bucket *b;
    zend_string *key;

    if (ht == NULL || k == NULL) {
        return NULL;
    }
    h = zend_hash_bytes(k, len);
    b = ht_find_str_bucket(ht, h, k, len, NULL);
    if (b != NULL) {
        b->val = val;
        return &b->val;
    }
    key = zend_string_init(k, len);     /* refcount 1: owned by the table */
    key->h = h;
    return ht_append(ht, h, key, val);
}

zval *zend_hash_index_update(HashTable *ht, zend_long idx, zval val)
{
    zend_ulong h = (zend_ulong) idx;
    Bucket *b;

    if (ht == NULL) {
        return NULL;
    }
    b = ht_find_index_bucket(ht, h, NULL);
    if (b != NULL) {
        b->val = val;
        return &b->val;
    }
    ht_update_next_free(ht, h);
    return ht_append(ht, h, NULL, val);
}

zval *zend_hash_next_index_insert(HashTable *ht, zval val)
{
    zend_long idx;

    if (ht == NULL) {
        return NULL;
    }
    idx = ht->nNextFreeElement;
    if (idx == ZEND_LONG_MAX
     && ht_find_index_bucket(ht, (zend_ulong) idx, NULL) != NULL) {
        return NULL;                    /* the next index is already occupied */
    }
    return zend_hash_index_update(ht, idx, val);
}

/* --------------------------------------------------------------- deletion */

/* Both return 0 on success and -1 when the key is not there, following the
 * SUCCESS/FAILURE convention the rest of the engine uses. */
int zend_hash_del(HashTable *ht, const zend_string *key)
{
    uint32_t pos = 0;
    Bucket *b;

    if (ht == NULL || key == NULL) {
        return -1;
    }
    b = ht_find_str_bucket(ht, zend_string_hash((zend_string *) key),
                           key->val, key->len, &pos);
    if (b == NULL) {
        return -1;
    }
    ht_delete_bucket(ht, pos);
    return 0;
}

int zend_hash_index_del(HashTable *ht, zend_long idx)
{
    uint32_t pos = 0;
    Bucket *b;

    if (ht == NULL) {
        return -1;
    }
    b = ht_find_index_bucket(ht, (zend_ulong) idx, &pos);
    if (b == NULL) {
        return -1;
    }
    ht_delete_bucket(ht, pos);
    return 0;
}

void zend_hash_clean(HashTable *ht)
{
    if (ht == NULL) {
        return;
    }
    ht->nNumUsed = 0;
    ht->nNumOfElements = 0;
    ht->nNextFreeElement = 0;
    ht->nInternalPointer = 0;
    ht_hash_reset(ht);
}

uint32_t zend_hash_num_elements(const HashTable *ht)
{
    return (ht != NULL) ? ht->nNumOfElements : 0;
}

/* -------------------------------------------------------- reindex and sort */

void zend_hash_renumber(HashTable *ht)
{
    if (ht == NULL) {
        return;
    }
    ht_compact_data(ht);        /* dense arData keeps the new indices dense */
    ht_renumber_keys(ht);
    ht_hash_rebuild(ht);
}

/* Stable merge sort over a plain Bucket array.  An element from the right run
 * is only taken when it compares strictly smaller, so equal elements keep their
 * relative order (php-src's sort has been stable since 8.0). */
static void ht_merge_sort(Bucket *base, Bucket *tmp, uint32_t n,
                          int (*cmp)(const Bucket *, const Bucket *))
{
    uint32_t mid, i, j, k;

    if (n < 2) {
        return;
    }
    mid = n / 2;
    ht_merge_sort(base, tmp, mid, cmp);
    ht_merge_sort(base + mid, tmp, n - mid, cmp);

    i = 0;
    j = mid;
    k = 0;
    while (i < mid && j < n) {
        if (cmp(&base[j], &base[i]) < 0) {
            tmp[k++] = base[j++];
        } else {
            tmp[k++] = base[i++];
        }
    }
    while (i < mid) {
        tmp[k++] = base[i++];
    }
    while (j < n) {
        tmp[k++] = base[j++];
    }
    memcpy(base, tmp, (size_t) n * sizeof(Bucket));
}

void zend_hash_sort(HashTable *ht, int (*cmp)(const Bucket *, const Bucket *),
                    int renumber)
{
    Bucket *live, *tmp;
    uint32_t n, newsize, i, j = 0;

    if (ht == NULL || cmp == NULL) {
        return;
    }
    n = ht->nNumOfElements;

    /* Collect the live buckets in insertion order, sort them, then rebuild
     * arData from scratch so the result has no tombstones at all. */
    live = emalloc((size_t) (n != 0 ? n : 1) * sizeof(Bucket));
    for (i = 0; i < ht->nNumUsed; i++) {
        if (Z_TYPE(ht->arData[i].val) == IS_UNDEF) {
            continue;
        }
        live[j++] = ht->arData[i];
    }
    if (n > 1) {
        tmp = emalloc((size_t) n * sizeof(Bucket));
        ht_merge_sort(live, tmp, n, cmp);
        efree(tmp);
    }

    newsize = ht_size_for(n);
    ht_alloc_data(ht, newsize);
    if (n != 0) {
        memcpy(ht->arData, live, (size_t) n * sizeof(Bucket));
    }
    efree(live);

    ht->nNumUsed = n;
    ht->nNumOfElements = n;
    ht->nInternalPointer = 0;           /* sorting rewinds the array */
    if (renumber) {
        ht_renumber_keys(ht);
    }
    ht_hash_rebuild(ht);
}
