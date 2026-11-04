#include "evm.h"
#include "keccak.h"
#include "rlp.h"
#include <stdlib.h>
#include <string.h>

typedef unsigned __int128 u128;

/* ------------------------------------------------------------------ gas cost */
enum {
    G_ZERO = 0, G_JUMPDEST = 1, G_BASE = 2, G_VERYLOW = 3, G_LOW = 5, G_MID = 8,
    G_HIGH = 10, G_SHA3 = 30, G_SHA3WORD = 6, G_COPY = 3, G_LOG = 375,
    G_LOGDATA = 8, G_LOGTOPIC = 375, G_CREATE = 32000, G_CODEDEPOSIT = 200,
    G_CALLVALUE = 9000, G_CALLSTIPEND = 2300, G_NEWACCOUNT = 25000,
    G_MEMORY = 3, G_TXCREATE = 32000, G_TXDATAZERO = 4,
    G_SELFDESTRUCT_REFUND = 24000, G_KECCAK256WORD = 6,
    COLD_ACCOUNT = 2600, COLD_SLOAD = 2100, WARM_ACCESS = 100,
    ACCESS_LIST_ADDR = 2400, ACCESS_LIST_SLOT = 1900,
    INITCODE_WORD = 2
};

static uint64_t gas_balance(fork_id f)
{
    if (IS_BERLIN(f)) return 0;             /* dynamic warm/cold */
    if (IS_ISTANBUL(f)) return 700;
    if (IS_EIP150(f)) return 400;
    return 20;
}
static uint64_t gas_extcodesize(fork_id f)
{
    if (IS_BERLIN(f)) return 0;
    if (IS_EIP150(f)) return 700;
    return 20;
}
static uint64_t gas_extcodehash(fork_id f)
{
    if (IS_BERLIN(f)) return 0;
    if (IS_ISTANBUL(f)) return 700;
    return 400;
}
static uint64_t gas_sload(fork_id f)
{
    if (IS_BERLIN(f)) return 0;
    if (IS_ISTANBUL(f)) return 800;
    if (IS_EIP150(f)) return 200;
    return 50;
}
static uint64_t gas_call_base(fork_id f)
{
    if (IS_BERLIN(f)) return 0;
    if (IS_EIP150(f)) return 700;
    return 40;
}
static uint64_t gas_selfdestruct(fork_id f) { return IS_EIP150(f) ? 5000 : 0; }
static uint64_t gas_exp_byte(fork_id f) { return IS_EIP158(f) ? 50 : 10; }

uint64_t memory_gas(uint64_t words)
{
    return G_MEMORY * words + words * words / 512;
}

/* ------------------------------------------------------------------ frame */
typedef struct {
    u256 *stack;               /* heap allocated: a 1024-deep call chain would blow the C stack */
    int sp;
    uint8_t *mem;
    size_t mem_cap;
    uint64_t msize;            /* addressable size in bytes (multiple of 32) */
    uint64_t mem_words;
    uint64_t gas;
    const uint8_t *code;
    size_t code_len;
    uint8_t *jumpdests;        /* bitmap */
    const uint8_t *input;
    size_t ilen;
    address self;
    address caller;
    address code_addr;
    u256 value;
    bool is_static;
    uint8_t *ret;              /* last call return data */
    size_t ret_len;
} frame;

static void frame_free(frame *f)
{
    free(f->stack);
    free(f->mem);
    free(f->jumpdests);
    free(f->ret);
}

static bool use_gas(frame *f, uint64_t g)
{
    if (f->gas < g) { f->gas = 0; return false; }
    f->gas -= g;
    return true;
}

static void analyse_jumpdests(frame *f)
{
    size_t n = (f->code_len + 7) / 8 + 1;
    f->jumpdests = (uint8_t *)calloc(n, 1);
    for (size_t i = 0; i < f->code_len; ) {
        uint8_t op = f->code[i];
        if (op == 0x5b) f->jumpdests[i / 8] |= (uint8_t)(1u << (i % 8));
        if (op >= 0x60 && op <= 0x7f) i += (size_t)(op - 0x5f) + 1;
        else i++;
    }
}

static bool is_jumpdest(frame *f, uint64_t pc)
{
    if (pc >= f->code_len) return false;
    return (f->jumpdests[pc / 8] >> (pc % 8)) & 1;
}

/* memory expansion; returns false on out-of-gas / unreasonable size */
static bool mem_expand(frame *f, uint64_t off, uint64_t len)
{
    if (len == 0) return true;
    if (off > UINT64_MAX - len) return false;
    uint64_t need = off + len;
    if (need <= f->msize) return true;
    uint64_t words = (need + 31) / 32;
    if (words > (1ULL << 32)) return false;
    uint64_t newcost = memory_gas(words);
    uint64_t oldcost = memory_gas(f->mem_words);
    if (newcost < oldcost) return false;
    if (!use_gas(f, newcost - oldcost)) return false;
    uint64_t newsize = words * 32;
    if (newsize > f->mem_cap) {
        size_t cap = f->mem_cap ? f->mem_cap : 1024;
        while (cap < newsize) cap *= 2;
        uint8_t *nm = (uint8_t *)realloc(f->mem, cap);
        if (!nm) return false;              /* out of host memory: exceptional halt */
        f->mem = nm;
        memset(f->mem + f->mem_cap, 0, cap - f->mem_cap);
        f->mem_cap = cap;
    }
    memset(f->mem + f->msize, 0, (size_t)(newsize - f->msize));
    f->msize = newsize;
    f->mem_words = words;
    return true;
}

static bool mem_expand_u256(frame *f, u256 off, u256 len)
{
    if (u256_is_zero(len)) return true;
    if (!u256_fits_u64(off) || !u256_fits_u64(len)) return false;
    return mem_expand(f, off.v[0], len.v[0]);
}

static void mem_write(frame *f, uint64_t off, const uint8_t *src, size_t n)
{
    if (!n) return;
    memcpy(f->mem + off, src, n);
}

static void mem_copy_padded(frame *f, uint64_t dst, const uint8_t *src, size_t srclen,
                            uint64_t srcoff, uint64_t len)
{
    for (uint64_t i = 0; i < len; i++) {
        uint64_t si = srcoff + i;
        uint8_t b = (srcoff > UINT64_MAX - i || si >= srclen) ? 0 : src[si];
        f->mem[dst + i] = b;
    }
}

