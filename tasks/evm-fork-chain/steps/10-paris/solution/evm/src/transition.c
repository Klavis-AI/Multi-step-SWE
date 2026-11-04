#include "transition.h"
#include "rlp.h"
#include "keccak.h"
#include "crypto.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --------------------------------------------------------------- bloom */
void bloom_add(uint8_t bloom[256], const uint8_t *data, size_t len)
{
    hash256 h;
    keccak256(data, len, h.b);
    for (int i = 0; i < 6; i += 2) {
        unsigned bit = (((unsigned)h.b[i] << 8) | h.b[i + 1]) & 0x7ff;
        bloom[256 - 1 - bit / 8] |= (uint8_t)(1u << (bit % 8));
    }
}

static void log_bloom(uint8_t bloom[256], const evm_log *l)
{
    bloom_add(bloom, l->addr.b, 20);
    for (int i = 0; i < l->ntopics; i++) bloom_add(bloom, l->topics[i].b, 32);
}

static void encode_logs(const evm_log *logs, size_t n, buf_t *out)
{
    buf_t list; buf_init(&list);
    for (size_t i = 0; i < n; i++) {
        buf_t e, tl;
        buf_init(&e); buf_init(&tl);
        rlp_put_bytes(&e, logs[i].addr.b, 20);
        for (int j = 0; j < logs[i].ntopics; j++) rlp_put_bytes(&tl, logs[i].topics[j].b, 32);
        rlp_put_list(&e, tl.data, tl.len);
        rlp_put_bytes(&e, logs[i].data, logs[i].data_len);
        rlp_put_list(&list, e.data, e.len);
        buf_free(&e); buf_free(&tl);
    }
    rlp_put_list(out, list.data, list.len);
    buf_free(&list);
}

static void encode_receipt(const receipt_t *r, buf_t *out)
{
    buf_t pl, logs;
    buf_init(&pl); buf_init(&logs);
    if (r->has_status) {
        uint8_t s = r->status;
        if (s) rlp_put_u64(&pl, 1); else rlp_put_bytes(&pl, NULL, 0);
    } else {
        rlp_put_bytes(&pl, r->post_root.b, 32);
    }
    rlp_put_u64(&pl, r->cumulative_gas);
    rlp_put_bytes(&pl, r->bloom, 256);
    encode_logs(r->logs, r->nlogs, &logs);
    buf_put(&pl, logs.data, logs.len);
    if (r->type) buf_putc(out, r->type);
    rlp_put_list(out, pl.data, pl.len);
    buf_free(&pl); buf_free(&logs);
}

/* ------------------------------------------------------------ difficulty */
static u256 calc_difficulty(fork_id fk, const env_t *e)
{
    if (IS_PARIS(fk)) return U256_ZERO;
    u256 parent = e->parent_difficulty;
    uint64_t num = e->number;
    uint64_t dt = e->timestamp - e->parent_timestamp;
    u256 adjust = u256_div(parent, u256_from_u64(2048));
    int64_t factor;
    if (IS_BYZANTIUM(fk)) {
        hash256 empty_uncles = {{0x1d,0xcc,0x4d,0xe8,0xde,0xc7,0x5d,0x7a,0xab,0x85,0xb5,0x67,0xb6,0xcc,0xd4,0x1a,
                                 0xd3,0x12,0x45,0x1b,0x94,0x8a,0x74,0x13,0xf0,0xa1,0x42,0xfd,0x40,0xd4,0x93,0x47}};
        int64_t y = (e->has_parent_uncle_hash && !hash_eq(e->parent_uncle_hash, empty_uncles)) ? 2 : 1;
        factor = y - (int64_t)(dt / 9);
        if (factor < -99) factor = -99;
    } else if (IS_HOMESTEAD(fk)) {
        factor = 1 - (int64_t)(dt / 10);
        if (factor < -99) factor = -99;
    } else {
        factor = (dt < 13) ? 1 : -1;
    }
    u256 diff;
    if (factor >= 0) diff = u256_add(parent, u256_mul(adjust, u256_from_u64((uint64_t)factor)));
    else diff = u256_sub(parent, u256_mul(adjust, u256_from_u64((uint64_t)(-factor))));
    u256 min = u256_from_u64(131072);
    if (u256_cmp(diff, min) < 0) diff = min;

    uint64_t delay = 0;
    if (IS_GRAY(fk)) delay = 11400000;
    else if (IS_ARROW(fk)) delay = 10700000;
    else if (IS_LONDON(fk)) delay = 9700000;
    else if (IS_MUIR(fk)) delay = 9000000;
    else if (IS_CONSTANTINOPLE(fk)) delay = 5000000;
    else if (IS_BYZANTIUM(fk)) delay = 3000000;
    uint64_t fake = (num > delay) ? (num - delay) : 0;
    uint64_t period = fake / 100000;
    if (period > 1) {
        u256 bomb = u256_exp(u256_from_u64(2), u256_from_u64(period - 2));
        diff = u256_add(diff, bomb);
    }
    return diff;
}

