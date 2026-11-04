#include "state.h"
#include "rlp.h"
#include "keccak.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ slotmap */
static uint64_t hash_bytes(const uint8_t *p, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static void slotmap_grow(slotmap *m)
{
    size_t ncap = m->cap ? m->cap * 2 : 16;
    slot_entry *ne = (slot_entry *)calloc(ncap, sizeof(slot_entry));
    for (size_t i = 0; i < m->cap; i++) {
        if (!m->e[i].used) continue;
        size_t j = hash_bytes(m->e[i].key.b, 32) & (ncap - 1);
        while (ne[j].used) j = (j + 1) & (ncap - 1);
        ne[j] = m->e[i];
    }
    free(m->e);
    m->e = ne;
    m->cap = ncap;
}

bool slotmap_get(const slotmap *m, hash256 k, u256 *out)
{
    if (!m->cap) return false;
    size_t j = hash_bytes(k.b, 32) & (m->cap - 1);
    while (m->e[j].used) {
        if (hash_eq(m->e[j].key, k)) { *out = m->e[j].val; return true; }
        j = (j + 1) & (m->cap - 1);
    }
    return false;
}

void slotmap_set(slotmap *m, hash256 k, u256 v)
{
    if (m->n * 2 >= m->cap) slotmap_grow(m);
    size_t j = hash_bytes(k.b, 32) & (m->cap - 1);
    while (m->e[j].used) {
        if (hash_eq(m->e[j].key, k)) { m->e[j].val = v; return; }
        j = (j + 1) & (m->cap - 1);
    }
    m->e[j].used = true;
    m->e[j].key = k;
    m->e[j].val = v;
    m->n++;
}

void slotmap_clear(slotmap *m) { free(m->e); m->e = NULL; m->cap = 0; m->n = 0; }

/* ------------------------------------------------------------------ acctmap */
static void acctmap_grow(acctmap *m)
{
    size_t ncap = m->cap ? m->cap * 2 : 32;
    account **ne = (account **)calloc(ncap, sizeof(account *));
    for (size_t i = 0; i < m->cap; i++) {
        if (!m->e[i]) continue;
        size_t j = hash_bytes(m->e[i]->addr.b, 20) & (ncap - 1);
        while (ne[j]) j = (j + 1) & (ncap - 1);
        ne[j] = m->e[i];
    }
    free(m->e);
    m->e = ne;
    m->cap = ncap;
}

static account *acctmap_find(const acctmap *m, address a)
{
    if (!m->cap) return NULL;
    size_t j = hash_bytes(a.b, 20) & (m->cap - 1);
    while (m->e[j]) {
        if (addr_eq(m->e[j]->addr, a)) return m->e[j];
        j = (j + 1) & (m->cap - 1);
    }
    return NULL;
}

static void acctmap_put(acctmap *m, account *ac)
{
    if (m->n * 2 >= m->cap) acctmap_grow(m);
    size_t j = hash_bytes(ac->addr.b, 20) & (m->cap - 1);
    while (m->e[j]) {
        if (addr_eq(m->e[j]->addr, ac->addr)) { m->e[j] = ac; return; }
        j = (j + 1) & (m->cap - 1);
    }
    m->e[j] = ac;
    m->n++;
}

/* ------------------------------------------------------------------ journal */
static jentry *jpush(state *s, jkind k)
{
    if (s->jn == s->jcap) {
        s->jcap = s->jcap ? s->jcap * 2 : 256;
        s->journal = (jentry *)realloc(s->journal, s->jcap * sizeof(jentry));
    }
    jentry *e = &s->journal[s->jn++];
    memset(e, 0, sizeof(*e));
    e->k = k;
    return e;
}

void state_init(state *s) { memset(s, 0, sizeof(*s)); }

account *state_get(state *s, address a)
{
    account *ac = acctmap_find(&s->accounts, a);
    if (ac && ac->exists) return ac;
    return NULL;
}

account *state_getor(state *s, address a)
{
    account *ac = acctmap_find(&s->accounts, a);
    if (ac) return ac;
    ac = (account *)calloc(1, sizeof(account));
    ac->addr = a;
    ac->code_hash = EMPTY_CODE_HASH;
    ac->exists = false;
    acctmap_put(&s->accounts, ac);
    return ac;
}

bool state_exists(const state *s, address a)
{
    account *ac = acctmap_find(&s->accounts, a);
    return ac && ac->exists;
}

bool state_empty(state *s, address a)
{
    account *ac = acctmap_find(&s->accounts, a);
    if (!ac || !ac->exists) return true;
    return ac->nonce == 0 && u256_is_zero(ac->balance) && ac->code_len == 0;
}

u256 state_balance(state *s, address a)
{
    account *ac = state_get(s, a);
    return ac ? ac->balance : U256_ZERO;
}

uint64_t state_nonce(state *s, address a)
{
    account *ac = state_get(s, a);
    return ac ? ac->nonce : 0;
}

const uint8_t *state_code(state *s, address a, size_t *len)
{
    account *ac = state_get(s, a);
    if (!ac) { *len = 0; return NULL; }
    *len = ac->code_len;
    return ac->code;
}

hash256 state_codehash(state *s, address a)
{
    account *ac = state_get(s, a);
    if (!ac) return hash_zero();
    return ac->code_hash;
}

u256 state_storage(state *s, address a, hash256 k)
{
    account *ac = state_get(s, a);
    u256 v;
    if (!ac) return U256_ZERO;
    if (slotmap_get(&ac->storage, k, &v)) return v;
    return U256_ZERO;
}

u256 state_orig_storage(state *s, address a, hash256 k)
{
    account *ac = acctmap_find(&s->accounts, a);
    u256 v;
    if (!ac) return U256_ZERO;
    if (slotmap_get(&ac->orig, k, &v)) return v;
    if (slotmap_get(&ac->storage, k, &v)) return v;
    return U256_ZERO;
}

u256 state_tstorage(state *s, address a, hash256 k)
{
    account *ac = acctmap_find(&s->accounts, a);
    u256 v;
    if (!ac) return U256_ZERO;
    if (slotmap_get(&ac->transient, k, &v)) return v;
    return U256_ZERO;
}

static const uint8_t RIPEMD_ADDR[20] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3};

