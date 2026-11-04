#include "tx.h"
#include "rlp.h"
#include "keccak.h"
#include "crypto.h"
#include "evm.h"
#include <stdlib.h>
#include <string.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

void tx_free(tx_t *t)
{
    free(t->data);
    for (size_t i = 0; i < t->nal; i++) free(t->al[i].keys);
    free(t->al);
    free(t->blob_hashes);
    free(t->auths);
    free(t->raw);
    memset(t, 0, sizeof *t);
}

static int decode_access_list(const rlp_item *it, access_tuple **out, size_t *n)
{
    if (!it->is_list) return -1;
    rlp_iter iter; rlp_item e;
    rlp_iter_init(&iter, it);
    size_t cap = 4, cnt = 0;
    access_tuple *al = (access_tuple *)calloc(cap, sizeof(access_tuple));
    while (!rlp_iter_done(&iter)) {
        if (rlp_iter_next(&iter, &e) != 0 || !e.is_list) { free(al); return -1; }
        rlp_iter t2; rlp_item f;
        rlp_iter_init(&t2, &e);
        if (rlp_iter_next(&t2, &f) != 0 || f.is_list || f.payload_len != 20) { free(al); return -1; }
        if (cnt == cap) { cap *= 2; al = (access_tuple *)realloc(al, cap * sizeof(access_tuple)); }
        memset(&al[cnt], 0, sizeof(access_tuple));
        memcpy(al[cnt].addr.b, f.payload, 20);
        if (rlp_iter_next(&t2, &f) != 0 || !f.is_list) { free(al); return -1; }
        rlp_iter t3; rlp_item k;
        rlp_iter_init(&t3, &f);
        size_t kcap = 4, kn = 0;
        hash256 *keys = (hash256 *)calloc(kcap, sizeof(hash256));
        while (!rlp_iter_done(&t3)) {
            if (rlp_iter_next(&t3, &k) != 0 || k.is_list || k.payload_len != 32) { free(keys); free(al); return -1; }
            if (kn == kcap) { kcap *= 2; keys = (hash256 *)realloc(keys, kcap * sizeof(hash256)); }
            memcpy(keys[kn].b, k.payload, 32);
            kn++;
        }
        al[cnt].keys = keys;
        al[cnt].nkeys = kn;
        cnt++;
    }
    *out = al;
    *n = cnt;
    return 0;
}

static void encode_access_list(const tx_t *t, buf_t *out)
{
    buf_t list; buf_init(&list);
    for (size_t i = 0; i < t->nal; i++) {
        buf_t tup, keys;
        buf_init(&tup); buf_init(&keys);
        rlp_put_bytes(&tup, t->al[i].addr.b, 20);
        for (size_t j = 0; j < t->al[i].nkeys; j++) rlp_put_bytes(&keys, t->al[i].keys[j].b, 32);
        rlp_put_list(&tup, keys.data, keys.len);
        rlp_put_list(&list, tup.data, tup.len);
        buf_free(&tup); buf_free(&keys);
    }
    rlp_put_list(out, list.data, list.len);
    buf_free(&list);
}

static void encode_auth_list(const tx_t *t, buf_t *out)
{
    buf_t list; buf_init(&list);
    for (size_t i = 0; i < t->nauths; i++) {
        buf_t e; buf_init(&e);
        rlp_put_u256(&e, t->auths[i].chain_id);
        rlp_put_bytes(&e, t->auths[i].addr.b, 20);
        rlp_put_u64(&e, t->auths[i].nonce);
        rlp_put_u64(&e, (uint64_t)t->auths[i].y);
        rlp_put_u256(&e, t->auths[i].r);
        rlp_put_u256(&e, t->auths[i].s);
        rlp_put_list(&list, e.data, e.len);
        buf_free(&e);
    }
    rlp_put_list(out, list.data, list.len);
    buf_free(&list);
}

