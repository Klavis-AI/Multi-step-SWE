#ifndef STATE_H
#define STATE_H
#include "types.h"
#include "buf.h"
#include "trie.h"
#include <stdbool.h>

/* ---- storage maps ---- */
typedef struct { hash256 key; u256 val; bool used; } slot_entry;
typedef struct { slot_entry *e; size_t cap, n; } slotmap;

bool slotmap_get(const slotmap *m, hash256 k, u256 *out);
void slotmap_set(slotmap *m, hash256 k, u256 v);
void slotmap_clear(slotmap *m);

/* ---- accounts ---- */
typedef struct account {
    address addr;
    u256 balance;
    uint64_t nonce;
    uint8_t *code;
    size_t code_len;
    hash256 code_hash;
    bool exists;
    bool destructed;
    bool touched;
    bool created;          /* created in the current transaction (EIP-6780) */
    slotmap storage;
    slotmap orig;          /* value at the start of the current transaction */
    slotmap transient;
    bool used;
} account;

typedef struct { account **e; size_t cap, n; } acctmap;

/* ---- logs ---- */
typedef struct {
    address addr;
    hash256 topics[4];
    int ntopics;
    uint8_t *data;
    size_t data_len;
} evm_log;

/* ---- journal ---- */
typedef enum {
    J_CREATE, J_BALANCE, J_NONCE, J_CODE, J_STORAGE, J_TRANSIENT,
    J_TOUCH, J_DESTRUCT, J_CREATED, J_ACCESS_ADDR, J_ACCESS_SLOT, J_REFUND, J_LOG
} jkind;

typedef struct {
    jkind k;
    address addr;
    hash256 slot;
    u256 prev;
    uint64_t prevnonce;
    uint8_t *prevcode;
    size_t prevcodelen;
    hash256 prevcodehash;
    bool prevbool;
    int64_t refund_delta;
} jentry;

typedef struct {
    acctmap accounts;
    jentry *journal;
    size_t jn, jcap;
    evm_log *logs;
    size_t nlogs, logcap;
    int64_t refund;
    /* EIP-2929 access sets */
    acctmap warm_addr;     /* reuse map keyed by address; account* used as a tag */
    slot_entry *warm_slots;
    size_t nwarm, warmcap;
    address *warm_slot_addr;
} state;

void state_init(state *s);
account *state_get(state *s, address a);            /* NULL if absent */
account *state_getor(state *s, address a);          /* creates a non-existing shell */
bool state_exists(const state *s, address a);
bool state_empty(state *s, address a);              /* EIP-161 empty: nonce 0, balance 0, no code */

u256 state_balance(state *s, address a);
uint64_t state_nonce(state *s, address a);
const uint8_t *state_code(state *s, address a, size_t *len);
hash256 state_codehash(state *s, address a);
u256 state_storage(state *s, address a, hash256 k);
u256 state_orig_storage(state *s, address a, hash256 k);
u256 state_tstorage(state *s, address a, hash256 k);

void state_add_balance(state *s, address a, u256 v);
void state_sub_balance(state *s, address a, u256 v);
void state_set_balance(state *s, address a, u256 v);
void state_set_nonce(state *s, address a, uint64_t n);
void state_set_code(state *s, address a, const uint8_t *c, size_t n);
void state_set_storage(state *s, address a, hash256 k, u256 v);
void state_set_tstorage(state *s, address a, hash256 k, u256 v);
void state_touch(state *s, address a);
void state_create_account(state *s, address a);
void state_destruct(state *s, address a);
bool state_is_destructed(state *s, address a);
void state_mark_created(state *s, address a);
bool state_was_created(state *s, address a);

void state_add_refund(state *s, int64_t v);
void state_add_log(state *s, const evm_log *l);

/* EIP-2929 */
bool state_addr_warm(state *s, address a);
void state_warm_addr(state *s, address a);
bool state_slot_warm(state *s, address a, hash256 k);
void state_warm_slot(state *s, address a, hash256 k);
void state_clear_access(state *s);

size_t state_snapshot(state *s);
void state_revert(state *s, size_t snap);
void state_commit_tx(state *s);        /* clear per-tx caches: orig, transient, created, destructed flags */

void state_root(state *s, hash256 *out, bool eip158_clear_empty);
void state_delete_touched_empty(state *s, bool active);
void state_finalise_destructed(state *s);
#endif
