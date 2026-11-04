#define _POSIX_C_SOURCE 200809L
#include "transition.h"
#include "hexutil.h"
#include "json.h"
#include "rlp.h"
#include "keccak.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>

#define ERR_EVM 2
#define ERR_CONFIG 3
#define ERR_MISSING_BLOCKHASH 4
#define ERR_JSON 10
#define ERR_IO 11

static void die(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "ERROR(%d): ", code);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(code);
}

static char *read_all(const char *path, size_t *len)
{
    FILE *fp;
    if (!strcmp(path, "stdin")) fp = stdin;
    else fp = fopen(path, "rb");
    if (!fp) return NULL;
    size_t cap = 65536, n = 0;
    char *buf = (char *)malloc(cap);
    for (;;) {
        if (n + 65536 > cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
        size_t got = fread(buf + n, 1, 65536, fp);
        n += got;
        if (got < 65536) break;
    }
    if (fp != stdin) fclose(fp);
    buf[n] = 0;
    *len = n;
    return buf;
}

static void write_out(const char *basedir, const char *target, const buf_t *content)
{
    if (!target || !*target) return;
    if (!strcmp(target, "stdout")) { fwrite(content->data, 1, content->len, stdout); fputc('\n', stdout); return; }
    if (!strcmp(target, "stderr")) { fwrite(content->data, 1, content->len, stderr); fputc('\n', stderr); return; }
    char path[4096];
    if (basedir && *basedir) {
        mkdir(basedir, 0755);
        snprintf(path, sizeof path, "%s/%s", basedir, target);
    } else {
        snprintf(path, sizeof path, "%s", target);
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) die(ERR_IO, "could not write %s: %s", path, strerror(errno));
    fwrite(content->data, 1, content->len, fp);
    fputc('\n', fp);
    fclose(fp);
}

/* ------------------------------------------------------------ arg parsing */
typedef struct { const char *key, *val; } argpair;

static const char *getopt_str(int argc, char **argv, const char *name, const char *def)
{
    size_t nl = strlen(name);
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], name, nl) == 0) {
            if (argv[i][nl] == '=') return argv[i] + nl + 1;
            if (argv[i][nl] == 0 && i + 1 < argc) return argv[i + 1];
        }
    }
    return def;
}

/* ----------------------------------------------------------- alloc input */
static void load_alloc(state *st, const jval *alloc)
{
    if (!alloc || alloc->type != JOBJ) die(ERR_JSON, "bad alloc");
    for (size_t i = 0; i < alloc->n; i++) {
        address a;
        jval kv; memset(&kv, 0, sizeof kv);
        kv.type = JSTR; kv.str = alloc->keys[i];
        if (jaddr(&kv, &a) != 0) die(ERR_JSON, "bad address in alloc");
        jval *o = alloc->items[i];
        state_create_account(st, a);
        u256 bal = U256_ZERO;
        jval *b = json_get(o, "balance");
        if (b && jnum_u256(b, &bal) != 0) die(ERR_JSON, "bad balance");
        state_set_balance(st, a, bal);
        uint64_t nonce = 0;
        jval *n = json_get(o, "nonce");
        if (n && jnum_u64(n, &nonce) != 0) die(ERR_JSON, "bad nonce");
        state_set_nonce(st, a, nonce);
        jval *c = json_get(o, "code");
        if (c) {
            buf_t cb; buf_init(&cb);
            if (jbytes(c, &cb) != 0) die(ERR_JSON, "bad code");
            state_set_code(st, a, cb.data, cb.len);
            buf_free(&cb);
        }
        jval *s = json_get(o, "storage");
        if (s && s->type == JOBJ) {
            for (size_t j = 0; j < s->n; j++) {
                hash256 k, v;
                jval kk; memset(&kk, 0, sizeof kk); kk.type = JSTR; kk.str = s->keys[j];
                if (jhash(&kk, &k) != 0) die(ERR_JSON, "bad storage key");
                if (jhash(s->items[j], &v) != 0) die(ERR_JSON, "bad storage value");
                u256 val = u256_from_hash(v);
                if (!u256_is_zero(val)) state_set_storage(st, a, k, val);
            }
        }
    }
    state_commit_tx(st);
}

static int cmp_addr_ptr(const void *x, const void *y)
{
    const account *const *a = (const account *const *)x;
    const account *const *b = (const account *const *)y;
    return memcmp((*a)->addr.b, (*b)->addr.b, 20);
}

static void dump_alloc(state *st, buf_t *o)
{
    size_t n = 0;
    for (size_t i = 0; i < st->accounts.cap; i++)
        if (st->accounts.e[i] && st->accounts.e[i]->exists) n++;
    account **list = (account **)calloc(n ? n : 1, sizeof(account *));
    size_t k = 0;
    for (size_t i = 0; i < st->accounts.cap; i++)
        if (st->accounts.e[i] && st->accounts.e[i]->exists) list[k++] = st->accounts.e[i];
    qsort(list, n, sizeof(account *), cmp_addr_ptr);
    json_wr_raw(o, "{");
    for (size_t i = 0; i < n; i++) {
        account *ac = list[i];
        if (i) json_wr_raw(o, ",");
        json_wr_raw(o, "\n ");
        buf_t key; buf_init(&key);
        wr_addr(&key, ac->addr);
        buf_putc(&key, 0);
        json_wr_str(o, (const char *)key.data);
        buf_free(&key);
        json_wr_raw(o, ": {");
        bool first = true;
        if (ac->code_len) {
            json_wr_raw(o, "\n  \"code\": ");
            wr_bytes_q(o, ac->code, ac->code_len);
            first = false;
        }
        size_t ns = 0;
        for (size_t j = 0; j < ac->storage.cap; j++)
            if (ac->storage.e[j].used && !u256_is_zero(ac->storage.e[j].val)) ns++;
        if (ns) {
            slot_entry *se = (slot_entry *)calloc(ns, sizeof(slot_entry));
            size_t m = 0;
            for (size_t j = 0; j < ac->storage.cap; j++)
                if (ac->storage.e[j].used && !u256_is_zero(ac->storage.e[j].val)) se[m++] = ac->storage.e[j];
            for (size_t x = 0; x < ns; x++)
                for (size_t y = x + 1; y < ns; y++)
                    if (memcmp(se[y].key.b, se[x].key.b, 32) < 0) { slot_entry t = se[x]; se[x] = se[y]; se[y] = t; }
            if (!first) json_wr_raw(o, ",");
            json_wr_raw(o, "\n  \"storage\": {");
            for (size_t x = 0; x < ns; x++) {
                if (x) json_wr_raw(o, ",");
                json_wr_raw(o, "\n   ");
                buf_t kk; buf_init(&kk);
                wr_hash(&kk, se[x].key);
                buf_putc(&kk, 0);
                json_wr_str(o, (const char *)kk.data);
                buf_free(&kk);
                json_wr_raw(o, ": ");
                hash256 hv = hash_from_u256(se[x].val);
                wr_hash_q(o, hv);
            }
            json_wr_raw(o, "\n  }");
            free(se);
            first = false;
        }
        if (!first) json_wr_raw(o, ",");
        json_wr_raw(o, "\n  \"balance\": ");
        wr_u256_q(o, ac->balance);
        if (ac->nonce) {
            json_wr_raw(o, ",\n  \"nonce\": ");
            wr_u64_q(o, ac->nonce);
        }
        json_wr_raw(o, "\n }");
    }
    json_wr_raw(o, "\n}");
    free(list);
}