/* --------------------------------------------------------------- 1559 */
static u256 calc_base_fee(fork_id fk, const env_t *e)
{
    if (!IS_LONDON(fk)) return U256_ZERO;
    if (!e->has_parent_base_fee) return u256_from_u64(1000000000);
    u256 parent_base = e->parent_base_fee;
    uint64_t target = e->parent_gas_limit / 2;
    if (e->parent_gas_used == target) return parent_base;
    if (e->parent_gas_used > target) {
        uint64_t delta = e->parent_gas_used - target;
        u256 num = u256_mul(parent_base, u256_from_u64(delta));
        u256 den = u256_mul(u256_from_u64(target), u256_from_u64(8));
        u256 inc = u256_div(num, den);
        if (u256_is_zero(inc)) inc = U256_ONE;
        return u256_add(parent_base, inc);
    }
    uint64_t delta = target - e->parent_gas_used;
    u256 num = u256_mul(parent_base, u256_from_u64(delta));
    u256 den = u256_mul(u256_from_u64(target), u256_from_u64(8));
    u256 dec = u256_div(num, den);
    if (u256_cmp(parent_base, dec) < 0) return U256_ZERO;
    return u256_sub(parent_base, dec);
}

/* ---------------------------------------------------------------- blobs */
#define GAS_PER_BLOB 131072ULL
static uint64_t blob_target(fork_id fk) { return IS_PRAGUE(fk) ? 6 : 3; }
static uint64_t blob_max(fork_id fk) { return IS_OSAKA(fk) ? 9 : (IS_PRAGUE(fk) ? 9 : 6); }
static uint64_t blob_update_fraction(fork_id fk) { return IS_PRAGUE(fk) ? 5007716ULL : 3338477ULL; }

static u256 fake_exponential(uint64_t factor, uint64_t numerator, uint64_t denominator)
{
    u256 i = U256_ONE;
    u256 output = U256_ZERO;
    u256 numer_acc = u256_mul(u256_from_u64(factor), u256_from_u64(denominator));
    u256 den = u256_from_u64(denominator);
    u256 num = u256_from_u64(numerator);
    while (!u256_is_zero(numer_acc)) {
        output = u256_add(output, numer_acc);
        numer_acc = u256_div(u256_mul(numer_acc, num), u256_mul(den, i));
        i = u256_add(i, U256_ONE);
    }
    return u256_div(output, den);
}

#define BLOB_BASE_COST 8192ULL

static uint64_t calc_excess_blob_gas(fork_id fk, const env_t *e)
{
    uint64_t parent_excess = e->has_peb ? e->parent_excess_blob_gas : 0;
    uint64_t parent_used = e->has_pbgu ? e->parent_blob_gas_used : 0;
    uint64_t target_gas = blob_target(fk) * GAS_PER_BLOB;
    uint64_t total = parent_excess + parent_used;
    if (total < target_gas) return 0;
    if (IS_OSAKA(fk)) {
        /* EIP-7918: once a blob is cheaper than the execution gas it takes to carry one,
         * the excess follows the reserve-price branch instead of the plain subtraction. */
        u256 pbf = e->has_parent_base_fee ? e->parent_base_fee : U256_ZERO;
        u256 lhs = u256_mul(u256_from_u64(BLOB_BASE_COST), pbf);
        u256 rhs = u256_mul(u256_from_u64(GAS_PER_BLOB),
                            fake_exponential(1, parent_excess, blob_update_fraction(fk)));
        if (u256_cmp(lhs, rhs) > 0) {
            uint64_t mx = blob_max(fk), tg = blob_target(fk);
            return parent_excess + parent_used * (mx - tg) / mx;
        }
    }
    return total - target_gas;
}

/* ------------------------------------------------------- system contracts */
static const address SYSTEM_ADDRESS = {{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                                        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe}};
static const address BEACON_ROOTS = {{0x00,0x0F,0x3d,0xf6,0xD7,0x32,0x80,0x7E,0xf1,0x31,
                                      0x9f,0xB7,0xB8,0xbB,0x85,0x22,0xd0,0xBe,0xac,0x02}};
static const address HISTORY_STORAGE = {{0x00,0x00,0xF9,0x08,0x27,0xF1,0xC5,0x3a,0x10,0xcb,
                                         0x7A,0x02,0x33,0x5B,0x17,0x53,0x20,0x00,0x29,0x35}};
static const address WITHDRAWAL_REQUEST = {{0x00,0x00,0x09,0x61,0xEf,0x48,0x0E,0xb5,0x5e,0x80,
                                            0xD1,0x9a,0xd8,0x35,0x79,0xA6,0x4c,0x00,0x70,0x02}};