static bool is_ripemd(address a) { return memcmp(a.b, RIPEMD_ADDR, 20) == 0; }

static void ensure_exists(state *s, account *ac)
{
    if (!ac->exists) {
        jentry *j = jpush(s, J_CREATE);
        j->addr = ac->addr;
        ac->exists = true;
    }
}

void state_create_account(state *s, address a)
{
    account *ac = state_getor(s, a);
    ensure_exists(s, ac);
    state_touch(s, a);
}

static bool acct_empty(const account *ac)
{
    return ac->nonce == 0 && u256_is_zero(ac->balance) && ac->code_len == 0;
}

void state_add_balance(state *s, address a, u256 v)
{
    account *ac = state_getor(s, a);
    ensure_exists(s, ac);
    if (u256_is_zero(v)) { if (acct_empty(ac)) state_touch(s, a); return; }
    state_touch(s, a);
    jentry *j = jpush(s, J_BALANCE);
    j->addr = a; j->prev = ac->balance;
    ac->balance = u256_add(ac->balance, v);
}

void state_sub_balance(state *s, address a, u256 v)
{
    account *ac = state_getor(s, a);
    ensure_exists(s, ac);
    if (u256_is_zero(v)) { if (acct_empty(ac)) state_touch(s, a); return; }
    state_touch(s, a);
    jentry *j = jpush(s, J_BALANCE);
    j->addr = a; j->prev = ac->balance;
    ac->balance = u256_sub(ac->balance, v);
}

void state_set_balance(state *s, address a, u256 v)
{
    account *ac = state_getor(s, a);
    ensure_exists(s, ac);
    state_touch(s, a);
    jentry *j = jpush(s, J_BALANCE);
    j->addr = a; j->prev = ac->balance;
    ac->balance = v;
}

void state_set_nonce(state *s, address a, uint64_t n)
{
    account *ac = state_getor(s, a);
    ensure_exists(s, ac);
    state_touch(s, a);
    jentry *j = jpush(s, J_NONCE);
    j->addr = a; j->prevnonce = ac->nonce;
    ac->nonce = n;
}