/* ------------------------------------------------------------- env input */
static void load_env(env_t *e, const jval *env, fork_id fk)
{
    memset(e, 0, sizeof *e);
    jval *v;
    if (!(v = json_get(env, "currentCoinbase")) || jaddr(v, &e->coinbase) != 0) die(ERR_CONFIG, "missing currentCoinbase");
    if (!(v = json_get(env, "currentGasLimit")) || jnum_u64(v, &e->gas_limit) != 0) die(ERR_CONFIG, "missing currentGasLimit");
    if (!(v = json_get(env, "currentNumber")) || jnum_u64(v, &e->number) != 0) die(ERR_CONFIG, "missing currentNumber");
    if (!(v = json_get(env, "currentTimestamp")) || jnum_u64(v, &e->timestamp) != 0) die(ERR_CONFIG, "missing currentTimestamp");
    if ((v = json_get(env, "currentDifficulty")) && v->type != JNULL) { jnum_u256(v, &e->difficulty); e->has_difficulty = true; }
    if ((v = json_get(env, "currentRandom")) && v->type != JNULL) { jhash(v, &e->random); e->has_random = true; }
    if ((v = json_get(env, "currentBaseFee")) && v->type != JNULL) { jnum_u256(v, &e->base_fee); e->has_base_fee = true; }
    if ((v = json_get(env, "parentDifficulty")) && v->type != JNULL) { jnum_u256(v, &e->parent_difficulty); e->has_parent_difficulty = true; }
    if ((v = json_get(env, "parentTimestamp")) && v->type != JNULL) { jnum_u64(v, &e->parent_timestamp); e->has_parent_timestamp = true; }
    if ((v = json_get(env, "parentGasUsed")) && v->type != JNULL) jnum_u64(v, &e->parent_gas_used);
    if ((v = json_get(env, "parentGasLimit")) && v->type != JNULL) jnum_u64(v, &e->parent_gas_limit);
    if ((v = json_get(env, "parentUncleHash")) && v->type != JNULL) { jhash(v, &e->parent_uncle_hash); e->has_parent_uncle_hash = true; }
    if ((v = json_get(env, "parentBaseFee")) && v->type != JNULL) { jnum_u256(v, &e->parent_base_fee); e->has_parent_base_fee = true; }
    if ((v = json_get(env, "parentExcessBlobGas")) && v->type != JNULL) { jnum_u64(v, &e->parent_excess_blob_gas); e->has_peb = true; }
    if ((v = json_get(env, "parentBlobGasUsed")) && v->type != JNULL) { jnum_u64(v, &e->parent_blob_gas_used); e->has_pbgu = true; }
    if ((v = json_get(env, "currentExcessBlobGas")) && v->type != JNULL) { jnum_u64(v, &e->excess_blob_gas); e->has_ebg = true; }
    if ((v = json_get(env, "parentBeaconBlockRoot")) && v->type != JNULL) { jhash(v, &e->parent_beacon_block_root); e->has_pbbr = true; }
    if ((v = json_get(env, "parentHash")) && v->type != JNULL) { jhash(v, &e->parent_hash); e->has_parent_hash = true; }

    if ((v = json_get(env, "blockHashes")) && v->type == JOBJ) {
        e->nbhashes = v->n;
        e->bhashes = (blockhash_entry *)calloc(v->n ? v->n : 1, sizeof(blockhash_entry));
        for (size_t i = 0; i < v->n; i++) {
            jval kk; memset(&kk, 0, sizeof kk); kk.type = JSTR; kk.str = v->keys[i];
            jnum_u64(&kk, &e->bhashes[i].num);
            jhash(v->items[i], &e->bhashes[i].h);
        }
    }
    if ((v = json_get(env, "ommers")) && v->type == JARR) {
        e->nommers = v->n;
        e->ommers = (ommer_t *)calloc(v->n ? v->n : 1, sizeof(ommer_t));
        for (size_t i = 0; i < v->n; i++) {
            jval *o = v->items[i];
            jnum_u64(json_get(o, "delta"), &e->ommers[i].delta);
            jaddr(json_get(o, "address"), &e->ommers[i].addr);
        }
    }
    if ((v = json_get(env, "withdrawals")) && v->type == JARR) {
        e->has_withdrawals = true;
        e->nwithdrawals = v->n;
        e->withdrawals = (withdrawal_t *)calloc(v->n ? v->n : 1, sizeof(withdrawal_t));
        for (size_t i = 0; i < v->n; i++) {
            jval *w = v->items[i];
            jnum_u64(json_get(w, "index"), &e->withdrawals[i].index);
            jnum_u64(json_get(w, "validatorIndex"), &e->withdrawals[i].validator_index);
            jaddr(json_get(w, "address"), &e->withdrawals[i].recipient);
            jnum_u256(json_get(w, "amount"), &e->withdrawals[i].amount);
        }
    } else if (IS_SHANGHAI(fk)) {
        die(ERR_CONFIG, "Shanghai config but missing 'withdrawals' in env section");
    }
    if (IS_CANCUN(fk) && !e->has_pbbr) die(ERR_CONFIG, "post-cancun env requires parentBeaconBlockRoot to be set");
    if (IS_PARIS(fk) && !e->has_random) die(ERR_CONFIG, "post-merge requires currentRandom to be defined in env");
    if (IS_PARIS(fk) && e->has_difficulty && !u256_is_zero(e->difficulty))
        die(ERR_CONFIG, "post-merge difficulty must be zero (or omitted) in env");
    if (!IS_PARIS(fk) && !e->has_difficulty && !e->has_parent_difficulty)
        die(ERR_CONFIG, "currentDifficulty was not provided, and cannot be calculated due to missing parentDifficulty");
}

