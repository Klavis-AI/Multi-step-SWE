"""smbprobe -- a raw SMB1/SMB2/SMB3 protocol client for benchmark measurement.

Python 3.11, standard library only (socket, struct, hashlib, hmac, os, time).
Everything else -- AES-128, CMAC, GCM/GMAC, CCM, MD4, RC4, SPNEGO DER,
NTLMv2 -- is implemented here from the Microsoft Open Specifications.

Design rules:
  * Every request method returns a plain ``dict`` that always carries at least
    ``status`` (NTSTATUS int) and ``raw`` (the response PDU bytes), plus the
    decoded fields of that response named exactly as the spec names them.
    Header and body fields share one flat dict, so the two body fields whose
    spec name collides with an SMB2 header field are renamed: the CLOSE
    response's Flags is ``CloseFlags`` and the IOCTL response's Flags is
    ``IoctlFlags``; ``Flags`` is always the 64-byte header's.
  * A protocol error status is *data*, never an exception.  Only socket,
    timeout and parse failures raise :class:`SmbProbeError`.
"""

import hashlib
import hmac
import os
import socket
import struct
import time

__all__ = ["SmbClient", "SmbProbeError"]


class SmbProbeError(Exception):
    """Raised on socket, timeout or PDU-parse failure -- never on an NTSTATUS."""


# ---------------------------------------------------------------------------
# AES-128 (encrypt direction only), FIPS-197
# ---------------------------------------------------------------------------

def _aes_build_sbox():
    sbox = [0] * 256
    p = q = 1
    while True:
        p = (p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)) & 0xFF
        q ^= (q << 1) & 0xFF
        q ^= (q << 2) & 0xFF
        q ^= (q << 4) & 0xFF
        if q & 0x80:
            q ^= 0x09
        q &= 0xFF
        x = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) \
              ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4))
        sbox[p] = (x ^ 0x63) & 0xFF
        if p == 1:
            break
    sbox[0] = 0x63
    return sbox


_SBOX = _aes_build_sbox()


def _xtime(a):
    a <<= 1
    return (a ^ 0x1B) & 0xFF if a & 0x100 else a


_TE0 = []
for _s in _SBOX:
    _s2 = _xtime(_s)
    _s3 = _s2 ^ _s
    _TE0.append((_s2 << 24) | (_s << 16) | (_s << 8) | _s3)
_TE1 = [((v >> 8) | (v << 24)) & 0xFFFFFFFF for v in _TE0]
_TE2 = [((v >> 16) | (v << 16)) & 0xFFFFFFFF for v in _TE0]
_TE3 = [((v >> 24) | (v << 8)) & 0xFFFFFFFF for v in _TE0]
_RCON = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36]