void state_set_code(state *s, address a, const uint8_t *c, size_t n)
{
    account *ac = state_getor(s, a);
    ensure_exists(s, ac);
    state_touch(s, a);
    jentry *j = jpush(s, J_CODE);
    j->addr = a; j->prevcode = ac->code; j->prevcodelen = ac->code_len; j->prevcodehash = ac->code_hash;
    if (n) {
        ac->code = (uint8_t *)malloc(n);
        memcpy(ac->code, c, n);
    } else {
        ac->code = NULL;
    }
    ac->code_len = n;
    if (n) keccak256(c, n, ac->code_hash.b);
    else ac->code_hash = EMPTY_CODE_HASH;
}

static void note_orig(state *s, account *ac, hash256 k)
{
    u256 v;
    if (slotmap_get(&ac->orig, k, &v)) return;
    if (!slotmap_get(&ac->storage, k, &v)) v = U256_ZERO;
    slotmap_set(&ac->orig, k, v);
}

void state_set_storage(state *s, address a, hash256 k, u256 v)
{
    account *ac = state_getor(s, a);
    ensure_exists(s, ac);
    state_touch(s, a);
    note_orig(s, ac, k);
    u256 prev;
    if (!slotmap_get(&ac->storage, k, &prev)) prev = U256_ZERO;
    jentry *j = jpush(s, J_STORAGE);
    j->addr = a; j->slot = k; j->prev = prev;
    slotmap_set(&ac->storage, k, v);
}

void state_set_tstorage(state *s, address a, hash256 k, u256 v)
{
    account *ac = state_getor(s, a);
    u256 prev;
    if (!slotmap_get(&ac->transient, k, &prev)) prev = U256_ZERO;
    jentry *j = jpush(s, J_TRANSIENT);
    j->addr = a; j->slot = k; j->prev = prev;
    slotmap_set(&ac->transient, k, v);
}

void state_touch(state *s, address a)
{
    account *ac = state_getor(s, a);
    if (ac->touched) return;
    jentry *j = jpush(s, J_TOUCH);
    j->addr = a;
    ac->touched = true;
}

void state_destruct(state *s, address a)
{
    account *ac = state_getor(s, a);
    if (ac->destructed) return;
    jentry *j = jpush(s, J_DESTRUCT);
    j->addr = a;
    ac->destructed = true;
}

bool state_is_destructed(state *s, address a)
{
    account *ac = acctmap_find(&s->accounts, a);
    return ac && ac->destructed;
}

void state_mark_created(state *s, address a)
{
    account *ac = state_getor(s, a);
    if (ac->created) return;
    jentry *j = jpush(s, J_CREATED);
    j->addr = a;
    ac->created = true;
}

bool state_was_created(state *s, address a)
{
    account *ac = acctmap_find(&s->accounts, a);
    return ac && ac->created;
}

void state_add_refund(state *s, int64_t v)
{
    jentry *j = jpush(s, J_REFUND);
    j->refund_delta = v;
    s->refund += v;
}

void state_add_log(state *s, const evm_log *l)
{
    if (s->nlogs == s->logcap) {
        s->logcap = s->logcap ? s->logcap * 2 : 16;
        s->logs = (evm_log *)realloc(s->logs, s->logcap * sizeof(evm_log));
    }
    s->logs[s->nlogs++] = *l;
    jpush(s, J_LOG);
}

/* ---------------------------------------------------------------- access set */
bool state_addr_warm(state *s, address a)
{
    return acctmap_find(&s->warm_addr, a) != NULL;
}

void state_warm_addr(state *s, address a)
{
    if (state_addr_warm(s, a)) return;
    account *tag = (account *)calloc(1, sizeof(account));
    tag->addr = a;
    acctmap_put(&s->warm_addr, tag);
    jentry *j = jpush(s, J_ACCESS_ADDR);
    j->addr = a;
}

static size_t warm_slot_index(state *s, address a, hash256 k)
{
    for (size_t i = 0; i < s->nwarm; i++)
        if (addr_eq(s->warm_slot_addr[i], a) && hash_eq(s->warm_slots[i].key, k)) return i;
    return (size_t)-1;
}