/* --------------------------------------------------------------- tx input */
static tx_t *load_txs_rlp(const uint8_t *p, size_t len, size_t *n)
{
    rlp_item top;
    if (rlp_decode(p, len, &top) != 0 || !top.is_list) die(ERR_JSON, "bad tx rlp list");
    rlp_iter it; rlp_item e;
    rlp_iter_init(&it, &top);
    size_t cap = 8, cnt = 0;
    tx_t *txs = (tx_t *)calloc(cap, sizeof(tx_t));
    while (!rlp_iter_done(&it)) {
        if (rlp_iter_next(&it, &e) != 0) die(ERR_JSON, "bad tx rlp");
        if (cnt == cap) { cap *= 2; txs = (tx_t *)realloc(txs, cap * sizeof(tx_t)); }
        memset(&txs[cnt], 0, sizeof(tx_t));
        const uint8_t *raw; size_t rawlen;
        if (e.is_list) { raw = e.payload - (e.total_len - e.payload_len); rawlen = e.total_len; }
        else { raw = e.payload; rawlen = e.payload_len; }
        if (tx_decode(raw, rawlen, &txs[cnt]) != 0) txs[cnt].type = 0xff;   /* mark undecodable */
        cnt++;
    }
    *n = cnt;
    return txs;
}

static tx_t *load_txs_json(const jval *arr, size_t *n, u256 chain_id, fork_id fk)
{
    if (!arr || arr->type != JARR) die(ERR_JSON, "bad txs json");
    tx_t *txs = (tx_t *)calloc(arr->n ? arr->n : 1, sizeof(tx_t));
    for (size_t i = 0; i < arr->n; i++) {
        jval *o = arr->items[i];
        tx_t *t = &txs[i];
        memset(t, 0, sizeof *t);
        jval *v;
        uint64_t ty = 0;
        if ((v = json_get(o, "type"))) jnum_u64(v, &ty);
        t->type = (uint8_t)ty;
        if ((v = json_get(o, "chainId"))) { jnum_u256(v, &t->chain_id); t->has_chain_id = true; }
        else t->chain_id = chain_id;
        if ((v = json_get(o, "nonce"))) jnum_u64(v, &t->nonce);
        if ((v = json_get(o, "gasPrice")) && v->type != JNULL) { jnum_u256(v, &t->gas_price); t->max_fee = t->gas_price; t->max_prio = t->gas_price; }
        if ((v = json_get(o, "maxFeePerGas")) && v->type != JNULL) { jnum_u256(v, &t->max_fee); if (!ty) t->type = 2; }
        if ((v = json_get(o, "maxPriorityFeePerGas")) && v->type != JNULL) jnum_u256(v, &t->max_prio);
        if ((v = json_get(o, "gas"))) jnum_u64(v, &t->gas);
        if ((v = json_get(o, "gasLimit"))) jnum_u64(v, &t->gas);
        if ((v = json_get(o, "to")) && v->type != JNULL && json_str(v) && strlen(json_str(v)) > 2) {
            t->has_to = true;
            jaddr(v, &t->to);
        }
        if ((v = json_get(o, "value"))) jnum_u256(v, &t->value);
        if ((v = json_get(o, "input")) || (v = json_get(o, "data"))) {
            buf_t d; buf_init(&d);
            jbytes(v, &d);
            t->data = d.data; t->data_len = d.len;
        }
        if ((v = json_get(o, "accessList")) && v->type == JARR) {
            t->nal = v->n;
            t->al = (access_tuple *)calloc(v->n ? v->n : 1, sizeof(access_tuple));
            for (size_t j = 0; j < v->n; j++) {
                jval *e = v->items[j];
                jaddr(json_get(e, "address"), &t->al[j].addr);
                jval *ks = json_get(e, "storageKeys");
                if (ks && ks->type == JARR) {
                    t->al[j].nkeys = ks->n;
                    t->al[j].keys = (hash256 *)calloc(ks->n ? ks->n : 1, sizeof(hash256));
                    for (size_t q = 0; q < ks->n; q++) jhash(ks->items[q], &t->al[j].keys[q]);
                }
            }
            if (!ty && t->type == 0) t->type = 1;
        }
        /* EIP-4844 and EIP-7702 carry their own lists in the JSON form too */
        if ((v = json_get(o, "maxFeePerBlobGas")) && v->type != JNULL) {
            jnum_u256(v, &t->max_fee_blob);
            if (!ty) t->type = 3;
        }
        if ((v = json_get(o, "blobVersionedHashes")) && v->type == JARR) {
            t->nblobs = v->n;
            t->blob_hashes = (hash256 *)calloc(v->n ? v->n : 1, sizeof(hash256));
            for (size_t j = 0; j < v->n; j++) jhash(v->items[j], &t->blob_hashes[j]);
            if (!ty) t->type = 3;
        }
        if ((v = json_get(o, "authorizationList")) && v->type == JARR) {
            t->nauths = v->n;
            t->auths = (authorization *)calloc(v->n ? v->n : 1, sizeof(authorization));
            for (size_t j = 0; j < v->n; j++) {
                jval *e = v->items[j];
                jval *x;
                if ((x = json_get(e, "chainId"))) jnum_u256(x, &t->auths[j].chain_id);
                jaddr(json_get(e, "address"), &t->auths[j].addr);
                if ((x = json_get(e, "nonce"))) jnum_u64(x, &t->auths[j].nonce);
                uint64_t yy = 0;
                if ((x = json_get(e, "yParity")) || (x = json_get(e, "v"))) jnum_u64(x, &yy);
                t->auths[j].y = (int)yy;
                if ((x = json_get(e, "r"))) jnum_u256(x, &t->auths[j].r);
                if ((x = json_get(e, "s"))) jnum_u256(x, &t->auths[j].s);
            }
            if (!ty) t->type = 4;
        }
        if ((v = json_get(o, "secretKey")) && v->type != JNULL) {
            hash256 key;
            jhash(v, &key);
            /* The tool signs EIP-155 style unless the transaction says `protected: false`,
             * and it does that at *every* fork name -- so a protected transaction handed to a
             * fork older than Spurious Dragon is signed and then rejected, rather than being
             * signed the old way.  This does not follow the fork. */
            bool eip155 = true;
            jval *pv = json_get(o, "protected");
            if (pv && pv->type == JBOOL) eip155 = pv->b;
            u256 cid = t->has_chain_id ? t->chain_id : chain_id;
            t->chain_id = cid;
            if (tx_sign(t, key.b, cid, eip155) != 0) die(ERR_JSON, "could not sign transaction");
        } else {
            if ((v = json_get(o, "v"))) jnum_u256(v, &t->v);
            if ((v = json_get(o, "r"))) jnum_u256(v, &t->r);
            if ((v = json_get(o, "s"))) jnum_u256(v, &t->s);
            tx_compute_hash(t);
        }
    }
    *n = arr->n;
    return txs;
}