#define NEXT(it, x) do { if (rlp_iter_next(&(it), &(x)) != 0) return -1; } while (0)
#define NEXT_U64(it, x, dst) do { NEXT(it, x); if (rlp_as_u64(&(x), &(dst)) != 0) { if ((x).payload_len == 0) (dst) = 0; else return -1; } } while (0)
#define NEXT_U256(it, x, dst) do { NEXT(it, x); if (rlp_as_u256(&(x), &(dst)) != 0) return -1; } while (0)
/* go-ethereum types the numeric fields of the legacy, access-list and dynamic-fee
 * transactions as *big.Int, so a field wider than 32 bytes decodes there and is only
 * rejected by a later validity check.  Saturate and remember; the blob and set-code
 * transactions use uint256 fields and stay strict (checked at the end of tx_decode). */
#define NEXT_U256S(it, x, dst) do { NEXT(it, x); \
    if (rlp_as_u256(&(x), &(dst)) != 0) { \
        if ((x).is_list || (x).payload_len <= 32 || (x).payload[0] == 0) return -1; \
        out->over256 = true; memset((dst).v, 0xff, sizeof (dst).v); \
    } } while (0)

int tx_decode(const uint8_t *p, size_t len, tx_t *out)
{
    memset(out, 0, sizeof *out);
    out->raw = (uint8_t *)malloc(len ? len : 1);
    memcpy(out->raw, p, len);
    out->raw_len = len;

    rlp_item top;
    uint8_t type = 0;
    const uint8_t *body = p;
    size_t blen = len;
    if (len == 0) return -1;
    if (p[0] < 0xc0) {
        type = p[0];
        if (type < 1 || type > 4) return -1;
        body = p + 1;
        blen = len - 1;
    }
    out->type = type;
    if (rlp_decode(body, blen, &top) != 0 || !top.is_list || top.total_len != blen) return -1;

    rlp_iter it; rlp_item x;
    rlp_iter_init(&it, &top);
    if (type == 0) {
        NEXT_U64(it, x, out->nonce);
        NEXT_U256S(it, x, out->gas_price);
        NEXT_U64(it, x, out->gas);
        NEXT(it, x);
        if (x.is_list) return -1;
        if (x.payload_len == 20) { out->has_to = true; memcpy(out->to.b, x.payload, 20); }
        else if (x.payload_len != 0) return -1;
        NEXT_U256S(it, x, out->value);
        NEXT(it, x);
        out->data_len = x.payload_len;
        out->data = (uint8_t *)malloc(x.payload_len ? x.payload_len : 1);
        memcpy(out->data, x.payload, x.payload_len);
        NEXT_U256S(it, x, out->v);
        NEXT_U256S(it, x, out->r);
        NEXT_U256S(it, x, out->s);
        if (!rlp_iter_done(&it)) return -1;
        out->max_fee = out->gas_price;
        out->max_prio = out->gas_price;
        return 0;
    }
    NEXT_U256S(it, x, out->chain_id);
    out->has_chain_id = true;
    NEXT_U64(it, x, out->nonce);
    if (type == 1) {
        NEXT_U256S(it, x, out->gas_price);
        out->max_fee = out->gas_price;
        out->max_prio = out->gas_price;
    } else {
        NEXT_U256S(it, x, out->max_prio);
        NEXT_U256S(it, x, out->max_fee);
        out->gas_price = out->max_fee;
    }
    NEXT_U64(it, x, out->gas);
    NEXT(it, x);
    if (x.is_list) return -1;
    if (x.payload_len == 20) { out->has_to = true; memcpy(out->to.b, x.payload, 20); }
    else if (x.payload_len != 0) return -1;
    else if (type == 3 || type == 4) return -1;          /* type 3/4 must have a destination */
    NEXT_U256S(it, x, out->value);
    NEXT(it, x);
    out->data_len = x.payload_len;
    out->data = (uint8_t *)malloc(x.payload_len ? x.payload_len : 1);
    memcpy(out->data, x.payload, x.payload_len);
    NEXT(it, x);
    if (decode_access_list(&x, &out->al, &out->nal) != 0) return -1;
    if (type == 3) {
        NEXT_U256S(it, x, out->max_fee_blob);
        NEXT(it, x);
        if (!x.is_list) return -1;
        rlp_iter bi; rlp_item b;
        rlp_iter_init(&bi, &x);
        size_t cap = 4, n = 0;
        hash256 *hs = (hash256 *)calloc(cap, sizeof(hash256));
        while (!rlp_iter_done(&bi)) {
            if (rlp_iter_next(&bi, &b) != 0 || b.is_list || b.payload_len != 32) { free(hs); return -1; }
            if (n == cap) { cap *= 2; hs = (hash256 *)realloc(hs, cap * sizeof(hash256)); }
            memcpy(hs[n].b, b.payload, 32);
            n++;
        }
        out->blob_hashes = hs;
        out->nblobs = n;
    }
    if (type == 4) {
        NEXT(it, x);
        if (!x.is_list) return -1;
        rlp_iter ai; rlp_item e;
        rlp_iter_init(&ai, &x);
        size_t cap = 4, n = 0;
        authorization *au = (authorization *)calloc(cap, sizeof(authorization));
        while (!rlp_iter_done(&ai)) {
            if (rlp_iter_next(&ai, &e) != 0 || !e.is_list) { free(au); return -1; }
            rlp_iter fi; rlp_item f;
            rlp_iter_init(&fi, &e);
            if (n == cap) { cap *= 2; au = (authorization *)realloc(au, cap * sizeof(authorization)); }
            memset(&au[n], 0, sizeof(authorization));
            if (rlp_iter_next(&fi, &f) != 0 || rlp_as_u256(&f, &au[n].chain_id) != 0) { free(au); return -1; }
            if (rlp_iter_next(&fi, &f) != 0 || f.is_list || f.payload_len != 20) { free(au); return -1; }
            memcpy(au[n].addr.b, f.payload, 20);
            uint64_t nn = 0;
            if (rlp_iter_next(&fi, &f) != 0) { free(au); return -1; }
            if (rlp_as_u64(&f, &nn) != 0) { if (f.payload_len) { free(au); return -1; } nn = 0; }
            au[n].nonce = nn;
            uint64_t yy = 0;
            if (rlp_iter_next(&fi, &f) != 0) { free(au); return -1; }
            if (rlp_as_u64(&f, &yy) != 0) { if (f.payload_len) { free(au); return -1; } yy = 0; }
            au[n].y = (int)yy;
            if (rlp_iter_next(&fi, &f) != 0 || rlp_as_u256(&f, &au[n].r) != 0) { free(au); return -1; }
            if (rlp_iter_next(&fi, &f) != 0 || rlp_as_u256(&f, &au[n].s) != 0) { free(au); return -1; }
            if (!rlp_iter_done(&fi)) { free(au); return -1; }
            n++;
        }
        out->auths = au;
        out->nauths = n;
        if (n == 0) return -1;                            /* EIP-7702: empty auth list invalid */
    }
    NEXT_U256S(it, x, out->v);
    NEXT_U256S(it, x, out->r);
    NEXT_U256S(it, x, out->s);
    if (!rlp_iter_done(&it)) return -1;
    /* the blob and set-code transactions carry uint256 fields, which cannot hold more
     * than 32 bytes at all, so an over-wide field is a decode failure there. */
    if (out->over256 && (type == 3 || type == 4)) return -1;
    return 0;
}