static const address CONSOLIDATION_REQUEST = {{0x00,0x00,0xBB,0xdD,0xc7,0xCE,0x48,0x86,0x42,0xfb,
                                               0x57,0x9F,0x8B,0x00,0xf3,0xa5,0x90,0x00,0x72,0x51}};
static const address DEPOSIT_CONTRACT = {{0x00,0x00,0x00,0x00,0x21,0x9a,0xb5,0x40,0x35,0x6c,
                                          0xBB,0x83,0x9C,0xbe,0x05,0x30,0x3d,0x77,0x05,0xFa}};

static int system_call(vm_t *vm, address target, const uint8_t *input, size_t ilen,
                       uint8_t **out, size_t *olen)
{
    state *st = vm->st;
    size_t cl;
    state_code(st, target, &cl);
    if (out) { *out = NULL; *olen = 0; }
    if (cl == 0) return 0;
    call_args a;
    memset(&a, 0, sizeof a);
    a.kind = CK_TX;
    a.caller = SYSTEM_ADDRESS;
    a.self = target;
    a.code_addr = target;
    a.input = input;
    a.ilen = ilen;
    a.gas = 30000000;
    a.transfer_value = false;
    size_t snap = state_snapshot(st);
    ex_result r = evm_call(vm, &a);
    if (r.status != EX_OK) { state_revert(st, snap); free(r.ret); return -1; }
    if (out) { *out = r.ret; *olen = r.ret_len; } else free(r.ret);
    /* go-ethereum runs the call from the system address with a zero-value transfer, which
     * touches it, and the Finalise that follows removes it again only because it is empty.
     * A withdrawal that credited the system address earlier in the block therefore keeps it
     * alive, which is what the multi-block beacon-root fixtures check. */
    state_add_balance(st, SYSTEM_ADDRESS, U256_ZERO);
    state_delete_touched_empty(st, IS_EIP158(vm->fork));
    return 0;
}

/* -------------------------------------------------------------- helpers */
/* true when a * b would need more than 256 bits */
static bool mul_over_256(u256 a, uint64_t b)
{
    if (u256_is_zero(a) || b == 0) return false;
    u256 max; memset(max.v, 0xff, 32);
    u256 q = u256_div(max, u256_from_u64(b));
    return u256_cmp(a, q) > 0;
}

static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = (char *)malloc(n); memcpy(p, s, n); return p; }

static void add_rejected(result_t *res, size_t idx, const char *err)
{
    res->rejected = (rejected_t *)realloc(res->rejected, (res->nrejected + 1) * sizeof(rejected_t));
    res->rejected[res->nrejected].index = idx;
    res->rejected[res->nrejected].error = dupstr(err);
    res->nrejected++;
}

/* --------------------------------------------------------- transaction */
typedef struct {
    uint64_t gas_used;
    bool ok;
    address created;
    bool has_created;
} tx_outcome;

static bool has_delegation(state *st, address a)
{
    size_t cl;
    const uint8_t *c = state_code(st, a, &cl);
    return c && cl == 23 && c[0] == 0xef && c[1] == 0x01 && c[2] == 0x00;
}

static void warm_precompiles(vm_t *vm)
{
    if (!IS_BERLIN(vm->fork)) return;
    for (uint64_t n = 1; n <= 0x11; n++) {
        address a = addr_zero();
        a.b[19] = (uint8_t)n;
        if (is_precompile(vm->fork, a)) state_warm_addr(vm->st, a);
    }
    if (IS_OSAKA(vm->fork)) {
        address a = addr_zero();
        a.b[18] = 0x01;
        state_warm_addr(vm->st, a);
    }
}

