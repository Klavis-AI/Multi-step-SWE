#include "rlp.h"

static int be_len(uint64_t v) { int n = 0; while (v) { n++; v >>= 8; } return n; }

void rlp_put_header(buf_t *out, size_t len, uint8_t short_off, uint8_t long_off)
{
    if (len < 56) {
        buf_putc(out, (uint8_t)(short_off + len));
    } else {
        int n = be_len((uint64_t)len);
        buf_putc(out, (uint8_t)(long_off + n));
        for (int i = n - 1; i >= 0; i--) buf_putc(out, (uint8_t)(len >> (8 * i)));
    }
}

void rlp_put_bytes(buf_t *out, const uint8_t *b, size_t len)
{
    if (len == 1 && b[0] < 0x80) { buf_putc(out, b[0]); return; }
    rlp_put_header(out, len, 0x80, 0xb7);
    buf_put(out, b, len);
}

void rlp_put_u64(buf_t *out, uint64_t v)
{
    uint8_t tmp[8];
    int n = be_len(v);
    for (int i = 0; i < n; i++) tmp[i] = (uint8_t)(v >> (8 * (n - 1 - i)));
    rlp_put_bytes(out, tmp, (size_t)n);
}

void rlp_put_u256(buf_t *out, u256 v)
{
    uint8_t be[32];
    u256_to_be(&v, be);
    int i = 0;
    while (i < 32 && be[i] == 0) i++;
    rlp_put_bytes(out, be + i, (size_t)(32 - i));
}

void rlp_put_list(buf_t *out, const uint8_t *payload, size_t len)
{
    rlp_put_header(out, len, 0xc0, 0xf7);
    buf_put(out, payload, len);
}

int rlp_decode(const uint8_t *p, size_t len, rlp_item *out)
{
    if (len == 0) return -1;
    uint8_t b0 = p[0];
    if (b0 < 0x80) {
        out->is_list = false; out->payload = p; out->payload_len = 1; out->total_len = 1;
        return 0;
    }
    if (b0 <= 0xb7) {
        size_t l = b0 - 0x80;
        if (len < 1 + l) return -1;
        if (l == 1 && p[1] < 0x80) return -1;              /* non-canonical single byte */
        out->is_list = false; out->payload = p + 1; out->payload_len = l; out->total_len = 1 + l;
        return 0;
    }
    if (b0 <= 0xbf) {
        size_t nl = b0 - 0xb7;
        if (len < 1 + nl || nl > 8) return -1;
        if (p[1] == 0) return -1;                          /* leading zero in length */
        size_t l = 0;
        for (size_t i = 0; i < nl; i++) l = (l << 8) | p[1 + i];
        if (l < 56) return -1;                             /* should have used short form */
        if (len < 1 + nl + l) return -1;
        out->is_list = false; out->payload = p + 1 + nl; out->payload_len = l; out->total_len = 1 + nl + l;
        return 0;
    }
    if (b0 <= 0xf7) {
        size_t l = b0 - 0xc0;
        if (len < 1 + l) return -1;
        out->is_list = true; out->payload = p + 1; out->payload_len = l; out->total_len = 1 + l;
        return 0;
    }
    {
        size_t nl = b0 - 0xf7;
        if (len < 1 + nl || nl > 8) return -1;
        if (p[1] == 0) return -1;
        size_t l = 0;
        for (size_t i = 0; i < nl; i++) l = (l << 8) | p[1 + i];
        if (l < 56) return -1;
        if (len < 1 + nl + l) return -1;
        out->is_list = true; out->payload = p + 1 + nl; out->payload_len = l; out->total_len = 1 + nl + l;
        return 0;
    }
}

int rlp_iter_next(rlp_iter *it, rlp_item *out)
{
    if (it->left == 0) return -1;
    if (rlp_decode(it->p, it->left, out) != 0) return -1;
    it->p += out->total_len;
    it->left -= out->total_len;
    return 0;
}

int rlp_as_u64(const rlp_item *it, uint64_t *out)
{
    if (it->is_list || it->payload_len > 8) return -1;
    if (it->payload_len > 1 && it->payload[0] == 0) return -1;
    if (it->payload_len == 1 && it->payload[0] == 0) return -1;   /* zero must be empty string */
    uint64_t v = 0;
    for (size_t i = 0; i < it->payload_len; i++) v = (v << 8) | it->payload[i];
    *out = v;
    return 0;
}

int rlp_as_u256(const rlp_item *it, u256 *out)
{
    if (it->is_list || it->payload_len > 32) return -1;
    if (it->payload_len > 0 && it->payload[0] == 0) return -1;
    u256_from_be(out, it->payload, it->payload_len);
    return 0;
}

int rlp_count(const rlp_item *list)
{
    if (!list->is_list) return -1;
    rlp_iter it; rlp_item x; int n = 0;
    rlp_iter_init(&it, list);
    while (!rlp_iter_done(&it)) {
        if (rlp_iter_next(&it, &x) != 0) return -1;
        n++;
    }
    return n;
}