/* ------------------------------------------------------------ result out */
static void dump_result(const result_t *r, fork_id fk, uint64_t block_number,
                        uint64_t block_timestamp, buf_t *o)
{
    uint64_t log_index = 0;
    json_wr_raw(o, "{\n \"stateRoot\": "); wr_hash_q(o, r->state_root);
    json_wr_raw(o, ",\n \"txRoot\": "); wr_hash_q(o, r->tx_root);
    json_wr_raw(o, ",\n \"receiptsRoot\": "); wr_hash_q(o, r->receipts_root);
    json_wr_raw(o, ",\n \"logsHash\": "); wr_hash_q(o, r->logs_hash);
    json_wr_raw(o, ",\n \"logsBloom\": "); wr_bytes_q(o, r->logs_bloom, 256);
    json_wr_raw(o, ",\n \"receipts\": [");
    for (size_t i = 0; i < r->nreceipts; i++) {
        const receipt_t *rc = &r->receipts[i];
        if (i) json_wr_raw(o, ",");
        json_wr_raw(o, "\n  {");
        if (rc->type) { json_wr_raw(o, "\n   \"type\": "); wr_u64_q(o, rc->type); json_wr_raw(o, ","); }
        /* The receipt carries both fields: `root` is the intermediate post-state root
         * before EIP-658 and empty after it, while `status` always reports the
         * outcome of the transaction. */
        if (rc->has_status) json_wr_raw(o, "\n   \"root\": \"0x\",\n   \"status\": ");
        else { json_wr_raw(o, "\n   \"root\": "); wr_hash_q(o, rc->post_root); json_wr_raw(o, ",\n   \"status\": "); }
        wr_u64_q(o, rc->status);
        json_wr_raw(o, ",\n   \"cumulativeGasUsed\": "); wr_u64_q(o, rc->cumulative_gas);
        json_wr_raw(o, ",\n   \"logsBloom\": "); wr_bytes_q(o, rc->bloom, 256);
        json_wr_raw(o, ",\n   \"logs\": ");
        if (rc->nlogs == 0) json_wr_raw(o, "null");
        else {
            json_wr_raw(o, "[");
            for (size_t j = 0; j < rc->nlogs; j++) {
                if (j) json_wr_raw(o, ",");
                json_wr_raw(o, "\n    {\"address\": ");
                wr_addr_q(o, rc->logs[j].addr);
                json_wr_raw(o, ", \"topics\": [");
                for (int k = 0; k < rc->logs[j].ntopics; k++) {
                    if (k) json_wr_raw(o, ", ");
                    wr_hash_q(o, rc->logs[j].topics[k]);
                }
                json_wr_raw(o, "], \"data\": ");
                wr_bytes_q(o, rc->logs[j].data, rc->logs[j].data_len);
                /* the tool fills the block envelope of every log from the environment and
                 * the receipt it belongs to */
                json_wr_raw(o, ", \"blockNumber\": "); wr_u64_q(o, block_number);
                json_wr_raw(o, ", \"transactionHash\": "); wr_hash_q(o, rc->tx_hash);
                json_wr_raw(o, ", \"transactionIndex\": "); wr_u64_q(o, rc->index);
                json_wr_raw(o, ", \"blockHash\": \"0x1337000000000000000000000000000000000000000000000000000000000000\"");
                json_wr_raw(o, ", \"blockTimestamp\": "); wr_u64_q(o, block_timestamp);
                json_wr_raw(o, ", \"logIndex\": "); wr_u64_q(o, log_index++);
                json_wr_raw(o, ", \"removed\": false}");
            }
            json_wr_raw(o, "\n   ]");
        }
        json_wr_raw(o, ",\n   \"transactionHash\": "); wr_hash_q(o, rc->tx_hash);
        json_wr_raw(o, ",\n   \"contractAddress\": "); wr_addr_q(o, rc->contract_address);
        json_wr_raw(o, ",\n   \"gasUsed\": "); wr_u64_q(o, rc->gas_used);
        /* The tool stamps every receipt with this fixed block hash and with the block
         * number from the environment; the expected outputs under the evm-tool testdata
         * directory show both. */
        json_wr_raw(o, ",\n   \"blockHash\": \"0x1337000000000000000000000000000000000000000000000000000000000000\"");
        json_wr_raw(o, ",\n   \"transactionIndex\": "); wr_u64_q(o, rc->index);
        json_wr_raw(o, ",\n   \"blockNumber\": "); wr_u64_q(o, block_number);
        /* the tool always leaves this one unset: it is derived from a block context that
         * t8n never builds */
        json_wr_raw(o, ",\n   \"effectiveGasPrice\": null");
        json_wr_raw(o, "\n  }");
    }
    json_wr_raw(o, "\n ]");
    if (r->nrejected) {
        json_wr_raw(o, ",\n \"rejected\": [");
        for (size_t i = 0; i < r->nrejected; i++) {
            if (i) json_wr_raw(o, ",");
            char tmp[64];
            snprintf(tmp, sizeof tmp, "\n  {\n   \"index\": %zu,\n   \"error\": ", r->rejected[i].index);
            json_wr_raw(o, tmp);
            json_wr_str(o, r->rejected[i].error);
            json_wr_raw(o, "\n  }");
        }
        json_wr_raw(o, "\n ]");
    }
    if (r->has_difficulty) { json_wr_raw(o, ",\n \"currentDifficulty\": "); wr_u256_q(o, r->difficulty); }
    else json_wr_raw(o, ",\n \"currentDifficulty\": null");
    json_wr_raw(o, ",\n \"gasUsed\": "); wr_u64_q(o, r->gas_used);
    if (r->has_base_fee) { json_wr_raw(o, ",\n \"currentBaseFee\": "); wr_u256_q(o, r->base_fee); }
    if (r->has_withdrawals_root) { json_wr_raw(o, ",\n \"withdrawalsRoot\": "); wr_hash_q(o, r->withdrawals_root); }
    if (r->has_blob_gas_used) { json_wr_raw(o, ",\n \"blobGasUsed\": "); wr_u64_q(o, r->blob_gas_used); }
    if (r->has_excess_blob_gas) { json_wr_raw(o, ",\n \"currentExcessBlobGas\": "); wr_u64_q(o, r->excess_blob_gas); }
    if (r->has_requests_hash) {
        json_wr_raw(o, ",\n \"requestsHash\": "); wr_hash_q(o, r->requests_hash);
        json_wr_raw(o, ",\n \"requests\": [");
        size_t printed = 0;
        for (size_t i = 0; i < r->nrequests; i++) {
            if (r->requests[i].len <= 1) continue;
            if (printed++) json_wr_raw(o, ",");
            json_wr_raw(o, "\n  ");
            wr_bytes_q(o, r->requests[i].data, r->requests[i].len);
        }
        json_wr_raw(o, "\n ]");
    } else {
        /* before EIP-7685 the tool still emits the key, with no list behind it */
        json_wr_raw(o, ",\n \"requests\": null");
    }
    json_wr_raw(o, "\n}");
    (void)fk;
}