static const char *apply_tx(state *st, env_t *env, tx_t *t, fork_id fk, u256 chain_id,
                            uint64_t gas_used_so_far, uint64_t blob_gas_so_far,
                            u256 base_fee, u256 blob_base_fee, tx_outcome *out)
{
    memset(out, 0, sizeof *out);
    if (!t->have_sender) return "could not recover sender";
    address sender = t->sender;

    /* --- static checks ------------------------------------------------ */
    if (t->over256) return "value exceeds 256 bits";
    if (t->type > 0 && !IS_BERLIN(fk)) return "transaction type not supported";
    if (t->type >= 2 && !IS_LONDON(fk)) return "transaction type not supported";
    if (t->type >= 3 && !IS_CANCUN(fk)) return "transaction type not supported";
    if (t->type >= 4 && !IS_PRAGUE(fk)) return "transaction type not supported";
    if (t->type >= 2 && u256_cmp(t->max_prio, t->max_fee) > 0)
        return "max priority fee per gas higher than max fee per gas";
    if (t->type >= 2 && u256_cmp(t->max_fee, base_fee) < 0)
        return "max fee per gas less than block base fee";
    if (IS_OSAKA(fk) && t->gas > (1ULL << 24)) return "transaction gas limit too high";   /* EIP-7825 */
    if (t->type == 3) {
        if (t->nblobs == 0) return "blob transaction missing blob hashes";
        for (size_t i = 0; i < t->nblobs; i++)
            if (t->blob_hashes[i].b[0] != 0x01) return "has invalid hash version";
        if (u256_cmp(t->max_fee_blob, blob_base_fee) < 0)
            return "max fee per blob gas less than block blob gas fee";
        if (IS_OSAKA(fk) && t->nblobs > 6)            /* EIP-7594 per-transaction blob cap */
            return "blob transaction has too many blobs";
        uint64_t maxblobs = blob_max(fk);
        if (blob_gas_so_far / GAS_PER_BLOB + t->nblobs > maxblobs)
            return "blob gas used exceeds maximum allowance";
    }
    if (IS_SHANGHAI(fk) && !t->has_to && t->data_len > MAX_INITCODE)
        return "max initcode size exceeded";

    if (gas_used_so_far + t->gas > env->gas_limit) return "gas limit reached";

    uint64_t floor_gas = 0;
    uint64_t intrinsic = tx_intrinsic_gas(t, fk, &floor_gas);
    if (t->gas < intrinsic) return "intrinsic gas too low";
    if (IS_PRAGUE(fk) && t->gas < floor_gas) return "insufficient gas for floor data gas cost";

    /* --- stateful checks ---------------------------------------------- */
    uint64_t nonce = state_nonce(st, sender);
    if (nonce < t->nonce) return "nonce too high";
    if (nonce > t->nonce) return "nonce too low";
    if (nonce == UINT64_MAX) return "nonce has max value";
    /* EIP-3607: a sender that carries code is not an externally owned account.  The
     * reference tool applies this at every fork name rather than gating it on the fork the
     * EIP shipped in, so the `stEIP3607` fixtures reject at Berlin as much as at Shanghai. */
    if (!IS_PRAGUE(fk) || !has_delegation(st, sender)) {
        size_t cl;
        state_code(st, sender, &cl);
        if (cl != 0) return "sender not an eoa";
    }

    u256 eff_price = t->gas_price;
    if (t->type >= 2) {
        u256 cand = u256_add(base_fee, t->max_prio);
        eff_price = (u256_cmp(cand, t->max_fee) < 0) ? cand : t->max_fee;
    } else if (IS_LONDON(fk)) {
        if (u256_cmp(t->gas_price, base_fee) < 0) return "max fee per gas less than block base fee";
    }

    /* The funds check is arbitrary-precision in the reference tool, so a product that needs
     * more than 256 bits is simply more than any balance -- computing it modulo 2^256 here
     * would wrap it down to something a balance can cover. */
    u256 mgval = u256_mul(u256_from_u64(t->gas), eff_price);
    u256 check_price = (t->type >= 2) ? t->max_fee : eff_price;
    if (mul_over_256(check_price, t->gas))
        return "insufficient funds for gas * price + value";
    u256 check = u256_mul(u256_from_u64(t->gas), check_price);
    u256 with_value = u256_add(check, t->value);
    if (u256_cmp(with_value, check) < 0)          /* the sum itself wrapped */
        return "insufficient funds for gas * price + value";
    check = with_value;
    if (t->type == 3) {
        if (mul_over_256(t->max_fee_blob, t->nblobs * GAS_PER_BLOB))
            return "insufficient funds for gas * price + value";
        u256 bl = u256_mul(u256_from_u64(t->nblobs * GAS_PER_BLOB), t->max_fee_blob);
        u256 sum = u256_add(check, bl);
        if (u256_cmp(sum, check) < 0)
            return "insufficient funds for gas * price + value";
        check = sum;
    }
    if (u256_cmp(state_balance(st, sender), check) < 0)
        return "insufficient funds for gas * price + value";

    /* --- execute ------------------------------------------------------ */
    state_sub_balance(st, sender, mgval);
    if (t->type == 3) {
        u256 bfee = u256_mul(u256_from_u64(t->nblobs * GAS_PER_BLOB), blob_base_fee);
        state_sub_balance(st, sender, bfee);
    }
    /* For a creation transaction the nonce is bumped inside evm_create, which
     * needs the pre-increment value to derive the contract address. */
    if (t->has_to) state_set_nonce(st, sender, nonce + 1);

    block_env blk;
    memset(&blk, 0, sizeof blk);
    blk.fork = fk;
    blk.chain_id = chain_id;
    blk.coinbase = env->coinbase;
    blk.number = env->number;
    blk.timestamp = env->timestamp;
    blk.gas_limit = env->gas_limit;
    blk.difficulty = env->difficulty;
    blk.prevrandao = env->random;
    blk.post_merge = IS_PARIS(fk);
    blk.base_fee = base_fee;
    blk.blob_base_fee = blob_base_fee;
    blk.bhashes = env->bhashes;
    blk.nbhashes = env->nbhashes;

    tx_env txe;
    memset(&txe, 0, sizeof txe);
    txe.origin = sender;
    txe.gas_price = eff_price;
    txe.blob_hashes = t->blob_hashes;
    txe.nblobs = t->nblobs;

    vm_t vm;
    memset(&vm, 0, sizeof vm);
    vm.st = st;
    vm.blk = &blk;
    vm.tx = &txe;
    vm.fork = fk;
    vm.depth = 0;

    if (IS_BERLIN(fk)) {
        state_warm_addr(st, sender);
        if (t->has_to) state_warm_addr(st, t->to);
        warm_precompiles(&vm);
        if (IS_SHANGHAI(fk)) state_warm_addr(st, env->coinbase);
        for (size_t i = 0; i < t->nal; i++) {
            state_warm_addr(st, t->al[i].addr);
            for (size_t j = 0; j < t->al[i].nkeys; j++)
                state_warm_slot(st, t->al[i].addr, t->al[i].keys[j]);
        }
    }

    uint64_t gas = t->gas - intrinsic;

    /* EIP-7702 authorizations */
    if (IS_PRAGUE(fk)) {
        for (size_t i = 0; i < t->nauths; i++) {
            authorization *au = &t->auths[i];
            if (!u256_is_zero(au->chain_id) && !u256_eq(au->chain_id, chain_id)) continue;
            if (au->y != 0 && au->y != 1) continue;
            u256 n_order, half;
            {
                static const uint8_t N[32] = {
                    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe,
                    0xba,0xae,0xdc,0xe6,0xaf,0x48,0xa0,0x3b,0xbf,0xd2,0x5e,0x8c,0xd0,0x36,0x41,0x41};
                u256_from_be(&n_order, N, 32);
                half = u256_div(n_order, u256_from_u64(2));
            }
            if (u256_cmp(au->s, half) > 0) continue;
            buf_t pl, msg;
            buf_init(&pl); buf_init(&msg);
            rlp_put_u256(&pl, au->chain_id);
            rlp_put_bytes(&pl, au->addr.b, 20);
            rlp_put_u64(&pl, au->nonce);
            buf_putc(&msg, 0x05);
            rlp_put_list(&msg, pl.data, pl.len);
            hash256 mh;
            keccak256(msg.data, msg.len, mh.b);
            buf_free(&pl); buf_free(&msg);
            address authority;
            if (!secp_recover(mh.b, &au->r, &au->s, au->y, true, &authority)) continue;
            state_warm_addr(st, authority);
            size_t acl;
            const uint8_t *ac = state_code(st, authority, &acl);
            if (acl != 0 && !(acl == 23 && ac[0] == 0xef && ac[1] == 0x01 && ac[2] == 0x00)) continue;
            uint64_t anonce = state_nonce(st, authority);
            if (anonce != au->nonce) continue;
            if (state_exists(st, authority)) state_add_refund(st, 25000 - 12500);
            state_set_nonce(st, authority, anonce + 1);
            if (addr_eq(au->addr, addr_zero())) {
                state_set_code(st, authority, NULL, 0);
            } else {
                uint8_t desig[23] = {0xef, 0x01, 0x00};
                memcpy(desig + 3, au->addr.b, 20);
                state_set_code(st, authority, desig, 23);
            }
        }
    }

    /* EIP-7702: once the authorizations have been applied, the delegation target of the
     * transaction's destination is warmed, so a call that resolves it pays the warm cost. */
    if (IS_PRAGUE(fk) && t->has_to) {
        size_t dcl;
        const uint8_t *dc = state_code(st, t->to, &dcl);
        if (dc && dcl == 23 && dc[0] == 0xef && dc[1] == 0x01 && dc[2] == 0x00) {
            address del;
            memcpy(del.b, dc + 3, 20);
            state_warm_addr(st, del);
        }
    }

    ex_result r;
    if (t->has_to) {
        call_args a;
        memset(&a, 0, sizeof a);
        a.kind = CK_TX;
        a.caller = sender;
        a.self = t->to;
        a.code_addr = t->to;
        a.value = t->value;
        a.transfer_value = true;
        a.input = t->data;
        a.ilen = t->data_len;
        a.gas = gas;
        state_touch(st, sender);
        r = evm_call(&vm, &a);
    } else {
        /* The receipt of a creation transaction carries the derived address whether
         * or not the creation succeeded. */
        out->created = create_address(sender, nonce);
        out->has_created = true;
        r = evm_create(&vm, sender, t->value, t->data, t->data_len, gas, false, U256_ZERO);
    }

    uint64_t gas_left = r.gas_left;
    uint64_t gas_spent = t->gas - gas_left;
    int64_t refund = st->refund;
    if (refund < 0) refund = 0;
    uint64_t cap = IS_LONDON(fk) ? gas_spent / 5 : gas_spent / 2;
    uint64_t ref = ((uint64_t)refund < cap) ? (uint64_t)refund : cap;
    gas_left += ref;
    uint64_t gas_final = t->gas - gas_left;
    if (IS_PRAGUE(fk) && gas_final < floor_gas) {
        gas_final = floor_gas;
        gas_left = t->gas - gas_final;
    }

    state_add_balance(st, sender, u256_mul(u256_from_u64(gas_left), eff_price));

    u256 coinbase_price = eff_price;
    if (IS_LONDON(fk)) coinbase_price = u256_sub(eff_price, base_fee);
    state_add_balance(st, env->coinbase, u256_mul(u256_from_u64(gas_final), coinbase_price));
    if (!IS_LONDON(fk)) state_touch(st, env->coinbase);

    state_finalise_destructed(st);
    state_delete_touched_empty(st, IS_EIP158(fk));

    out->gas_used = gas_final;
    out->ok = (r.status == EX_OK && !r.deposit_oog);
    free(r.ret);
    if (env->bhashes && blk.bhash_missing) return "__BLOCKHASH_MISSING__";
    return NULL;
}