static void put_payload(const tx_t *t, buf_t *pl, bool with_sig, u256 chain_id, bool eip155)
{
    if (t->type == 0) {
        rlp_put_u64(pl, t->nonce);
        rlp_put_u256(pl, t->gas_price);
        rlp_put_u64(pl, t->gas);
        if (t->has_to) rlp_put_bytes(pl, t->to.b, 20); else rlp_put_bytes(pl, NULL, 0);
        rlp_put_u256(pl, t->value);
        rlp_put_bytes(pl, t->data, t->data_len);
        if (with_sig) {
            rlp_put_u256(pl, t->v);
            rlp_put_u256(pl, t->r);
            rlp_put_u256(pl, t->s);
        } else if (eip155) {
            rlp_put_u256(pl, chain_id);
            rlp_put_u64(pl, 0);
            rlp_put_u64(pl, 0);
        }
        return;
    }
    rlp_put_u256(pl, t->chain_id);
    rlp_put_u64(pl, t->nonce);
    if (t->type == 1) {
        rlp_put_u256(pl, t->gas_price);
    } else {
        rlp_put_u256(pl, t->max_prio);
        rlp_put_u256(pl, t->max_fee);
    }
    rlp_put_u64(pl, t->gas);
    if (t->has_to) rlp_put_bytes(pl, t->to.b, 20); else rlp_put_bytes(pl, NULL, 0);
    rlp_put_u256(pl, t->value);
    rlp_put_bytes(pl, t->data, t->data_len);
    encode_access_list(t, pl);
    if (t->type == 3) {
        rlp_put_u256(pl, t->max_fee_blob);
        buf_t bl; buf_init(&bl);
        for (size_t i = 0; i < t->nblobs; i++) rlp_put_bytes(&bl, t->blob_hashes[i].b, 32);
        rlp_put_list(pl, bl.data, bl.len);
        buf_free(&bl);
    }
    if (t->type == 4) encode_auth_list(t, pl);
    if (with_sig) {
        rlp_put_u256(pl, t->v);
        rlp_put_u256(pl, t->r);
        rlp_put_u256(pl, t->s);
    }
}