/* ------------------------------------------------------------------- t8n */
static int cmd_t8n(int argc, char **argv)
{
    const char *in_alloc = getopt_str(argc, argv, "--input.alloc", "alloc.json");
    const char *in_env = getopt_str(argc, argv, "--input.env", "env.json");
    const char *in_txs = getopt_str(argc, argv, "--input.txs", "txs.json");
    const char *out_alloc = getopt_str(argc, argv, "--output.alloc", "alloc.json");
    const char *out_result = getopt_str(argc, argv, "--output.result", "result.json");
    const char *out_body = getopt_str(argc, argv, "--output.body", "");
    const char *basedir = getopt_str(argc, argv, "--output.basedir", "");
    const char *forkname = getopt_str(argc, argv, "--state.fork", "GrayGlacier");
    const char *chainid_s = getopt_str(argc, argv, "--state.chainid", "1");
    const char *reward_s = getopt_str(argc, argv, "--state.reward", "0");

    int fi = fork_by_name(forkname);
    if (fi < 0) die(ERR_CONFIG, "failed constructing chain configuration: unsupported fork %s", forkname);
    fork_id fk = (fork_id)fi;
    u256 chain_id = u256_from_u64((uint64_t)strtoull(chainid_s, NULL, 0));
    int64_t reward = (int64_t)strtoll(reward_s, NULL, 0);

    size_t n;
    char *alloc_txt = read_all(in_alloc, &n);
    if (!alloc_txt) die(ERR_IO, "failed reading alloc file %s", in_alloc);
    jval *alloc = json_parse(alloc_txt, n);
    if (!alloc) die(ERR_JSON, "failed unmarshalling alloc");

    char *env_txt = read_all(in_env, &n);
    if (!env_txt) die(ERR_IO, "failed reading env file %s", in_env);
    jval *envj = json_parse(env_txt, n);
    if (!envj) die(ERR_JSON, "failed unmarshalling env");

    env_t env;
    load_env(&env, envj, fk);

    state st;
    state_init(&st);
    load_alloc(&st, alloc);

    tx_t *txs = NULL;
    size_t ntxs = 0;
    {
        size_t tl;
        char *txt = read_all(in_txs, &tl);
        if (!txt) die(ERR_IO, "failed reading txs file %s", in_txs);
        size_t l = strlen(in_txs);
        bool is_rlp = (l > 4 && !strcmp(in_txs + l - 4, ".rlp"));
        jval *tj = json_parse(txt, tl);
        if (!tj) die(ERR_JSON, "failed unmarshalling txs");
        if (is_rlp || tj->type == JSTR) {
            buf_t b; buf_init(&b);
            if (jbytes(tj, &b) != 0) die(ERR_JSON, "bad tx rlp");
            txs = load_txs_rlp(b.data, b.len, &ntxs);
        } else {
            txs = load_txs_json(tj, &ntxs, chain_id, fk);
        }
    }

    /* recover senders; undecodable or unsignable transactions are rejected */
    result_t res;
    tx_t *good = (tx_t *)calloc(ntxs ? ntxs : 1, sizeof(tx_t));
    size_t ngood = 0;
    size_t *orig_index = (size_t *)calloc(ntxs ? ntxs : 1, sizeof(size_t));
    rejected_t *pre = (rejected_t *)calloc(ntxs ? ntxs : 1, sizeof(rejected_t));
    size_t npre = 0;
    for (size_t i = 0; i < ntxs; i++) {
        if (txs[i].type == 0xff) {
            pre[npre].index = i; pre[npre].error = strdup("could not decode transaction"); npre++;
            continue;
        }
        /* Recover the sender from the signature even for a transaction the tool signed
         * itself: signing follows the transaction's `protected` flag while recovery follows
         * the fork, so an EIP-155 signature handed to a fork older than Spurious Dragon is
         * produced and then rejected, exactly as the reference tool does it. */
        if (tx_recover(&txs[i], fk, chain_id) != 0) {
            pre[npre].index = i;
            pre[npre].error = strdup("invalid transaction v, r, s values");
            npre++;
            continue;
        }
        tx_compute_hash(&txs[i]);
        good[ngood] = txs[i];
        orig_index[ngood] = i;
        ngood++;
    }

    int rc = apply_block(&st, &env, good, ngood, fk, chain_id, reward, &res);
    if (rc == 4) die(ERR_MISSING_BLOCKHASH, "getHash invoked, blockhash for that block not provided");

    /* merge pre-rejections, renumbering to the original transaction order */
    if (npre) {
        rejected_t *all = (rejected_t *)calloc(npre + res.nrejected, sizeof(rejected_t));
        size_t k = 0;
        for (size_t i = 0; i < res.nrejected; i++) {
            all[k].index = orig_index[res.rejected[i].index];
            all[k].error = res.rejected[i].error;
            k++;
        }
        for (size_t i = 0; i < npre; i++) all[k++] = pre[i];
        for (size_t x = 0; x < k; x++)
            for (size_t y = x + 1; y < k; y++)
                if (all[y].index < all[x].index) { rejected_t t = all[x]; all[x] = all[y]; all[y] = t; }
        free(res.rejected);
        res.rejected = all;
        res.nrejected = k;
    } else {
        for (size_t i = 0; i < res.nrejected; i++) res.rejected[i].index = orig_index[res.rejected[i].index];
    }

    buf_t ro, ao, bo;
    buf_init(&ro); buf_init(&ao); buf_init(&bo);
    dump_result(&res, fk, env.number, env.timestamp, &ro);
    dump_alloc(&st, &ao);
    if (*out_body) { buf_t hex; buf_init(&hex); wr_bytes_q(&hex, res.body.data, res.body.len); buf_put(&bo, hex.data, hex.len); buf_free(&hex); }

    /* Whatever subset of the outputs is aimed at stdout goes there as one object keyed by
     * output name — the tool does this for any subset, not only for all of them. */
    bool r_std = !strcmp(out_result, "stdout");
    bool a_std = !strcmp(out_alloc, "stdout");
    bool b_std = *out_body && !strcmp(out_body, "stdout");
    if (r_std || a_std || b_std) {
        buf_t all; buf_init(&all);
        json_wr_raw(&all, "{");
        bool first = true;
        if (a_std) {
            json_wr_raw(&all, "\n \"alloc\": "); buf_put(&all, ao.data, ao.len); first = false;
        }
        if (r_std) {
            json_wr_raw(&all, first ? "\n \"result\": " : ",\n \"result\": ");
            buf_put(&all, ro.data, ro.len); first = false;
        }
        if (b_std) {
            json_wr_raw(&all, first ? "\n \"body\": " : ",\n \"body\": ");
            buf_put(&all, bo.data, bo.len); first = false;
        }
        json_wr_raw(&all, "\n}");
        fwrite(all.data, 1, all.len, stdout);
        fputc('\n', stdout);
        buf_free(&all);
    }
    if (!r_std) write_out(basedir, out_result, &ro);
    if (!a_std) write_out(basedir, out_alloc, &ao);
    if (*out_body && !b_std) write_out(basedir, out_body, &bo);
    return 0;
}