/* ------------------------------------------------------------- requests */
static void collect_deposits(const receipt_t *rs, size_t n, buf_t *req)
{
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < rs[i].nlogs; j++) {
            const evm_log *l = &rs[i].logs[j];
            if (!addr_eq(l->addr, DEPOSIT_CONTRACT)) continue;
            if (l->ntopics < 1) continue;
            static const uint8_t TOPIC[32] = {
                0x64,0x9b,0xbc,0x62,0xd0,0xe3,0x13,0x42,0xaf,0xea,0x4e,0x5c,0xd8,0x2d,0x40,0x49,
                0xe7,0xe1,0xee,0x91,0x2f,0xc0,0x88,0x9a,0xa7,0x90,0x80,0x3b,0xe3,0x90,0x38,0xc5};
            if (memcmp(l->topics[0].b, TOPIC, 32) != 0) continue;
            /* The deposit event ABI-encodes five dynamic byte strings, which for these fixed
             * sizes always lands the payloads at the same offsets.  EIP-6110's request is the
             * five payloads concatenated, in order, and nothing else. */
            if (l->data_len != 576) continue;
            static const size_t base[5] = { 192, 288, 352, 416, 544 };
            static const size_t len[5]  = {  48,  32,   8,  96,   8 };
            for (int k = 0; k < 5; k++) buf_put(req, l->data + base[k], len[k]);
        }
    }
}