/* ---------------------------------------------------------------- addresses */
address create_address(address sender, uint64_t nonce)
{
    buf_t payload, node;
    buf_init(&payload); buf_init(&node);
    rlp_put_bytes(&payload, sender.b, 20);
    rlp_put_u64(&payload, nonce);
    rlp_put_list(&node, payload.data, payload.len);
    hash256 h;
    keccak256(node.data, node.len, h.b);
    buf_free(&payload); buf_free(&node);
    address a;
    memcpy(a.b, h.b + 12, 20);
    return a;
}

address create2_address(address sender, u256 salt, const uint8_t *init, size_t ilen)
{
    uint8_t pre[1 + 20 + 32 + 32];
    hash256 ih;
    keccak256(init, ilen, ih.b);
    pre[0] = 0xff;
    memcpy(pre + 1, sender.b, 20);
    u256_to_be(&salt, pre + 21);
    memcpy(pre + 53, ih.b, 32);
    hash256 h;
    keccak256(pre, sizeof pre, h.b);
    address a;
    memcpy(a.b, h.b + 12, 20);
    return a;
}

/* --------------------------------------------------------------- EIP-7702 */
static bool delegation_of(const uint8_t *code, size_t len, address *out)
{
    if (len != 23) return false;
    if (code[0] != 0xef || code[1] != 0x01 || code[2] != 0x00) return false;
    memcpy(out->b, code + 3, 20);
    return true;
}

/* ------------------------------------------------------------ access helpers */
static uint64_t access_addr_cost(vm_t *vm, address a)
{
    if (!IS_BERLIN(vm->fork)) return 0;
    if (state_addr_warm(vm->st, a)) return WARM_ACCESS;
    state_warm_addr(vm->st, a);
    return COLD_ACCOUNT;
}

static uint64_t access_slot_cost(vm_t *vm, address a, hash256 k)
{
    if (!IS_BERLIN(vm->fork)) return 0;
    if (state_slot_warm(vm->st, a, k)) return WARM_ACCESS;
    state_warm_slot(vm->st, a, k);
    return COLD_SLOAD;
}

/* ------------------------------------------------------------- SSTORE gas */
static uint64_t clearing_refund(fork_id f) { return IS_LONDON(f) ? 4800 : 15000; }

/* returns false on out-of-gas */
static bool sstore_gas(vm_t *vm, frame *f, hash256 key, u256 val)
{
    state *st = vm->st;
    address self = f->self;
    fork_id fk = vm->fork;
    uint64_t cost = 0;
    u256 current = state_storage(st, self, key);

    if (IS_ISTANBUL(fk)) {
        if (f->gas <= 2300) return false;                 /* EIP-2200 sentry */
    }
    if (IS_BERLIN(fk)) {
        if (!state_slot_warm(st, self, key)) {
            cost = COLD_SLOAD;
            state_warm_slot(st, self, key);
        }
        if (u256_eq(current, val)) return use_gas(f, cost + WARM_ACCESS);
        u256 orig = state_orig_storage(st, self, key);
        if (u256_eq(orig, current)) {
            if (u256_is_zero(orig)) return use_gas(f, cost + 20000);
            if (u256_is_zero(val)) state_add_refund(st, (int64_t)clearing_refund(fk));
            return use_gas(f, cost + (5000 - COLD_SLOAD));
        }
        if (!u256_is_zero(orig)) {
            if (u256_is_zero(current)) state_add_refund(st, -(int64_t)clearing_refund(fk));
            else if (u256_is_zero(val)) state_add_refund(st, (int64_t)clearing_refund(fk));
        }
        if (u256_eq(orig, val)) {
            if (u256_is_zero(orig)) state_add_refund(st, 20000 - WARM_ACCESS);
            else state_add_refund(st, (5000 - COLD_SLOAD) - WARM_ACCESS);
        }
        return use_gas(f, cost + WARM_ACCESS);
    }
    if (IS_ISTANBUL(fk)) {                                 /* EIP-2200 */
        if (u256_eq(current, val)) return use_gas(f, 800);
        u256 orig = state_orig_storage(st, self, key);
        if (u256_eq(orig, current)) {
            if (u256_is_zero(orig)) return use_gas(f, 20000);
            if (u256_is_zero(val)) state_add_refund(st, 15000);
            return use_gas(f, 5000);
        }
        if (!u256_is_zero(orig)) {
            if (u256_is_zero(current)) state_add_refund(st, -15000);
            else if (u256_is_zero(val)) state_add_refund(st, 15000);
        }
        if (u256_eq(orig, val)) {
            if (u256_is_zero(orig)) state_add_refund(st, 20000 - 800);
            else state_add_refund(st, 5000 - 800);
        }
        return use_gas(f, 800);
    }
    if (IS_EIP1283(fk)) {                                  /* Constantinople only */
        if (u256_eq(current, val)) return use_gas(f, 200);
        u256 orig = state_orig_storage(st, self, key);
        if (u256_eq(orig, current)) {
            if (u256_is_zero(orig)) return use_gas(f, 20000);
            if (u256_is_zero(val)) state_add_refund(st, 15000);
            return use_gas(f, 5000);
        }
        if (!u256_is_zero(orig)) {
            if (u256_is_zero(current)) state_add_refund(st, -15000);
            else if (u256_is_zero(val)) state_add_refund(st, 15000);
        }
        if (u256_eq(orig, val)) {
            if (u256_is_zero(orig)) state_add_refund(st, 19800);
            else state_add_refund(st, 4800);
        }
        return use_gas(f, 200);
    }
    /* legacy */
    if (u256_is_zero(current) && !u256_is_zero(val)) return use_gas(f, 20000);
    if (!u256_is_zero(current) && u256_is_zero(val)) {
        state_add_refund(st, 15000);
        return use_gas(f, 5000);
    }
    return use_gas(f, 5000);
}

/* ------------------------------------------------------------ interpreter */
#define PUSHS(x) do { if (f->sp >= STACK_LIMIT) goto halt; f->stack[f->sp++] = (x); } while (0)
#define NEEDS(n) do { if (f->sp < (n)) goto halt; } while (0)
#define POP()    (f->stack[--f->sp])
#define PEEK(i)  (f->stack[f->sp - 1 - (i)])
#define GAS(g)   do { if (!use_gas(f, (g))) goto halt; } while (0)

static hash256 block_hash_of(vm_t *vm, uint64_t num)
{
    for (size_t i = 0; i < vm->blk->nbhashes; i++)
        if (vm->blk->bhashes[i].num == num) return vm->blk->bhashes[i].h;
    vm->blk->bhash_missing = true;
    return hash_zero();
}

static ex_result run_frame(vm_t *vm, frame *f);