/* true when a * b would need more than 256 bits */
static bool mul_over256(u256 a, uint64_t b)
{
    if (u256_is_zero(a) || b == 0) return false;
    u256 max; memset(max.v, 0xff, 32);
    u256 q = u256_div(max, u256_from_u64(b));
    return u256_cmp(a, q) > 0;
}

/* ------------------------------------------------------------------- t9n */
static int cmd_t9n(int argc, char **argv)
{
    const char *in_txs = getopt_str(argc, argv, "--input.txs", "txs.json");
    const char *forkname = getopt_str(argc, argv, "--state.fork", "GrayGlacier");
    const char *chainid_s = getopt_str(argc, argv, "--state.chainid", "1");
    int fi = fork_by_name(forkname);
    if (fi < 0) die(ERR_CONFIG, "failed constructing chain configuration: unsupported fork %s", forkname);
    fork_id fk = (fork_id)fi;
    u256 chain_id = u256_from_u64((uint64_t)strtoull(chainid_s, NULL, 0));

    size_t tl;
    char *txt = read_all(in_txs, &tl);
    if (!txt) die(ERR_IO, "failed reading txs file %s", in_txs);
    jval *tj = json_parse(txt, tl);
    if (!tj) die(ERR_JSON, "failed unmarshalling txs");
    buf_t b; buf_init(&b);
    if (jbytes(tj, &b) != 0) die(ERR_IO, "only rlp supported");
    size_t ntxs = 0;
    tx_t *txs = load_txs_rlp(b.data, b.len, &ntxs);

    buf_t o; buf_init(&o);
    json_wr_raw(&o, "[");
    for (size_t i = 0; i < ntxs; i++) {
        if (i) json_wr_raw(&o, ",");
        json_wr_raw(&o, "\n  {");
        tx_t *t = &txs[i];
        const char *err = NULL;
        bool have_hash = false, have_addr = false;
        uint64_t igas = 0;
        if (t->type == 0xff) {
            err = "could not decode transaction";
        } else {
            tx_compute_hash(t);
            have_hash = true;
            if (t->type > 0 && !IS_BERLIN(fk)) err = "transaction type not supported";
            else if (t->type >= 2 && !IS_LONDON(fk)) err = "transaction type not supported";
            else if (t->type >= 3 && !IS_CANCUN(fk)) err = "transaction type not supported";
            else if (t->type >= 4 && !IS_PRAGUE(fk)) err = "transaction type not supported";
            else if (tx_recover(t, fk, chain_id) != 0) err = "invalid transaction v, r, s values";
            else {
                have_addr = true;
                uint64_t floor = 0;
                igas = tx_intrinsic_gas(t, fk, &floor);
                if (t->over256) err = "value exceeds 256 bits";
                else if (t->gas < igas) err = "intrinsic gas too low";
                else if (IS_PRAGUE(fk) && t->gas < floor) err = "insufficient gas for floor data gas cost";
                else if (t->nonce == UINT64_MAX) err = "nonce exceeds 2^64-1";
                else if (u256_cmp(t->max_fee, t->max_prio) < 0) err = "maxFeePerGas < maxPriorityFeePerGas";
                else if (mul_over256(t->gas_price, t->gas)) err = "gas * gasPrice exceeds 256 bits";
                else if (mul_over256(t->max_fee, t->gas)) err = "gas * maxFeePerGas exceeds 256 bits";
                else if (IS_SHANGHAI(fk) && !t->has_to && t->data_len > MAX_INITCODE) err = "max initcode size exceeded";
                else if (IS_OSAKA(fk) && t->gas > (1ULL << 24)) err = "gas limit exceeds maximum";
            }
        }
        bool first = true;
        if (err) { json_wr_raw(&o, "\n    \"error\": "); json_wr_str(&o, err); first = false; }
        if (have_addr) {
            if (!first) json_wr_raw(&o, ",");
            json_wr_raw(&o, "\n    \"address\": "); wr_addr_q(&o, t->sender);
            first = false;
        }
        if (have_hash) {
            if (!first) json_wr_raw(&o, ",");
            json_wr_raw(&o, "\n    \"hash\": "); wr_hash_q(&o, t->hash);
            first = false;
        }
        if (igas) {
            if (!first) json_wr_raw(&o, ",");
            json_wr_raw(&o, "\n    \"intrinsicGas\": "); wr_u64_q(&o, igas);
        }
        json_wr_raw(&o, "\n  }");
    }
    json_wr_raw(&o, "\n]");
    fwrite(o.data, 1, o.len, stdout);
    fputc('\n', stdout);
    return 0;
}