static void requests_hash(buf_t *reqs, size_t n, hash256 *out)
{
    buf_t acc; buf_init(&acc);
    for (size_t i = 0; i < n; i++) {
        if (reqs[i].len <= 1) continue;                      /* empty request: skipped */
        uint8_t d[32];
        sha256_hash(reqs[i].data, reqs[i].len, d);
        buf_put(&acc, d, 32);
    }
    sha256_hash(acc.data, acc.len, out->b);
    buf_free(&acc);
}

int apply_block(state *st, env_t *env, tx_t *txs, size_t ntxs, fork_id fk,
                u256 chain_id, int64_t reward, result_t *res)
{
    memset(res, 0, sizeof *res);
    buf_init(&res->body);

    if (env->has_difficulty) { res->difficulty = env->difficulty; }
    else if (!IS_PARIS(fk) && env->has_parent_difficulty) {
        res->difficulty = calc_difficulty(fk, env);
        env->difficulty = res->difficulty;
    } else {
        res->difficulty = env->difficulty;
    }
    res->has_difficulty = !IS_PARIS(fk);
    if (IS_PARIS(fk)) { env->difficulty = U256_ZERO; res->difficulty = U256_ZERO; }

    u256 base_fee = U256_ZERO;
    if (IS_LONDON(fk)) {
        base_fee = env->has_base_fee ? env->base_fee : calc_base_fee(fk, env);
        env->base_fee = base_fee;
        res->base_fee = base_fee;
        res->has_base_fee = true;
    }

    uint64_t excess = 0;
    u256 blob_base_fee = U256_ZERO;
    if (IS_CANCUN(fk)) {
        excess = env->has_ebg ? env->excess_blob_gas : calc_excess_blob_gas(fk, env);
        blob_base_fee = fake_exponential(1, excess, blob_update_fraction(fk));
        res->excess_blob_gas = excess;
        res->has_excess_blob_gas = true;
    }

    /* pre-block system calls */
    {
        block_env blk;
        memset(&blk, 0, sizeof blk);
        blk.fork = fk; blk.chain_id = chain_id; blk.coinbase = env->coinbase;
        blk.number = env->number; blk.timestamp = env->timestamp; blk.gas_limit = env->gas_limit;
        blk.difficulty = env->difficulty; blk.prevrandao = env->random;
        blk.post_merge = IS_PARIS(fk); blk.base_fee = base_fee; blk.blob_base_fee = blob_base_fee;
        blk.bhashes = env->bhashes; blk.nbhashes = env->nbhashes;
        tx_env txe; memset(&txe, 0, sizeof txe);
        vm_t vm; memset(&vm, 0, sizeof vm);
        vm.st = st; vm.blk = &blk; vm.tx = &txe; vm.fork = fk;
        if (IS_CANCUN(fk) && env->has_pbbr) {
            system_call(&vm, BEACON_ROOTS, env->parent_beacon_block_root.b, 32, NULL, NULL);
            state_commit_tx(st);
        }
        if (IS_PRAGUE(fk) && env->has_parent_hash) {
            system_call(&vm, HISTORY_STORAGE, env->parent_hash.b, 32, NULL, NULL);
            state_commit_tx(st);
        }
    }

    uint64_t cumulative = 0, blob_gas_used = 0;
    buf_t *tx_encs = (buf_t *)calloc(ntxs ? ntxs : 1, sizeof(buf_t));
    buf_t *rcpt_encs = (buf_t *)calloc(ntxs ? ntxs : 1, sizeof(buf_t));
    size_t nincluded = 0;
    buf_t bodylist; buf_init(&bodylist);

    for (size_t i = 0; i < ntxs; i++) {
        tx_t *t = &txs[i];
        tx_outcome oc;
        size_t snap = state_snapshot(st);
        const char *err = apply_tx(st, env, t, fk, chain_id, cumulative, blob_gas_used,
                                   base_fee, blob_base_fee, &oc);
        if (err) {
            state_revert(st, snap);
            state_commit_tx(st);
            if (!strcmp(err, "__BLOCKHASH_MISSING__")) { free(tx_encs); free(rcpt_encs); return 4; }
            add_rejected(res, i, err);
            continue;
        }
        cumulative += oc.gas_used;
        if (t->type == 3) blob_gas_used += t->nblobs * GAS_PER_BLOB;

        receipt_t rc;
        memset(&rc, 0, sizeof rc);
        rc.type = t->type;
        rc.cumulative_gas = cumulative;
        rc.gas_used = oc.gas_used;
        rc.index = nincluded;
        rc.tx_hash = t->hash;
        rc.has_contract = oc.has_created;
        rc.contract_address = oc.has_created ? oc.created : addr_zero();
        rc.nlogs = st->nlogs;
        if (st->nlogs) {
            rc.logs = (evm_log *)calloc(st->nlogs, sizeof(evm_log));
            memcpy(rc.logs, st->logs, st->nlogs * sizeof(evm_log));
        }
        for (size_t j = 0; j < rc.nlogs; j++) log_bloom(rc.bloom, &rc.logs[j]);
        rc.status = oc.ok ? 1 : 0;
        if (IS_BYZANTIUM(fk)) rc.has_status = true;          /* EIP-658 status receipts */
        else { rc.has_status = false; state_root(st, &rc.post_root, IS_EIP158(fk)); }

        st->nlogs = 0;
        state_commit_tx(st);

        res->receipts = (receipt_t *)realloc(res->receipts, (res->nreceipts + 1) * sizeof(receipt_t));
        res->receipts[res->nreceipts++] = rc;

        buf_init(&tx_encs[nincluded]);
        tx_encode(t, &tx_encs[nincluded]);
        if (t->type) rlp_put_bytes(&bodylist, tx_encs[nincluded].data, tx_encs[nincluded].len);
        else buf_put(&bodylist, tx_encs[nincluded].data, tx_encs[nincluded].len);
        nincluded++;
    }

    /* withdrawals */
    if (IS_SHANGHAI(fk)) {
        buf_t *wl = (buf_t *)calloc(env->nwithdrawals ? env->nwithdrawals : 1, sizeof(buf_t));
        for (size_t i = 0; i < env->nwithdrawals; i++) {
            withdrawal_t *w = &env->withdrawals[i];
            u256 wei = u256_mul(w->amount, u256_from_u64(1000000000ULL));
            state_add_balance(st, w->recipient, wei);
            buf_t pl; buf_init(&pl);
            rlp_put_u64(&pl, w->index);
            rlp_put_u64(&pl, w->validator_index);
            rlp_put_bytes(&pl, w->recipient.b, 20);
            rlp_put_u256(&pl, w->amount);
            buf_init(&wl[i]);
            rlp_put_list(&wl[i], pl.data, pl.len);
            buf_free(&pl);
        }
        state_delete_touched_empty(st, true);
        state_commit_tx(st);
        trie_root_list(wl, env->nwithdrawals, &res->withdrawals_root);
        res->has_withdrawals_root = true;
        for (size_t i = 0; i < env->nwithdrawals; i++) buf_free(&wl[i]);
        free(wl);
    }

    /* Block and ommer rewards.  `--state.reward` is a parameter of the tool, not a
     * consensus rule it reasons about: it applies whatever it is given, at every fork name,
     * post-merge included. */
    if (reward > 0) {
        u256 br = u256_from_u64((uint64_t)reward);
        u256 miner = br;
        for (size_t i = 0; i < env->nommers; i++) {
            ommer_t *o = &env->ommers[i];
            u256 orew = u256_div(u256_mul(br, u256_from_u64(8 - o->delta)), u256_from_u64(8));
            state_add_balance(st, o->addr, orew);
            miner = u256_add(miner, u256_div(br, u256_from_u64(32)));
        }
        state_add_balance(st, env->coinbase, miner);
        state_delete_touched_empty(st, IS_EIP158(fk));
        state_commit_tx(st);
    } else if (reward == 0) {
        /* a reward of zero still touches the coinbase */
        state_touch(st, env->coinbase);
        state_delete_touched_empty(st, IS_EIP158(fk));
        state_commit_tx(st);
    }

    /* EIP-7685 requests */
    if (IS_PRAGUE(fk)) {
        buf_t reqs[3];
        for (int i = 0; i < 3; i++) buf_init(&reqs[i]);
        buf_putc(&reqs[0], 0x00);
        collect_deposits(res->receipts, res->nreceipts, &reqs[0]);
        block_env blk; memset(&blk, 0, sizeof blk);
        blk.fork = fk; blk.chain_id = chain_id; blk.coinbase = env->coinbase;
        blk.number = env->number; blk.timestamp = env->timestamp; blk.gas_limit = env->gas_limit;
        blk.post_merge = true; blk.base_fee = base_fee; blk.blob_base_fee = blob_base_fee;
        blk.prevrandao = env->random;
        tx_env txe; memset(&txe, 0, sizeof txe);
        vm_t vm; memset(&vm, 0, sizeof vm);
        vm.st = st; vm.blk = &blk; vm.tx = &txe; vm.fork = fk;
        uint8_t *o = NULL; size_t ol = 0;
        buf_putc(&reqs[1], 0x01);
        if (system_call(&vm, WITHDRAWAL_REQUEST, NULL, 0, &o, &ol) == 0 && ol) buf_put(&reqs[1], o, ol);
        free(o); o = NULL; ol = 0;
        state_commit_tx(st);
        buf_putc(&reqs[2], 0x02);
        if (system_call(&vm, CONSOLIDATION_REQUEST, NULL, 0, &o, &ol) == 0 && ol) buf_put(&reqs[2], o, ol);
        free(o);
        state_commit_tx(st);
        requests_hash(reqs, 3, &res->requests_hash);
        res->has_requests_hash = true;
        res->requests = (buf_t *)calloc(3, sizeof(buf_t));
        memcpy(res->requests, reqs, sizeof reqs);
        res->nrequests = 3;
    }

    /* roots */
    for (size_t i = 0; i < res->nreceipts; i++) {
        buf_init(&rcpt_encs[i]);
        encode_receipt(&res->receipts[i], &rcpt_encs[i]);
    }
    trie_root_list(tx_encs, nincluded, &res->tx_root);
    trie_root_list(rcpt_encs, res->nreceipts, &res->receipts_root);
    state_root(st, &res->state_root, IS_EIP158(fk));
    res->gas_used = cumulative;
    if (IS_CANCUN(fk)) { res->blob_gas_used = blob_gas_used; res->has_blob_gas_used = true; }

    /* logs hash and bloom over all receipts */
    {
        size_t total = 0;
        for (size_t i = 0; i < res->nreceipts; i++) total += res->nreceipts ? res->receipts[i].nlogs : 0;
        evm_log *all = (evm_log *)calloc(total ? total : 1, sizeof(evm_log));
        size_t k = 0;
        for (size_t i = 0; i < res->nreceipts; i++)
            for (size_t j = 0; j < res->receipts[i].nlogs; j++) all[k++] = res->receipts[i].logs[j];
        buf_t enc; buf_init(&enc);
        encode_logs(all, total, &enc);
        keccak256(enc.data, enc.len, res->logs_hash.b);
        buf_free(&enc);
        for (size_t i = 0; i < total; i++) log_bloom(res->logs_bloom, &all[i]);
        free(all);
    }
    rlp_put_list(&res->body, bodylist.data, bodylist.len);
    buf_free(&bodylist);
    for (size_t i = 0; i < nincluded; i++) buf_free(&tx_encs[i]);
    for (size_t i = 0; i < res->nreceipts; i++) buf_free(&rcpt_encs[i]);
    free(tx_encs);
    free(rcpt_encs);
    return 0;
}