/* resolve EIP-7702 delegation for code execution */
static const uint8_t *resolve_code(vm_t *vm, address target, size_t *len, uint64_t *extra_gas,
                                   frame *f, bool charge)
{
    size_t cl;
    const uint8_t *code = state_code(vm->st, target, &cl);
    address del;
    if (IS_PRAGUE(vm->fork) && code && delegation_of(code, cl, &del)) {
        if (charge) {
            uint64_t c = access_addr_cost(vm, del);
            if (extra_gas) *extra_gas += c;
            (void)f;
        }
        code = state_code(vm->st, del, &cl);
    }
    *len = cl;
    return code;
}

static ex_result do_call(vm_t *vm, frame *f, int op);
static ex_result do_create(vm_t *vm, frame *f, bool create2);

static ex_result run_frame(vm_t *vm, frame *f)
{
    ex_result r;
    uint64_t pc = 0;
    memset(&r, 0, sizeof r);
    analyse_jumpdests(f);

    for (;;) {
        if (pc >= f->code_len) { r.status = EX_OK; r.gas_left = f->gas; goto done; }
        uint8_t op = f->code[pc];
        switch (op) {
        case 0x00: r.status = EX_OK; r.gas_left = f->gas; goto done;   /* STOP */
        case 0x01: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_add(a, b)); } pc++; break;
        case 0x02: NEEDS(2); GAS(G_LOW);     { u256 a = POP(), b = POP(); PUSHS(u256_mul(a, b)); } pc++; break;
        case 0x03: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_sub(a, b)); } pc++; break;
        case 0x04: NEEDS(2); GAS(G_LOW);     { u256 a = POP(), b = POP(); PUSHS(u256_div(a, b)); } pc++; break;
        case 0x05: NEEDS(2); GAS(G_LOW);     { u256 a = POP(), b = POP(); PUSHS(u256_sdiv(a, b)); } pc++; break;
        case 0x06: NEEDS(2); GAS(G_LOW);     { u256 a = POP(), b = POP(); PUSHS(u256_mod(a, b)); } pc++; break;
        case 0x07: NEEDS(2); GAS(G_LOW);     { u256 a = POP(), b = POP(); PUSHS(u256_smod(a, b)); } pc++; break;
        case 0x08: NEEDS(3); GAS(G_MID);     { u256 a = POP(), b = POP(), m = POP(); PUSHS(u256_addmod(a, b, m)); } pc++; break;
        case 0x09: NEEDS(3); GAS(G_MID);     { u256 a = POP(), b = POP(), m = POP(); PUSHS(u256_mulmod(a, b, m)); } pc++; break;
        case 0x0a: {                                                   /* EXP */
            NEEDS(2); GAS(G_HIGH);
            u256 base = POP(), e = POP();
            GAS(gas_exp_byte(vm->fork) * (uint64_t)u256_bytelen(e));
            PUSHS(u256_exp(base, e));
            pc++; break;
        }
        case 0x0b: NEEDS(2); GAS(G_LOW); { u256 k = POP(), v = POP(); PUSHS(u256_signextend(k, v)); } pc++; break;

        case 0x10: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_from_u64(u256_cmp(a, b) < 0)); } pc++; break;
        case 0x11: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_from_u64(u256_cmp(a, b) > 0)); } pc++; break;
        case 0x12: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_from_u64(u256_scmp(a, b) < 0)); } pc++; break;
        case 0x13: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_from_u64(u256_scmp(a, b) > 0)); } pc++; break;
        case 0x14: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_from_u64(u256_eq(a, b))); } pc++; break;
        case 0x15: NEEDS(1); GAS(G_VERYLOW); { u256 a = POP(); PUSHS(u256_from_u64(u256_is_zero(a))); } pc++; break;
        case 0x16: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_and(a, b)); } pc++; break;
        case 0x17: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_or(a, b)); } pc++; break;
        case 0x18: NEEDS(2); GAS(G_VERYLOW); { u256 a = POP(), b = POP(); PUSHS(u256_xor(a, b)); } pc++; break;
        case 0x19: NEEDS(1); GAS(G_VERYLOW); { u256 a = POP(); PUSHS(u256_not(a)); } pc++; break;
        case 0x1a: NEEDS(2); GAS(G_VERYLOW); { u256 i = POP(), v = POP(); PUSHS(u256_byte(i, v)); } pc++; break;
        case 0x1b: if (!IS_CONSTANTINOPLE(vm->fork)) goto halt; NEEDS(2); GAS(G_VERYLOW); { u256 s = POP(), v = POP(); PUSHS(u256_shl(s, v)); } pc++; break;
        case 0x1c: if (!IS_CONSTANTINOPLE(vm->fork)) goto halt; NEEDS(2); GAS(G_VERYLOW); { u256 s = POP(), v = POP(); PUSHS(u256_shr(s, v)); } pc++; break;
        case 0x1d: if (!IS_CONSTANTINOPLE(vm->fork)) goto halt; NEEDS(2); GAS(G_VERYLOW); { u256 s = POP(), v = POP(); PUSHS(u256_sar(s, v)); } pc++; break;
        case 0x1e: {                                                    /* CLZ, EIP-7939 */
            if (!IS_OSAKA(vm->fork)) goto halt;
            NEEDS(1); GAS(G_LOW);
            u256 v = POP();
            PUSHS(u256_from_u64((uint64_t)(256 - u256_bitlen(v))));
            pc++; break;
        }
        case 0x20: {                                                    /* KECCAK256 */
            NEEDS(2); GAS(G_SHA3);
            u256 off = POP(), len = POP();
            if (!mem_expand_u256(f, off, len)) goto halt;
            uint64_t l = u256_is_zero(len) ? 0 : len.v[0];
            GAS(G_KECCAK256WORD * ((l + 31) / 32));
            hash256 h;
            keccak256(l ? f->mem + off.v[0] : (const uint8_t *)"", l, h.b);
            PUSHS(u256_from_hash(h));
            pc++; break;
        }
        case 0x30: GAS(G_BASE); PUSHS(u256_from_addr(f->self)); pc++; break;
        case 0x31: {                                                    /* BALANCE */
            NEEDS(1);
            u256 a = POP();
            address ad = addr_from_u256(a);
            if (IS_BERLIN(vm->fork)) GAS(access_addr_cost(vm, ad));
            else GAS(gas_balance(vm->fork));
            PUSHS(state_balance(vm->st, ad));
            pc++; break;
        }
        case 0x32: GAS(G_BASE); PUSHS(u256_from_addr(vm->tx->origin)); pc++; break;
        case 0x33: GAS(G_BASE); PUSHS(u256_from_addr(f->caller)); pc++; break;
        case 0x34: GAS(G_BASE); PUSHS(f->value); pc++; break;
        case 0x35: {                                                    /* CALLDATALOAD */
            NEEDS(1); GAS(G_VERYLOW);
            u256 off = POP();
            uint8_t w[32];
            for (int i = 0; i < 32; i++) {
                if (!u256_fits_u64(off) || off.v[0] > UINT64_MAX - (uint64_t)i) { w[i] = 0; continue; }
                uint64_t idx = off.v[0] + (uint64_t)i;
                w[i] = (idx < f->ilen) ? f->input[idx] : 0;
            }
            u256 v; u256_from_be(&v, w, 32);
            PUSHS(v);
            pc++; break;
        }
        case 0x36: GAS(G_BASE); PUSHS(u256_from_u64(f->ilen)); pc++; break;
        case 0x37: {                                                    /* CALLDATACOPY */
            NEEDS(3); GAS(G_VERYLOW);
            u256 dst = POP(), src = POP(), len = POP();
            if (!mem_expand_u256(f, dst, len)) goto halt;
            uint64_t l = u256_is_zero(len) ? 0 : len.v[0];
            GAS(G_COPY * ((l + 31) / 32));
            if (l) mem_copy_padded(f, dst.v[0], f->input, f->ilen,
                                   u256_fits_u64(src) ? src.v[0] : UINT64_MAX, l);
            pc++; break;
        }
        case 0x38: GAS(G_BASE); PUSHS(u256_from_u64(f->code_len)); pc++; break;
        case 0x39: {                                                    /* CODECOPY */
            NEEDS(3); GAS(G_VERYLOW);
            u256 dst = POP(), src = POP(), len = POP();
            if (!mem_expand_u256(f, dst, len)) goto halt;
            uint64_t l = u256_is_zero(len) ? 0 : len.v[0];
            GAS(G_COPY * ((l + 31) / 32));
            if (l) mem_copy_padded(f, dst.v[0], f->code, f->code_len,
                                   u256_fits_u64(src) ? src.v[0] : UINT64_MAX, l);
            pc++; break;
        }
        case 0x3a: GAS(G_BASE); PUSHS(vm->tx->gas_price); pc++; break;
        case 0x3b: {                                                    /* EXTCODESIZE */
            NEEDS(1);
            address ad = addr_from_u256(POP());
            if (IS_BERLIN(vm->fork)) GAS(access_addr_cost(vm, ad));
            else GAS(gas_extcodesize(vm->fork));
            size_t cl; state_code(vm->st, ad, &cl);
            PUSHS(u256_from_u64(cl));
            pc++; break;
        }
        case 0x3c: {                                                    /* EXTCODECOPY */
            NEEDS(4);
            address ad = addr_from_u256(POP());
            u256 dst = POP(), src = POP(), len = POP();
            if (IS_BERLIN(vm->fork)) GAS(access_addr_cost(vm, ad));
            else GAS(gas_extcodesize(vm->fork));
            if (!mem_expand_u256(f, dst, len)) goto halt;
            uint64_t l = u256_is_zero(len) ? 0 : len.v[0];
            GAS(G_COPY * ((l + 31) / 32));
            size_t cl; const uint8_t *c = state_code(vm->st, ad, &cl);
            if (l) mem_copy_padded(f, dst.v[0], c, cl, u256_fits_u64(src) ? src.v[0] : UINT64_MAX, l);
            pc++; break;
        }
        case 0x3d:                                                      /* RETURNDATASIZE */
            if (!IS_BYZANTIUM(vm->fork)) goto halt;
            GAS(G_BASE); PUSHS(u256_from_u64(f->ret_len)); pc++; break;
        case 0x3e: {                                                    /* RETURNDATACOPY */
            if (!IS_BYZANTIUM(vm->fork)) goto halt;
            NEEDS(3); GAS(G_VERYLOW);
            u256 dst = POP(), src = POP(), len = POP();
            if (!u256_fits_u64(src) || !u256_fits_u64(len)) goto halt;
            if (src.v[0] > UINT64_MAX - len.v[0]) goto halt;
            if (src.v[0] + len.v[0] > f->ret_len) goto halt;
            if (!mem_expand_u256(f, dst, len)) goto halt;
            GAS(G_COPY * ((len.v[0] + 31) / 32));
            if (len.v[0]) mem_write(f, dst.v[0], f->ret + src.v[0], (size_t)len.v[0]);
            pc++; break;
        }
        case 0x3f: {                                                    /* EXTCODEHASH */
            if (!IS_CONSTANTINOPLE(vm->fork)) goto halt;
            NEEDS(1);
            address ad = addr_from_u256(POP());
            if (IS_BERLIN(vm->fork)) GAS(access_addr_cost(vm, ad));
            else GAS(gas_extcodehash(vm->fork));
            if (state_empty(vm->st, ad)) PUSHS(U256_ZERO);
            else PUSHS(u256_from_hash(state_codehash(vm->st, ad)));
            pc++; break;
        }
        case 0x40: {                                                    /* BLOCKHASH */
            NEEDS(1); GAS(20);
            u256 n = POP();
            hash256 h = hash_zero();
            if (u256_fits_u64(n) && vm->blk->number > 0) {
                uint64_t num = n.v[0];
                if (num < vm->blk->number && num + 256 >= vm->blk->number)
                    h = block_hash_of(vm, num);
            }
            PUSHS(u256_from_hash(h));
            pc++; break;
        }
        case 0x41: GAS(G_BASE); PUSHS(u256_from_addr(vm->blk->coinbase)); pc++; break;
        case 0x42: GAS(G_BASE); PUSHS(u256_from_u64(vm->blk->timestamp)); pc++; break;
        case 0x43: GAS(G_BASE); PUSHS(u256_from_u64(vm->blk->number)); pc++; break;
        case 0x44:                                                      /* DIFFICULTY / PREVRANDAO */
            GAS(G_BASE);
            if (vm->blk->post_merge) PUSHS(u256_from_hash(vm->blk->prevrandao));
            else PUSHS(vm->blk->difficulty);
            pc++; break;
        case 0x45: GAS(G_BASE); PUSHS(u256_from_u64(vm->blk->gas_limit)); pc++; break;
        case 0x46:                                                      /* CHAINID */
            if (!IS_ISTANBUL(vm->fork)) goto halt;
            GAS(G_BASE); PUSHS(vm->blk->chain_id); pc++; break;
        case 0x47:                                                      /* SELFBALANCE */
            if (!IS_ISTANBUL(vm->fork)) goto halt;
            GAS(G_LOW); PUSHS(state_balance(vm->st, f->self)); pc++; break;
        case 0x48:                                                      /* BASEFEE */
            if (!IS_LONDON(vm->fork)) goto halt;
            GAS(G_BASE); PUSHS(vm->blk->base_fee); pc++; break;
        case 0x49: {                                                    /* BLOBHASH */
            if (!IS_CANCUN(vm->fork)) goto halt;
            NEEDS(1); GAS(G_VERYLOW);
            u256 i = POP();
            if (u256_fits_u64(i) && i.v[0] < vm->tx->nblobs) PUSHS(u256_from_hash(vm->tx->blob_hashes[i.v[0]]));
            else PUSHS(U256_ZERO);
            pc++; break;
        }
        case 0x4a:                                                      /* BLOBBASEFEE */
            if (!IS_CANCUN(vm->fork)) goto halt;
            GAS(G_BASE); PUSHS(vm->blk->blob_base_fee); pc++; break;

        case 0x50: NEEDS(1); GAS(G_BASE); (void)POP(); pc++; break;
        case 0x51: {                                                    /* MLOAD */
            NEEDS(1); GAS(G_VERYLOW);
            u256 off = POP();
            if (!mem_expand_u256(f, off, u256_from_u64(32))) goto halt;
            u256 v; u256_from_be(&v, f->mem + off.v[0], 32);
            PUSHS(v);
            pc++; break;
        }
        case 0x52: {                                                    /* MSTORE */
            NEEDS(2); GAS(G_VERYLOW);
            u256 off = POP(), v = POP();
            if (!mem_expand_u256(f, off, u256_from_u64(32))) goto halt;
            u256_to_be(&v, f->mem + off.v[0]);
            pc++; break;
        }
        case 0x53: {                                                    /* MSTORE8 */
            NEEDS(2); GAS(G_VERYLOW);
            u256 off = POP(), v = POP();
            if (!mem_expand_u256(f, off, u256_from_u64(1))) goto halt;
            f->mem[off.v[0]] = (uint8_t)(v.v[0] & 0xff);
            pc++; break;
        }
        case 0x54: {                                                    /* SLOAD */
            NEEDS(1);
            u256 k = POP();
            hash256 key = hash_from_u256(k);
            if (IS_BERLIN(vm->fork)) GAS(access_slot_cost(vm, f->self, key));
            else GAS(gas_sload(vm->fork));
            PUSHS(state_storage(vm->st, f->self, key));
            pc++; break;
        }
        case 0x55: {                                                    /* SSTORE */
            if (f->is_static) goto halt;
            NEEDS(2);
            u256 k = POP(), v = POP();
            hash256 key = hash_from_u256(k);
            if (!sstore_gas(vm, f, key, v)) goto halt;
            state_set_storage(vm->st, f->self, key, v);
            pc++; break;
        }
        case 0x56: {                                                    /* JUMP */
            NEEDS(1); GAS(G_MID);
            u256 d = POP();
            if (!u256_fits_u64(d) || !is_jumpdest(f, d.v[0])) goto halt;
            pc = d.v[0];
            break;
        }
        case 0x57: {                                                    /* JUMPI */
            NEEDS(2); GAS(G_HIGH);
            u256 d = POP(), c = POP();
            if (!u256_is_zero(c)) {
                if (!u256_fits_u64(d) || !is_jumpdest(f, d.v[0])) goto halt;
                pc = d.v[0];
            } else pc++;
            break;
        }
        case 0x58: GAS(G_BASE); PUSHS(u256_from_u64(pc)); pc++; break;
        case 0x59: GAS(G_BASE); PUSHS(u256_from_u64(f->msize)); pc++; break;
        case 0x5a: GAS(G_BASE); PUSHS(u256_from_u64(f->gas)); pc++; break;
        case 0x5b: GAS(G_JUMPDEST); pc++; break;
        case 0x5c: {                                                    /* TLOAD */
            if (!IS_CANCUN(vm->fork)) goto halt;
            NEEDS(1); GAS(WARM_ACCESS);
            u256 k = POP();
            PUSHS(state_tstorage(vm->st, f->self, hash_from_u256(k)));
            pc++; break;
        }
        case 0x5d: {                                                    /* TSTORE */
            if (!IS_CANCUN(vm->fork)) goto halt;
            if (f->is_static) goto halt;
            NEEDS(2); GAS(WARM_ACCESS);
            u256 k = POP(), v = POP();
            state_set_tstorage(vm->st, f->self, hash_from_u256(k), v);
            pc++; break;
        }
        case 0x5e: {                                                    /* MCOPY */
            if (!IS_CANCUN(vm->fork)) goto halt;
            NEEDS(3); GAS(G_VERYLOW);
            u256 dst = POP(), src = POP(), len = POP();
            if (u256_is_zero(len)) { pc++; break; }
            if (!u256_fits_u64(dst) || !u256_fits_u64(src) || !u256_fits_u64(len)) goto halt;
            uint64_t hi = dst.v[0] > src.v[0] ? dst.v[0] : src.v[0];
            if (!mem_expand(f, hi, len.v[0])) goto halt;
            GAS(G_COPY * ((len.v[0] + 31) / 32));
            memmove(f->mem + dst.v[0], f->mem + src.v[0], (size_t)len.v[0]);
            pc++; break;
        }
        case 0x5f:                                                      /* PUSH0 */
            if (!IS_SHANGHAI(vm->fork)) goto halt;
            GAS(G_BASE); PUSHS(U256_ZERO); pc++; break;
        default:
            if (op >= 0x60 && op <= 0x7f) {                              /* PUSH1..32 */
                GAS(G_VERYLOW);
                size_t n = (size_t)(op - 0x5f);
                uint8_t w[32];
                memset(w, 0, 32);
                for (size_t i = 0; i < n; i++) {
                    size_t idx = (size_t)pc + 1 + i;
                    w[i] = (idx < f->code_len) ? f->code[idx] : 0;
                }
                u256 v; u256_from_be(&v, w, n);
                PUSHS(v);
                pc += 1 + n;
                break;
            }
            if (op >= 0x80 && op <= 0x8f) {                              /* DUP1..16 */
                int n = op - 0x7f;
                NEEDS(n); GAS(G_VERYLOW);
                u256 v = PEEK(n - 1);
                PUSHS(v);
                pc++; break;
            }
            if (op >= 0x90 && op <= 0x9f) {                              /* SWAP1..16 */
                int n = op - 0x8f;
                NEEDS(n + 1); GAS(G_VERYLOW);
                u256 t = PEEK(0);
                PEEK(0) = PEEK(n);
                PEEK(n) = t;
                pc++; break;
            }
            if (op >= 0xa0 && op <= 0xa4) {                              /* LOG0..4 */
                if (f->is_static) goto halt;
                int n = op - 0xa0;
                NEEDS(2 + n);
                u256 off = POP(), len = POP();
                evm_log lg;
                memset(&lg, 0, sizeof lg);
                lg.addr = f->self;
                lg.ntopics = n;
                for (int i = 0; i < n; i++) { u256 t = POP(); lg.topics[i] = hash_from_u256(t); }
                if (!mem_expand_u256(f, off, len)) goto halt;
                uint64_t l = u256_is_zero(len) ? 0 : len.v[0];
                GAS(G_LOG + (uint64_t)G_LOGTOPIC * (uint64_t)n + G_LOGDATA * l);
                lg.data_len = (size_t)l;
                if (l) {
                    lg.data = (uint8_t *)malloc((size_t)l);
                    if (!lg.data) goto halt;
                    memcpy(lg.data, f->mem + off.v[0], (size_t)l);
                }
                state_add_log(vm->st, &lg);
                pc++; break;
            }
            switch (op) {
            case 0xf0: case 0xf5: {                                      /* CREATE / CREATE2 */
                if (op == 0xf5 && !IS_CONSTANTINOPLE(vm->fork)) goto halt;
                if (f->is_static) goto halt;
                r = do_create(vm, f, op == 0xf5);
                if (r.status == EX_HALT && r.gas_left == UINT64_MAX) goto halt;
                pc++; break;
            }
            case 0xf1: case 0xf2: case 0xf4: case 0xfa: {                /* CALL family */
                if (op == 0xf4 && !IS_HOMESTEAD(vm->fork)) goto halt;
                if (op == 0xfa && !IS_BYZANTIUM(vm->fork)) goto halt;
                r = do_call(vm, f, op);
                if (r.status == EX_HALT && r.gas_left == UINT64_MAX) goto halt;
                pc++; break;
            }
            case 0xf3: {                                                 /* RETURN */
                NEEDS(2);
                u256 off = POP(), len = POP();
                if (!mem_expand_u256(f, off, len)) goto halt;
                uint64_t l = u256_is_zero(len) ? 0 : len.v[0];
                r.status = EX_OK; r.gas_left = f->gas;
                r.ret_len = (size_t)l;
                if (l) {
                    r.ret = (uint8_t *)malloc((size_t)l);
                    if (!r.ret) goto halt;
                    memcpy(r.ret, f->mem + off.v[0], (size_t)l);
                }
                goto done;
            }
            case 0xfd: {                                                 /* REVERT */
                if (!IS_BYZANTIUM(vm->fork)) goto halt;
                NEEDS(2);
                u256 off = POP(), len = POP();
                if (!mem_expand_u256(f, off, len)) goto halt;
                uint64_t l = u256_is_zero(len) ? 0 : len.v[0];
                r.status = EX_REVERT; r.gas_left = f->gas;
                r.ret_len = (size_t)l;
                if (l) {
                    r.ret = (uint8_t *)malloc((size_t)l);
                    if (!r.ret) goto halt;
                    memcpy(r.ret, f->mem + off.v[0], (size_t)l);
                }
                goto done;
            }
            case 0xff: {                                                 /* SELFDESTRUCT */
                if (f->is_static) goto halt;
                NEEDS(1);
                address ben = addr_from_u256(POP());
                uint64_t cost = gas_selfdestruct(vm->fork);
                if (IS_BERLIN(vm->fork) && !state_addr_warm(vm->st, ben)) {
                    state_warm_addr(vm->st, ben);
                    cost += COLD_ACCOUNT;
                }
                if (IS_EIP150(vm->fork)) {
                    if (IS_EIP158(vm->fork)) {
                        if (state_empty(vm->st, ben) && !u256_is_zero(state_balance(vm->st, f->self)))
                            cost += G_NEWACCOUNT;
                    } else if (!state_exists(vm->st, ben)) {
                        cost += G_NEWACCOUNT;
                    }
                }
                if (!IS_LONDON(vm->fork) && !state_is_destructed(vm->st, f->self))
                    state_add_refund(vm->st, G_SELFDESTRUCT_REFUND);
                GAS(cost);
                u256 bal = state_balance(vm->st, f->self);
                if (IS_CANCUN(vm->fork)) {
                    state_sub_balance(vm->st, f->self, bal);
                    state_add_balance(vm->st, ben, bal);
                    if (state_was_created(vm->st, f->self)) {
                        /* EIP-6780: the account is destroyed, and its balance is
                         * zeroed now — a later call in the same transaction starts
                         * from zero even when it was its own beneficiary. */
                        state_destruct(vm->st, f->self);
                        state_set_balance(vm->st, f->self, U256_ZERO);
                    } else {
                        state_touch(vm->st, ben);
                    }
                } else {
                    state_add_balance(vm->st, ben, bal);
                    state_set_balance(vm->st, f->self, U256_ZERO);
                    state_destruct(vm->st, f->self);
                }
                r.status = EX_OK; r.gas_left = f->gas;
                goto done;
            }
            default:
                goto halt;
            }
            break;
        }
    }
