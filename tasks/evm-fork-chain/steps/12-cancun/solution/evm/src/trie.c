#include "trie.h"
#include "rlp.h"
#include "keccak.h"
#include <stdlib.h>

const hash256 EMPTY_TRIE_ROOT = {{
    0x56,0xe8,0x1f,0x17,0x1b,0xcc,0x55,0xa6,0xff,0x83,0x45,0xe6,0x92,0xc0,0xf8,0x6e,
    0x5b,0x48,0xe0,0x1b,0x99,0x6c,0xad,0xc0,0x01,0x62,0x2f,0xb5,0xe3,0x63,0xb4,0x21}};
const hash256 EMPTY_CODE_HASH = {{
    0xc5,0xd2,0x46,0x01,0x86,0xf7,0x23,0x3c,0x92,0x7e,0x7d,0xb2,0xdc,0xc7,0x03,0xc0,
    0xe5,0x00,0xb6,0x53,0xca,0x82,0x27,0x3b,0x7b,0xfa,0xd8,0x04,0x5d,0x85,0xa4,0x70}};

static int cmp_kv(const void *a, const void *b)
{
    const trie_kv *x = (const trie_kv *)a, *y = (const trie_kv *)b;
    size_t n = x->klen < y->klen ? x->klen : y->klen;
    int c = memcmp(x->key, y->key, n);
    if (c) return c;
    return x->klen < y->klen ? -1 : (x->klen > y->klen ? 1 : 0);
}

static inline int nib(const trie_kv *e, size_t i)
{
    uint8_t b = e->key[i / 2];
    return (i % 2) ? (b & 0x0f) : (b >> 4);
}

/* hex-prefix encode nibbles [from,to) of e */
static void hp_encode(buf_t *out, const trie_kv *e, size_t from, size_t to, bool leaf)
{
    size_t n = to - from;
    buf_t nb; buf_init(&nb);
    uint8_t first = leaf ? 0x20 : 0x00;
    if (n % 2) {
        first |= 0x10 | (uint8_t)nib(e, from);
        from++;
    }
    buf_putc(&nb, first);
    for (size_t i = from; i < to; i += 2)
        buf_putc(&nb, (uint8_t)((nib(e, i) << 4) | nib(e, i + 1)));
    rlp_put_bytes(out, nb.data, nb.len);
    buf_free(&nb);
}

static void build(trie_kv *e, size_t n, size_t depth, buf_t *node);

/* write a reference to the child node into `out` */
static void put_ref(buf_t *out, const buf_t *child)
{
    if (child->len < 32) {
        buf_put(out, child->data, child->len);     /* embedded, already RLP */
    } else {
        hash256 h;
        keccak256(child->data, child->len, h.b);
        rlp_put_bytes(out, h.b, 32);
    }
}

static void build(trie_kv *e, size_t n, size_t depth, buf_t *node)
{
    buf_t payload; buf_init(&payload);
    if (n == 1) {
        hp_encode(&payload, &e[0], depth, e[0].klen * 2, true);
        rlp_put_bytes(&payload, e[0].val, e[0].vlen);
        rlp_put_list(node, payload.data, payload.len);
        buf_free(&payload);
        return;
    }
    /* longest common nibble prefix starting at depth */
    size_t maxlen = e[0].klen * 2;
    size_t lcp = depth;
    while (lcp < maxlen) {
        int c = nib(&e[0], lcp);
        bool same = true;
        for (size_t i = 1; i < n; i++) {
            if (e[i].klen * 2 <= lcp || nib(&e[i], lcp) != c) { same = false; break; }
        }
        if (!same) break;
        lcp++;
    }
    if (lcp > depth) {
        buf_t child; buf_init(&child);
        build(e, n, lcp, &child);
        hp_encode(&payload, &e[0], depth, lcp, false);
        put_ref(&payload, &child);
        buf_free(&child);
        rlp_put_list(node, payload.data, payload.len);
        buf_free(&payload);
        return;
    }
    /* branch */
    {
        size_t idx = 0;
        const uint8_t *branch_val = NULL; size_t branch_vlen = 0;
        if (e[0].klen * 2 == depth) { branch_val = e[0].val; branch_vlen = e[0].vlen; idx = 1; }
        for (int k = 0; k < 16; k++) {
            size_t start = idx;
            while (idx < n && nib(&e[idx], depth) == k) idx++;
            if (idx == start) {
                rlp_put_bytes(&payload, NULL, 0);
            } else {
                buf_t child; buf_init(&child);
                build(e + start, idx - start, depth + 1, &child);
                put_ref(&payload, &child);
                buf_free(&child);
            }
        }
        rlp_put_bytes(&payload, branch_val, branch_vlen);
        rlp_put_list(node, payload.data, payload.len);
        buf_free(&payload);
    }
}

void trie_root(trie_kv *kvs, size_t n, hash256 *out)
{
    if (n == 0) { *out = EMPTY_TRIE_ROOT; return; }
    qsort(kvs, n, sizeof(trie_kv), cmp_kv);
    buf_t node; buf_init(&node);
    build(kvs, n, 0, &node);
    keccak256(node.data, node.len, out->b);
    buf_free(&node);
}

void trie_root_list(const buf_t *values, size_t n, hash256 *out)
{
    if (n == 0) { *out = EMPTY_TRIE_ROOT; return; }
    trie_kv *kvs = (trie_kv *)calloc(n, sizeof(trie_kv));
    for (size_t i = 0; i < n; i++) {
        buf_t k; buf_init(&k);
        rlp_put_u64(&k, (uint64_t)i);
        memcpy(kvs[i].key, k.data, k.len);
        kvs[i].klen = k.len;
        buf_free(&k);
        kvs[i].val = values[i].data;
        kvs[i].vlen = values[i].len;
    }
    trie_root(kvs, n, out);
    free(kvs);
}