void tx_encode(const tx_t *t, buf_t *out)
{
    buf_t pl; buf_init(&pl);
    put_payload(t, &pl, true, U256_ZERO, false);
    if (t->type) buf_putc(out, t->type);
    rlp_put_list(out, pl.data, pl.len);
    buf_free(&pl);
}

void tx_signing_hash(const tx_t *t, u256 chain_id, bool eip155, hash256 *out)
{
    buf_t pl, msg;
    buf_init(&pl); buf_init(&msg);
    put_payload(t, &pl, false, chain_id, eip155);
    if (t->type) buf_putc(&msg, t->type);
    rlp_put_list(&msg, pl.data, pl.len);
    keccak256(msg.data, msg.len, out->b);
    buf_free(&pl); buf_free(&msg);
}

void tx_compute_hash(tx_t *t)
{
    /* When the transaction arrived as RLP the hash is over the bytes exactly as they came:
     * re-encoding would normalise a field that was wider than 32 bytes. */
    if (t->raw && t->raw_len) { keccak256(t->raw, t->raw_len, t->hash.b); return; }
    buf_t e; buf_init(&e);
    tx_encode(t, &e);
    keccak256(e.data, e.len, t->hash.b);
    buf_free(&e);
}

int tx_recover(tx_t *t, fork_id fk, u256 chain_id)
{
    hash256 sh;
    int y;
    if (t->type == 0) {
        if (!u256_fits_u64(t->v)) {
            /* EIP-155 with a chain id that does not fit in 64 bits */
            if (!IS_EIP158(fk)) return -1;
            u256 two = u256_from_u64(2), c35 = u256_from_u64(35);
            if (u256_cmp(t->v, c35) < 0) return -1;
            u256 cid = u256_div(u256_sub(t->v, c35), two);
            u256 rec = u256_sub(t->v, u256_add(u256_mul(cid, two), c35));
            if (!u256_fits_u64(rec) || rec.v[0] > 1) return -1;
            if (!u256_eq(cid, chain_id)) return -1;
            y = (int)rec.v[0];
            t->chain_id = cid;
            t->has_chain_id = true;
            tx_signing_hash(t, cid, true, &sh);
        } else {
            uint64_t v = t->v.v[0];
            if (v == 27 || v == 28) {
                y = (int)(v - 27);
                t->has_chain_id = false;
                tx_signing_hash(t, U256_ZERO, false, &sh);
            } else if (v >= 35) {
                if (!IS_EIP158(fk)) return -1;
                uint64_t cid = (v - 35) / 2;
                y = (int)((v - 35) % 2);
                u256 c = u256_from_u64(cid);
                if (!u256_eq(c, chain_id)) return -1;
                t->chain_id = c;
                t->has_chain_id = true;
                tx_signing_hash(t, c, true, &sh);
            } else {
                return -1;
            }
        }
    } else {
        if (!u256_fits_u64(t->v) || t->v.v[0] > 1) return -1;
        y = (int)t->v.v[0];
        if (!u256_eq(t->chain_id, chain_id)) return -1;
        tx_signing_hash(t, chain_id, false, &sh);
    }
    if (!secp_recover(sh.b, &t->r, &t->s, y, IS_HOMESTEAD(fk), &t->sender)) return -1;
    t->have_sender = true;
    return 0;
}