halt:
    r.status = EX_HALT;
    r.gas_left = 0;
    free(r.ret);
    r.ret = NULL;
    r.ret_len = 0;
done:
    return r;
}

/* ------------------------------------------------------------ call / create */
static uint64_t all_but_64th(uint64_t g) { return g - g / 64; }

ex_result evm_call(vm_t *vm, const call_args *a)
{
    ex_result r;
    memset(&r, 0, sizeof r);
    r.gas_left = a->gas;

    if (vm->depth > CALL_DEPTH_LIMIT) { r.status = EX_HALT; return r; }
    if (a->kind == CK_CALL || a->kind == CK_TX) {
        if (!u256_is_zero(a->value) && u256_cmp(state_balance(vm->st, a->caller), a->value) < 0) {
            r.status = EX_HALT;
            return r;
        }
    } else if (a->kind == CK_CALLCODE) {
        if (u256_cmp(state_balance(vm->st, a->caller), a->value) < 0) { r.status = EX_HALT; return r; }
    }

    size_t snap = state_snapshot(vm->st);
    bool prec = is_precompile(vm->fork, a->code_addr);

    if (a->kind == CK_CALL || a->kind == CK_TX) {
        if (!state_exists(vm->st, a->self)) {
            if (!prec && IS_EIP158(vm->fork) && u256_is_zero(a->value)) {
                r.status = EX_OK;
                return r;                      /* calling a non-existing account is a no-op */
            }
            state_create_account(vm->st, a->self);
        }
        state_sub_balance(vm->st, a->caller, a->value);
        state_add_balance(vm->st, a->self, a->value);
    } else if (a->kind == CK_STATIC) {
        state_add_balance(vm->st, a->self, U256_ZERO);   /* a static call still touches */
    }

    if (prec) {
        vm->depth++;
        ex_result pr = run_precompile(vm, a->code_addr, a->input, a->ilen, a->gas);
        vm->depth--;
        if (pr.status != EX_OK && pr.status != EX_REVERT) state_revert(vm->st, snap);
        return pr;
    }

    uint64_t extra = 0;
    size_t clen = 0;
    const uint8_t *code = resolve_code(vm, a->code_addr, &clen, &extra, NULL, false);
    if (clen == 0) { r.status = EX_OK; r.gas_left = a->gas; return r; }

    frame f;
    memset(&f, 0, sizeof f);
    f.gas = a->gas;
    f.code = code;
    f.code_len = clen;
    f.input = a->input;
    f.ilen = a->ilen;
    f.self = a->self;
    f.caller = a->caller;
    f.code_addr = a->code_addr;
    f.value = a->value;
    f.is_static = a->is_static;

    f.stack = (u256 *)malloc(sizeof(u256) * STACK_LIMIT);
    if (!f.stack) { r.status = EX_HALT; r.gas_left = 0; return r; }
    vm->depth++;
    r = run_frame(vm, &f);
    vm->depth--;
    frame_free(&f);

    if (r.status != EX_OK) state_revert(vm->st, snap);
    return r;
}