class AES128:
    """AES-128 block cipher, encrypt direction (FIPS-197). 16-byte key."""

    __slots__ = ("rk",)

    def __init__(self, key):
        if len(key) != 16:
            raise SmbProbeError("AES128 requires a 16-byte key, got %d" % len(key))
        w = list(struct.unpack(">4I", key))
        for i in range(4, 44):
            t = w[i - 1]
            if i % 4 == 0:
                t = ((t << 8) | (t >> 24)) & 0xFFFFFFFF
                t = (_SBOX[(t >> 24) & 0xFF] << 24) | (_SBOX[(t >> 16) & 0xFF] << 16) \
                    | (_SBOX[(t >> 8) & 0xFF] << 8) | _SBOX[t & 0xFF]
                t ^= _RCON[i // 4 - 1] << 24
            w.append(w[i - 4] ^ t)
        self.rk = w

    def encrypt_block(self, block):
        """Encrypt one 16-byte block."""
        rk = self.rk
        s0, s1, s2, s3 = struct.unpack(">4I", block)
        s0 ^= rk[0]; s1 ^= rk[1]; s2 ^= rk[2]; s3 ^= rk[3]
        k = 4
        for _ in range(9):
            t0 = _TE0[(s0 >> 24) & 0xFF] ^ _TE1[(s1 >> 16) & 0xFF] \
                 ^ _TE2[(s2 >> 8) & 0xFF] ^ _TE3[s3 & 0xFF] ^ rk[k]
            t1 = _TE0[(s1 >> 24) & 0xFF] ^ _TE1[(s2 >> 16) & 0xFF] \
                 ^ _TE2[(s3 >> 8) & 0xFF] ^ _TE3[s0 & 0xFF] ^ rk[k + 1]
            t2 = _TE0[(s2 >> 24) & 0xFF] ^ _TE1[(s3 >> 16) & 0xFF] \
                 ^ _TE2[(s0 >> 8) & 0xFF] ^ _TE3[s1 & 0xFF] ^ rk[k + 2]
            t3 = _TE0[(s3 >> 24) & 0xFF] ^ _TE1[(s0 >> 16) & 0xFF] \
                 ^ _TE2[(s1 >> 8) & 0xFF] ^ _TE3[s2 & 0xFF] ^ rk[k + 3]
            s0, s1, s2, s3 = t0, t1, t2, t3
            k += 4
        sb = _SBOX
        o0 = ((sb[(s0 >> 24) & 0xFF] << 24) | (sb[(s1 >> 16) & 0xFF] << 16)
              | (sb[(s2 >> 8) & 0xFF] << 8) | sb[s3 & 0xFF]) ^ rk[40]
        o1 = ((sb[(s1 >> 24) & 0xFF] << 24) | (sb[(s2 >> 16) & 0xFF] << 16)
              | (sb[(s3 >> 8) & 0xFF] << 8) | sb[s0 & 0xFF]) ^ rk[41]
        o2 = ((sb[(s2 >> 24) & 0xFF] << 24) | (sb[(s3 >> 16) & 0xFF] << 16)
              | (sb[(s0 >> 8) & 0xFF] << 8) | sb[s1 & 0xFF]) ^ rk[42]
        o3 = ((sb[(s3 >> 24) & 0xFF] << 24) | (sb[(s0 >> 16) & 0xFF] << 16)
              | (sb[(s1 >> 8) & 0xFF] << 8) | sb[s2 & 0xFF]) ^ rk[43]
        return struct.pack(">4I", o0, o1, o2, o3)


def _xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


# ---------------------------------------------------------------------------
# AES-CMAC (RFC 4493) -- SMB 3.0/3.0.2 signing
# ---------------------------------------------------------------------------

def aes_cmac(key, msg):
    """AES-128-CMAC per RFC 4493; returns the 16-byte MAC."""
    aes = AES128(key)

    def dbl(b):
        v = int.from_bytes(b, "big") << 1
        if b[0] & 0x80:
            v ^= 0x87
        return (v & ((1 << 128) - 1)).to_bytes(16, "big")

    k1 = dbl(aes.encrypt_block(b"\x00" * 16))
    k2 = dbl(k1)
    n = (len(msg) + 15) // 16
    if n == 0:
        last = _xor(b"\x80" + b"\x00" * 15, k2)
        n = 1
        body = b""
    else:
        body = msg[: (n - 1) * 16]
        tail = msg[(n - 1) * 16:]
        if len(tail) == 16:
            last = _xor(tail, k1)
        else:
            last = _xor(tail + b"\x80" + b"\x00" * (15 - len(tail)), k2)
    x = b"\x00" * 16
    for i in range(n - 1):
        x = aes.encrypt_block(_xor(x, body[i * 16:(i + 1) * 16]))
    return aes.encrypt_block(_xor(x, last))


# ---------------------------------------------------------------------------
# AES-128-GCM / GMAC (NIST SP800-38D) -- SMB 3.1.1 encryption and signing
# ---------------------------------------------------------------------------

class _GHash:
    """GHASH over GF(2^128) using a 4-bit windowed multiplication table."""

    __slots__ = ("_m", "_y")

    def __init__(self, h):
        hi, lo = struct.unpack(">QQ", h)
        m = [(0, 0)] * 16
        m[8] = (hi, lo)
        i = 4
        while i:
            vh, vl = m[i * 2]
            lsb = vl & 1
            vl = ((vl >> 1) | ((vh & 1) << 63)) & 0xFFFFFFFFFFFFFFFF
            vh >>= 1
            if lsb:
                vh ^= 0xE100000000000000
            m[i] = (vh, vl)
            i >>= 1
        i = 2
        while i < 16:
            for j in range(1, i):
                m[i + j] = (m[i][0] ^ m[j][0], m[i][1] ^ m[j][1])
            i *= 2
        self._m = m
        self._y = (0, 0)

    def _mul(self, xh, xl):
        m = self._m
        zh = zl = 0
        for i in range(32):
            nib = (xl >> (i * 4)) & 0xF if i < 16 else (xh >> ((i - 16) * 4)) & 0xF
            for _ in range(4):
                lsb = zl & 1
                zl = (zl >> 1) | ((zh & 1) << 63)
                zh >>= 1
                if lsb:
                    zh ^= 0xE100000000000000
            mh, ml = m[nib]
            zh ^= mh
            zl ^= ml
        return zh, zl

    def update(self, data):
        yh, yl = self._y
        for off in range(0, len(data), 16):
            blk = data[off:off + 16]
            if len(blk) < 16:
                blk = blk + b"\x00" * (16 - len(blk))
            bh, bl = struct.unpack(">QQ", blk)
            self._y = (yh ^ bh, yl ^ bl)
            yh, yl = self._mul(*self._y)
        self._y = (yh, yl)

    def digest(self):
        return struct.pack(">QQ", *self._y)


def _gcm_core(key, iv, aad, data, encrypt):
    aes = AES128(key)
    h = aes.encrypt_block(b"\x00" * 16)
    if len(iv) == 12:
        j0 = iv + b"\x00\x00\x00\x01"
    else:
        g = _GHash(h)
        g.update(iv + b"\x00" * ((-len(iv)) % 16))
        g.update(struct.pack(">QQ", 0, len(iv) * 8))
        j0 = g.digest()
    ctr = int.from_bytes(j0[12:], "big")
    prefix = j0[:12]
    out = bytearray()
    for off in range(0, len(data), 16):
        ctr = (ctr + 1) & 0xFFFFFFFF
        ks = aes.encrypt_block(prefix + struct.pack(">I", ctr))
        chunk = data[off:off + 16]
        out += bytes(x ^ y for x, y in zip(chunk, ks))
    out = bytes(out)
    ct = out if encrypt else data
    g = _GHash(h)
    g.update(aad + b"\x00" * ((-len(aad)) % 16))
    g.update(ct + b"\x00" * ((-len(ct)) % 16))
    g.update(struct.pack(">QQ", len(aad) * 8, len(ct) * 8))
    tag = _xor(g.digest(), aes.encrypt_block(j0))
    return out, tag


def aes_gcm_encrypt(key, nonce, plaintext, aad):
    """AES-128-GCM seal; returns (ciphertext, 16-byte tag)."""
    return _gcm_core(key, nonce, aad, plaintext, True)


def aes_gcm_decrypt(key, nonce, ciphertext, aad, tag):
    """AES-128-GCM open; returns the plaintext or raises on tag mismatch."""
    pt, calc = _gcm_core(key, nonce, aad, ciphertext, False)
    if not hmac.compare_digest(calc, tag):
        raise SmbProbeError("AES-GCM authentication tag mismatch")
    return pt


def aes_gmac(key, nonce, msg):
    """AES-128-GMAC: GCM with empty plaintext and the message as AAD."""
    return _gcm_core(key, nonce, msg, b"", True)[1]


# ---------------------------------------------------------------------------
# AES-128-CCM (RFC 3610) -- SMB 3.0/3.0.2/3.1.1 encryption
# ---------------------------------------------------------------------------

def _ccm_mac(aes, nonce, aad, data, tlen=16):
    lval = 15 - len(nonce)
    flags = (0x40 if aad else 0) | (((tlen - 2) // 2) << 3) | (lval - 1)
    b0 = bytes([flags]) + nonce + len(data).to_bytes(lval, "big")
    x = aes.encrypt_block(b0)
    if aad:
        if len(aad) < 0xFF00:
            enc = struct.pack(">H", len(aad)) + aad
        else:
            enc = b"\xff\xfe" + struct.pack(">I", len(aad)) + aad
        enc += b"\x00" * ((-len(enc)) % 16)
        for off in range(0, len(enc), 16):
            x = aes.encrypt_block(_xor(x, enc[off:off + 16]))
    pad = data + b"\x00" * ((-len(data)) % 16)
    for off in range(0, len(pad), 16):
        x = aes.encrypt_block(_xor(x, pad[off:off + 16]))
    return x[:tlen]


def _ccm_ctr(aes, nonce, data, t):
    lval = 15 - len(nonce)
    a0 = bytes([lval - 1]) + nonce + b"\x00" * lval
    s0 = aes.encrypt_block(a0)
    tag = _xor(t, s0[:len(t)])
    out = bytearray()
    for i in range(0, len(data), 16):
        blk = bytes([lval - 1]) + nonce + ((i // 16) + 1).to_bytes(lval, "big")
        ks = aes.encrypt_block(blk)
        out += bytes(x ^ y for x, y in zip(data[i:i + 16], ks))
    return bytes(out), tag


def aes_ccm_encrypt(key, nonce, plaintext, aad, tlen=16):
    """AES-128-CCM seal per RFC 3610; returns (ciphertext, tag)."""
    aes = AES128(key)
    t = _ccm_mac(aes, nonce, aad, plaintext, tlen)
    return _ccm_ctr(aes, nonce, plaintext, t)


def aes_ccm_decrypt(key, nonce, ciphertext, aad, tag, tlen=16):
    """AES-128-CCM open per RFC 3610; returns plaintext or raises."""
    aes = AES128(key)
    lval = 15 - len(nonce)
    a0 = bytes([lval - 1]) + nonce + b"\x00" * lval
    s0 = aes.encrypt_block(a0)
    t = _xor(tag, s0[:tlen])
    pt = bytearray()
    for i in range(0, len(ciphertext), 16):
        blk = bytes([lval - 1]) + nonce + ((i // 16) + 1).to_bytes(lval, "big")
        ks = aes.encrypt_block(blk)
        pt += bytes(x ^ y for x, y in zip(ciphertext[i:i + 16], ks))
    pt = bytes(pt)
    if not hmac.compare_digest(_ccm_mac(aes, nonce, aad, pt, tlen), t):
        raise SmbProbeError("AES-CCM authentication tag mismatch")
    return pt


# ---------------------------------------------------------------------------
# MD4 (RFC 1320) -- hashlib has no md4 under OpenSSL 3
# ---------------------------------------------------------------------------

def md4(data):
    """MD4 message digest per RFC 1320; returns 16 bytes."""
    msg = bytearray(data)
    bitlen = len(msg) * 8
    msg.append(0x80)
    while len(msg) % 64 != 56:
        msg.append(0)
    msg += struct.pack("<Q", bitlen & 0xFFFFFFFFFFFFFFFF)
    a, b, c, d = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476
    mask = 0xFFFFFFFF

    def rol(x, n):
        x &= mask
        return ((x << n) | (x >> (32 - n))) & mask

    for off in range(0, len(msg), 64):
        x = struct.unpack("<16I", msg[off:off + 64])
        aa, bb, cc, dd = a, b, c, d
        for i in range(0, 16, 4):
            a = rol(a + ((b & c) | (~b & d)) + x[i], 3)
            d = rol(d + ((a & b) | (~a & c)) + x[i + 1], 7)
            c = rol(c + ((d & a) | (~d & b)) + x[i + 2], 11)
            b = rol(b + ((c & d) | (~c & a)) + x[i + 3], 19)
        for i in range(4):
            a = rol(a + ((b & c) | (b & d) | (c & d)) + x[i] + 0x5A827999, 3)
            d = rol(d + ((a & b) | (a & c) | (b & c)) + x[i + 4] + 0x5A827999, 5)
            c = rol(c + ((d & a) | (d & b) | (a & b)) + x[i + 8] + 0x5A827999, 9)
            b = rol(b + ((c & d) | (c & a) | (d & a)) + x[i + 12] + 0x5A827999, 13)
        for i in (0, 2, 1, 3):
            a = rol(a + (b ^ c ^ d) + x[i] + 0x6ED9EBA1, 3)
            d = rol(d + (a ^ b ^ c) + x[i + 8] + 0x6ED9EBA1, 9)
            c = rol(c + (d ^ a ^ b) + x[i + 4] + 0x6ED9EBA1, 11)
            b = rol(b + (c ^ d ^ a) + x[i + 12] + 0x6ED9EBA1, 15)
        a = (a + aa) & mask
        b = (b + bb) & mask
        c = (c + cc) & mask
        d = (d + dd) & mask
    return struct.pack("<4I", a, b, c, d)


# ---------------------------------------------------------------------------
# RC4 (used for NTLMSSP_NEGOTIATE_KEY_EXCH)
# ---------------------------------------------------------------------------

def rc4(key, data):
    """RC4 stream cipher; used for the NTLM key-exchange blob."""
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 0xFF
        s[i], s[j] = s[j], s[i]
    out = bytearray()
    i = j = 0
    for ch in data:
        i = (i + 1) & 0xFF
        j = (j + s[i]) & 0xFF
        s[i], s[j] = s[j], s[i]
        out.append(ch ^ s[(s[i] + s[j]) & 0xFF])
    return bytes(out)


def _hmac_md5(key, data):
    return hmac.new(key, data, hashlib.md5).digest()


def sp800_108_kdf(key, label, context, length=16):
    """SP800-108 counter-mode KDF with HMAC-SHA256 ([MS-SMB2] 3.1.4.2).

    *label* must already carry its terminating NUL -- [MS-SMB2] counts it as
    part of the label (e.g. "SMB2AESCMAC" has size 12) -- and SP800-108's own
    0x00 separator byte is appended on top of it.
    """
    out = b""
    i = 1
    while len(out) < length:
        buf = struct.pack(">I", i) + label + b"\x00" + context + struct.pack(">I", length * 8)
        out += hmac.new(key, buf, hashlib.sha256).digest()
        i += 1
    return out[:length]


# ---------------------------------------------------------------------------
# Minimal DER + SPNEGO ([MS-SPNG] 2.2.1)
# ---------------------------------------------------------------------------

def _der_len(n):
    if n < 0x80:
        return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(b)]) + b


def _der_tlv(tag, value):
    return bytes([tag]) + _der_len(len(value)) + value


def _der_oid(dotted):
    parts = [int(x) for x in dotted.split(".")]
    body = bytes([parts[0] * 40 + parts[1]])
    for p in parts[2:]:
        chunk = bytes([p & 0x7F])
        p >>= 7
        while p:
            chunk = bytes([(p & 0x7F) | 0x80]) + chunk
            p >>= 7
        body += chunk
    return _der_tlv(0x06, body)


def _der_parse(buf, off=0):
    """Parse one DER TLV at *off*; returns (tag, value_bytes, next_offset)."""
    if off >= len(buf):
        raise SmbProbeError("DER truncated at offset %d" % off)
    tag = buf[off]
    off += 1
    if off >= len(buf):
        raise SmbProbeError("DER truncated length")
    ln = buf[off]
    off += 1
    if ln & 0x80:
        nb = ln & 0x7F
        ln = int.from_bytes(buf[off:off + nb], "big")
        off += nb
    if off + ln > len(buf):
        raise SmbProbeError("DER length %d overruns buffer" % ln)
    return tag, buf[off:off + ln], off + ln


OID_SPNEGO = "1.3.6.1.5.5.2"
OID_NTLMSSP = "1.3.6.1.4.1.311.2.2.10"


def spnego_neg_token_init(mech_token, mechs=(OID_NTLMSSP,)):
    """Build a SPNEGO NegTokenInit carrying an NTLMSSP token ([MS-SPNG] 2.2.1)."""
    mech_list = _der_tlv(0x30, b"".join(_der_oid(m) for m in mechs))
    inner = _der_tlv(0xA0, mech_list)
    if mech_token:
        inner += _der_tlv(0xA2, _der_tlv(0x04, mech_token))
    neg = _der_tlv(0x30, inner)
    body = _der_oid(OID_SPNEGO) + _der_tlv(0xA0, neg)
    return _der_tlv(0x60, body)


def spnego_neg_token_resp(response_token=None, neg_state=None, mech_list_mic=None):
    """Build a SPNEGO NegTokenResp ([MS-SPNG] 2.2.1)."""
    inner = b""
    if neg_state is not None:
        inner += _der_tlv(0xA0, _der_tlv(0x0A, bytes([neg_state])))
    if response_token is not None:
        inner += _der_tlv(0xA2, _der_tlv(0x04, response_token))
    if mech_list_mic is not None:
        inner += _der_tlv(0xA3, _der_tlv(0x04, mech_list_mic))
    return _der_tlv(0xA1, _der_tlv(0x30, inner))


def spnego_parse(blob):
    """Decode a SPNEGO token; returns a dict with negState/mechToken/supportedMech."""
    out = {"raw": blob, "mechTypes": [], "mechToken": None,
           "negState": None, "supportedMech": None, "mechListMIC": None}
    if not blob:
        return out
    tag, val, _ = _der_parse(blob)
    if tag == 0x60:
        _t, _v, nxt = _der_parse(val)          # thisMech OID
        tag2, val2, _ = _der_parse(val, nxt)   # [0] NegTokenInit
        if tag2 != 0xA0:
            return out
        _t, seq, _ = _der_parse(val2)
        off = 0
        while off < len(seq):
            t, v, off = _der_parse(seq, off)
            if t == 0xA0:
                o2 = 0
                _t, lst, _ = _der_parse(v)
                while o2 < len(lst):
                    _tt, _vv, o2 = _der_parse(lst, o2)
            elif t == 0xA2:
                _t, tok, _ = _der_parse(v)
                out["mechToken"] = tok
    elif tag == 0xA1:
        _t, seq, _ = _der_parse(val)
        off = 0
        while off < len(seq):
            t, v, off = _der_parse(seq, off)
            if t == 0xA0:
                _t, st, _ = _der_parse(v)
                out["negState"] = st[0]
            elif t == 0xA1:
                out["supportedMech"] = v
            elif t == 0xA2:
                _t, tok, _ = _der_parse(v)
                out["mechToken"] = tok
            elif t == 0xA3:
                _t, mic, _ = _der_parse(v)
                out["mechListMIC"] = mic
    elif blob[:8] == b"NTLMSSP\x00":
        out["mechToken"] = blob
    return out


# ---------------------------------------------------------------------------
# NTLMv2 ([MS-NLMP])
# ---------------------------------------------------------------------------

NTLMSSP_NEGOTIATE_UNICODE = 0x00000001
NTLMSSP_REQUEST_TARGET = 0x00000004
NTLMSSP_NEGOTIATE_SIGN = 0x00000010
NTLMSSP_NEGOTIATE_SEAL = 0x00000020
NTLMSSP_NEGOTIATE_NTLM = 0x00000200
NTLMSSP_NEGOTIATE_ALWAYS_SIGN = 0x00008000
NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY = 0x00080000
NTLMSSP_NEGOTIATE_TARGET_INFO = 0x00800000
NTLMSSP_NEGOTIATE_VERSION = 0x02000000
NTLMSSP_NEGOTIATE_128 = 0x20000000
NTLMSSP_NEGOTIATE_KEY_EXCH = 0x40000000
NTLMSSP_NEGOTIATE_56 = 0x80000000

MSV_AV_EOL = 0x0000
MSV_AV_NB_COMPUTER_NAME = 0x0001
MSV_AV_NB_DOMAIN_NAME = 0x0002
MSV_AV_DNS_COMPUTER_NAME = 0x0003
MSV_AV_DNS_DOMAIN_NAME = 0x0004
MSV_AV_DNS_TREE_NAME = 0x0005
MSV_AV_FLAGS = 0x0006
MSV_AV_TIMESTAMP = 0x0007
MSV_AV_SINGLE_HOST = 0x0008
MSV_AV_TARGET_NAME = 0x0009
MSV_AV_CHANNEL_BINDINGS = 0x000A


def ntlm_negotiate(flags=None, domain=b"", workstation=b""):
    """Build an NTLMSSP NEGOTIATE_MESSAGE ([MS-NLMP] 2.2.1.1)."""
    if flags is None:
        flags = (NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET
                 | NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_SEAL
                 | NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_ALWAYS_SIGN
                 | NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY
                 | NTLMSSP_NEGOTIATE_VERSION | NTLMSSP_NEGOTIATE_128
                 | NTLMSSP_NEGOTIATE_KEY_EXCH | NTLMSSP_NEGOTIATE_56)
    ver = struct.pack("<BBHBBBB", 6, 1, 7601, 0, 0, 0, 15)
    base = 32 + 8
    msg = b"NTLMSSP\x00" + struct.pack("<I", 1) + struct.pack("<I", flags)
    msg += struct.pack("<HHI", len(domain), len(domain), base)
    msg += struct.pack("<HHI", len(workstation), len(workstation), base + len(domain))
    msg += ver + domain + workstation
    return msg


def ntlm_parse_challenge(blob):
    """Decode an NTLMSSP CHALLENGE_MESSAGE ([MS-NLMP] 2.2.1.2)."""
    if len(blob) < 48 or blob[:8] != b"NTLMSSP\x00":
        raise SmbProbeError("not an NTLMSSP CHALLENGE_MESSAGE")
    mtype = struct.unpack_from("<I", blob, 8)[0]
    if mtype != 2:
        raise SmbProbeError("NTLMSSP message type %d, expected 2" % mtype)
    tlen, _tmax, toff = struct.unpack_from("<HHI", blob, 12)
    flags = struct.unpack_from("<I", blob, 20)[0]
    challenge = blob[24:32]
    ilen, _imax, ioff = struct.unpack_from("<HHI", blob, 40)
    info = blob[ioff:ioff + ilen]
    av = []
    off = 0
    while off + 4 <= len(info):
        aid, alen = struct.unpack_from("<HH", info, off)
        av.append((aid, info[off + 4:off + 4 + alen]))
        off += 4 + alen
        if aid == MSV_AV_EOL:
            break
    return {"NegotiateFlags": flags, "ServerChallenge": challenge,
            "TargetName": blob[toff:toff + tlen].decode("utf-16le", "replace"),
            "TargetInfo": info, "AvPairs": av, "raw": blob}


def _av_encode(pairs):
    out = b""
    for aid, val in pairs:
        out += struct.pack("<HH", aid, len(val)) + val
    return out


def ntowf_v2(user, password, domain):
    """NTOWFv2 = HMAC_MD5(MD4(UTF16LE(passwd)), UTF16LE(UPPER(user)+domain))."""
    nt = md4(password.encode("utf-16le"))
    return _hmac_md5(nt, (user.upper() + domain).encode("utf-16le"))


def ntlm_authenticate(challenge, user, password, domain=None, workstation="PROBE",
                      client_challenge=None, timestamp=None, use_mic=True,
                      negotiate_msg=b"", exported_session_key=None):
    """Build an NTLMSSP AUTHENTICATE_MESSAGE with NTLMv2 ([MS-NLMP] 3.3.2/2.2.1.3).

    Returns (message_bytes, ExportedSessionKey).
    """
    if domain is None:
        domain = challenge["TargetName"] or ""
    resp_key = ntowf_v2(user, password, domain)
    if client_challenge is None:
        client_challenge = os.urandom(8)
    if timestamp is None:
        ts = None
        for aid, val in challenge["AvPairs"]:
            if aid == MSV_AV_TIMESTAMP and len(val) == 8:
                ts = val
        timestamp = ts if ts else struct.pack(
            "<Q", int((time.time() + 11644473600) * 10000000))

    av = [(a, v) for a, v in challenge["AvPairs"] if a != MSV_AV_EOL]
    if use_mic:
        found = False
        for i, (aid, val) in enumerate(av):
            if aid == MSV_AV_FLAGS:
                av[i] = (aid, struct.pack("<I", struct.unpack("<I", val)[0] | 0x02))
                found = True
        if not found:
            av.append((MSV_AV_FLAGS, struct.pack("<I", 0x02)))
    target_info = _av_encode(av) + struct.pack("<HH", MSV_AV_EOL, 0)

    temp = (b"\x01\x01" + b"\x00" * 6 + timestamp + client_challenge
            + b"\x00" * 4 + target_info + b"\x00" * 4)
    proof = _hmac_md5(resp_key, challenge["ServerChallenge"] + temp)
    nt_response = proof + temp
    session_base_key = _hmac_md5(resp_key, proof)
    lm_response = b"\x00" * 24

    flags = challenge["NegotiateFlags"]
    flags |= (NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET
              | NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_ALWAYS_SIGN
              | NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY
              | NTLMSSP_NEGOTIATE_VERSION | NTLMSSP_NEGOTIATE_128
              | NTLMSSP_NEGOTIATE_56)
    if flags & NTLMSSP_NEGOTIATE_KEY_EXCH:
        if exported_session_key is None:
            exported_session_key = os.urandom(16)
        enc_key = rc4(session_base_key, exported_session_key)
    else:
        exported_session_key = session_base_key
        enc_key = b""

    dom = domain.encode("utf-16le")
    usr = user.encode("utf-16le")
    wks = workstation.encode("utf-16le")
    ver = struct.pack("<BBHBBBB", 6, 1, 7601, 0, 0, 0, 15)
    mic_len = 16 if use_mic else 0
    base = 64 + 8 + mic_len
    offs = []
    cur = base
    for blob in (lm_response, nt_response, dom, usr, wks, enc_key):
        offs.append(cur)
        cur += len(blob)
    msg = b"NTLMSSP\x00" + struct.pack("<I", 3)
    msg += struct.pack("<HHI", len(lm_response), len(lm_response), offs[0])
    msg += struct.pack("<HHI", len(nt_response), len(nt_response), offs[1])
    msg += struct.pack("<HHI", len(dom), len(dom), offs[2])
    msg += struct.pack("<HHI", len(usr), len(usr), offs[3])
    msg += struct.pack("<HHI", len(wks), len(wks), offs[4])
    msg += struct.pack("<HHI", len(enc_key), len(enc_key), offs[5])
    msg += struct.pack("<I", flags) + ver
    if use_mic:
        msg += b"\x00" * 16
    msg += lm_response + nt_response + dom + usr + wks + enc_key
    if use_mic:
        mic = _hmac_md5(exported_session_key,
                        negotiate_msg + challenge["raw"] + msg)
        msg = msg[:72] + mic + msg[88:]
    return msg, exported_session_key


_SIGN_MAGIC_C2S = b"session key to client-to-server signing key magic constant\x00"
_SEAL_MAGIC_C2S = b"session key to client-to-server sealing key magic constant\x00"


def ntlm_sign_key(exported_session_key):
    """SIGNKEY for the client-to-server direction ([MS-NLMP] 3.4.5.2)."""
    return hashlib.md5(exported_session_key + _SIGN_MAGIC_C2S).digest()


def ntlm_seal_key(exported_session_key):
    """SEALKEY for the client-to-server direction ([MS-NLMP] 3.4.5.3)."""
    return hashlib.md5(exported_session_key + _SEAL_MAGIC_C2S).digest()


def ntlm_mac(exported_session_key, message, seq_num=0, key_exchange=True):
    """NTLMSSP MAC with extended session security ([MS-NLMP] 3.4.4.2)."""
    sk = ntlm_sign_key(exported_session_key)
    checksum = _hmac_md5(sk, struct.pack("<I", seq_num) + message)[:8]
    if key_exchange:
        checksum = rc4(ntlm_seal_key(exported_session_key), checksum)
    return struct.pack("<I", 1) + checksum + struct.pack("<I", seq_num)


def spnego_mech_list(mechs=(OID_NTLMSSP,)):
    """The DER MechTypeList the SPNEGO mechListMIC is computed over ([MS-SPNG] 3.3.5.1)."""
    return _der_tlv(0x30, b"".join(_der_oid(m) for m in mechs))


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

STATUS_SUCCESS = 0x00000000
STATUS_PENDING = 0x00000103
STATUS_NOTIFY_ENUM_DIR = 0x0000010C
STATUS_MORE_PROCESSING_REQUIRED = 0xC0000016
STATUS_NO_MORE_FILES = 0x80000006
STATUS_BUFFER_OVERFLOW = 0x80000005

# --- SMB1 -----------------------------------------------------------------
SMB1_HEADER = b"\xffSMB"
SMB_COM_CREATE_DIRECTORY = 0x00
SMB_COM_DELETE_DIRECTORY = 0x01
SMB_COM_CLOSE = 0x04
SMB_COM_DELETE = 0x06
SMB_COM_RENAME = 0x07
SMB_COM_CHECK_DIRECTORY = 0x10
SMB_COM_LOCKING_ANDX = 0x24
SMB_COM_TRANSACTION = 0x25
SMB_COM_ECHO = 0x2B
SMB_COM_READ_ANDX = 0x2E
SMB_COM_WRITE_ANDX = 0x2F
SMB_COM_TRANSACTION2 = 0x32
SMB_COM_FIND_CLOSE2 = 0x34
SMB_COM_TREE_DISCONNECT = 0x71
SMB_COM_NEGOTIATE = 0x72
SMB_COM_SESSION_SETUP_ANDX = 0x73
SMB_COM_LOGOFF_ANDX = 0x74
SMB_COM_TREE_CONNECT_ANDX = 0x75
SMB_COM_OPEN_ANDX = 0x2D
SMB_COM_SEEK = 0x12
SMB_COM_FLUSH = 0x05
SMB_COM_PROCESS_EXIT = 0x11
SMB_COM_QUERY_INFORMATION = 0x08
SMB_COM_SET_INFORMATION = 0x09
SMB_COM_SET_INFORMATION2 = 0x22
SMB_COM_QUERY_INFORMATION2 = 0x23
SMB_COM_QUERY_INFORMATION_DISK = 0x80
SMB_COM_NT_RENAME = 0xA5
SMB_COM_LOCK_BYTE_RANGE = 0x0C
SMB_COM_UNLOCK_BYTE_RANGE = 0x0D
SMB_COM_FIND_CLOSE2 = 0x34
SMB_COM_NT_TRANSACT = 0xA0
SMB_COM_NT_CREATE_ANDX = 0xA2
SMB_COM_NO_ANDX = 0xFF

SMB_FLAGS_CASE_INSENSITIVE = 0x08
SMB_FLAGS_CANONICALIZED_PATHS = 0x10
SMB_FLAGS_REPLY = 0x80
SMB_FLAGS2_LONG_NAMES = 0x0001
SMB_FLAGS2_EAS = 0x0002
SMB_FLAGS2_SMB_SECURITY_SIGNATURE = 0x0004
SMB_FLAGS2_IS_LONG_NAME = 0x0040
SMB_FLAGS2_EXTENDED_SECURITY = 0x0800
SMB_FLAGS2_NT_STATUS = 0x4000
SMB_FLAGS2_UNICODE = 0x8000
SMB1_DEFAULT_FLAGS2 = (SMB_FLAGS2_UNICODE | SMB_FLAGS2_NT_STATUS
                       | SMB_FLAGS2_EXTENDED_SECURITY | SMB_FLAGS2_IS_LONG_NAME
                       | SMB_FLAGS2_EAS | SMB_FLAGS2_LONG_NAMES)

TRANS2_OPEN2 = 0x00
TRANS2_FIND_FIRST2 = 0x01
TRANS2_FIND_NEXT2 = 0x02
TRANS2_QUERY_FS_INFORMATION = 0x03
TRANS2_SET_FS_INFORMATION = 0x04
TRANS2_QUERY_PATH_INFORMATION = 0x05
TRANS2_SET_PATH_INFORMATION = 0x06
TRANS2_QUERY_FILE_INFORMATION = 0x07
TRANS2_SET_FILE_INFORMATION = 0x08

CAP_EXTENDED_SECURITY = 0x80000000
CAP_NT_SMBS = 0x00000010
CAP_STATUS32 = 0x00000040
CAP_UNICODE = 0x00000004
CAP_LARGE_FILES = 0x00000008
CAP_NT_FIND = 0x00000200
CAP_LEVEL_II_OPLOCKS = 0x00000080

SMB1_DEFAULT_DIALECTS = ["PC NETWORK PROGRAM 1.0", "LANMAN1.0",
                         "Windows for Workgroups 3.1a", "LM1.2X002",
                         "LANMAN2.1", "NT LM 0.12"]

# --- SMB2 -----------------------------------------------------------------
SMB2_HEADER = b"\xfeSMB"
SMB2_TRANSFORM_PROTOCOL_ID = b"\xfdSMB"

SMB2_NEGOTIATE = 0x0000
SMB2_SESSION_SETUP = 0x0001
SMB2_LOGOFF = 0x0002
SMB2_TREE_CONNECT = 0x0003
SMB2_TREE_DISCONNECT = 0x0004
SMB2_CREATE = 0x0005
SMB2_CLOSE = 0x0006
SMB2_FLUSH = 0x0007
SMB2_READ = 0x0008
SMB2_WRITE = 0x0009
SMB2_LOCK = 0x000A
SMB2_IOCTL = 0x000B
SMB2_CANCEL = 0x000C
SMB2_ECHO = 0x000D
SMB2_QUERY_DIRECTORY = 0x000E
SMB2_CHANGE_NOTIFY = 0x000F
SMB2_QUERY_INFO = 0x0010
SMB2_SET_INFO = 0x0011
SMB2_OPLOCK_BREAK = 0x0012

SMB2_FLAGS_SERVER_TO_REDIR = 0x00000001
SMB2_FLAGS_ASYNC_COMMAND = 0x00000002
SMB2_FLAGS_RELATED_OPERATIONS = 0x00000004
SMB2_FLAGS_SIGNED = 0x00000008
SMB2_FLAGS_PRIORITY_MASK = 0x00000070
SMB2_FLAGS_DFS_OPERATIONS = 0x10000000
SMB2_FLAGS_REPLAY_OPERATION = 0x20000000

SMB2_NEGOTIATE_SIGNING_ENABLED = 0x0001
SMB2_NEGOTIATE_SIGNING_REQUIRED = 0x0002

SMB2_PREAUTH_INTEGRITY_CAPABILITIES = 0x0001
SMB2_ENCRYPTION_CAPABILITIES = 0x0002
SMB2_COMPRESSION_CAPABILITIES = 0x0003
SMB2_NETNAME_NEGOTIATE_CONTEXT_ID = 0x0005
SMB2_TRANSPORT_CAPABILITIES = 0x0006
SMB2_RDMA_TRANSFORM_CAPABILITIES = 0x0007
SMB2_SIGNING_CAPABILITIES = 0x0008

SMB2_ENCRYPTION_AES128_CCM = 0x0001
SMB2_ENCRYPTION_AES128_GCM = 0x0002
SMB2_SIGNING_HMAC_SHA256 = 0x0000
SMB2_SIGNING_AES_CMAC = 0x0001
SMB2_SIGNING_AES_GMAC = 0x0002

SMB2_GLOBAL_CAP_DFS = 0x00000001
SMB2_GLOBAL_CAP_LEASING = 0x00000002
SMB2_GLOBAL_CAP_LARGE_MTU = 0x00000004
SMB2_GLOBAL_CAP_MULTI_CHANNEL = 0x00000008
SMB2_GLOBAL_CAP_PERSISTENT_HANDLES = 0x00000010
SMB2_GLOBAL_CAP_DIRECTORY_LEASING = 0x00000020
SMB2_GLOBAL_CAP_ENCRYPTION = 0x00000040

SMB2_OPLOCK_LEVEL_NONE = 0x00
SMB2_OPLOCK_LEVEL_II = 0x01
SMB2_OPLOCK_LEVEL_EXCLUSIVE = 0x08
SMB2_OPLOCK_LEVEL_BATCH = 0x09
SMB2_OPLOCK_LEVEL_LEASE = 0xFF

SMB2_SESSION_FLAG_ENCRYPT_DATA = 0x0004
SMB2_SHAREFLAG_ENCRYPT_DATA = 0x00008000

FILE_READ_DATA = 0x00000001
FILE_WRITE_DATA = 0x00000002
FILE_APPEND_DATA = 0x00000004
FILE_READ_EA = 0x00000008
FILE_WRITE_EA = 0x00000010
FILE_READ_ATTRIBUTES = 0x00000080
FILE_WRITE_ATTRIBUTES = 0x00000100
DELETE = 0x00010000
READ_CONTROL = 0x00020000
SYNCHRONIZE = 0x00100000
GENERIC_ALL = 0x10000000
MAXIMUM_ALLOWED = 0x02000000

FILE_SHARE_READ = 0x00000001
FILE_SHARE_WRITE = 0x00000002
FILE_SHARE_DELETE = 0x00000004

FILE_SUPERSEDE = 0x00000000
FILE_OPEN = 0x00000001
FILE_CREATE = 0x00000002
FILE_OPEN_IF = 0x00000003
FILE_OVERWRITE = 0x00000004
FILE_OVERWRITE_IF = 0x00000005

FILE_DIRECTORY_FILE = 0x00000001
FILE_NON_DIRECTORY_FILE = 0x00000040
FILE_DELETE_ON_CLOSE = 0x00001000

FILE_ATTRIBUTE_NORMAL = 0x00000080
FILE_ATTRIBUTE_DIRECTORY = 0x00000010
FILE_ATTRIBUTE_READONLY = 0x00000001
FILE_ATTRIBUTE_HIDDEN = 0x00000002
FILE_ATTRIBUTE_SYSTEM = 0x00000004
FILE_ATTRIBUTE_ARCHIVE = 0x00000020

SMB2_0_INFO_FILE = 0x01
SMB2_0_INFO_FILESYSTEM = 0x02
SMB2_0_INFO_SECURITY = 0x03
SMB2_0_INFO_QUOTA = 0x04

FileBasicInformation = 0x04
FileStandardInformation = 0x05
FileInternalInformation = 0x06
FileEaInformation = 0x07
FileAccessInformation = 0x08
FileRenameInformation = 0x0A
FileDispositionInformation = 0x0D
FilePositionInformation = 0x0E
FileAllInformation = 0x12
FileEndOfFileInformation = 0x14
FileStreamInformation = 0x16
FileNetworkOpenInformation = 0x22
FileIdBothDirectoryInformation = 0x25
FileDirectoryInformation = 0x01
FileFullDirectoryInformation = 0x02
FileBothDirectoryInformation = 0x03
FileNamesInformation = 0x0C

FSCTL_DFS_GET_REFERRALS = 0x00060194
FSCTL_PIPE_WAIT = 0x00110018
FSCTL_VALIDATE_NEGOTIATE_INFO = 0x00140204

FSCTL_SRV_REQUEST_RESUME_KEY = 0x00140078
FSCTL_SRV_COPYCHUNK = 0x001440F2
FSCTL_SRV_COPYCHUNK_WRITE = 0x001480F2
FSCTL_SET_SPARSE = 0x000900C4
FSCTL_SET_ZERO_DATA = 0x000980C8
FSCTL_QUERY_ALLOCATED_RANGES = 0x000940CF
FSCTL_SET_REPARSE_POINT = 0x000900A4
FSCTL_QUERY_NETWORK_INTERFACE_INFO = 0x001401FC
SMB2_0_IOCTL_IS_FSCTL = 0x00000001

SMB2_DIALECTS = {0x0202: "2.0.2", 0x0210: "2.1", 0x0300: "3.0",
                 0x0302: "3.0.2", 0x0311: "3.1.1", 0x02FF: "2.???"}


def _u(s):
    return s.encode("utf-16le")


def _from_u(b):
    return b.decode("utf-16le", "replace")


# ---------------------------------------------------------------------------
# SMB1 encoders/decoders
# ---------------------------------------------------------------------------

class _Smb1Mixin:
    """SMB1 (CIFS) request builders and response decoders."""

    def _smb1_header(self, command, tid=None, uid=None, flags2=None, mid=None):
        flags = SMB_FLAGS_CASE_INSENSITIVE | SMB_FLAGS_CANONICALIZED_PATHS
        if flags2 is None:
            flags2 = self.smb1_flags2
        if mid is None:
            self.smb1_mid = (self.smb1_mid + 1) & 0xFFFF
            mid = self.smb1_mid
        tid = self.smb1_tid if tid is None else tid
        uid = self.smb1_uid if uid is None else uid
        return struct.pack("<4sBIBH2s8sHHHHH", SMB1_HEADER, command, 0, flags,
                           flags2, b"\x00\x00", b"\x00" * 8, 0,
                           tid & 0xFFFF, self.smb1_pid & 0xFFFF,
                           uid & 0xFFFF, mid)

    @staticmethod
    def _smb1_parse(pdu):
        if len(pdu) < 33 or pdu[:4] != SMB1_HEADER:
            raise SmbProbeError("not an SMB1 PDU: %r" % pdu[:8])
        (_pid, command, status, flags, flags2, pid_high, sig, _res,
         tid, pid_low, uid, mid) = struct.unpack_from("<4sBIBHH8sHHHHH", pdu, 0)
        wc = pdu[32]
        words = pdu[33:33 + wc * 2]
        off = 33 + wc * 2
        bc = struct.unpack_from("<H", pdu, off)[0] if off + 2 <= len(pdu) else 0
        data = pdu[off + 2:off + 2 + bc]
        return {"Command": command, "status": status, "Status": status,
                "Flags": flags, "Flags2": flags2, "TID": tid, "UID": uid,
                "MID": mid, "PID": (pid_high << 16) | pid_low,
                "SecuritySignature": sig, "WordCount": wc, "Words": words,
                "ByteCount": bc, "Bytes": data, "raw": pdu}

    @staticmethod
    def _pad_to_even(cur_offset):
        """Return b'\\x00' when *cur_offset* (from the SMB header start) is odd."""
        return b"\x00" if cur_offset & 1 else b""

    def _smb1_str(self, s, cur_offset):
        """NUL-terminate *s* per Flags2 UNICODE, padding to header-relative
        even alignment first ([MS-CIFS] 2.2.3.3)."""
        if self.smb1_flags2 & SMB_FLAGS2_UNICODE:
            return self._pad_to_even(cur_offset) + _u(s) + b"\x00\x00"
        return s.encode("latin-1") + b"\x00"

    def smb1_command(self, command, words=b"", data=b"", tid=None, uid=None,
                     flags2=None):
        """Send any SMB1 PDU: header + WordCount/Words/ByteCount/Bytes ([MS-CIFS] 2.2.3.1)."""
        if len(words) % 2:
            raise SmbProbeError("SMB1 Words must be an even number of bytes")
        pdu = self._smb1_header(command, tid, uid, flags2)
        pdu += bytes([len(words) // 2]) + words + struct.pack("<H", len(data)) + data
        self._send(pdu)
        return self._smb1_parse(self._recv())

    def smb1_negotiate(self, dialects=None):
        """SMB_COM_NEGOTIATE ([MS-CIFS] 2.2.4.52, [MS-SMB] 2.2.4.5)."""
        if dialects is None:
            dialects = list(SMB1_DEFAULT_DIALECTS)
        body = b"".join(b"\x02" + d.encode("latin-1") + b"\x00" for d in dialects)
        pdu = self._smb1_header(SMB_COM_NEGOTIATE)
        pdu += b"\x00" + struct.pack("<H", len(body)) + body
        self._send(pdu)
        rsp = self._recv()
        if rsp[:4] == SMB2_HEADER:
            out = self._smb2_split(rsp, False, rsp)[0]
            out["SMB2Upgrade"] = True
            out["DialectsSent"] = dialects
            if out["status"] == STATUS_SUCCESS:
                self.dialect = out["DialectRevision"]
                self.server_capabilities = out["Capabilities"]
            self.smb1_negotiated = out
            return out
        r = self._smb1_parse(rsp)
        r["DialectsSent"] = dialects
        w = r["Words"]
        if r["WordCount"] == 17:
            (di, secmode, maxmpx, maxvcs, maxbuf, maxraw, sesskey, caps,
             systime, tz, chal_len) = struct.unpack("<HBHHIIIIqhB", w)
            r.update({"DialectIndex": di, "SecurityMode": secmode,
                      "MaxMpxCount": maxmpx, "MaxNumberVcs": maxvcs,
                      "MaxBufferSize": maxbuf, "MaxRawSize": maxraw,
                      "SessionKey": sesskey, "Capabilities": caps,
                      "SystemTime": systime, "ServerTimeZone": tz,
                      "ChallengeLength": chal_len})
            r["DialectName"] = dialects[di] if 0 <= di < len(dialects) else None
            b = r["Bytes"]
            if caps & CAP_EXTENDED_SECURITY:
                r["ServerGuid"] = b[:16]
                r["SecurityBlob"] = b[16:]
                r["Challenge"] = b""
            else:
                r["ServerGuid"] = b""
                r["Challenge"] = b[:chal_len]
                r["SecurityBlob"] = b""
                r["DomainName"] = _from_u(b[chal_len:]) \
                    if r["Flags2"] & SMB_FLAGS2_UNICODE else b[chal_len:].decode("latin-1")
            self.smb1_maxbuf = maxbuf
        elif r["WordCount"] == 13:
            (di, secmode, maxbuf, maxmpx, maxvcs, rawmode, sesskey, tz,
             chal_len) = struct.unpack("<HHHHHHIhH", w[:18])
            r.update({"DialectIndex": di, "SecurityMode": secmode,
                      "MaxBufferSize": maxbuf, "MaxMpxCount": maxmpx,
                      "MaxNumberVcs": maxvcs, "RawMode": rawmode,
                      "SessionKey": sesskey, "ServerTimeZone": tz,
                      "ChallengeLength": chal_len})
            r["DialectName"] = dialects[di] if 0 <= di < len(dialects) else None
        elif r["WordCount"] == 1:
            r["DialectIndex"] = struct.unpack("<H", w)[0]
            r["DialectName"] = dialects[r["DialectIndex"]] \
                if 0 <= r["DialectIndex"] < len(dialects) else None
        self.smb1_negotiated = r
        return r

    def smb1_session_setup(self, user, password, domain=""):
        """SMB_COM_SESSION_SETUP_ANDX, extended security ([MS-SMB] 2.2.4.6)."""
        caps = (CAP_UNICODE | CAP_LARGE_FILES | CAP_NT_SMBS | CAP_STATUS32
                | CAP_NT_FIND | CAP_LEVEL_II_OPLOCKS | CAP_EXTENDED_SECURITY)
        neg = ntlm_negotiate()
        blob = spnego_neg_token_init(neg)
        uid = 0
        session_key = b""
        chal = None
        r = None
        for _ in range(4):
            r = self._smb1_ssetup_round(blob, caps, uid=uid)
            uid = r["UID"] or uid
            self.smb1_uid = uid
            if r["status"] != STATUS_MORE_PROCESSING_REQUIRED:
                break
            tok = spnego_parse(r["SecurityBlob"])
            mech = tok["mechToken"] or b""
            if mech[:8] == b"NTLMSSP\x00" and mech[8:12] == b"\x02\x00\x00\x00":
                chal = ntlm_parse_challenge(mech)
                auth, session_key = ntlm_authenticate(
                    chal, user, password, domain or None, negotiate_msg=neg,
                    use_mic=self.use_ntlm_mic)
                mic = ntlm_mac(session_key, spnego_mech_list()) \
                    if self.use_ntlm_mic else None
                blob = spnego_neg_token_resp(auth, mech_list_mic=mic)
            elif tok["mechListMIC"] and not mech:
                blob = spnego_neg_token_resp(
                    mech_list_mic=ntlm_mac(session_key, spnego_mech_list(), 0))
            else:
                raise SmbProbeError(
                    "unexpected SPNEGO token in SESSION_SETUP_ANDX response")
        r["SessionKey"] = session_key
        r["NtlmChallenge"] = chal
        self.smb1_session_key = session_key
        return r

    def _smb1_ssetup_round(self, blob, caps, uid=0):
        words = struct.pack("<BBHHHHIHII", SMB_COM_NO_ANDX, 0, 0, 16644, 50, 0,
                            0, len(blob), 0, caps)
        cur = 32 + 1 + len(words) + 2 + len(blob)
        body = blob + self._pad_to_even(cur)
        body += _u("Python") + b"\x00\x00" + _u("smbprobe") + b"\x00\x00"
        pdu = self._smb1_header(SMB_COM_SESSION_SETUP_ANDX, uid=uid)
        pdu += bytes([len(words) // 2]) + words + struct.pack("<H", len(body)) + body
        self._send(pdu)
        r = self._smb1_parse(self._recv())
        if r["WordCount"] >= 4:
            andx, _res, andxoff, action, bloblen = struct.unpack_from("<BBHHH", r["Words"], 0)
            r.update({"AndXCommand": andx, "AndXOffset": andxoff,
                      "Action": action, "SecurityBlobLength": bloblen})
            r["SecurityBlob"] = r["Bytes"][:bloblen]
            tail = r["Bytes"][bloblen:]
            if r["Flags2"] & SMB_FLAGS2_UNICODE and len(tail) > 1:
                tail = tail[1:] if (32 + 1 + r["WordCount"] * 2 + 2 + bloblen) & 1 else tail
                parts = _from_u(tail).split("\x00")
                r["NativeOS"] = parts[0] if parts else ""
                r["NativeLanMan"] = parts[1] if len(parts) > 1 else ""
                r["PrimaryDomain"] = parts[2] if len(parts) > 2 else ""
        else:
            r["SecurityBlob"] = b""
        return r

    def smb1_tree_connect(self, share):
        """SMB_COM_TREE_CONNECT_ANDX ([MS-CIFS] 2.2.4.55)."""
        if "\\\\" not in share:
            share = "\\\\%s\\%s" % (self.host, share)
        password = b"\x00"
        words = struct.pack("<BBHHH", SMB_COM_NO_ANDX, 0, 0, 0x0008, len(password))
        cur = 32 + 1 + len(words) + 2 + len(password)
        body = password + self._smb1_str(share, cur) + b"?????\x00"
        pdu = self._smb1_header(SMB_COM_TREE_CONNECT_ANDX)
        pdu += bytes([len(words) // 2]) + words + struct.pack("<H", len(body)) + body
        self._send(pdu)
        r = self._smb1_parse(self._recv())
        if r["WordCount"] >= 3:
            andx, _res, andxoff, opt = struct.unpack_from("<BBHH", r["Words"], 0)
            r.update({"AndXCommand": andx, "AndXOffset": andxoff,
                      "OptionalSupport": opt})
            if r["WordCount"] >= 7:
                ma, gm = struct.unpack_from("<II", r["Words"], 6)
                r["MaximalShareAccessRights"] = ma
                r["GuestMaximalShareAccessRights"] = gm
            b = r["Bytes"]
            nul = b.find(b"\x00")
            r["Service"] = b[:nul].decode("latin-1") if nul >= 0 else ""
        if r["status"] == STATUS_SUCCESS:
            self.smb1_tid = r["TID"]
        return r

    def smb1_nt_create(self, name, **kw):
        """SMB_COM_NT_CREATE_ANDX ([MS-CIFS] 2.2.4.64, [MS-SMB] 2.2.4.9)."""
        flags = kw.get("flags", 0)
        root_fid = kw.get("root_fid", 0)
        access = kw.get("desired_access", FILE_READ_DATA | FILE_WRITE_DATA
                        | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | DELETE)
        alloc = kw.get("allocation_size", 0)
        attrs = kw.get("file_attributes", FILE_ATTRIBUTE_NORMAL)
        share = kw.get("share_access", FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
        disp = kw.get("create_disposition", FILE_OPEN_IF)
        opts = kw.get("create_options", FILE_NON_DIRECTORY_FILE)
        impers = kw.get("impersonation", 2)
        secflags = kw.get("security_flags", 0)
        nm = _u(name) if self.smb1_flags2 & SMB_FLAGS2_UNICODE else name.encode("latin-1")
        namelen = len(nm) + (2 if self.smb1_flags2 & SMB_FLAGS2_UNICODE else 1)
        words = struct.pack("<BBHBHIIIQIIIIIB", SMB_COM_NO_ANDX, 0, 0, 0,
                            namelen, flags, root_fid, access, alloc, attrs,
                            share, disp, opts, impers, secflags)
        cur = 32 + 1 + len(words) + 2
        body = self._smb1_str(name, cur)
        pdu = self._smb1_header(SMB_COM_NT_CREATE_ANDX)
        pdu += bytes([len(words) // 2]) + words + struct.pack("<H", len(body)) + body
        self._send(pdu)
        r = self._smb1_parse(self._recv())
        if r["WordCount"] >= 34:
            (andx, _res, andxoff, oplock, fid, action, ctime, atime, wtime,
             chtime, attr, alloc_sz, eof, rtype, npipe,
             isdir) = struct.unpack_from("<BBHBHIqqqqIqqHHB", r["Words"], 0)
            r.update({"AndXCommand": andx, "AndXOffset": andxoff,
                      "OplockLevel": oplock, "FID": fid, "CreateAction": action,
                      "CreationTime": ctime, "LastAccessTime": atime,
                      "LastWriteTime": wtime, "LastChangeTime": chtime,
                      "ExtFileAttributes": attr, "AllocationSize": alloc_sz,
                      "EndOfFile": eof, "ResourceType": rtype,
                      "NMPipeStatus": npipe, "Directory": isdir})
        return r

    def smb1_write(self, fid, offset, data):
        """SMB_COM_WRITE_ANDX with 64-bit offset ([MS-CIFS] 2.2.4.43)."""
        hdr_len = 32 + 1 + 28 + 2
        pad = self._pad_to_even(hdr_len)
        data_off = hdr_len + len(pad)
        words = struct.pack("<BBHHIIHHHHHI", SMB_COM_NO_ANDX, 0, 0, fid,
                            offset & 0xFFFFFFFF, 0, 0, 0,
                            (len(data) >> 16) & 0xFFFF, len(data) & 0xFFFF,
                            data_off, offset >> 32)
        body = pad + data
        pdu = self._smb1_header(SMB_COM_WRITE_ANDX)
        pdu += bytes([len(words) // 2]) + words + struct.pack("<H", len(body)) + body
        self._send(pdu)
        r = self._smb1_parse(self._recv())
        if r["WordCount"] >= 6:
            andx, _res, andxoff, count, remaining, counthigh = \
                struct.unpack_from("<BBHHHH", r["Words"], 0)
            r.update({"AndXCommand": andx, "AndXOffset": andxoff,
                      "Count": count | (counthigh << 16),
                      "Remaining": remaining, "CountHigh": counthigh})
        return r

    def smb1_read(self, fid, offset, count):
        """SMB_COM_READ_ANDX with 64-bit offset ([MS-CIFS] 2.2.4.42)."""
        words = struct.pack("<BBHHIHHIHI", SMB_COM_NO_ANDX, 0, 0, fid,
                            offset & 0xFFFFFFFF, count & 0xFFFF, count & 0xFFFF,
                            (count >> 16) & 0xFFFF, 0, offset >> 32)
        pdu = self._smb1_header(SMB_COM_READ_ANDX)
        pdu += bytes([len(words) // 2]) + words + struct.pack("<H", 0)
        self._send(pdu)
        r = self._smb1_parse(self._recv())
        r["Data"] = b""
        if r["WordCount"] >= 12:
            (andx, _res, andxoff, avail, compact, _r1, dlen, doff,
             dlenhigh) = struct.unpack_from("<BBHHHHHHH", r["Words"], 0)
            total = dlen | (dlenhigh << 16)
            r.update({"AndXCommand": andx, "AndXOffset": andxoff,
                      "Available": avail, "DataCompactionMode": compact,
                      "DataLength": total, "DataOffset": doff})
            r["Data"] = r["raw"][doff:doff + total]
        return r

    def smb1_close(self, fid):
        """SMB_COM_CLOSE ([MS-CIFS] 2.2.4.5)."""
        return self.smb1_command(SMB_COM_CLOSE, struct.pack("<HI", fid, 0xFFFFFFFF))

    def smb1_trans2(self, subcommand, params, data=b"", max_param=1024,
                    max_data=65535, setup=None):
        """SMB_COM_TRANSACTION2 with reassembly of split replies ([MS-CIFS] 2.2.4.46)."""
        setup_words = struct.pack("<H", subcommand) if setup is None else setup
        sc = len(setup_words) // 2
        words_fixed_len = 28 + len(setup_words)
        base = 32 + 1 + words_fixed_len + 2
        name = b"\x00"
        cur = base + len(name)
        pad1 = b"\x00" * ((-cur) % 4)
        poff = cur + len(pad1)
        pad2 = b"\x00" * ((-(poff + len(params))) % 4)
        doff = poff + len(params) + len(pad2)
        words = struct.pack("<HHHHBBHIHHHHHBB",
                            len(params), len(data), max_param, max_data, 0, 0,
                            0, 0, 0, len(params), poff, len(data),
                            doff if data else 0, sc, 0) + setup_words
        body = name + pad1 + params + pad2 + data
        pdu = self._smb1_header(SMB_COM_TRANSACTION2)
        pdu += bytes([len(words) // 2]) + words + struct.pack("<H", len(body)) + body
        self._send(pdu)
        out_params = bytearray()
        out_data = bytearray()
        r = None
        while True:
            r = self._smb1_parse(self._recv())
            if r["WordCount"] < 10:
                break
            (tpc, tdc, _r1, pc, po, pd, dc, do, dd, rsc,
             _r2) = struct.unpack_from("<HHHHHHHHHBB", r["Words"], 0)
            r.update({"TotalParameterCount": tpc, "TotalDataCount": tdc,
                      "ParameterCount": pc, "ParameterOffset": po,
                      "ParameterDisplacement": pd, "DataCount": dc,
                      "DataOffset": do, "DataDisplacement": dd,
                      "SetupCount": rsc})
            if len(out_params) < pd + pc:
                out_params.extend(b"\x00" * (pd + pc - len(out_params)))
            out_params[pd:pd + pc] = r["raw"][po:po + pc]
            if len(out_data) < dd + dc:
                out_data.extend(b"\x00" * (dd + dc - len(out_data)))
            out_data[dd:dd + dc] = r["raw"][do:do + dc]
            if len(out_params) >= tpc and len(out_data) >= tdc:
                break
            if r["status"] not in (STATUS_SUCCESS, STATUS_BUFFER_OVERFLOW):
                break
        r["Trans2Parameters"] = bytes(out_params)
        r["Trans2Data"] = bytes(out_data)
        r["Params"] = r["Trans2Parameters"]
        r["Data"] = r["Trans2Data"]
        r["Setup"] = r["Words"][20:] if r["WordCount"] >= 10 else b""
        return r

    def smb1_delete(self, name, search_attributes=0x0006):
        """SMB_COM_DELETE ([MS-CIFS] 2.2.4.7)."""
        words = struct.pack("<H", search_attributes)
        cur = 32 + 1 + len(words) + 2 + 1
        body = b"\x04" + self._smb1_str(name, cur)
        return self.smb1_command(SMB_COM_DELETE, words, body)

    def smb1_rename(self, old, new, search_attributes=0x0006):
        """SMB_COM_RENAME ([MS-CIFS] 2.2.4.8)."""
        words = struct.pack("<H", search_attributes)
        cur = 32 + 1 + len(words) + 2 + 1
        first = b"\x04" + self._smb1_str(old, cur)
        cur2 = 32 + 1 + len(words) + 2 + len(first) + 1
        body = first + b"\x04" + self._smb1_str(new, cur2)
        return self.smb1_command(SMB_COM_RENAME, words, body)

    def smb1_echo(self, count=1, data=b"smbprobe"):
        """SMB_COM_ECHO ([MS-CIFS] 2.2.4.39)."""
        pdu = self._smb1_header(SMB_COM_ECHO)
        words = struct.pack("<H", count)
        pdu += bytes([1]) + words + struct.pack("<H", len(data)) + data
        self._send(pdu)
        replies = []
        for _ in range(max(1, count)):
            r = self._smb1_parse(self._recv())
            if r["WordCount"] >= 1:
                r["SequenceNumber"] = struct.unpack_from("<H", r["Words"], 0)[0]
            r["Data"] = r["Bytes"]
            replies.append(r)
        out = replies[-1]
        out["Replies"] = replies
        return out

    def smb1_nt_transact(self, function, setup=b"", params=b"", data=b"",
                         max_param=1024, max_data=65536, max_setup=0):
        """SMB_COM_NT_TRANSACT ([MS-CIFS] 2.2.4.62)."""
        setup = setup or b""
        words_len = 38 + len(setup)
        base = 32 + 1 + words_len + 2
        pad1 = (-base) % 4
        param_off = base + pad1
        pad2 = (-(param_off + len(params))) % 4
        data_off = param_off + len(params) + pad2
        words = struct.pack("<BHIIIIIIII", max_setup, 0, len(params), len(data),
                            max_param, max_data, len(params),
                            param_off if params else 0, len(data),
                            data_off if data else 0)
        words += struct.pack("<BH", len(setup) // 2, function) + setup
        body = b"\x00" * pad1 + params + b"\x00" * pad2 + data
        pdu = self._smb1_header(SMB_COM_NT_TRANSACT)
        pdu += bytes([len(words) // 2]) + words + struct.pack("<H", len(body)) + body
        self._send(pdu)
        r = self._smb1_parse(self._recv())
        if r["WordCount"] >= 18:
            w = r["Words"]
            tpc, tdc, pc, po, pd, dc, do, dd = struct.unpack_from("<IIIIIIII", w, 3)
            _ = (tpc, tdc, pd, dd)
            raw = r["raw"]
            r["Params"] = raw[po:po + pc] if pc else b""
            r["Data"] = raw[do:do + dc] if dc else b""
            r["SetupCount"] = w[35] if len(w) > 35 else 0
        else:
            r["Params"] = b""
            r["Data"] = b""
        return r

    def smb1_wait_notification(self, timeout=10.0):
        """Read one unsolicited SMB1 PDU (an oplock break carries MID 0xFFFF)."""
        old = self.sock.gettimeout()
        self.sock.settimeout(timeout)
        try:
            return self._smb1_parse(self._recv())
        except (OSError, SmbProbeError):
            return None
        finally:
            try:
                self.sock.settimeout(old)
            except OSError:
                pass

    def smb1_open_andx(self, name, flags=0, access_mode=0x0042, search_attrs=0,
                       file_attrs=0, open_function=0x0011):
        """SMB_COM_OPEN_ANDX ([MS-CIFS] 2.2.4.41).

        Fifteen words, exactly as 2.2.4.41.1 lays them out: AndX triple, Flags,
        AccessMode, SearchAttrs, FileAttrs, the four-byte UTIME CreationTime,
        OpenMode, AllocationSize, Timeout and Reserved[2].
        """
        words = struct.pack("<BBHHHHHIHIII", SMB_COM_NO_ANDX, 0, 0, flags,
                            access_mode, search_attrs, file_attrs,
                            0, open_function, 0, 0, 0)
        assert len(words) == 30, len(words)
        cur = 32 + 1 + len(words) + 2
        body = self._smb1_str(name, cur)
        r = self.smb1_command(SMB_COM_OPEN_ANDX, words, body)
        if r["WordCount"] >= 15:
            (andx, _r, andxoff, fid, attrs, mtime, size, granted, ftype,
             state, action) = struct.unpack_from("<BBHHHIIHHHH", r["Words"], 0)
            r.update({"FID": fid, "FileAttrs": attrs, "Size": size,
                      "GrantedAccess": granted, "Action": action})
        return r

    def smb1_tree_disconnect(self):
        """SMB_COM_TREE_DISCONNECT ([MS-CIFS] 2.2.4.51)."""
        r = self.smb1_command(SMB_COM_TREE_DISCONNECT)
        if r["status"] == STATUS_SUCCESS:
            self.smb1_tid = 0
        return r

    def smb1_logoff(self):
        """SMB_COM_LOGOFF_ANDX ([MS-CIFS] 2.2.4.54)."""
        r = self.smb1_command(SMB_COM_LOGOFF_ANDX,
                              struct.pack("<BBH", SMB_COM_NO_ANDX, 0, 0))
        if r["status"] == STATUS_SUCCESS:
            self.smb1_uid = 0
        return r

    def smb1_locking_andx(self, fid, locks=(), unlocks=(), type_of_lock=0,
                          new_oplock_level=0, timeout=0):
        """SMB_COM_LOCKING_ANDX ([MS-CIFS] 2.2.4.32); locks=[(pid,offset,length)]."""
        large = bool(type_of_lock & 0x10)
        body = b""
        for pid, off, ln in list(unlocks) + list(locks):
            if large:
                body += struct.pack("<HHIQQ", pid, 0, 0, off, ln)
            else:
                body += struct.pack("<HII", pid, off, ln)
        words = struct.pack("<BBHHBBIHH", SMB_COM_NO_ANDX, 0, 0, fid,
                            type_of_lock, new_oplock_level, timeout,
                            len(unlocks), len(locks))
        return self.smb1_command(SMB_COM_LOCKING_ANDX, words, body)


# ---------------------------------------------------------------------------
# SMB2 negotiate-context and create-context builders ([MS-SMB2] 2.2.3.1/2.2.13.2)
# ---------------------------------------------------------------------------

def build_negotiate_context(ctype, data):
    """Wrap *data* in an SMB2_NEGOTIATE_CONTEXT header ([MS-SMB2] 2.2.3.1)."""
    return struct.pack("<HHI", ctype, len(data), 0) + data


def preauth_integrity_context(hash_ids=(1,), salt=None):
    """SMB2_PREAUTH_INTEGRITY_CAPABILITIES data ([MS-SMB2] 2.2.3.1.1)."""
    if salt is None:
        salt = os.urandom(32)
    return (struct.pack("<HH", len(hash_ids), len(salt))
            + b"".join(struct.pack("<H", h) for h in hash_ids) + salt)


def encryption_context(ciphers=(SMB2_ENCRYPTION_AES128_GCM, SMB2_ENCRYPTION_AES128_CCM)):
    """SMB2_ENCRYPTION_CAPABILITIES data ([MS-SMB2] 2.2.3.1.2)."""
    return struct.pack("<H", len(ciphers)) + b"".join(struct.pack("<H", c) for c in ciphers)


def compression_context(algorithms=(1, 2, 3), flags=0):
    """SMB2_COMPRESSION_CAPABILITIES data ([MS-SMB2] 2.2.3.1.3)."""
    return (struct.pack("<HHI", len(algorithms), 0, flags)
            + b"".join(struct.pack("<H", a) for a in algorithms))


def netname_context(name):
    """SMB2_NETNAME_NEGOTIATE_CONTEXT_ID data ([MS-SMB2] 2.2.3.1.4)."""
    return _u(name)


def signing_context(algorithms=(SMB2_SIGNING_AES_GMAC, SMB2_SIGNING_AES_CMAC,
                                SMB2_SIGNING_HMAC_SHA256)):
    """SMB2_SIGNING_CAPABILITIES data ([MS-SMB2] 2.2.3.1.7)."""
    return struct.pack("<H", len(algorithms)) + b"".join(struct.pack("<H", a) for a in algorithms)


def parse_negotiate_contexts(buf, count):
    """Decode a NEGOTIATE response context list ([MS-SMB2] 2.2.4)."""
    out = []
    off = 0
    for _ in range(count):
        if off + 8 > len(buf):
            break
        ctype, dlen, _res = struct.unpack_from("<HHI", buf, off)
        data = buf[off + 8:off + 8 + dlen]
        ctx = {"ContextType": ctype, "DataLength": dlen, "Data": data}
        if ctype == SMB2_PREAUTH_INTEGRITY_CAPABILITIES and dlen >= 4:
            hc, sl = struct.unpack_from("<HH", data, 0)
            ctx["HashAlgorithmCount"] = hc
            ctx["SaltLength"] = sl
            ctx["HashAlgorithms"] = list(struct.unpack_from("<%dH" % hc, data, 4))
            ctx["Salt"] = data[4 + hc * 2:4 + hc * 2 + sl]
        elif ctype == SMB2_ENCRYPTION_CAPABILITIES and dlen >= 2:
            cc = struct.unpack_from("<H", data, 0)[0]
            ctx["CipherCount"] = cc
            ctx["Ciphers"] = list(struct.unpack_from("<%dH" % cc, data, 2))
        elif ctype == SMB2_COMPRESSION_CAPABILITIES and dlen >= 8:
            ac, _pad, flags = struct.unpack_from("<HHI", data, 0)
            ctx["CompressionAlgorithmCount"] = ac
            ctx["Flags"] = flags
            ctx["CompressionAlgorithms"] = list(struct.unpack_from("<%dH" % ac, data, 8))
        elif ctype == SMB2_SIGNING_CAPABILITIES and dlen >= 2:
            sc = struct.unpack_from("<H", data, 0)[0]
            ctx["SigningAlgorithmCount"] = sc
            ctx["SigningAlgorithms"] = list(struct.unpack_from("<%dH" % sc, data, 2))
        elif ctype == SMB2_NETNAME_NEGOTIATE_CONTEXT_ID:
            ctx["NetName"] = _from_u(data)
        elif ctype == SMB2_TRANSPORT_CAPABILITIES and dlen >= 4:
            ctx["Flags"] = struct.unpack_from("<I", data, 0)[0]
        ctx["Offset"] = off
        out.append(ctx)
        # [MS-SMB2] 2.2.3.1: every NEGOTIATE_CONTEXT starts 8-byte aligned.
        off += 8 + dlen
        off += (-off) % 8
    return out


SMB2_COMPRESSION_PROTOCOL_ID = b"\xfcSMB"

COMPRESSION_NONE = 0
COMPRESSION_LZNT1 = 1
COMPRESSION_LZ77 = 2
COMPRESSION_LZ77_HUFFMAN = 3
COMPRESSION_PATTERN_V1 = 4


def lz77_plain_compress(data):
    """A valid plain-LZ77 ([MS-XCA] 2.4) encoding that emits only literals.

    An encoder is free to emit no matches at all, so this is the simplest
    conforming output: flag groups of 32 zero bits, each followed by 32 literal
    bytes.  It is used to check that a server decompresses what it advertised,
    not to check compression ratios.
    """
    out = bytearray()
    i = 0
    while i < len(data):
        chunk = data[i:i + 32]
        out += struct.pack("<I", 0)          # 32 literal flags, all clear
        out += chunk
        i += 32
    if len(data) % 32 == 0:
        out += struct.pack("<I", 0)          # a trailing empty flag group
    return bytes(out)


def pattern_v1_compress(byte_value, count):
    """Pattern_V1 ([MS-XCA] 2.5): one byte repeated ``count`` times."""
    return struct.pack("<BBHI", byte_value, 0, 0, count)


def compression_transform(original_size, algorithm, payload, offset=0):
    """SMB2_COMPRESSION_TRANSFORM_HEADER, unchained ([MS-SMB2] 2.2.42.1)."""
    return (SMB2_COMPRESSION_PROTOCOL_ID
            + struct.pack("<IHHI", original_size, algorithm, 0, offset)
            + payload)


def build_create_contexts(contexts):
    """Build an SMB2_CREATE_CONTEXT chain from [(name, data), ...] ([MS-SMB2] 2.2.13.2)."""
    items = list(contexts)
    out = b""
    for i, (name, data) in enumerate(items):
        if isinstance(name, str):
            name = name.encode("ascii")
        body = struct.pack("<IHHHHI", 0, 16, len(name), 0,
                           0 if not data else 16 + len(name) + ((-len(name)) % 8),
                           len(data))
        body += name + b"\x00" * ((-len(name)) % 8) + data
        pad = (-len(body)) % 8
        if i != len(items) - 1:
            body += b"\x00" * pad
            body = struct.pack("<I", len(body)) + body[4:]
        out += body
    return out


def parse_create_contexts(buf):
    """Decode an SMB2_CREATE_CONTEXT chain ([MS-SMB2] 2.2.13.2)."""
    out = []
    off = 0
    while off + 16 <= len(buf):
        nxt, noff, nlen, _res, doff, dlen = struct.unpack_from("<IHHHHI", buf, off)
        name = buf[off + noff:off + noff + nlen]
        data = buf[off + doff:off + doff + dlen] if dlen else b""
        entry = {"Name": name.decode("latin-1"), "NameBytes": name, "Data": data,
                 "DataLength": dlen, "Next": nxt}
        if name == b"MxAc" and len(data) >= 8:
            qs, ma = struct.unpack("<II", data[:8])
            entry["QueryStatus"] = qs
            entry["MaximalAccess"] = ma
        elif name == b"QFid" and len(data) >= 32:
            entry["DiskFileId"] = data[:8]
            entry["VolumeId"] = data[8:16]
        elif name == b"DHnQ" and len(data) >= 16:
            entry["Reserved"] = data[:16]
        elif name == b"DH2Q" and len(data) >= 8:
            to, fl = struct.unpack("<II", data[:8])
            entry["Timeout"] = to
            entry["Flags"] = fl
        elif name == b"RqLs" and len(data) >= 32:
            entry["LeaseKey"] = data[:16]
            entry["LeaseState"] = struct.unpack_from("<I", data, 16)[0]
            entry["LeaseFlags"] = struct.unpack_from("<I", data, 20)[0]
        out.append(entry)
        if not nxt:
            break
        off += nxt
    return out


def lease_context_v1(lease_key=None, lease_state=0x07):
    """RqLs (SMB2_CREATE_REQUEST_LEASE) data ([MS-SMB2] 2.2.13.2.8)."""
    if lease_key is None:
        lease_key = os.urandom(16)
    return lease_key + struct.pack("<IIQ", lease_state, 0, 0)


def durable_v2_context(timeout=0, flags=0, create_guid=None):
    """DH2Q (SMB2_CREATE_DURABLE_HANDLE_REQUEST_V2) data ([MS-SMB2] 2.2.13.2.11)."""
    if create_guid is None:
        create_guid = os.urandom(16)
    return struct.pack("<II", timeout, flags) + b"\x00" * 8 + create_guid


# ---------------------------------------------------------------------------
# File-information decoders ([MS-FSCC])
# ---------------------------------------------------------------------------

def decode_file_basic_information(b):
    """FILE_BASIC_INFORMATION ([MS-FSCC] 2.4.7)."""
    ct, at, wt, cht, attr = struct.unpack_from("<qqqqI", b, 0)
    return {"CreationTime": ct, "LastAccessTime": at, "LastWriteTime": wt,
            "ChangeTime": cht, "FileAttributes": attr}


def decode_file_standard_information(b):
    """FILE_STANDARD_INFORMATION ([MS-FSCC] 2.4.41)."""
    alloc, eof, links, delp, isdir = struct.unpack_from("<qqIBB", b, 0)
    return {"AllocationSize": alloc, "EndOfFile": eof, "NumberOfLinks": links,
            "DeletePending": delp, "Directory": isdir}


def decode_file_all_information(b):
    """FILE_ALL_INFORMATION ([MS-FSCC] 2.4.2)."""
    out = decode_file_basic_information(b)
    out.update(decode_file_standard_information(b[40:]))
    out["IndexNumber"] = struct.unpack_from("<q", b, 64)[0]
    out["EaSize"] = struct.unpack_from("<I", b, 72)[0]
    out["AccessFlags"] = struct.unpack_from("<I", b, 76)[0]
    out["CurrentByteOffset"] = struct.unpack_from("<q", b, 80)[0]
    out["Mode"] = struct.unpack_from("<I", b, 88)[0]
    out["AlignmentRequirement"] = struct.unpack_from("<I", b, 92)[0]
    nlen = struct.unpack_from("<I", b, 96)[0]
    out["FileNameLength"] = nlen
    out["FileName"] = _from_u(b[100:100 + nlen])
    return out


def decode_file_network_open_information(b):
    """FILE_NETWORK_OPEN_INFORMATION ([MS-FSCC] 2.4.29)."""
    ct, at, wt, cht, alloc, eof, attr = struct.unpack_from("<qqqqqqI", b, 0)
    return {"CreationTime": ct, "LastAccessTime": at, "LastWriteTime": wt,
            "ChangeTime": cht, "AllocationSize": alloc, "EndOfFile": eof,
            "FileAttributes": attr}


_DIR_INFO_LAYOUT = {
    FileDirectoryInformation: (64, None, None),
    FileFullDirectoryInformation: (68, None, None),
    FileBothDirectoryInformation: (94, 68, None),
    FileIdBothDirectoryInformation: (104, 68, 96),
    FileNamesInformation: (12, None, None),
}


def decode_directory_entries(buf, info_class):
    """Decode a directory-information list ([MS-FSCC] 2.4.8/2.4.14/2.4.17/2.4.26)."""
    base = _DIR_INFO_LAYOUT.get(info_class, (0, None, None))[0]
    if not base:
        return [{"Raw": buf}]
    short_at = _DIR_INFO_LAYOUT[info_class][1]
    fileid_at = _DIR_INFO_LAYOUT[info_class][2]
    out = []
    off = 0
    while off + base <= len(buf):
        nxt, idx = struct.unpack_from("<II", buf, off)
        if info_class == FileNamesInformation:
            nlen = struct.unpack_from("<I", buf, off + 8)[0]
            e = {"NextEntryOffset": nxt, "FileIndex": idx, "FileNameLength": nlen}
        else:
            ct, at, wt, cht, eof, alloc, attr, nlen = \
                struct.unpack_from("<qqqqqqII", buf, off + 8)
            e = {"NextEntryOffset": nxt, "FileIndex": idx, "CreationTime": ct,
                 "LastAccessTime": at, "LastWriteTime": wt, "ChangeTime": cht,
                 "EndOfFile": eof, "AllocationSize": alloc,
                 "FileAttributes": attr, "FileNameLength": nlen}
            if info_class != FileDirectoryInformation:
                e["EaSize"] = struct.unpack_from("<I", buf, off + 64)[0]
            if short_at is not None:
                sl = buf[off + short_at]
                e["ShortNameLength"] = sl
                e["ShortName"] = _from_u(buf[off + short_at + 2:off + short_at + 2 + sl])
            if fileid_at is not None:
                e["FileId"] = struct.unpack_from("<Q", buf, off + fileid_at)[0]
        e["FileName"] = _from_u(buf[off + base:off + base + nlen])
        out.append(e)
        if not nxt:
            break
        off += nxt
    return out


def decode_fs_information(b, info_class):
    """Decode a FileFsXxxInformation buffer ([MS-FSCC] 2.5)."""
    if info_class == 1 and len(b) >= 18:
        ct, serial, lbl = struct.unpack_from("<qII", b, 0)
        return {"VolumeCreationTime": ct, "VolumeSerialNumber": serial,
                "VolumeLabelLength": lbl, "VolumeLabel": _from_u(b[18:18 + lbl])}
    if info_class == 3 and len(b) >= 24:
        t, a, spu, bps = struct.unpack_from("<qqII", b, 0)
        return {"TotalAllocationUnits": t, "AvailableAllocationUnits": a,
                "SectorsPerAllocationUnit": spu, "BytesPerSector": bps}
    if info_class == 4 and len(b) >= 8:
        dt, ch = struct.unpack_from("<II", b, 0)
        return {"DeviceType": dt, "Characteristics": ch}
    if info_class == 5 and len(b) >= 12:
        attr, mcl, nlen = struct.unpack_from("<IiI", b, 0)
        return {"FileSystemAttributes": attr, "MaximumComponentNameLength": mcl,
                "FileSystemNameLength": nlen,
                "FileSystemName": _from_u(b[12:12 + nlen])}
    if info_class == 7 and len(b) >= 32:
        t, cav, av, spu, bps = struct.unpack_from("<qqqII", b, 0)
        return {"TotalAllocationUnits": t, "CallerAvailableAllocationUnits": cav,
                "ActualAvailableAllocationUnits": av,
                "SectorsPerAllocationUnit": spu, "BytesPerSector": bps}
    return {"Raw": b}


# ---------------------------------------------------------------------------
# SMB2/3
# ---------------------------------------------------------------------------

class _Smb2Mixin:
    """SMB2/SMB3 request builders, response decoders, signing and encryption."""

    # -- header ------------------------------------------------------------
    def smb2_header(self, command, flags=0, credit_charge=0, credit_request=1,
                    message_id=0, session_id=0, tree_id=0, next_command=0,
                    status=0):
        """Build the 64-byte SMB2 SYNC header ([MS-SMB2] 2.2.1.2)."""
        return struct.pack("<4sHHIHHIIQIIQ16s", SMB2_HEADER, 64, credit_charge,
                           status, command, credit_request, flags, next_command,
                           message_id, 0, tree_id, session_id, b"\x00" * 16)

    @staticmethod
    def _smb2_parse_header(pdu, off=0):
        if len(pdu) - off < 64 or pdu[off:off + 4] != SMB2_HEADER:
            raise SmbProbeError("not an SMB2 header at offset %d: %r"
                                % (off, pdu[off:off + 8]))
        (_pid, ssize, charge, status, command, credit, flags, nxt,
         mid) = struct.unpack_from("<4sHHIHHIIQ", pdu, off)
        h = {"ProtocolId": _pid, "StructureSize": ssize, "CreditCharge": charge,
             "Status": status, "status": status, "Command": command,
             "CreditResponse": credit, "Credit": credit, "Flags": flags,
             "NextCommand": nxt, "MessageId": mid,
             "SessionId": struct.unpack_from("<Q", pdu, off + 40)[0],
             "Signature": pdu[off + 48:off + 64]}
        if flags & SMB2_FLAGS_ASYNC_COMMAND:
            h["AsyncId"] = struct.unpack_from("<Q", pdu, off + 32)[0]
            h["TreeId"] = 0
        else:
            h["Reserved"] = struct.unpack_from("<I", pdu, off + 32)[0]
            h["TreeId"] = struct.unpack_from("<I", pdu, off + 36)[0]
            h["AsyncId"] = 0
        return h

    # -- signing -----------------------------------------------------------
    def _signing_algorithm(self):
        if self.dialect >= 0x0311 and self.signing_algorithm == SMB2_SIGNING_AES_GMAC:
            return "gmac"
        if self.dialect >= 0x0300:
            return "cmac"
        return "hmac-sha256"

    def sign_pdu(self, pdu, key=None):
        """Compute and insert the SMB2 signature ([MS-SMB2] 3.1.4.1)."""
        key = key or self.keys.get("signing")
        if not key:
            raise SmbProbeError("no signing key available")
        body = pdu[:48] + b"\x00" * 16 + pdu[64:]
        alg = self._signing_algorithm()
        if alg == "hmac-sha256":
            sig = hmac.new(key, body, hashlib.sha256).digest()[:16]
        elif alg == "cmac":
            sig = aes_cmac(key, body)
        else:
            mid = struct.unpack_from("<Q", body, 24)[0]
            cmd = struct.unpack_from("<H", body, 12)[0]
            high = 2 if cmd == SMB2_CANCEL else 0
            nonce = struct.pack("<QI", mid, high)
            sig = aes_gmac(key, nonce, body)
        return body[:48] + sig + body[64:]

    def verify_signature(self, pdu, off=0, length=None, key=None):
        """Verify one message's signature ([MS-SMB2] 3.1.5.1); returns bool."""
        key = key or self.keys.get("signing")
        if not key:
            return False
        end = len(pdu) if length is None else off + length
        msg = pdu[off:end]
        given = msg[48:64]
        body = msg[:48] + b"\x00" * 16 + msg[64:]
        alg = self._signing_algorithm()
        if alg == "hmac-sha256":
            calc = hmac.new(key, body, hashlib.sha256).digest()[:16]
        elif alg == "cmac":
            calc = aes_cmac(key, body)
        else:
            mid = struct.unpack_from("<Q", body, 24)[0]
            cmd = struct.unpack_from("<H", body, 12)[0]
            high = 1 | (2 if cmd == SMB2_CANCEL else 0)
            calc = aes_gmac(key, struct.pack("<QI", mid, high), body)
        return hmac.compare_digest(calc, given)

    # -- encryption --------------------------------------------------------
    def encrypt_pdu(self, pdu, session_id=None):
        """Wrap *pdu* in an SMB2_TRANSFORM_HEADER ([MS-SMB2] 2.2.41, 3.1.4.3)."""
        key = self.keys.get("encryption")
        if not key:
            raise SmbProbeError("no encryption key; session not set up")
        sid = self.session_id if session_id is None else session_id
        nlen = 12 if self.cipher_id == SMB2_ENCRYPTION_AES128_GCM else 11
        nonce = os.urandom(nlen)
        nonce_field = nonce + b"\x00" * (16 - nlen)
        aad = nonce_field + struct.pack("<IHHQ", len(pdu), 0, 0x0001, sid)
        if self.cipher_id == SMB2_ENCRYPTION_AES128_GCM:
            ct, tag = aes_gcm_encrypt(key, nonce, pdu, aad)
        else:
            ct, tag = aes_ccm_encrypt(key, nonce, pdu, aad)
        return SMB2_TRANSFORM_PROTOCOL_ID + tag + aad + ct

    def decrypt_pdu(self, pdu):
        """Decrypt an SMB2_TRANSFORM_HEADER-wrapped message ([MS-SMB2] 3.1.5.1)."""
        if len(pdu) < 52 or pdu[:4] != SMB2_TRANSFORM_PROTOCOL_ID:
            raise SmbProbeError("not an SMB2 TRANSFORM_HEADER")
        key = self.keys.get("decryption")
        if not key:
            raise SmbProbeError("no decryption key; session not set up")
        tag = pdu[4:20]
        aad = pdu[20:52]
        nonce_field = pdu[20:36]
        orig_size, _res, flags, sid = struct.unpack_from("<IHHQ", pdu, 36)
        ct = pdu[52:]
        if self.cipher_id == SMB2_ENCRYPTION_AES128_GCM:
            pt = aes_gcm_decrypt(key, nonce_field[:12], ct, aad, tag)
        else:
            pt = aes_ccm_decrypt(key, nonce_field[:11], ct, aad, tag)
        if len(pt) != orig_size:
            raise SmbProbeError("TRANSFORM OriginalMessageSize %d != %d"
                                % (orig_size, len(pt)))
        self.last_transform = {"Signature": tag, "Nonce": nonce_field,
                               "OriginalMessageSize": orig_size, "Flags": flags,
                               "SessionId": sid}
        return pt

    # -- transport ---------------------------------------------------------
    def _credit_charge(self, payload):
        if self.dialect < 0x0210 or not (self.server_capabilities & SMB2_GLOBAL_CAP_LARGE_MTU):
            return 0
        return max(1, (max(1, payload) - 1) // 65536 + 1)

    def _next_message_id(self, charge):
        mid = self.message_id
        self.message_id += max(1, charge)
        return mid

    def smb2_command(self, command, body, *, session_id=None, tree_id=None,
                     flags=0, credit_charge=None, credit_request=1,
                     message_id=None, sign=None, encrypt=None, expect_reply=True):
        """Send one hand-built SMB2 request and return the decoded reply ([MS-SMB2] 3.2.4)."""
        if credit_charge is None:
            credit_charge = self._credit_charge(len(body) + 64)
        if message_id is None:
            message_id = self._next_message_id(credit_charge)
        sid = self.session_id if session_id is None else session_id
        tid = self.tree_id if tree_id is None else tree_id
        pdu = self.smb2_header(command, flags=flags, credit_charge=credit_charge,
                               credit_request=credit_request,
                               message_id=message_id, session_id=sid,
                               tree_id=tid) + body
        do_sign = sign
        if do_sign is None:
            do_sign = self.sign_next
            self.sign_next = None          # one-shot override
        if do_sign is None:
            do_sign = bool(self.signing_required and self.keys.get("signing"))
        if do_sign:
            pdu = pdu[:16] + struct.pack("<I", flags | SMB2_FLAGS_SIGNED) + pdu[20:]
            pdu = self.sign_pdu(pdu)
        do_enc = self.encrypt if encrypt is None else encrypt
        self.last_request = pdu
        self.credits -= max(1, credit_charge)
        self._send(self.encrypt_pdu(pdu) if do_enc else pdu)
        if not expect_reply:
            return {"status": None, "raw": b"", "MessageId": message_id,
                    "request": pdu, "NoReply": True}
        return self._smb2_receive(message_id)

    def smb2_receive_reply(self, message_id=0):
        """Read the reply to a request sent with ``expect_reply=False``."""
        return self._smb2_receive(message_id)

    def _smb2_receive(self, message_id):
        deadline = time.time() + self.timeout
        interim = None
        while True:
            raw = self._recv()
            encrypted = raw[:4] == SMB2_TRANSFORM_PROTOCOL_ID
            pdu = self.decrypt_pdu(raw) if encrypted else raw
            msgs = self._smb2_split(pdu, encrypted, raw)
            head = msgs[0]
            if (head["Command"] == SMB2_OPLOCK_BREAK
                    and head["MessageId"] == 0xFFFFFFFFFFFFFFFF):
                self.notifications.append(head)
                if time.time() > deadline:
                    raise SmbProbeError("timed out waiting for reply to mid %d" % message_id)
                continue
            if head["Status"] == STATUS_PENDING and (head["Flags"] & SMB2_FLAGS_ASYNC_COMMAND):
                self.last_interim = interim = head
                if time.time() > deadline:
                    raise SmbProbeError("timed out after STATUS_PENDING")
                continue
            head["Chain"] = msgs
            head["InterimResponse"] = interim
            return head

    def _smb2_split(self, pdu, encrypted, raw):
        msgs = []
        off = 0
        while off < len(pdu):
            h = self._smb2_parse_header(pdu, off)
            nxt = h["NextCommand"]
            end = off + nxt if nxt else len(pdu)
            h["Encrypted"] = encrypted
            h["raw"] = pdu[off:end]
            h["Offset"] = off
            self.credits += h["CreditResponse"]
            if (self.keys.get("signing") and (h["Flags"] & SMB2_FLAGS_SIGNED)
                    and not encrypted):
                h["SignatureValid"] = self.verify_signature(pdu, off, end - off)
                if self.verify_signatures and not h["SignatureValid"]:
                    raise SmbProbeError(
                        "bad server signature on command 0x%04x mid %d"
                        % (h["Command"], h["MessageId"]))
            else:
                h["SignatureValid"] = None
            self._smb2_decode_body(h)
            msgs.append(h)
            if not nxt:
                break
            off += nxt
        if encrypted:
            msgs[0]["TransformHeader"] = self.last_transform
            msgs[0]["rawTransform"] = raw
        return msgs

    # -- body decoders -----------------------------------------------------
    def _smb2_decode_body(self, h):
        b = h["raw"][64:]
        cmd = h["Command"]
        h["Body"] = b
        if len(b) < 2:
            return
        h["BodyStructureSize"] = struct.unpack_from("<H", b, 0)[0]
        err = h["Status"] not in (STATUS_SUCCESS, STATUS_BUFFER_OVERFLOW,
                                  STATUS_NOTIFY_ENUM_DIR,
                                  STATUS_MORE_PROCESSING_REQUIRED)
        if err and h["BodyStructureSize"] == 9:
            ctx_cnt, _r, blen = struct.unpack_from("<BBI", b, 2)
            h["ErrorContextCount"] = ctx_cnt
            h["ByteCount"] = blen
            h["ErrorData"] = b[8:8 + blen]
            return
        try:
            if cmd == SMB2_NEGOTIATE:
                self._dec_negotiate(h, b)
            elif cmd == SMB2_SESSION_SETUP:
                ss, flags, off, ln = struct.unpack_from("<HHHH", b, 0)
                h.update({"SessionFlags": flags, "SecurityBufferOffset": off,
                          "SecurityBufferLength": ln,
                          "SecurityBuffer": h["raw"][off:off + ln]})
            elif cmd == SMB2_TREE_CONNECT:
                ss, stype, _r, sflags, caps, access = struct.unpack_from("<HBBIII", b, 0)
                h.update({"ShareType": stype, "ShareFlags": sflags,
                          "Capabilities": caps, "MaximalAccess": access})
            elif cmd == SMB2_CREATE:
                self._dec_create(h, b)
            elif cmd == SMB2_CLOSE:
                (ss, flags, _r, ct, at, wt, cht, alloc,
                 eof, attr) = struct.unpack_from("<HHIqqqqqqI", b, 0)
                h.update({"CloseFlags": flags, "CreationTime": ct,
                          "LastAccessTime": at, "LastWriteTime": wt,
                          "ChangeTime": cht, "AllocationSize": alloc,
                          "EndOfFile": eof, "FileAttributes": attr})
            elif cmd == SMB2_READ:
                ss, doff, _res, dlen, remaining, _r2 = struct.unpack_from("<HBBIII", b, 0)
                h.update({"DataOffset": doff, "DataLength": dlen,
                          "DataRemaining": remaining,
                          "Buffer": h["raw"][doff:doff + dlen]})
            elif cmd == SMB2_WRITE:
                ss, _r, count, remaining, coff, clen = struct.unpack_from("<HHIIHH", b, 0)
                h.update({"Count": count, "Remaining": remaining})
            elif cmd == SMB2_IOCTL:
                self._dec_ioctl(h, b)
            elif cmd == SMB2_LOCK:
                h["Reserved"] = struct.unpack_from("<H", b, 2)[0]
            elif cmd in (SMB2_QUERY_DIRECTORY, SMB2_QUERY_INFO):
                ss, off, ln = struct.unpack_from("<HHI", b, 0)
                h.update({"OutputBufferOffset": off, "OutputBufferLength": ln,
                          "Buffer": h["raw"][off:off + ln]})
            elif cmd == SMB2_CHANGE_NOTIFY:
                ss, off, ln = struct.unpack_from("<HHI", b, 0)
                h.update({"OutputBufferOffset": off, "OutputBufferLength": ln,
                          "Buffer": h["raw"][off:off + ln]})
            elif cmd == SMB2_OPLOCK_BREAK:
                self._dec_oplock(h, b)
        except struct.error as exc:
            raise SmbProbeError("short %s response body: %s" % (cmd, exc))

    def _dec_negotiate(self, h, b):
        (ss, secmode, dialect, ctxcnt, guid, caps, maxtrans, maxread,
         maxwrite, systime, boottime, soff, slen,
         ctxoff) = struct.unpack_from("<HHHH16sIIIIqqHHI", b, 0)
        h.update({"SecurityMode": secmode, "DialectRevision": dialect,
                  "NegotiateContextCount": ctxcnt, "ServerGuid": guid,
                  "Capabilities": caps, "MaxTransactSize": maxtrans,
                  "MaxReadSize": maxread, "MaxWriteSize": maxwrite,
                  "SystemTime": systime, "ServerStartTime": boottime,
                  "SecurityBufferOffset": soff, "SecurityBufferLength": slen,
                  "NegotiateContextOffset": ctxoff,
                  "SecurityBuffer": h["raw"][soff:soff + slen],
                  "DialectName": SMB2_DIALECTS.get(dialect, "0x%04x" % dialect)})
        h["NegotiateContexts"] = (parse_negotiate_contexts(h["raw"][ctxoff:], ctxcnt)
                                  if dialect == 0x0311 and ctxcnt else [])

    def _dec_create(self, h, b):
        (ss, oplock, _fl, action, ct, at, wt, cht, alloc, eof, attr, _r2,
         fid_p, fid_v, ccoff, cclen) = struct.unpack_from("<HBBIqqqqqqIIQQII", b, 0)
        h.update({"OplockLevel": oplock, "CreateAction": action,
                  "CreationTime": ct, "LastAccessTime": at, "LastWriteTime": wt,
                  "ChangeTime": cht, "AllocationSize": alloc, "EndOfFile": eof,
                  "FileAttributes": attr,
                  "FileId": struct.pack("<QQ", fid_p, fid_v),
                  "CreateContextsOffset": ccoff, "CreateContextsLength": cclen})
        h["CreateContexts"] = (parse_create_contexts(h["raw"][ccoff:ccoff + cclen])
                               if cclen else [])

    def _dec_ioctl(self, h, b):
        (ss, _r, ctl, fid_p, fid_v, ioff, ilen, ooff, olen, flags,
         _r2) = struct.unpack_from("<HHIQQIIIIII", b, 0)
        h.update({"CtlCode": ctl, "FileId": struct.pack("<QQ", fid_p, fid_v),
                  "InputOffset": ioff, "InputCount": ilen,
                  "OutputOffset": ooff, "OutputCount": olen,
                  "IoctlFlags": flags, "Buffer": h["raw"][ooff:ooff + olen]})

    def _dec_oplock(self, h, b):
        ss = struct.unpack_from("<H", b, 0)[0]
        if ss == 44:
            h.update({"BreakType": "lease", "NewEpoch": struct.unpack_from("<H", b, 2)[0],
                      "BreakFlags": struct.unpack_from("<I", b, 4)[0],
                      "LeaseKey": b[8:24],
                      "CurrentLeaseState": struct.unpack_from("<I", b, 24)[0],
                      "NewLeaseState": struct.unpack_from("<I", b, 28)[0]})
        else:
            h.update({"BreakType": "oplock", "OplockLevel": b[2],
                      "FileId": b[8:24]})

    # -- NEGOTIATE ---------------------------------------------------------
    def smb2_negotiate(self, dialects, contexts=None, client_guid=None,
                       security_mode=1, capabilities=0):
        """SMB2 NEGOTIATE, with 3.1.1 negotiate contexts ([MS-SMB2] 2.2.3).

        *capabilities* is sent verbatim; Samba only returns an
        SMB2_ENCRYPTION_CAPABILITIES context when SMB2_GLOBAL_CAP_ENCRYPTION is
        set, so pass ``capabilities=client.client_capabilities`` for encryption.
        """
        if client_guid is None:
            client_guid = self.client_guid
        want_ctx = 0x0311 in dialects
        if contexts is None and want_ctx:
            contexts = [(SMB2_PREAUTH_INTEGRITY_CAPABILITIES,
                         preauth_integrity_context((1,), self.preauth_salt)),
                        (SMB2_ENCRYPTION_CAPABILITIES,
                         encryption_context(self.client_ciphers))]
            if self.client_signing_algorithms:
                contexts.append((SMB2_SIGNING_CAPABILITIES,
                                 signing_context(self.client_signing_algorithms)))
        body = struct.pack("<HHHHI16sIHH", 36, len(dialects), security_mode, 0,
                           capabilities, client_guid, 0, 0, 0)
        body += b"".join(struct.pack("<H", d) for d in dialects)
        if contexts:
            pad = (-(64 + len(body))) % 8
            body += b"\x00" * pad
            ctxoff = 64 + len(body)
            blob = b""
            for i, (ctype, data) in enumerate(contexts):
                blob += build_negotiate_context(ctype, data)
                if i != len(contexts) - 1:
                    blob += b"\x00" * ((-len(blob)) % 8)
            body = (body[:28] + struct.pack("<IHH", ctxoff, len(contexts), 0)
                    + body[36:] + blob)
        else:
            ctxoff = 0
        self.client_dialects = list(dialects)
        self.preauth_hash = b"\x00" * 64
        r = self.smb2_command(SMB2_NEGOTIATE, body, session_id=0, tree_id=0,
                              credit_request=self.initial_credit_request,
                              sign=False, encrypt=False)
        if want_ctx:
            self.preauth_hash = hashlib.sha512(
                b"\x00" * 64 + self.last_request).digest()
            self.preauth_hash = hashlib.sha512(
                self.preauth_hash + r["raw"]).digest()
        if r["status"] == STATUS_SUCCESS:
            self.dialect = r["DialectRevision"]
            self.server_capabilities = r["Capabilities"]
            self.server_security_mode = r["SecurityMode"]
            self.max_transact = r["MaxTransactSize"]
            self.max_read = r["MaxReadSize"]
            self.max_write = r["MaxWriteSize"]
            self.signing_required = bool(r["SecurityMode"]
                                         & SMB2_NEGOTIATE_SIGNING_REQUIRED)
            if self.dialect >= 0x0300:
                self.cipher_id = SMB2_ENCRYPTION_AES128_CCM
            for ctx in r.get("NegotiateContexts", []):
                if ctx["ContextType"] == SMB2_ENCRYPTION_CAPABILITIES:
                    self.cipher_id = ctx["Ciphers"][0] if ctx.get("Ciphers") else 0
                elif ctx["ContextType"] == SMB2_SIGNING_CAPABILITIES:
                    if ctx.get("SigningAlgorithms"):
                        self.signing_algorithm = ctx["SigningAlgorithms"][0]
                elif ctx["ContextType"] == SMB2_PREAUTH_INTEGRITY_CAPABILITIES:
                    self.preauth_hash_id = ctx["HashAlgorithms"][0] \
                        if ctx.get("HashAlgorithms") else 0
        return r

    # -- SESSION SETUP -----------------------------------------------------
    def smb2_session_setup(self, user, password, domain=""):
        """SMB2 SESSION_SETUP: SPNEGO/NTLMv2 plus key derivation ([MS-SMB2] 3.2.4.2.3)."""
        neg = ntlm_negotiate()
        blob = spnego_neg_token_init(neg)
        session_key = b""
        chal = None
        sid = 0
        r = None
        for _ in range(4):
            r = self._smb2_ssetup_round(blob, sid, defer_verify=True)
            if self.dialect == 0x0311:
                self.preauth_hash = hashlib.sha512(
                    self.preauth_hash + self.last_request).digest()
            sid = r["SessionId"] or sid
            self.session_id = sid
            if r["status"] != STATUS_MORE_PROCESSING_REQUIRED:
                break
            if self.dialect == 0x0311:
                self.preauth_hash = hashlib.sha512(self.preauth_hash + r["raw"]).digest()
            tok = spnego_parse(r["SecurityBuffer"])
            mech = tok["mechToken"] or b""
            if mech[:8] == b"NTLMSSP\x00" and mech[8:12] == b"\x02\x00\x00\x00":
                chal = ntlm_parse_challenge(mech)
                auth, session_key = ntlm_authenticate(
                    chal, user, password, domain or None, negotiate_msg=neg,
                    use_mic=self.use_ntlm_mic)
                mic = None
                if self.use_ntlm_mic:
                    mic = ntlm_mac(session_key, spnego_mech_list())
                blob = spnego_neg_token_resp(auth, mech_list_mic=mic)
            elif tok["mechListMIC"] and not mech:
                blob = spnego_neg_token_resp(
                    mech_list_mic=ntlm_mac(session_key, spnego_mech_list(), 0))
            else:
                raise SmbProbeError(
                    "unexpected SPNEGO token in SESSION_SETUP response")
        self.session_key = session_key
        if session_key:
            self._derive_keys(session_key)
        r["SessionKey"] = session_key
        r["NtlmChallenge"] = chal
        r["PreauthIntegrityHashValue"] = self.preauth_hash
        r["Keys"] = dict(self.keys)
        if r["Flags"] & SMB2_FLAGS_SIGNED and self.keys.get("signing"):
            r["SignatureValid"] = self.verify_signature(r["raw"])
            if self.verify_signatures and not r["SignatureValid"]:
                raise SmbProbeError("final SESSION_SETUP response signature invalid")
        if r["status"] == STATUS_SUCCESS:
            self.session_id = r["SessionId"] or sid
            if r.get("SessionFlags", 0) & SMB2_SESSION_FLAG_ENCRYPT_DATA:
                self.encrypt = True
            # [MS-SMB2] 3.2.5.3.1: at 3.1.1 a session that established a signing
            # key signs everything after the session setup, whatever the
            # server's SecurityMode said.
            if self.dialect >= 0x0311 and self.keys.get("signing") and not self.encrypt:
                self.signing_required = True
        return r

    def _smb2_ssetup_round(self, blob, sid, defer_verify=False):
        body = struct.pack("<HBBIIHHQ", 25, 0, self.client_security_mode,
                           self.client_capabilities, 0, 88, len(blob), 0) + blob
        prev = self.verify_signatures
        if defer_verify:
            self.verify_signatures = False
        try:
            return self.smb2_command(SMB2_SESSION_SETUP, body, session_id=sid,
                                     tree_id=0, sign=False, encrypt=False,
                                     credit_request=self.initial_credit_request)
        finally:
            self.verify_signatures = prev

    def _derive_keys(self, session_key):
        self.keys = {}
        if self.dialect >= 0x0311:
            ctx = self.preauth_hash
            if self.preauth_corrupt:
                ctx = bytes([ctx[0] ^ 0xFF]) + ctx[1:]
            self.keys["signing"] = sp800_108_kdf(session_key, b"SMBSigningKey\x00", ctx)
            self.keys["application"] = sp800_108_kdf(session_key, b"SMBAppKey\x00", ctx)
            self.keys["encryption"] = sp800_108_kdf(session_key, b"SMBC2SCipherKey\x00", ctx)
            self.keys["decryption"] = sp800_108_kdf(session_key, b"SMBS2CCipherKey\x00", ctx)
        elif self.dialect >= 0x0300:
            self.keys["signing"] = sp800_108_kdf(session_key, b"SMB2AESCMAC\x00", b"SmbSign\x00")
            self.keys["application"] = sp800_108_kdf(session_key, b"SMB2APP\x00", b"SmbRpc\x00")
            self.keys["encryption"] = sp800_108_kdf(session_key, b"SMB2AESCCM\x00", b"ServerIn \x00")
            self.keys["decryption"] = sp800_108_kdf(session_key, b"SMB2AESCCM\x00", b"ServerOut\x00")
        else:
            self.keys["signing"] = session_key
            self.keys["application"] = session_key
            self.keys["encryption"] = None
            self.keys["decryption"] = None

    # -- TREE / FILE ops ---------------------------------------------------
    def smb2_tree_connect(self, unc, **kw):
        """SMB2 TREE_CONNECT ([MS-SMB2] 2.2.9)."""
        if not unc.startswith("\\\\"):
            unc = "\\\\%s\\%s" % (self.host, unc)
        path = _u(unc)
        body = struct.pack("<HHHH", 9, 0, 64 + 8, len(path)) + path
        r = self.smb2_command(SMB2_TREE_CONNECT, body, tree_id=0, **_passthru(kw))
        if r["status"] == STATUS_SUCCESS:
            self.tree_id = r["TreeId"]
            self.share_flags = r["ShareFlags"]
            if r["ShareFlags"] & SMB2_SHAREFLAG_ENCRYPT_DATA:
                self.encrypt = True
        return r

    def smb2_tree_disconnect(self):
        """SMB2 TREE_DISCONNECT ([MS-SMB2] 2.2.11)."""
        r = self.smb2_command(SMB2_TREE_DISCONNECT, struct.pack("<HH", 4, 0))
        if r["status"] == STATUS_SUCCESS:
            self.tree_id = 0
        return r

    def smb2_logoff(self):
        """SMB2 LOGOFF ([MS-SMB2] 2.2.7)."""
        r = self.smb2_command(SMB2_LOGOFF, struct.pack("<HH", 4, 0), tree_id=0)
        if r["status"] == STATUS_SUCCESS:
            self.session_id = 0
            self.encrypt = False
        return r

    # -- request-body builders (also used to compose compound chains) ------
    SMB2_LAST_FILE_ID = b"\xff" * 16

    def body_create(self, name, **kw):
        """Build an SMB2 CREATE request body ([MS-SMB2] 2.2.13)."""
        oplock = kw.get("oplock", SMB2_OPLOCK_LEVEL_NONE)
        impersonation = kw.get("impersonation", 2)
        access = kw.get("desired_access", FILE_READ_DATA | FILE_WRITE_DATA
                        | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES
                        | DELETE | READ_CONTROL | SYNCHRONIZE)
        attrs = kw.get("file_attributes", 0)
        share = kw.get("share_access", FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
        disp = kw.get("create_disposition", FILE_OPEN_IF)
        opts = kw.get("create_options", 0)
        contexts = kw.get("contexts")
        nm = _u(name)
        blob = build_create_contexts(contexts) if contexts else b""
        name_off = 120
        body = struct.pack("<HBBIQQIIIIIHHII", 57, 0, oplock, impersonation,
                           0, 0, access, attrs, share, disp, opts,
                           name_off, len(nm), 0, 0)
        buf = nm if nm else b"\x00\x00"
        if blob:
            buf += b"\x00" * ((-(name_off + len(buf))) % 8)
            body = body[:48] + struct.pack("<II", name_off + len(buf), len(blob)) + body[56:]
            buf += blob
        return body + buf

    def body_close(self, file_id, flags=0):
        """Build an SMB2 CLOSE request body ([MS-SMB2] 2.2.15)."""
        return struct.pack("<HHI", 24, flags, 0) + file_id

    def body_flush(self, file_id):
        """Build an SMB2 FLUSH request body ([MS-SMB2] 2.2.17)."""
        return struct.pack("<HHI", 24, 0, 0) + file_id

    def body_read(self, file_id, offset, length, minimum_count=0, channel=0,
                  remaining_bytes=0, flags=0):
        """Build an SMB2 READ request body ([MS-SMB2] 2.2.19)."""
        return struct.pack("<HBBIQ16sIIIHH", 49, 0, flags, length, offset,
                           file_id, minimum_count, channel, remaining_bytes,
                           0, 0) + b"\x00"

    def body_write(self, file_id, offset, data, flags=0, channel=0,
                   remaining_bytes=0):
        """Build an SMB2 WRITE request body ([MS-SMB2] 2.2.21)."""
        return struct.pack("<HHIQ16sIIHHI", 49, 64 + 48, len(data), offset,
                           file_id, channel, remaining_bytes, 0, 0, flags) + data

    def body_query_info(self, file_id, info_type, info_class,
                        output_buffer_length=65536, additional_information=0,
                        flags=0, input_buffer=b""):
        """Build an SMB2 QUERY_INFO request body ([MS-SMB2] 2.2.37)."""
        return struct.pack("<HBBIHHIII16s", 41, info_type, info_class,
                           output_buffer_length, 64 + 40 if input_buffer else 0,
                           0, len(input_buffer), additional_information,
                           flags, file_id) + input_buffer

    def body_set_info(self, file_id, info_type, info_class, buf,
                      additional_information=0):
        """Build an SMB2 SET_INFO request body ([MS-SMB2] 2.2.39)."""
        return struct.pack("<HBBIHHI16s", 33, info_type, info_class, len(buf),
                           64 + 32, 0, additional_information, file_id) + buf

    def body_query_directory(self, file_id, info_class, pattern="*", flags=0,
                             file_index=0, output_buffer_length=65536):
        """Build an SMB2 QUERY_DIRECTORY request body ([MS-SMB2] 2.2.33)."""
        pat = _u(pattern)
        return struct.pack("<HBBI16sHHI", 33, info_class, flags, file_index,
                           file_id, 64 + 32, len(pat), output_buffer_length) + pat

    def smb2_create(self, name, **kw):
        """SMB2 CREATE, with create contexts ([MS-SMB2] 2.2.13)."""
        oplock = kw.get("oplock", SMB2_OPLOCK_LEVEL_NONE)
        impersonation = kw.get("impersonation", 2)
        access = kw.get("desired_access", FILE_READ_DATA | FILE_WRITE_DATA
                        | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES
                        | DELETE | READ_CONTROL | SYNCHRONIZE)
        attrs = kw.get("file_attributes", 0)
        share = kw.get("share_access", FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
        disp = kw.get("create_disposition", FILE_OPEN_IF)
        opts = kw.get("create_options", 0)
        contexts = kw.get("contexts")
        r = self.smb2_command(SMB2_CREATE, self.body_create(name, **kw),
                              **_passthru(kw))
        if r["status"] == STATUS_SUCCESS:
            self.last_file_id = r["FileId"]
        return r

    def smb2_close(self, file_id, flags=0):
        """SMB2 CLOSE ([MS-SMB2] 2.2.15)."""
        return self.smb2_command(SMB2_CLOSE, self.body_close(file_id, flags))

    def smb2_flush(self, file_id):
        """SMB2 FLUSH ([MS-SMB2] 2.2.17)."""
        return self.smb2_command(SMB2_FLUSH, self.body_flush(file_id))

    def smb2_read(self, file_id, offset, length, minimum_count=0, channel=0,
                  remaining_bytes=0, flags=0):
        """SMB2 READ ([MS-SMB2] 2.2.19)."""
        body = self.body_read(file_id, offset, length, minimum_count, channel,
                              remaining_bytes, flags)
        return self.smb2_command(SMB2_READ, body,
                                 credit_charge=self._credit_charge(length))

    def smb2_write(self, file_id, offset, data, flags=0, channel=0,
                   remaining_bytes=0):
        """SMB2 WRITE ([MS-SMB2] 2.2.21)."""
        body = self.body_write(file_id, offset, data, flags, channel, remaining_bytes)
        return self.smb2_command(SMB2_WRITE, body,
                                 credit_charge=self._credit_charge(len(data)))

    def smb2_lock(self, file_id, locks):
        """SMB2 LOCK; locks = [(offset, length, flags)] ([MS-SMB2] 2.2.26)."""
        self.lock_sequence = (self.lock_sequence + 1) & 0xF
        seq = 0 if self.dialect == 0x0202 else (self.lock_sequence << 28)
        body = struct.pack("<HHI", 48, len(locks), seq) + file_id
        for off, ln, fl in locks:
            body += struct.pack("<QQII", off, ln, fl, 0)
        return self.smb2_command(SMB2_LOCK, body)

    def smb2_ioctl(self, file_id, ctl_code, in_buf=b"", max_out=65536, flags=1):
        """SMB2 IOCTL ([MS-SMB2] 2.2.31)."""
        body = struct.pack("<HHI16sIIIIIIII", 57, 0, ctl_code, file_id,
                           64 + 56, len(in_buf), 0, 0, 0, max_out,
                           flags, 0) + in_buf
        return self.smb2_command(SMB2_IOCTL, body,
                                 credit_charge=self._credit_charge(max(len(in_buf), max_out)))

    def smb2_echo(self):
        """SMB2 ECHO ([MS-SMB2] 2.2.28)."""
        return self.smb2_command(SMB2_ECHO, struct.pack("<HH", 4, 0), tree_id=0)

    def smb2_cancel(self, message_id, async_id=0):
        """SMB2 CANCEL for an outstanding request ([MS-SMB2] 2.2.30)."""
        charge = self._credit_charge(64)
        pdu = self.smb2_header(SMB2_CANCEL,
                               flags=SMB2_FLAGS_ASYNC_COMMAND if async_id else 0,
                               credit_charge=charge, credit_request=0,
                               message_id=message_id, session_id=self.session_id,
                               tree_id=self.tree_id)
        if async_id:
            pdu = pdu[:32] + struct.pack("<Q", async_id) + pdu[40:]
        pdu += struct.pack("<HH", 4, 0)
        if self.signing_required and self.keys.get("signing"):
            pdu = pdu[:16] + struct.pack("<I", struct.unpack_from("<I", pdu, 16)[0]
                                         | SMB2_FLAGS_SIGNED) + pdu[20:]
            pdu = self.sign_pdu(pdu)
        self.last_request = pdu
        self._send(self.encrypt_pdu(pdu) if self.encrypt else pdu)
        return {"status": None, "raw": b"", "MessageId": message_id, "Sent": True}

    def smb2_query_info(self, file_id, info_type, info_class,
                        output_buffer_length=65536, additional_information=0,
                        flags=0, input_buffer=b""):
        """SMB2 QUERY_INFO ([MS-SMB2] 2.2.37)."""
        body = self.body_query_info(file_id, info_type, info_class,
                                    output_buffer_length, additional_information,
                                    flags, input_buffer)
        r = self.smb2_command(SMB2_QUERY_INFO, body,
                              credit_charge=self._credit_charge(output_buffer_length))
        buf = r.get("Buffer", b"")
        if buf and r["status"] in (STATUS_SUCCESS, STATUS_BUFFER_OVERFLOW):
            r["Decoded"] = _decode_info(info_type, info_class, buf)
        return r

    def smb2_set_info(self, file_id, info_type, info_class, buf,
                      additional_information=0):
        """SMB2 SET_INFO ([MS-SMB2] 2.2.39)."""
        body = self.body_set_info(file_id, info_type, info_class, buf,
                                  additional_information)
        return self.smb2_command(SMB2_SET_INFO, body)

    def smb2_query_directory(self, file_id, info_class, pattern="*", flags=0,
                             file_index=0, output_buffer_length=65536):
        """SMB2 QUERY_DIRECTORY ([MS-SMB2] 2.2.33)."""
        body = self.body_query_directory(file_id, info_class, pattern, flags,
                                         file_index, output_buffer_length)
        r = self.smb2_command(SMB2_QUERY_DIRECTORY, body,
                              credit_charge=self._credit_charge(output_buffer_length))
        if r["status"] in (STATUS_SUCCESS, STATUS_BUFFER_OVERFLOW) and r.get("Buffer"):
            r["Entries"] = decode_directory_entries(r["Buffer"], info_class)
        return r

    def smb2_change_notify(self, file_id, completion_filter=0x00000FFF, flags=0,
                           output_buffer_length=4096, expect_reply=True):
        """SMB2 CHANGE_NOTIFY ([MS-SMB2] 2.2.35)."""
        body = struct.pack("<HHI16sII", 32, flags, output_buffer_length,
                           file_id, completion_filter, 0)
        return self.smb2_command(SMB2_CHANGE_NOTIFY, body,
                                 expect_reply=expect_reply)

    def smb2_oplock_break_ack(self, file_id, oplock_level=SMB2_OPLOCK_LEVEL_NONE):
        """SMB2 OPLOCK_BREAK acknowledgement ([MS-SMB2] 2.2.24.1)."""
        body = struct.pack("<HBBI", 24, oplock_level, 0, 0) + file_id
        return self.smb2_command(SMB2_OPLOCK_BREAK, body)

    def smb2_lease_break_ack(self, lease_key, lease_state):
        """SMB2 LEASE_BREAK acknowledgement ([MS-SMB2] 2.2.24.2)."""
        body = (struct.pack("<HHI", 36, 0, 0) + lease_key
                + struct.pack("<IQ", lease_state, 0))
        return self.smb2_command(SMB2_OPLOCK_BREAK, body)

    def smb2_compound(self, requests, related=False, encrypt=None, sign=None):
        """Send a compounded request chain ([MS-SMB2] 3.2.4.1.4).

        *requests* is a list of ``(command, body)``; when *related* is true every
        message after the first carries SMB2_FLAGS_RELATED_OPERATIONS.
        """
        pdus = []
        ids = []
        for i, (command, body) in enumerate(requests):
            charge = self._credit_charge(len(body) + 64)
            mid = self._next_message_id(charge)
            ids.append(mid)
            flags = SMB2_FLAGS_RELATED_OPERATIONS if (related and i) else 0
            pdu = self.smb2_header(command, flags=flags, credit_charge=charge,
                                   credit_request=1, message_id=mid,
                                   session_id=self.session_id,
                                   tree_id=self.tree_id) + body
            pdus.append(bytearray(pdu))
        blob = b""
        for i, pdu in enumerate(pdus):
            if i != len(pdus) - 1:
                pad = (-len(pdu)) % 8
                pdu += b"\x00" * pad
                struct.pack_into("<I", pdu, 20, len(pdu))
            do_sign = sign
            if do_sign is None:
                do_sign = bool(self.signing_required and self.keys.get("signing"))
            if do_sign:
                struct.pack_into("<I", pdu, 16,
                                 struct.unpack_from("<I", pdu, 16)[0] | SMB2_FLAGS_SIGNED)
                pdu = self.sign_pdu(bytes(pdu))
            blob += bytes(pdu)
            self.credits -= 1
        self.last_request = blob
        do_enc = self.encrypt if encrypt is None else encrypt
        self._send(self.encrypt_pdu(blob) if do_enc else blob)
        r = self._smb2_receive(ids[0])
        r["MessageIds"] = ids
        return r

    def wait_for_notification(self, timeout=10.0):
        """Block for an unsolicited server message (oplock/lease break, etc.)."""
        if self.notifications:
            return self.notifications.pop(0)
        old = self.sock.gettimeout()
        self.sock.settimeout(timeout)
        try:
            raw = self._recv()
        finally:
            self.sock.settimeout(old)
        encrypted = raw[:4] == SMB2_TRANSFORM_PROTOCOL_ID
        pdu = self.decrypt_pdu(raw) if encrypted else raw
        return self._smb2_split(pdu, encrypted, raw)[0]


def _passthru(kw):
    out = {}
    for k in ("session_id", "tree_id", "flags", "credit_charge",
              "credit_request", "message_id", "sign", "encrypt", "expect_reply"):
        if k in kw:
            out[k] = kw[k]
    return out


def _decode_info(info_type, info_class, buf):
    if info_type == SMB2_0_INFO_FILE:
        return {FileBasicInformation: decode_file_basic_information,
                FileStandardInformation: decode_file_standard_information,
                FileAllInformation: decode_file_all_information,
                FileNetworkOpenInformation: decode_file_network_open_information,
                }.get(info_class, lambda b: {"Raw": b})(buf)
    if info_type == SMB2_0_INFO_FILESYSTEM:
        return decode_fs_information(buf, info_class)
    return {"Raw": buf}


# ---------------------------------------------------------------------------
# Client
# ---------------------------------------------------------------------------

class SmbClient(_Smb1Mixin, _Smb2Mixin):
    """Raw SMB1/SMB2/SMB3 client over direct TCP ([MS-SMB2] 2.1)."""

    def __init__(self, host="127.0.0.1", port=445, timeout=10.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None

        # transport / SMB1 state
        self.smb1_tid = 0
        self.smb1_uid = 0
        self.smb1_mid = 0
        self.smb1_pid = os.getpid() & 0xFFFF
        self.smb1_flags2 = SMB1_DEFAULT_FLAGS2
        self.smb1_maxbuf = 16644
        self.smb1_negotiated = None
        self.smb1_session_key = b""

        # SMB2 connection state
        self.client_guid = os.urandom(16)
        self.client_dialects = []
        self.dialect = 0x0202
        self.message_id = 0
        self.credits = 1
        self.initial_credit_request = 64
        self.session_id = 0
        self.tree_id = 0
        self.share_flags = 0
        self.server_capabilities = 0
        self.server_security_mode = 0
        self.max_transact = 65536
        self.max_read = 65536
        self.max_write = 65536
        self.lock_sequence = 0
        self.last_file_id = None
        self.last_request = b""
        self.last_interim = None
        self.last_transform = None
        self.notifications = []

        # client-advertised capabilities
        self.client_security_mode = SMB2_NEGOTIATE_SIGNING_ENABLED
        self.client_capabilities = (SMB2_GLOBAL_CAP_DFS | SMB2_GLOBAL_CAP_LEASING
                                    | SMB2_GLOBAL_CAP_LARGE_MTU
                                    | SMB2_GLOBAL_CAP_ENCRYPTION)
        self.client_ciphers = (SMB2_ENCRYPTION_AES128_GCM, SMB2_ENCRYPTION_AES128_CCM)
        self.client_signing_algorithms = ()
        self.preauth_salt = None
        self.preauth_corrupt = False
        self.use_ntlm_mic = True

        # crypto knobs the harness drives directly.  ``sign_next`` is a
        # one-shot override of the signing decision for the next request
        # (True/False), consumed when that request is sent.
        self.signing_required = False
        self.verify_signatures = True
        self.sign_next = None
        self.encrypt = False
        self.preauth_hash = b"\x00" * 64
        self.preauth_hash_id = 1
        self.cipher_id = 0
        self.signing_algorithm = SMB2_SIGNING_AES_CMAC
        self.session_key = b""
        self.keys = {"signing": None, "encryption": None,
                     "decryption": None, "application": None}

    # -- transport ---------------------------------------------------------
    def connect(self):
        """Open the direct-TCP transport ([MS-SMB2] 2.1)."""
        try:
            self.sock = socket.create_connection((self.host, self.port), self.timeout)
        except OSError as exc:
            raise SmbProbeError("connect to %s:%d failed: %s"
                                % (self.host, self.port, exc)) from exc
        self.sock.settimeout(self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return self

    def close(self):
        """Close the transport."""
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self):
        return self.connect()

    def __exit__(self, *_exc):
        self.close()

    def _send(self, pdu):
        if self.sock is None:
            raise SmbProbeError("not connected")
        frame = struct.pack(">I", len(pdu) & 0x00FFFFFF) + pdu
        try:
            self.sock.sendall(frame)
        except OSError as exc:
            raise SmbProbeError("send failed: %s" % exc) from exc

    def _recvn(self, n):
        buf = b""
        while len(buf) < n:
            try:
                chunk = self.sock.recv(n - len(buf))
            except socket.timeout as exc:
                raise SmbProbeError("timeout waiting for %d bytes" % n) from exc
            except OSError as exc:
                raise SmbProbeError("recv failed: %s" % exc) from exc
            if not chunk:
                raise SmbProbeError("connection closed by peer after %d/%d bytes"
                                    % (len(buf), n))
            buf += chunk
        return buf

    def _recv(self):
        hdr = self._recvn(4)
        if hdr[0] != 0x00:
            raise SmbProbeError("unexpected NBT message type 0x%02x" % hdr[0])
        return self._recvn(int.from_bytes(hdr[1:4], "big"))

    def send_raw(self, pdu):
        """Send arbitrary bytes framed with the 4-byte direct-TCP header."""
        self._send(pdu)

    def recv_raw(self):
        """Receive one direct-TCP frame and return its payload unparsed."""
        return self._recv()
