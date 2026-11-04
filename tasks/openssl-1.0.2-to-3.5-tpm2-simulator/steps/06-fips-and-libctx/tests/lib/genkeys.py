#!/usr/bin/env python3
"""Derive the fixed key material the behaviour suite uses, into keys.json.

Everything here is computed from a hard-coded seed with no OpenSSL and no
randomness, so the same keys come out on any machine.  Deriving them outside
the library on purpose: if the generator used the engine's own
_cpri__GenerateKeyRSA, a migration that broke key generation would also
silently change every key-dependent expectation.

Run once; keys.json is the artefact the case generator reads.

Provenance.  The seed is the label handed to Stream, and Stream is SHA-256 in
counter mode over that label, so keys.json is a pure function of this file: no
RNG, no clock, no machine-dependent value.  gencases.py turns those keys into
cases.txt, and record.sh records what the baseline answers to them -- the
frozen snapshot in build/baseline, from
https://github.com/stwagnr/tpm2simulator at commit
e45324eba268723d39856111e7933c5c76238481 (2017-10-15), built by mkdrv.sh
with -DSPEC_VERSION=116 -DCMAKE_BUILD_TYPE=None -DCMAKE_C_FLAGS="-O1 -g", over
300 byte-identical replays.
"""
import hashlib
import json
import pathlib

# ---------------------------------------------------------------- integers