ex_result evm_create(vm_t *vm, address creator, u256 value, const uint8_t *init,
                     size_t ilen, uint64_t gas, bool create2, u256 salt)
{
    ex_result r;
    memset(&r, 0, sizeof r);
    r.gas_left = gas;

    if (vm->depth > CALL_DEPTH_LIMIT) { r.status = EX_HALT; return r; }
    if (u256_cmp(state_balance(vm->st, creator), value) < 0) { r.status = EX_HALT; return r; }

    uint64_t nonce = state_nonce(vm->st, creator);
    if (nonce + 1 < nonce) { r.status = EX_HALT; return r; }
    state_set_nonce(vm->st, creator, nonce + 1);

    address newaddr = create2 ? create2_address(creator, salt, init, ilen)
                              : create_address(creator, nonce);
    if (IS_BERLIN(vm->fork)) state_warm_addr(vm->st, newaddr);

    /* collision check */
    {
        account *ac = state_get(vm->st, newaddr);
        bool collide = false;
        if (ac) {
            if (ac->nonce != 0) collide = true;
            if (ac->code_len != 0) collide = true;
            for (size_t i = 0; i < ac->storage.cap; i++)
                if (ac->storage.e[i].used && !u256_is_zero(ac->storage.e[i].val)) { collide = true; break; }
        }
        if (collide) { r.status = EX_HALT; r.gas_left = 0; return r; }
    }

    size_t snap = state_snapshot(vm->st);
    if (!state_exists(vm->st, newaddr)) state_create_account(vm->st, newaddr);
    state_mark_created(vm->st, newaddr);
    if (IS_EIP158(vm->fork)) state_set_nonce(vm->st, newaddr, 1);
    state_sub_balance(vm->st, creator, value);
    state_add_balance(vm->st, newaddr, value);

    frame f;
    memset(&f, 0, sizeof f);
    f.gas = gas;
    f.code = init;
    f.code_len = ilen;
    f.self = newaddr;
    f.caller = creator;
    f.code_addr = newaddr;
    f.value = value;

    f.stack = (u256 *)malloc(sizeof(u256) * STACK_LIMIT);
    vm->depth++;
    ex_result er = run_frame(vm, &f);
    vm->depth--;
    frame_free(&f);

    r.created = newaddr;
    if (er.status == EX_OK) {
        bool fail = false;
        bool store_oog = false;
        if (IS_EIP158(vm->fork) && er.ret_len > MAX_CODE_SIZE) fail = true;
        if (!fail && IS_LONDON(vm->fork) && er.ret_len >= 1 && er.ret[0] == 0xEF) fail = true;
        if (!fail) {
            uint64_t dep = (uint64_t)er.ret_len * G_CODEDEPOSIT;
            if (er.gas_left < dep) { store_oog = true; }
            else {
                er.gas_left -= dep;
                state_set_code(vm->st, newaddr, er.ret, er.ret_len);
            }
        }
        if (fail || store_oog) {
            if (fail || IS_HOMESTEAD(vm->fork)) {
                state_revert(vm->st, snap);
                r.status = EX_HALT;
                r.gas_left = 0;
                free(er.ret);
                return r;
            }
            /* Frontier: a code-deposit failure leaves the empty contract in place and keeps
             * the gas that is left.  The reference tool returns the deposit error without
             * reverting, and then treats it asymmetrically: the CREATE opcode pushes the new
             * address as though the creation had succeeded, while a creation *transaction*
             * carrying the same error reports status 0.  Signal it rather than deciding here.
             */
            r.status = EX_OK;
            r.deposit_oog = true;
            r.gas_left = er.gas_left;
            r.ret = NULL;
            r.ret_len = 0;
            free(er.ret);
            return r;
        }
        r.status = EX_OK;
        r.gas_left = er.gas_left;
        r.ret = NULL;
        r.ret_len = 0;
        free(er.ret);
        return r;
    }
    state_revert(vm->st, snap);
    if (er.status == EX_REVERT) {
        r.status = EX_REVERT;
        r.gas_left = er.gas_left;
        r.ret = er.ret;
        r.ret_len = er.ret_len;
        return r;
    }
    r.status = EX_HALT;
    r.gas_left = 0;
    free(er.ret);
    return r;
}