/* ------------------------------------------------------------------ b11r */
static const hash256 EMPTY_UNCLE_HASH = {{0x1d,0xcc,0x4d,0xe8,0xde,0xc7,0x5d,0x7a,0xab,0x85,0xb5,0x67,0xb6,0xcc,0xd4,0x1a,
                                          0xd3,0x12,0x45,0x1b,0x94,0x8a,0x74,0x13,0xf0,0xa1,0x42,0xfd,0x40,0xd4,0x93,0x47}};

static int cmd_b11r(int argc, char **argv)
{
    const char *in_header = getopt_str(argc, argv, "--input.header", "header.json");
    const char *in_ommers = getopt_str(argc, argv, "--input.ommers", "");
    const char *in_withdrawals = getopt_str(argc, argv, "--input.withdrawals", "");
    const char *in_txs = getopt_str(argc, argv, "--input.txs", "txs.rlp");
    const char *out_block = getopt_str(argc, argv, "--output.block", "block.json");
    const char *basedir = getopt_str(argc, argv, "--output.basedir", "");

    size_t n;
    char *ht = read_all(in_header, &n);
    if (!ht) die(ERR_IO, "failed reading header file %s", in_header);
    jval *h = json_parse(ht, n);
    if (!h) die(ERR_JSON, "failed unmarshalling header");

    /* ommers */
    buf_t ommer_list; buf_init(&ommer_list);
    size_t nommers = 0;
    if (*in_ommers) {
        char *ot = read_all(in_ommers, &n);
        if (ot) {
            jval *oj = json_parse(ot, n);
            if (oj && oj->type == JARR) {
                nommers = oj->n;
                for (size_t i = 0; i < oj->n; i++) {
                    buf_t b; buf_init(&b);
                    jbytes(oj->items[i], &b);
                    /* The tool's --help calls these "ommer header RLPs", but it decodes
                     * each entry as a block and keeps that block's header.  Accept the
                     * block form it actually reads, and a bare header as well. */
                    rlp_item it, first;
                    const uint8_t *hdr = b.data;
                    size_t hdr_len = b.len;
                    if (rlp_decode(b.data, b.len, &it) == 0 && it.is_list) {
                        rlp_iter ii;
                        rlp_iter_init(&ii, &it);
                        if (rlp_iter_next(&ii, &first) == 0 && first.is_list) {
                            hdr = first.payload - (first.total_len - first.payload_len);
                            hdr_len = first.total_len;
                        }
                    }
                    buf_put(&ommer_list, hdr, hdr_len);
                    buf_free(&b);
                }
            }
        }
    }
    buf_t ommers_rlp; buf_init(&ommers_rlp);
    rlp_put_list(&ommers_rlp, ommer_list.data, ommer_list.len);
    hash256 uncle_hash;
    keccak256(ommers_rlp.data, ommers_rlp.len, uncle_hash.b);

    /* transactions */
    buf_t txs_payload; buf_init(&txs_payload);
    size_t ntxs = 0;
    buf_t *tx_encs = NULL;
    if (*in_txs) {
        char *tt = read_all(in_txs, &n);
        if (tt) {
            jval *tj = json_parse(tt, n);
            if (tj) {
                buf_t b; buf_init(&b);
                if (jbytes(tj, &b) == 0 && b.len) {
                    rlp_item top;
                    if (rlp_decode(b.data, b.len, &top) == 0 && top.is_list) {
                        rlp_iter it; rlp_item e;
                        rlp_iter_init(&it, &top);
                        size_t cap = 8;
                        tx_encs = (buf_t *)calloc(cap, sizeof(buf_t));
                        while (!rlp_iter_done(&it)) {
                            if (rlp_iter_next(&it, &e) != 0) break;
                            if (ntxs == cap) { cap *= 2; tx_encs = (buf_t *)realloc(tx_encs, cap * sizeof(buf_t)); }
                            buf_init(&tx_encs[ntxs]);
                            if (e.is_list) buf_put(&tx_encs[ntxs], e.payload - (e.total_len - e.payload_len), e.total_len);
                            else buf_put(&tx_encs[ntxs], e.payload, e.payload_len);
                            ntxs++;
                        }
                        buf_put(&txs_payload, b.data + (b.len - top.payload_len), top.payload_len);
                    }
                }
            }
        }
    }
    hash256 tx_root;
    trie_root_list(tx_encs, ntxs, &tx_root);

    /* withdrawals */
    buf_t wpayload; buf_init(&wpayload);
    bool have_withdrawals = false;
    hash256 wroot = EMPTY_TRIE_ROOT;
    if (*in_withdrawals) {
        char *wt = read_all(in_withdrawals, &n);
        if (wt) {
            jval *wj = json_parse(wt, n);
            if (wj && wj->type == JARR) {
                have_withdrawals = true;
                buf_t *wl = (buf_t *)calloc(wj->n ? wj->n : 1, sizeof(buf_t));
                for (size_t i = 0; i < wj->n; i++) {
                    jval *w = wj->items[i];
                    uint64_t idx = 0, vi = 0;
                    address ad; u256 am;
                    jnum_u64(json_get(w, "index"), &idx);
                    jnum_u64(json_get(w, "validatorIndex"), &vi);
                    jaddr(json_get(w, "address"), &ad);
                    jnum_u256(json_get(w, "amount"), &am);
                    buf_t pl; buf_init(&pl);
                    rlp_put_u64(&pl, idx);
                    rlp_put_u64(&pl, vi);
                    rlp_put_bytes(&pl, ad.b, 20);
                    rlp_put_u256(&pl, am);
                    buf_init(&wl[i]);
                    rlp_put_list(&wl[i], pl.data, pl.len);
                    buf_put(&wpayload, wl[i].data, wl[i].len);
                    buf_free(&pl);
                }
                trie_root_list(wl, wj->n, &wroot);
                free(wl);
            }
        }
    }

    /* header */
    buf_t hp; buf_init(&hp);
    hash256 t32; address a20; u256 u; uint64_t q; buf_t bb;
    jval *v;
#define H_HASH(name, dflt) do { if ((v = json_get(h, name)) && v->type != JNULL) jhash(v, &t32); else t32 = (dflt); rlp_put_bytes(&hp, t32.b, 32); } while (0)
    H_HASH("parentHash", hash_zero());
    if ((v = json_get(h, "sha3Uncles")) && v->type != JNULL) jhash(v, &t32);
    else t32 = nommers ? uncle_hash : EMPTY_UNCLE_HASH;
    rlp_put_bytes(&hp, t32.b, 32);
    if ((v = json_get(h, "miner")) && v->type != JNULL) jaddr(v, &a20); else a20 = addr_zero();
    rlp_put_bytes(&hp, a20.b, 20);
    H_HASH("stateRoot", hash_zero());
    if ((v = json_get(h, "transactionsRoot")) && v->type != JNULL) jhash(v, &t32); else t32 = tx_root;
    rlp_put_bytes(&hp, t32.b, 32);
    H_HASH("receiptsRoot", EMPTY_TRIE_ROOT);
    buf_init(&bb);
    if ((v = json_get(h, "logsBloom")) && v->type != JNULL) jbytes(v, &bb);
    while (bb.len < 256) buf_putc(&bb, 0);
    rlp_put_bytes(&hp, bb.data, 256);
    buf_free(&bb);
    if ((v = json_get(h, "difficulty")) && v->type != JNULL) jnum_u256(v, &u); else u = U256_ZERO;
    rlp_put_u256(&hp, u);
    if ((v = json_get(h, "number")) && v->type != JNULL) jnum_u256(v, &u); else u = U256_ZERO;
    rlp_put_u256(&hp, u);
    if ((v = json_get(h, "gasLimit")) && v->type != JNULL) jnum_u64(v, &q); else q = 0;
    rlp_put_u64(&hp, q);
    if ((v = json_get(h, "gasUsed")) && v->type != JNULL) jnum_u64(v, &q); else q = 0;
    rlp_put_u64(&hp, q);
    if ((v = json_get(h, "timestamp")) && v->type != JNULL) jnum_u64(v, &q); else q = 0;
    rlp_put_u64(&hp, q);
    buf_init(&bb);
    if ((v = json_get(h, "extraData")) && v->type != JNULL) jbytes(v, &bb);
    rlp_put_bytes(&hp, bb.data, bb.len);
    buf_free(&bb);
    H_HASH("mixHash", hash_zero());
    buf_init(&bb);
    if ((v = json_get(h, "nonce")) && v->type != JNULL) jbytes(v, &bb);
    while (bb.len < 8) buf_putc(&bb, 0);
    rlp_put_bytes(&hp, bb.data, 8);
    buf_free(&bb);
    if ((v = json_get(h, "baseFeePerGas")) && v->type != JNULL) { jnum_u256(v, &u); rlp_put_u256(&hp, u); }
    if (have_withdrawals || ((v = json_get(h, "withdrawalsRoot")) && v->type != JNULL)) {
        if ((v = json_get(h, "withdrawalsRoot")) && v->type != JNULL) jhash(v, &t32); else t32 = wroot;
        rlp_put_bytes(&hp, t32.b, 32);
    }
    if ((v = json_get(h, "blobGasUsed")) && v->type != JNULL) { jnum_u64(v, &q); rlp_put_u64(&hp, q); }
    if ((v = json_get(h, "excessBlobGas")) && v->type != JNULL) { jnum_u64(v, &q); rlp_put_u64(&hp, q); }
    if ((v = json_get(h, "parentBeaconBlockRoot")) && v->type != JNULL) { jhash(v, &t32); rlp_put_bytes(&hp, t32.b, 32); }
    if ((v = json_get(h, "requestsHash")) && v->type != JNULL) { jhash(v, &t32); rlp_put_bytes(&hp, t32.b, 32); }
#undef H_HASH

    buf_t header_rlp; buf_init(&header_rlp);
    rlp_put_list(&header_rlp, hp.data, hp.len);
    hash256 bh;
    keccak256(header_rlp.data, header_rlp.len, bh.b);

    buf_t body; buf_init(&body);
    buf_put(&body, header_rlp.data, header_rlp.len);
    rlp_put_list(&body, txs_payload.data, txs_payload.len);
    rlp_put_list(&body, ommer_list.data, ommer_list.len);
    if (have_withdrawals) rlp_put_list(&body, wpayload.data, wpayload.len);
    buf_t block; buf_init(&block);
    rlp_put_list(&block, body.data, body.len);

    buf_t o; buf_init(&o);
    json_wr_raw(&o, "{\n \"rlp\": ");
    wr_bytes_q(&o, block.data, block.len);
    json_wr_raw(&o, ",\n \"hash\": ");
    wr_hash_q(&o, bh);
    json_wr_raw(&o, "\n}");
    write_out(basedir, out_block, &o);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: evm <t8n|t9n|b11r|--version>\n"); return 1; }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("evm version 1.16.6-stable\n"); return 0; }
    }
    const char *cmd = NULL;
    int ci = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') { cmd = argv[i]; ci = i; break; }
    }
    if (!cmd) { fprintf(stderr, "no subcommand\n"); return 1; }
    if (!strcmp(cmd, "t8n")) return cmd_t8n(argc - ci, argv + ci);
    if (!strcmp(cmd, "t9n")) return cmd_t9n(argc - ci, argv + ci);
    if (!strcmp(cmd, "b11r")) return cmd_b11r(argc - ci, argv + ci);
    fprintf(stderr, "unknown subcommand %s\n", cmd);
    return 1;
}