int tx_sign(tx_t *t, const uint8_t key[32], u256 chain_id, bool eip155)
{
    hash256 sh;
    tx_signing_hash(t, chain_id, eip155, &sh);
    static secp256k1_context *c = NULL;
    if (!c) c = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_sign_recoverable(c, &sig, sh.b, key, NULL, NULL)) return -1;
    uint8_t out64[64];
    int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(c, out64, &recid, &sig);
    u256_from_be(&t->r, out64, 32);
    u256_from_be(&t->s, out64 + 32, 32);
    if (t->type == 0) {
        if (eip155) {
            u256 two = u256_from_u64(2);
            t->v = u256_add(u256_add(u256_mul(chain_id, two), u256_from_u64(35)), u256_from_u64((uint64_t)recid));
            t->chain_id = chain_id;
            t->has_chain_id = true;
        } else {
            t->v = u256_from_u64(27 + (uint64_t)recid);
        }
    } else {
        t->v = u256_from_u64((uint64_t)recid);
    }
    secp256k1_pubkey pk;
    if (secp256k1_ec_pubkey_create(c, &pk, key)) {
        uint8_t ser[65]; size_t slen = 65;
        secp256k1_ec_pubkey_serialize(c, ser, &slen, &pk, SECP256K1_EC_UNCOMPRESSED);
        hash256 h;
        keccak256(ser + 1, 64, h.b);
        memcpy(t->sender.b, h.b + 12, 20);
        t->have_sender = true;
    }
    tx_compute_hash(t);
    return 0;
}

uint64_t tx_intrinsic_gas(const tx_t *t, fork_id fk, uint64_t *floor_out)
{
    uint64_t gas = 21000;
    if (!t->has_to && IS_HOMESTEAD(fk)) gas = 53000;
    uint64_t nonzero = 0, zero = 0;
    for (size_t i = 0; i < t->data_len; i++) {
        if (t->data[i]) nonzero++; else zero++;
    }
    uint64_t nz_cost = IS_ISTANBUL(fk) ? 16 : 68;
    gas += zero * 4 + nonzero * nz_cost;
    if (!t->has_to && IS_SHANGHAI(fk)) gas += 2 * ((t->data_len + 31) / 32);
    for (size_t i = 0; i < t->nal; i++) {
        gas += 2400;
        gas += 1900 * t->al[i].nkeys;
    }
    if (IS_PRAGUE(fk)) gas += 25000 * t->nauths;
    if (floor_out) {
        uint64_t tokens = zero + 4 * nonzero;
        *floor_out = IS_PRAGUE(fk) ? (21000 + 10 * tokens) : 0;
    }
    return gas;
}