static ex_result oops(void) { ex_result r; memset(&r, 0, sizeof r); r.status = EX_HALT; r.gas_left = UINT64_MAX; return r; }

static ex_result do_create(vm_t *vm, frame *f, bool create2)
{
    int need = create2 ? 4 : 3;
    if (f->sp < need) return oops();
    u256 value = f->stack[--f->sp];
    u256 off = f->stack[--f->sp];
    u256 len = f->stack[--f->sp];
    u256 salt = U256_ZERO;
    if (create2) salt = f->stack[--f->sp];

    if (!mem_expand_u256(f, off, len)) return oops();
    uint64_t l = u256_is_zero(len) ? 0 : len.v[0];
    uint64_t words = (l + 31) / 32;

    uint64_t cost = G_CREATE;
    if (create2) cost += G_KECCAK256WORD * words;
    if (IS_SHANGHAI(vm->fork)) {
        if (l > MAX_INITCODE) return oops();
        cost += INITCODE_WORD * words;
    }
    if (!use_gas(f, cost)) return oops();

    uint64_t child = f->gas;
    if (IS_EIP150(vm->fork)) child = all_but_64th(f->gas);
    if (!use_gas(f, child)) return oops();

    free(f->ret); f->ret = NULL; f->ret_len = 0;

    uint8_t *init = NULL;
    if (l) {
        init = (uint8_t *)malloc((size_t)l);
        if (!init) { f->gas = 0; return oops(); }
        memcpy(init, f->mem + off.v[0], (size_t)l);
    }
    ex_result cr = evm_create(vm, f->self, value, init, (size_t)l, child, create2, salt);
    free(init);

    f->gas += cr.gas_left;
    if (cr.status == EX_OK) {
        if (f->sp >= STACK_LIMIT) return oops();
        f->stack[f->sp++] = u256_from_addr(cr.created);
    } else {
        if (f->sp >= STACK_LIMIT) return oops();
        f->stack[f->sp++] = U256_ZERO;
    }
    if (cr.status == EX_REVERT) { f->ret = cr.ret; f->ret_len = cr.ret_len; }
    else free(cr.ret);
    ex_result none; memset(&none, 0, sizeof none); none.status = EX_OK;
    return none;
}