bool state_slot_warm(state *s, address a, hash256 k)
{
    return warm_slot_index(s, a, k) != (size_t)-1;
}

void state_warm_slot(state *s, address a, hash256 k)
{
    if (state_slot_warm(s, a, k)) return;
    if (s->nwarm == s->warmcap) {
        s->warmcap = s->warmcap ? s->warmcap * 2 : 64;
        s->warm_slots = (slot_entry *)realloc(s->warm_slots, s->warmcap * sizeof(slot_entry));
        s->warm_slot_addr = (address *)realloc(s->warm_slot_addr, s->warmcap * sizeof(address));
    }
    s->warm_slots[s->nwarm].key = k;
    s->warm_slots[s->nwarm].used = true;
    s->warm_slot_addr[s->nwarm] = a;
    s->nwarm++;
    jentry *j = jpush(s, J_ACCESS_SLOT);
    j->addr = a; j->slot = k;
}

void state_clear_access(state *s)
{
    free(s->warm_addr.e);
    s->warm_addr.e = NULL; s->warm_addr.cap = 0; s->warm_addr.n = 0;
    s->nwarm = 0;
}

/* --------------------------------------------------------------- snapshotting */
size_t state_snapshot(state *s) { return s->jn; }

void state_revert(state *s, size_t snap)
{
    while (s->jn > snap) {
        jentry *j = &s->journal[--s->jn];
        account *ac = acctmap_find(&s->accounts, j->addr);
        switch (j->k) {
        case J_CREATE:   if (ac) ac->exists = false; break;
        case J_BALANCE:  if (ac) ac->balance = j->prev; break;
        case J_NONCE:    if (ac) ac->nonce = j->prevnonce; break;
        case J_CODE:
            if (ac) { ac->code = j->prevcode; ac->code_len = j->prevcodelen; ac->code_hash = j->prevcodehash; }
            break;
        case J_STORAGE:  if (ac) slotmap_set(&ac->storage, j->slot, j->prev); break;
        case J_TRANSIENT:if (ac) slotmap_set(&ac->transient, j->slot, j->prev); break;
        case J_TOUCH:
            /* A reverted frame un-dirties the account again — except address
             * 0x...03, which go-ethereum pins in the dirty set for good. */
            if (ac && !is_ripemd(j->addr)) ac->touched = false;
            break;
        case J_DESTRUCT: if (ac) ac->destructed = false; break;
        case J_CREATED:  if (ac) ac->created = false; break;
        case J_ACCESS_ADDR: {
            /* remove from warm set */
            if (s->warm_addr.cap) {
                size_t idx = hash_bytes(j->addr.b, 20) & (s->warm_addr.cap - 1);
                while (s->warm_addr.e[idx]) {
                    if (addr_eq(s->warm_addr.e[idx]->addr, j->addr)) break;
                    idx = (idx + 1) & (s->warm_addr.cap - 1);
                }
                if (s->warm_addr.e[idx]) {
                    /* rebuild to keep probing invariants simple */
                    account **old = s->warm_addr.e;
                    size_t cap = s->warm_addr.cap;
                    s->warm_addr.e = (account **)calloc(cap, sizeof(account *));
                    s->warm_addr.cap = cap; s->warm_addr.n = 0;
                    for (size_t i = 0; i < cap; i++)
                        if (old[i] && !addr_eq(old[i]->addr, j->addr)) acctmap_put(&s->warm_addr, old[i]);
                    free(old);
                }
            }
            break;
        }
        case J_ACCESS_SLOT: {
            size_t i = warm_slot_index(s, j->addr, j->slot);
            if (i != (size_t)-1) {
                s->warm_slots[i] = s->warm_slots[s->nwarm - 1];
                s->warm_slot_addr[i] = s->warm_slot_addr[s->nwarm - 1];
                s->nwarm--;
            }
            break;
        }
        case J_REFUND:   s->refund -= j->refund_delta; break;
        case J_LOG:      if (s->nlogs) s->nlogs--; break;
        }
    }
}