def egcd(a, b):
    if b == 0:
        return (a, 1, 0)
    g, x, y = egcd(b, a % b)
    return (g, y, x - (a // b) * y)


def inv(a, m):
    g, x, _ = egcd(a % m, m)
    if g != 1:
        raise ValueError("not invertible")
    return x % m


_SMALL = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]


def is_prime(n):
    if n < 2:
        return False
    for p in _SMALL:
        if n % p == 0:
            return n == p
    d, s = n - 1, 0
    while d % 2 == 0:
        d //= 2
        s += 1
    for a in _SMALL:                      # deterministic for n < 3.3e24, and
        x = pow(a, d, n)                  # overwhelmingly strong beyond it
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = x * x % n
            if x == n - 1:
                break
        else:
            return False
    return True


class Stream:
    """A deterministic byte stream: SHA-256 in counter mode over a label."""

    def __init__(self, label):
        self.label = label.encode()
        self.n = 0

    def bytes(self, k):
        out = b""
        while len(out) < k:
            out += hashlib.sha256(self.label + b"%08x" % self.n).digest()
            self.n += 1
        return out[:k]

    def int(self, bits):
        v = int.from_bytes(self.bytes((bits + 7) // 8), "big")
        return v >> (-bits % 8)


def gen_prime(stream, bits):
    while True:
        c = stream.int(bits) | (1 << (bits - 1)) | 1
        if is_prime(c):
            return c


# ------------------------------------------------------------------- RSA

def rsa_key(label, bits, e):
    """A key in the shape the engine wants: modulus, one prime, and the
    private exponent the engine's _cpri__TestKeyRSA would compute -- which
    is the inverse of e modulo (p-1)(q-1), not modulo lambda(n)."""
    s = Stream(label)
    while True:
        p = gen_prime(s, bits // 2)
        q = gen_prime(s, bits // 2)
        if p == q:
            continue
        phi = (p - 1) * (q - 1)
        if phi % e == 0:
            continue
        n = p * q
        if n.bit_length() != bits:
            continue
        return {
            "bits": bits, "e": e,
            "n": "%0*x" % (bits // 4, n),
            "p": "%0*x" % (bits // 8, p),
            "q": "%0*x" % (bits // 8, q),
            "d": "%0*x" % (bits // 4, inv(e, phi)),
        }


# ------------------------------------------------------------------- ECC

CURVES = {
    "NIST_P256": dict(
        p=0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff,
        a=0xffffffff00000001000000000000000000000000fffffffffffffffffffffffc,
        b=0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b,
        gx=0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296,
        gy=0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5,
        n=0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551,
        bytes=32),
    "NIST_P384": dict(
        p=int("fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
              "ffffffff0000000000000000ffffffff", 16),
        a=int("fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
              "ffffffff0000000000000000fffffffc", 16),
        b=int("b3312fa7e23ee7e4988e056be3f82d19181d9c6efe8141120314088f5013875a"
              "c656398d8a2ed19d2a85c8edd3ec2aef", 16),
        gx=int("aa87ca22be8b05378eb1c71ef320ad746e1d3b628ba79b9859f741e082542a38"
               "5502f25dbf55296c3a545e3872760ab7", 16),
        gy=int("3617de4a96262c6f5d9e98bf9292dc29f8f41dbd289a147ce9da3113b5f0b8c0"
               "0a60b1ce1d7e819d7a431d7c90ea0e5f", 16),
        n=int("ffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf"
              "581a0db248b0a77aecec196accc52973", 16),
        bytes=48),
    # The TPM's Barreto-Naehrig pairing curve, as the engine's own table
    # reports it (y^2 = x^3 + 3 over p, generator (1, 2)).
    "BN_P256": dict(
        p=0xfffffffffffcf0cd46e5f25eee71a49f0cdc65fb12980a82d3292ddbaed33013,
        a=0,
        b=3,
        gx=1,
        gy=2,
        n=0xfffffffffffcf0cd46e5f25eee71a49e0cdc65fb1299921af62d536cd10b500d,
        bytes=32),
}


def ec_add(c, P, Q):
    if P is None:
        return Q
    if Q is None:
        return P
    p = c["p"]
    if P[0] == Q[0] and (P[1] + Q[1]) % p == 0:
        return None
    if P == Q:
        lam = (3 * P[0] * P[0] + c["a"]) * inv(2 * P[1], p) % p
    else:
        lam = (Q[1] - P[1]) * inv(Q[0] - P[0], p) % p
    x = (lam * lam - P[0] - Q[0]) % p
    return (x, (lam * (P[0] - x) - P[1]) % p)


def ec_mul(c, k, P):
    R, Q = None, P
    while k:
        if k & 1:
            R = ec_add(c, R, Q)
        Q = ec_add(c, Q, Q)
        k >>= 1
    return R


def on_curve(c, P):
    if P is None:
        return False
    x, y = P
    return (y * y - x * x * x - c["a"] * x - c["b"]) % c["p"] == 0


def ecc_key(label, name):
    c = CURVES[name]
    s = Stream(label)
    while True:
        d = s.int(c["n"].bit_length())
        if 1 <= d < c["n"]:
            break
    Q = ec_mul(c, d, (c["gx"], c["gy"]))
    assert on_curve(c, Q), name
    w = c["bytes"] * 2
    return {"curve": name, "d": "%0*x" % (w, d),
            "qx": "%0*x" % (w, Q[0]), "qy": "%0*x" % (w, Q[1])}


def main():
    out = {
        "rsa": {
            "k1024e65537": rsa_key("tpm-suite/rsa/1024/65537", 1024, 65537),
            "k2048e65537": rsa_key("tpm-suite/rsa/2048/65537", 2048, 65537),
            "k1024e3": rsa_key("tpm-suite/rsa/1024/3", 1024, 3),
            "k2048e3": rsa_key("tpm-suite/rsa/2048/3", 2048, 3),
        },
        "ecc": {},
        "curves": {},
    }
    for name in CURVES:
        c = CURVES[name]
        w = c["bytes"] * 2
        out["curves"][name] = {
            "bytes": c["bytes"],
            "n": "%0*x" % (w, c["n"]),
            "p": "%0*x" % (w, c["p"]),
            "gx": "%0*x" % (w, c["gx"]),
            "gy": "%0*x" % (w, c["gy"]),
            # A valid x with no matching y: used for the off-curve rejections.
            "offx": "%0*x" % (w, c["gx"]),
            "offy": "%0*x" % (w, (c["gy"] + 1) % c["p"]),
        }
        for i in (1, 2):
            out["ecc"]["%s_%d" % (name, i)] = ecc_key(
                "tpm-suite/ecc/%s/%d" % (name, i), name)
    # Two extra scalars per curve for the commit/two-phase operations.
    for name in CURVES:
        c = CURVES[name]
        s = Stream("tpm-suite/ecc/%s/scalars" % name)
        w = c["bytes"] * 2
        out["curves"][name]["r1"] = "%0*x" % (w, s.int(c["n"].bit_length() - 1) | 1)
        out["curves"][name]["r2"] = "%0*x" % (w, s.int(c["n"].bit_length() - 1) | 1)

    path = pathlib.Path(__file__).with_name("keys.json")
    path.write_text(json.dumps(out, indent=1, sort_keys=True) + "\n")

    # Self-check: the arithmetic above, verified against itself, so a bad
    # inverse or a mis-transcribed curve cannot reach the case generator.
    for k in out["rsa"].values():
        n, p, q, d = (int(k[x], 16) for x in ("n", "p", "q", "d"))
        e = k["e"]
        assert p * q == n, "modulus mismatch"
        assert is_prime(p) and is_prime(q), "not prime"
        assert e * d % ((p - 1) * (q - 1)) == 1, "bad private exponent"
        m = 0x1234567890abcdef
        assert pow(pow(m, e, n), d, n) == m, "roundtrip failed"
    for k in out["ecc"].values():
        c = CURVES[k["curve"]]
        assert on_curve(c, (int(k["qx"], 16), int(k["qy"], 16)))
        assert ec_mul(c, int(k["d"], 16), (c["gx"], c["gy"])) == \
            (int(k["qx"], 16), int(k["qy"], 16))
    for name, cc in out["curves"].items():
        c = CURVES[name]
        assert on_curve(c, (int(cc["gx"], 16), int(cc["gy"], 16)))
        assert not on_curve(c, (int(cc["offx"], 16), int(cc["offy"], 16)))
    print("keys.json: %d rsa, %d ecc, %d curves -- all self-checks passed"
          % (len(out["rsa"]), len(out["ecc"]), len(out["curves"])))


if __name__ == "__main__":
    main()