static ex_result do_call(vm_t *vm, frame *f, int op)
{
    bool has_value = (op == 0xf1 || op == 0xf2);
    int need = has_value ? 7 : 6;
    if (f->sp < need) return oops();

    u256 gas_req = f->stack[--f->sp];
    address target = addr_from_u256(f->stack[--f->sp]);
    u256 value = U256_ZERO;
    if (has_value) value = f->stack[--f->sp];
    u256 inoff = f->stack[--f->sp], inlen = f->stack[--f->sp];
    u256 outoff = f->stack[--f->sp], outlen = f->stack[--f->sp];

    if (f->is_static && op == 0xf1 && !u256_is_zero(value)) return oops();

    if (!mem_expand_u256(f, inoff, inlen)) return oops();
    if (!mem_expand_u256(f, outoff, outlen)) return oops();

    uint64_t base = 0;
    if (IS_BERLIN(vm->fork)) base += access_addr_cost(vm, target);
    else base += gas_call_base(vm->fork);

    /* EIP-7702: resolving a delegation costs an extra account access */
    if (IS_PRAGUE(vm->fork)) {
        size_t cl; const uint8_t *c = state_code(vm->st, target, &cl);
        address del;
        if (c && delegation_of(c, cl, &del)) base += access_addr_cost(vm, del);
    }

    bool transfers = has_value && !u256_is_zero(value);
    if (op == 0xf1) {
        if (IS_EIP158(vm->fork)) {
            if (transfers && state_empty(vm->st, target)) base += G_NEWACCOUNT;
        } else if (!state_exists(vm->st, target)) {
            base += G_NEWACCOUNT;
        }
    }
    if (transfers) base += G_CALLVALUE;

    if (f->gas < base) { f->gas = 0; return oops(); }
    uint64_t avail = f->gas - base;
    uint64_t child;
    if (IS_EIP150(vm->fork)) {
        child = all_but_64th(avail);
        if (u256_fits_u64(gas_req) && gas_req.v[0] < child) child = gas_req.v[0];
    } else {
        if (!u256_fits_u64(gas_req)) return oops();
        child = gas_req.v[0];
    }
    if (!use_gas(f, base)) return oops();
    if (!use_gas(f, child)) return oops();
    if (transfers) child += G_CALLSTIPEND;

    call_args a;
    memset(&a, 0, sizeof a);
    a.input = inlen.v[0] && u256_fits_u64(inlen) ? f->mem + inoff.v[0] : (const uint8_t *)"";
    a.ilen = u256_is_zero(inlen) ? 0 : (size_t)inlen.v[0];
    a.gas = child;
    a.code_addr = target;
    switch (op) {
    case 0xf1:                                   /* CALL */
        a.kind = CK_CALL;
        a.caller = f->self; a.self = target; a.value = value; a.transfer_value = true;
        a.is_static = f->is_static;
        break;
    case 0xf2:                                   /* CALLCODE */
        a.kind = CK_CALLCODE;
        a.caller = f->self; a.self = f->self; a.value = value; a.transfer_value = false;
        a.is_static = f->is_static;
        break;
    case 0xf4:                                   /* DELEGATECALL */
        a.kind = CK_DELEGATE;
        a.caller = f->caller; a.self = f->self; a.value = f->value; a.transfer_value = false;
        a.is_static = f->is_static;
        break;
    default:                                     /* STATICCALL */
        a.kind = CK_STATIC;
        a.caller = f->self; a.self = target; a.value = U256_ZERO; a.transfer_value = false;
        a.is_static = true;
        break;
    }

    /* copy input out of memory: the callee may grow our memory buffer via realloc */
    uint8_t *inbuf = NULL;
    if (a.ilen) {
        inbuf = (uint8_t *)malloc(a.ilen);
        if (!inbuf) { f->gas = 0; return oops(); }
        memcpy(inbuf, a.input, a.ilen);
        a.input = inbuf;
    }

    free(f->ret); f->ret = NULL; f->ret_len = 0;
    ex_result cr = evm_call(vm, &a);
    free(inbuf);

    f->gas += cr.gas_left;
    if (f->sp >= STACK_LIMIT) { free(cr.ret); return oops(); }
    f->stack[f->sp++] = u256_from_u64(cr.status == EX_OK ? 1 : 0);

    if (cr.status == EX_OK || cr.status == EX_REVERT) {
        uint64_t ol = u256_is_zero(outlen) ? 0 : outlen.v[0];
        uint64_t n = cr.ret_len < ol ? cr.ret_len : ol;
        if (n) mem_write(f, outoff.v[0], cr.ret, (size_t)n);
        f->ret = cr.ret;
        f->ret_len = cr.ret_len;
    } else {
        free(cr.ret);
    }
    ex_result none; memset(&none, 0, sizeof none); none.status = EX_OK;
    return none;
}