void state_commit_tx(state *s)
{
    for (size_t i = 0; i < s->accounts.cap; i++) {
        account *ac = s->accounts.e[i];
        if (!ac) continue;
        slotmap_clear(&ac->orig);
        slotmap_clear(&ac->transient);
        ac->created = false;
        ac->destructed = false;
        ac->touched = false;
    }
    s->jn = 0;
    s->refund = 0;
    state_clear_access(s);
}

void state_finalise_destructed(state *s)
{
    for (size_t i = 0; i < s->accounts.cap; i++) {
        account *ac = s->accounts.e[i];
        if (!ac || !ac->destructed) continue;
        ac->exists = false;
        ac->balance = U256_ZERO;
        ac->nonce = 0;
        ac->code = NULL;
        ac->code_len = 0;
        ac->code_hash = EMPTY_CODE_HASH;
        slotmap_clear(&ac->storage);
        ac->destructed = false;
    }
}

void state_delete_touched_empty(state *s, bool active)
{
    if (!active) return;
    for (size_t i = 0; i < s->accounts.cap; i++) {
        account *ac = s->accounts.e[i];
        if (!ac || !ac->exists || !ac->touched) continue;
        if (ac->nonce == 0 && u256_is_zero(ac->balance) && ac->code_len == 0) {
            ac->exists = false;
            slotmap_clear(&ac->storage);
        }
    }
}

/* ------------------------------------------------------------------- hashing */
static void account_storage_root(account *ac, hash256 *out)
{
    size_t n = 0;
    for (size_t i = 0; i < ac->storage.cap; i++)
        if (ac->storage.e[i].used && !u256_is_zero(ac->storage.e[i].val)) n++;
    if (n == 0) { *out = EMPTY_TRIE_ROOT; return; }
    trie_kv *kv = (trie_kv *)calloc(n, sizeof(trie_kv));
    buf_t *vals = (buf_t *)calloc(n, sizeof(buf_t));
    size_t j = 0;
    for (size_t i = 0; i < ac->storage.cap; i++) {
        if (!ac->storage.e[i].used || u256_is_zero(ac->storage.e[i].val)) continue;
        keccak256(ac->storage.e[i].key.b, 32, kv[j].key);
        kv[j].klen = 32;
        buf_init(&vals[j]);
        rlp_put_u256(&vals[j], ac->storage.e[i].val);
        kv[j].val = vals[j].data;
        kv[j].vlen = vals[j].len;
        j++;
    }
    trie_root(kv, n, out);
    for (size_t i = 0; i < n; i++) buf_free(&vals[i]);
    free(vals);
    free(kv);
}

void state_root(state *s, hash256 *out, bool eip158_clear_empty)
{
    (void)eip158_clear_empty;
    size_t n = 0;
    for (size_t i = 0; i < s->accounts.cap; i++)
        if (s->accounts.e[i] && s->accounts.e[i]->exists) n++;
    if (n == 0) { *out = EMPTY_TRIE_ROOT; return; }
    trie_kv *kv = (trie_kv *)calloc(n, sizeof(trie_kv));
    buf_t *vals = (buf_t *)calloc(n, sizeof(buf_t));
    size_t j = 0;
    for (size_t i = 0; i < s->accounts.cap; i++) {
        account *ac = s->accounts.e[i];
        if (!ac || !ac->exists) continue;
        hash256 sr;
        account_storage_root(ac, &sr);
        buf_t payload; buf_init(&payload);
        rlp_put_u64(&payload, ac->nonce);
        rlp_put_u256(&payload, ac->balance);
        rlp_put_bytes(&payload, sr.b, 32);
        rlp_put_bytes(&payload, ac->code_hash.b, 32);
        buf_init(&vals[j]);
        rlp_put_list(&vals[j], payload.data, payload.len);
        buf_free(&payload);
        keccak256(ac->addr.b, 20, kv[j].key);
        kv[j].klen = 32;
        kv[j].val = vals[j].data;
        kv[j].vlen = vals[j].len;
        j++;
    }
    trie_root(kv, n, out);
    for (size_t i = 0; i < n; i++) buf_free(&vals[i]);
    free(vals);
    free(kv);
}
