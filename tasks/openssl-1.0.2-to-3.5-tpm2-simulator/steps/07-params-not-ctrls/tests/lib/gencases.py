#!/usr/bin/env python3
"""Generate the behaviour-suite case file.

Only inputs are produced here.  Every expected answer comes from running this
file against the untouched baseline (see record.sh) -- nothing in this script
predicts what the engine will do.

Provenance.  These are inputs, so what has to be written down here is not an
answer but a derivation: every byte of the corpus is computed, none is drawn.
Messages come from msg(), which repeats the low byte of i*7+tag+1; key material
comes from keys.json, which genkeys.py derives from labelled SHA-256 counter
streams.  No RNG, no clock, no machine-dependent value, so this file is a pure
function of itself and cases.txt can be re-derived rather than merely trusted.
What the baseline answered to those cases is expected.txt, taken by record.sh
against the frozen snapshot in build/baseline -- the TCG TPM 2.0 reference
simulator at https://github.com/stwagnr/tpm2simulator, commit
e45324eba268723d39856111e7933c5c76238481 (2017-10-15), linking the OpenSSL
1.0.2b vendored in that tree -- built by mkdrv.sh with -DSPEC_VERSION=116
-DCMAKE_BUILD_TYPE=None -DCMAKE_C_FLAGS="-O1 -g", over 300 replays every one of
which had to come out byte-identical.

Case ids are <family>/<kind>/<nnn>, kind being "ok" for ordinary inputs and
"adv" for adversarial ones: malformed sizes, unimplemented algorithms, null
pointers, boundary lengths, values the engine is documented to refuse.  A
step's scored set is chosen by family, so the ids carry the grouping.

Deliberately absent, because the baseline takes the process down on them.  A
port to a modern OpenSSL is free to turn any of these into a clean error
return -- indeed it should -- so "must still segfault" is not an expectation
anyone can be asked to meet.  Each was confirmed under gdb; none is
reachable from the TPM layer, which is why the
baseline itself never trips over them.
  * hashcopy on a sequence-mode state -- _cpri__CopyHashState reinterprets a
    marshalled byte image as a live EVP_MD_CTX.
  * the _math__Mul success path -- BN_CTX_get without BN_CTX_start, then
    BN_CTX_end on an empty stack.  Its two refusal paths are exercised.
  * div with a quotient or remainder buffer smaller than the result -- BnTo2B
    computes the zero-fill count in a UINT16 and tests it for < 0, so a short
    buffer underflows to ~64KB of zero writes instead of CRYPT_UNDERFLOW.
  * _math__Comp with a null operand -- it reads a[0] before checking aSize.
  * rsasign with a null modulus -- the pAssert checks the key but not
    key->publicKey, which is then dereferenced for the signature size.
  * rsagen with a null seed -- _cpri__GenerateKeyRSA has no null check.
  * ECC verification under TPM_ALG_SM2 -- ValidateSignatureSM2Dsa has the same
    missing BN_CTX_start as _math__Mul.  SM2 *signing* works and is covered.
"""
import json
import pathlib

CASES = []
_SEQ = {}


def case(family, kind, *args):
    n = _SEQ.get((family, kind), 0) + 1
    _SEQ[(family, kind)] = n
    cid = "%s/%s/%03d" % (family, kind, n)
    CASES.append(" ".join([cid] + [str(a) for a in args]))


K = json.loads(pathlib.Path(__file__).with_name("keys.json").read_text())

HASHES = ["SHA1", "SHA256", "SHA384"]
BAD_HASHES = ["SHA512", "SM3_256", "NULL_ALG", "0x0000", "0x0099", "0xffff"]
DIGEST = {"SHA1": 20, "SHA256": 32, "SHA384": 48}

# Message lengths chosen around every block/padding boundary the three hashes
# have: 64-byte blocks with a 9-byte tail for SHA-1/256, 128-byte blocks with a
# 17-byte tail for SHA-384.
LENS = [0, 1, 2, 3, 55, 56, 57, 63, 64, 65, 110, 111, 112, 119, 120, 127, 128,
        129, 255, 256, 257, 1000]
# Relocation uses its own, shorter set: what has to survive the copy is the
# buffered partial block and the running chain value, so the lengths that
# matter are the ones either side of a block boundary, plus one long enough
# to have absorbed many blocks first.
MOVE_LENS = [0, 1, 55, 63, 64, 65, 127, 128, 200, 1024]


def msg(n, tag=0):
    """A reproducible message: the low byte of i*7+tag+1 repeated."""
    return "" if n == 0 else "".join("%02x" % ((i * 7 + tag + 1) & 0xFF)
                                     for i in range(n))


def tok(s):
    return s if s else "-"


# --------------------------------------------------------------- hash info

for h in HASHES + BAD_HASHES:
    case("hashinfo", "ok" if h in HASHES else "adv", "hashinfo", h)
for i in range(6):
    case("hashinfo", "ok" if i < 3 else "adv", "hashindex", i)
for i in (100, 65535):
    case("hashinfo", "adv", "hashindex", i)

# ------------------------------------------------------------- hash digest

for h in HASHES:
    for n in LENS:
        case("hash", "ok", "hashblock", h, DIGEST[h], tok(msg(n)))
    # Truncating output: _cpri__HashBlock keeps the most significant bytes.
    for out in (0, 1, DIGEST[h] // 2, DIGEST[h] - 1, DIGEST[h] + 1, 200):
        case("hash", "adv", "hashblock", h, out, tok(msg(40)))
for h in BAD_HASHES:
    case("hash", "adv", "hashblock", h, 32, tok(msg(40)))

# Start/update/complete, both context modes, against the same messages, so a
# port that gets one mode right and the other wrong cannot average out.
for h in HASHES:
    for seq in (0, 1):
        for n in (0, 1, 55, 64, 65, 128, 1000):
            case("hashseq", "ok", "hash", h, seq, DIGEST[h], tok(msg(n)))
        # Chunked exactly as a sequence object would feed it.
        case("hashseq", "ok", "hash", h, seq, DIGEST[h],
             msg(1), msg(1, 1), msg(1, 2))
        case("hashseq", "ok", "hash", h, seq, DIGEST[h],
             msg(63), msg(1, 1))
        case("hashseq", "ok", "hash", h, seq, DIGEST[h],
             msg(64), msg(64, 1))
        case("hashseq", "ok", "hash", h, seq, DIGEST[h],
             msg(100), "-", msg(100, 1))
        case("hashseq", "adv", "hash", h, seq, DIGEST[h], "-", "-", "-")
        case("hashseq", "adv", "hash", h, seq, 0, msg(40))
        case("hashseq", "adv", "hash", h, seq, DIGEST[h] - 3, msg(40))
        case("hashseq", "adv", "hash", h, seq, DIGEST[h] * 4, msg(40))
        # A completed state is inert: the second update and complete must not
        # restart it.
        case("hashstate", "ok", "hashafter", h, seq, tok(msg(40)))
    for n in (1, 64, 200):
        case("hashstate", "ok", "hashcopy", h, 0,
             tok(msg(n)), tok(msg(n, 1)), tok(msg(n, 2)))
    # Relocation gets the block boundaries and every shape the TPM layer puts a
    # sequence through: saved once, saved again, loaded twice from one blob,
    # saved and then flushed, and finished after the original already was.  A
    # state whose private data does not travel with the bytes of its container
    # answers differently here, and nowhere else in the engine's C interface.
    # The length that is swept is the one absorbed BEFORE the copy: what has to
    # travel with the bytes of the container is the buffered partial block and
    # the running chain value, and that is what the prefix length varies.  The
    # tails are fixed and pairwise different, so each state's digest names which
    # state produced it.
    for n in MOVE_LENS:
        case("hashstate", "ok", "hashmove", h,
             tok(msg(n)), msg(17, 1), msg(19, 2))
    for n in MOVE_LENS[::2]:
        case("hashstate", "ok", "hashmove2", h,
             tok(msg(n)), msg(13, 1), msg(29, 2))
        case("hashstate", "ok", "hashmove3", h,
             tok(msg(n)), msg(11, 1), msg(13, 2), msg(17, 3))
        case("hashstate", "ok", "hashmovedrop", h,
             tok(msg(n)), msg(23, 1))
        case("hashstate", "ok", "hashmoveafter", h,
             tok(msg(n)), msg(31, 1))
    case("hashstate", "adv", "hashcopy", h, 0, "-", "-", "-")
    case("hashstate", "adv", "hashmove", h, "-", "-", "-")
    case("hashstate", "adv", "hashmove2", h, "-", "-", "-")
    case("hashstate", "adv", "hashmove3", h, "-", "-", "-", "-")
    case("hashstate", "adv", "hashmovedrop", h, "-", "-")
    case("hashstate", "adv", "hashmoveafter", h, "-", "-")
    case("hashstate", "ok", "hashexport", h, msg(40), msg(40, 1))
    case("hashstate", "adv", "hashexport", h, "-", "-")
for h in BAD_HASHES:
    case("hashseq", "adv", "hash", h, 1, 32, tok(msg(40)))
    case("hashstate", "adv", "hashafter", h, 1, tok(msg(40)))

# ------------------------------------------------------------------- hmac

# RFC 4231 / RFC 2202 test vectors, which fix the answer independently of the
# library, followed by the key-length boundaries around the block size.
RFC = [
    ("0b" * 20, "4869205468657265"),                       # "Hi There"
    ("4a656665", "7768617420646f2079612077616e7420666f72206e6f7468696e673f"),
    ("aa" * 20, "dd" * 50),
    ("0102030405060708090a0b0c0d0e0f10111213141516171819", "cd" * 50),
    ("0c" * 20, "546573742057697468205472756e636174696f6e"),
    ("aa" * 131, "54657374205573696e67204c6172676572205468616e20426c6f636b2d"
                 "53697a65204b6579202d2048617368204b6579204669727374"),
    ("aa" * 131, "5468697320697320612074657374207573696e672061206c6172676572"
                 "207468616e20626c6f636b2d73697a65206b657920616e642061206c61"
                 "72676572207468616e20626c6f636b2d73697a6520646174612e205468"
                 "65206b6579206e6565647320746f20626520686173686564206265666f"
                 "7265206265696e6720757365642062792074686520484d414320616c67"
                 "6f726974686d2e"),
]
for h in HASHES:
    for key, data in RFC:
        for seq in (0, 1):
            case("hmac", "ok", "hmac", h, seq, DIGEST[h], key, data)
    for klen in (0, 1, 20, 63, 64, 65, 127, 128, 129, 200):
        case("hmac", "ok" if klen else "adv", "hmac", h, 1, DIGEST[h],
             tok(msg(klen, 3)), msg(48))
    for out in (0, 1, DIGEST[h] - 1, DIGEST[h] + 1, 200):
        case("hmac", "adv", "hmac", h, 1, out, "0b" * 20, msg(48))
    case("hmac", "adv", "hmac", h, 1, DIGEST[h], "NULL", msg(48))
    case("hmac", "ok", "hmac", h, 1, DIGEST[h], "0b" * 20, msg(30), msg(30, 1))
for h in BAD_HASHES:
    case("hmac", "adv", "hmac", h, 1, 32, "0b" * 20, msg(48))

# ------------------------------------------------------------------- mgf1

for h in HASHES:
    for n in (0, 1, 7, DIGEST[h] - 1, DIGEST[h], DIGEST[h] + 1,
              DIGEST[h] * 2, DIGEST[h] * 3 + 5, 255, 256, 512):
        case("mgf1", "ok" if n else "adv", "mgf1", h, n, msg(32, 4))
    for seed in ("-", "NULL", msg(1), msg(1000)):
        case("mgf1", "adv", "mgf1", h, 64, seed)
for h in BAD_HASHES:
    case("mgf1", "adv", "mgf1", h, 64, msg(32, 4))

# --------------------------------------------------------------- kdfa/kdfe

KEY32 = msg(32, 5)
CTX = msg(16, 6)
CTXV = msg(24, 7)
for h in HASHES:
    for bits in (8, 16, 128, 255, 256, 257, 384, 512, 1024):
        for once in (0, 1):
            # _cpri__KDFa asserts that a single-pass request is byte aligned.
            case("kdfa", "ok" if (once == 0 or bits % 8 == 0) else "adv",
                 "kdfa", h, KEY32, "ATH", CTX, CTXV, bits, once, "NULL")
    for ctr in ("NULL", 0, 1, 7, 4294967295):
        case("kdfa", "ok", "kdfa", h, KEY32, "ATH", CTX, CTXV, 256, 0, ctr)
    for lab in ("ATH", "A", "STORAGE", "NULL"):
        case("kdfa", "ok", "kdfa", h, KEY32, lab, CTX, CTXV, 256, 0, "NULL")
    for u, v in (("-", "-"), ("NULL", "NULL"), (CTX, "-"), ("-", CTXV),
                 (msg(200, 8), msg(200, 9))):
        case("kdfa", "ok", "kdfa", h, KEY32, "ATH", u, v, 256, 0, "NULL")
    for key in ("-", "NULL", msg(1, 10), msg(200, 11)):
        case("kdfa", "adv", "kdfa", h, key, "ATH", CTX, CTXV, 256, 0, "NULL")
    case("kdfa", "adv", "kdfa", h, KEY32, "ATH", CTX, CTXV, 0, 0, "NULL")

    for bits in (8, 128, 255, 256, 384, 512):
        case("kdfe", "ok", "kdfe", h, KEY32, "DUPLICATE", CTX, CTXV, bits)
    for u, v in (("-", "-"), ("NULL", "NULL"), (CTX, "-")):
        case("kdfe", "ok", "kdfe", h, KEY32, "IDENTITY", u, v, 256)
    for z in ("-", "NULL", msg(1, 12), msg(48, 13)):
        case("kdfe", "adv", "kdfe", h, z, "DUPLICATE", CTX, CTXV, 256)
    case("kdfe", "adv", "kdfe", h, KEY32, "DUPLICATE", CTX, CTXV, 0)
for h in BAD_HASHES:
    case("kdfa", "adv", "kdfa", h, KEY32, "ATH", CTX, CTXV, 256, 0, "NULL")
    case("kdfe", "adv", "kdfe", h, KEY32, "DUPLICATE", CTX, CTXV, 256)

# -------------------------------------------------------------- symmetric

AES128 = "2b7e151628aed2a6abf7158809cf4f3c"
AES256 = "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
PT = ["6bc1bee22e409f96e93d7e117393172a", "ae2d8a571e03ac9c9eb76fac45af8e51",
      "30c81c46a35ce411e5fbc1191a0a52ef", "f69f2445df4f9b17ad2b417be66c3710"]
IV = "000102030405060708090a0b0c0d0e0f"
CTRIV = "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"

for bits, key in ((128, AES128), (256, AES256)):
    for mode in ("ECB", "CBC", "CFB", "OFB", "CTR"):
        iv = "NULL" if mode == "ECB" else (CTRIV if mode == "CTR" else IV)
        for nblk in (1, 2, 3, 4):
            case("sym", "ok", "sym", mode, "e", bits, key, iv,
                 "".join(PT[:nblk]))
        if mode != "CBC":                      # no AES-CBC decrypt exists
            case("sym", "ok", "sym", mode, "d", bits, key, iv,
                 "".join(PT[:2]))
            case("symrt", "ok", "symroundtrip", mode, bits, key, iv,
                 "".join(PT))
        # Stream modes accept any length; the block modes must refuse one that
        # is not a whole number of blocks.
        for n in (0, 1, 15, 17, 31, 33):
            case("sym", "ok" if mode in ("CFB", "OFB", "CTR") else "adv",
                 "sym", mode, "e", bits, key, iv, tok(msg(n, 14)))
    # A short IV is a buffer the engine reads a full block out of, so only the
    # legal lengths are driven; the null-pointer refusals are below.
    for mode in ("CBC", "CFB", "OFB", "CTR"):
        case("sym", "adv", "sym", mode, "e", bits, key, "NULL", PT[0])
    case("sym", "adv", "sym", "ECB", "e", bits, "NULL", "NULL", PT[0])
    case("sym", "adv", "sym", "CBC", "e", bits, key, IV, "NULL")
for badbits in (0, 8, 64, 192, 255, 257, 512):
    case("sym", "adv", "sym", "ECB", "e", badbits,
         AES256[:badbits // 4] if 0 < badbits <= 256 else AES256, "NULL", PT[0])
for alg in ("AES", "SM4", "CAMELLIA", "NULL_ALG", "0x0000", "0xffff"):
    for bits in (0, 128, 192, 256, 512):
        case("symblock", "ok" if alg == "AES" else "adv", "symblock", alg, bits)
for mode in ("CBC", "ECB"):
    case("sym", "adv", "sym", mode, "d", 128, AES128, IV, PT[0])
for mode in ("SHA256", "NULL_ALG", "0x0000"):
    case("sym", "adv", "sym", mode, "e", 128, AES128, IV, PT[0])

# -------------------------------------------------------------------- rsa

def rsa(name):
    return K["rsa"][name]


for name in ("k1024e65537", "k2048e65537", "k1024e3", "k2048e3"):
    k = rsa(name)
    e, n, p, q, d = k["e"], k["n"], k["p"], k["q"], k["d"]
    half = len(n) // 2

    # The engine derives d from (n, p) or from (p, q); both routes must land
    # on the same private exponent, which keys.json computed independently.
    # The second route needs the modulus buffer sized as output space, so it
    # is handed a zero-filled TPM2B of the modulus length rather than the
    # modulus itself.
    case("rsakey", "ok", "rsatest", e, n, p, "NULL")
    case("rsakey", "ok", "rsatest", e, n, q, "NULL")
    # _cpri__TestKeyRSA asserts prime2->size < MAX_RSA_KEY_BYTES/2, and a
    # 2048-bit key's prime is exactly that, so the two-prime route is a
    # success case at 1024 bits and a refusal at 2048.
    twok = "ok" if k["bits"] <= 1024 else "adv"
    case("rsakey", twok, "rsatest", e, "00" * (len(n) // 2), p, q)
    case("rsakey", twok, "rsatest", e, n, p, q)
    case("rsakey", "adv", "rsatest", e, "-", p, q)
    case("rsakey", "adv", "rsatest", e, n, p[:-2] + "00", "NULL")
    case("rsakey", "adv", "rsatest", 4, n, p, "NULL")
    case("rsakey", "adv", "rsatest", 1, n, p, "NULL")
    case("rsakey", "adv", "rsatest", 0, n, p, "NULL")
    case("rsakey", "adv", "rsatest", e, n, "-", "NULL")
    case("rsakey", "adv", "rsatest", e, "NULL", p, "NULL")
    case("rsakey", "adv", "rsatest", e, n, "NULL", "NULL")

    nbytes = len(n) // 2
    # Raw (TPM_ALG_NULL padding) is the one scheme with no randomness, so the
    # ciphertext itself is an expectation rather than just the roundtrip.
    for tag, blob in (("small", msg(4, 20).rjust(nbytes * 2, "0")),
                      ("full", "01" + msg(nbytes - 1, 21))):
        case("rsaraw", "ok", "rsaenc", "NULL_ALG", "NULL_ALG", "NULL",
             e, n, blob)
        case("rsaraw", "ok", "rsadec", "NULL_ALG", "NULL_ALG", "NULL",
             e, n, d, blob)
    case("rsaraw", "adv", "rsaenc", "NULL_ALG", "NULL_ALG", "NULL",
         e, n, msg(nbytes - 1, 22))
    case("rsaraw", "adv", "rsaenc", "NULL_ALG", "NULL_ALG", "NULL",
         e, n, "ff" * nbytes)
    case("rsaraw", "adv", "rsadec", "NULL_ALG", "NULL_ALG", "NULL",
         e, n, d, msg(nbytes - 1, 23))
    case("rsaraw", "adv", "rsadec", "NULL_ALG", "NULL_ALG", "NULL",
         e, n, "-", msg(nbytes, 24))

    for pad, lab, hsh in (("RSAES", "NULL", "NULL_ALG"),
                          ("OAEP", "NULL", "SHA1"),
                          ("OAEP", "NULL", "SHA256"),
                          ("OAEP", "DUPLICATE", "SHA256"),
                          ("OAEP", "NULL", "SHA384")):
        for mlen in (0, 1, 16, 32):
            case("rsapad", "ok", "rsaroundtrip", pad, hsh, lab,
                 e, n, d, tok(msg(mlen, 25)))
        # Just past what the padding can carry.
        over = nbytes - 10 if pad == "RSAES" else nbytes - 2 * 20
        case("rsapad", "adv", "rsaroundtrip", pad, hsh, lab,
             e, n, d, msg(max(over, 1), 26))
        case("rsapad", "adv", "rsaroundtrip", pad, hsh, lab,
             e, n, d, msg(nbytes + 1, 27))
    case("rsapad", "adv", "rsaroundtrip", "OAEP", "SHA512", "NULL",
         e, n, d, msg(16, 28))
    case("rsapad", "adv", "rsaroundtrip", "RSAPSS", "SHA256", "NULL",
         e, n, d, msg(16, 29))
    case("rsapad", "adv", "rsaroundtrip", "0x0099", "SHA256", "NULL",
         e, n, d, msg(16, 30))

    # RSASSA is deterministic PKCS#1 v1.5, so the signature is the expectation.
    for h in HASHES:
        case("rsasign", "ok", "rsasign", "RSASSA", h, e, n, d,
             msg(DIGEST[h], 31))
        case("rsasign", "ok", "rsasignverify", "RSASSA", h, e, n, d,
             msg(DIGEST[h], 31), 0)
        # RSAPSS salts at random: only the sign/verify pair is stable.
        case("rsapss", "ok", "rsasignverify", "RSAPSS", h, e, n, d,
             msg(DIGEST[h], 32), 0)
        case("rsapss", "ok", "rsasignverify", "RSAPSS", h, e, n, d,
             msg(DIGEST[h], 32), DIGEST[h])
        case("rsasign", "adv", "rsasign", "RSASSA", h, e, n, d,
             msg(DIGEST[h] - 1, 33))
        case("rsasign", "adv", "rsasign", "RSASSA", h, e, n, d,
             msg(DIGEST[h] + 1, 34))
        case("rsasign", "adv", "rsasign", "RSASSA", h, e, n, "-",
             msg(DIGEST[h], 35))
    case("rsasign", "adv", "rsasign", "RSASSA", "SHA512", e, n, d, msg(64, 36))
    case("rsasign", "adv", "rsasign", "ECDSA", "SHA256", e, n, d, msg(32, 37))
    case("rsasign", "adv", "rsasign", "NULL_ALG", "SHA256", e, n, d,
         msg(32, 38))
    # A null modulus is excluded: _cpri__SignRSA dereferences it before any
    # check.  A null *private* key is fine and is covered above.

    if name == "k2048e65537":
        # Verification against a signature produced elsewhere in this file is
        # circular, so the corrupted-signature cases use bytes with no valid
        # structure at all: what is being pinned is the refusal.
        for bad in ("00" * nbytes, "ff" * nbytes, msg(nbytes, 40),
                    msg(nbytes - 1, 41), "-"):
            case("rsaverify", "adv", "rsaverify", "RSASSA", "SHA256", e, n,
                 msg(32, 42), bad, 0)
            case("rsaverify", "adv", "rsaverify", "RSAPSS", "SHA256", e, n,
                 msg(32, 42), bad, 0)

# Seeded key generation is deterministic but slow, so it is sampled rather
# than swept: two sizes, two exponents, two seeds.
for bits in (1024, 2048):
    for e in (65537, 3):
        for s in (0, 1):
            case("rsagen", "ok", "rsagen", bits, e, "SHA256",
                 msg(32, 50 + s), "RSA", "-", 0)
case("rsagen", "adv", "rsagen", 512, 65537, "SHA256", msg(32, 50), "RSA", "-", 0)
case("rsagen", "adv", "rsagen", 1023, 65537, "SHA256", msg(32, 50), "RSA", "-", 0)
case("rsagen", "adv", "rsagen", 4096, 65537, "SHA256", msg(32, 50), "RSA", "-", 0)
case("rsagen", "adv", "rsagen", 1024, 2, "SHA256", msg(32, 50), "RSA", "-", 0)
case("rsagen", "adv", "rsagen", 1024, 65537, "SHA512", msg(32, 50), "RSA", "-", 0)
case("rsagen", "adv", "rsagen", 1024, 65537, "SHA256", "-", "RSA", "-", 0)
case("rsagen", "adv", "rsagen", 1024, 65537, "SHA256", msg(32, 50), "NULL",
     "-", 0)
case("rsagen", "adv", "rsagen", 1024, 65537, "SHA256", msg(32, 50), "RSA",
     msg(16, 51), 0)
case("rsagen", "adv", "rsagen", 1024, 65537, "SHA256", msg(32, 50), "RSA",
     "-", "NULL")

# -------------------------------------------------------------------- ecc

CURVES = ["NIST_P256", "NIST_P384", "BN_P256"]
BAD_CURVES = ["NONE", "NIST_P192", "NIST_P224", "NIST_P521", "BN_P638",
              "SM2_P256", "0x00ff"]

case("eccinfo", "ok", "ecccurves")
for c in CURVES:
    case("eccinfo", "ok", "eccinfo", c)
for c in BAD_CURVES:
    case("eccinfo", "adv", "eccinfo", c)
for i in range(5):
    case("eccinfo", "ok" if i < 3 else "adv", "curvebyindex", i)

for c in CURVES:
    cc = K["curves"][c]
    nb = cc["bytes"]
    k1 = K["ecc"]["%s_1" % c]
    k2 = K["ecc"]["%s_2" % c]
    zero = "00" * nb
    one = "00" * (nb - 1) + "01"

    case("eccpoint", "ok", "eccpoint", c, cc["gx"], cc["gy"])
    case("eccpoint", "ok", "eccpoint", c, k1["qx"], k1["qy"])
    case("eccpoint", "ok", "eccpoint", c, k2["qx"], k2["qy"])
    case("eccpoint", "adv", "eccpoint", c, cc["offx"], cc["offy"])
    case("eccpoint", "adv", "eccpoint", c, zero, zero)
    case("eccpoint", "adv", "eccpoint", c, cc["p"], cc["gy"])
    case("eccpoint", "adv", "eccpoint", c, "-", "-")
    case("eccpoint", "adv", "eccpoint", c, "NULL", "NULL")
    case("eccpoint", "adv", "eccpoint", c, k1["qx"], k2["qy"])

    # Scalar multiplication of the generator, of a public point, and the
    # two-scalar form the commit protocol uses.
    for d in (one, "02".rjust(nb * 2, "0"), k1["d"], k2["d"], cc["r1"]):
        case("eccmul", "ok", "eccmul", c, d, "NULL", "NULL", "NULL")
        case("eccmul", "ok", "eccmul", c, d, k1["qx"], k1["qy"], "NULL")
    case("eccmul", "ok", "eccmul", c, "NULL", k1["qx"], k1["qy"], cc["r1"])
    case("eccmul", "ok", "eccmul", c, k1["d"], k2["qx"], k2["qy"], cc["r2"])
    case("eccmul", "adv", "eccmul", c, zero, "NULL", "NULL", "NULL")
    case("eccmul", "adv", "eccmul", c, cc["n"], "NULL", "NULL", "NULL")
    case("eccmul", "adv", "eccmul", c, "-", "NULL", "NULL", "NULL")
    case("eccmul", "adv", "eccmul", c, k1["d"], cc["offx"], cc["offy"], "NULL")
    case("eccmul", "adv", "eccmul", c, k1["d"], zero, zero, "NULL")
    case("eccmul", "adv", "eccmul", c, "NULL", "NULL", "NULL", "NULL")

    for h in HASHES:
        for s in (0, 1):
            case("eccgen", "ok", "eccgen", c, h, msg(32, 60 + s), "ECC",
                 "-", 0)
    case("eccgen", "ok", "eccgen", c, "SHA256", msg(32, 60), "ECC", "-", "NULL")
    case("eccgen", "ok", "eccgen", c, "SHA256", msg(32, 60), "NULL", "-", 0)
    case("eccgen", "adv", "eccgen", c, "SHA512", msg(32, 60), "ECC", "-", 0)
    case("eccgen", "adv", "eccgen", c, "SHA256", "-", "ECC", "-", 0)
    case("eccgen", "adv", "eccgen", c, "SHA256", "NULL", "ECC", "-", 0)
    case("eccephem", "ok", "eccephemeral", c)

    # ECSCHNORR and ECDAA take the nonce as an input, so their signatures are
    # reproducible.  ECDSA draws its own (_cpri__SignEcc drops kIn at
    # CpriECC.c:1776), so neither its signature nor its round-trip verdict is a
    # function of the inputs -- see the eccsv block below and the fixed-vector
    # eccverify cases that pin the verifier's behaviour instead.
    dg = msg(32, 61)
    for scheme in ("ECSCHNORR", "ECDAA"):
        for h in HASHES:
            case("eccsign", "ok", "eccsign", scheme, h, c, k1["d"], dg,
                 cc["r1"])
            case("eccsign", "ok", "eccsign", scheme, h, c, k2["d"], dg,
                 cc["r2"])
        case("eccsign", "adv", "eccsign", scheme, "SHA256", c, k1["d"], dg,
             zero)
        case("eccsign", "adv", "eccsign", scheme, "SHA256", c, k1["d"], dg,
             cc["n"])
        case("eccsign", "adv", "eccsign", scheme, "SHA256", c, "-", dg,
             cc["r1"])
        case("eccsign", "adv", "eccsign", scheme, "SHA512", c, k1["d"], dg,
             cc["r1"])
    # The ECDSA half of this family scores that signing succeeds and that both
    # components come back in [1, n-1], which holds for every nonce; the driver
    # withholds the round-trip verdict for ECDSA because it is a coin flip once
    # in 256 (CpriECC.c:1909).  ECSCHNORR takes the nonce it is handed, so it
    # keeps the round trip.
    for h in HASHES:
        case("eccsv", "ok", "eccsignverify", "ECDSA", h, c, k1["d"],
             k1["qx"], k1["qy"], dg, "NULL")
        case("eccsv", "ok", "eccsignverify", "ECSCHNORR", h, c, k1["d"],
             k1["qx"], k1["qy"], dg, cc["r1"])

    # Rehearse ecc_inrange() against values whose answers are known before the
    # run: the eccsv rrange/srange fields are only worth scoring if the check
    # behind them is right at the boundaries.
    nminus1 = "%0*x" % (len(cc["n"]), int(cc["n"], 16) - 1)
    for v in (zero, one, nminus1, cc["n"], k1["d"], "ff" * nb):
        case("eccrange", "ok", "eccrange", c, v)
    case("eccrange", "adv", "eccrange", c, "-")      # empty: reads as zero
    case("eccrange", "adv", "eccrange", c, "NULL")   # absent: no value at all
    # SM2 gets no signature case at all: SignSM2Dsa draws its own nonce and
    # ignores the one it is handed (two runs give two signatures), and the
    # verification side segfaults, so there is no stable pair to record.  The
    # scheme is still exercised through the deterministic ecc22 cases below.
    case("eccsv", "adv", "eccsignverify", "ECDH", "SHA256", c, k1["d"],
         k1["qx"], k1["qy"], dg, "NULL")
    case("eccsv", "adv", "eccsignverify", "0x0099", "SHA256", c, k1["d"],
         k1["qx"], k1["qy"], dg, "NULL")

    for bad in (zero, one, cc["n"], cc["r1"]):
        case("eccverify", "adv", "eccverify", "ECDSA", "SHA256", c,
             k1["qx"], k1["qy"], dg, bad, bad)
        case("eccverify", "adv", "eccverify", "ECSCHNORR", "SHA256", c,
             k1["qx"], k1["qy"], dg, bad, bad)
    # A valid key that did not make this signature, rather than an off-curve
    # point: an off-curve public key is not a question about this tree.  It
    # reaches EC_POINT_set_affine_coordinates_GFp at CpriECC.c:1895 before
    # any of the tree's own checks, and whether that call stores the point
    # or refuses it is a property of the linked OpenSSL, not of the port.
    # k2 is on the curve at every version, and rejecting a signature made
    # under another key is the thing the case is reaching for.
    case("eccverify", "adv", "eccverify", "ECDSA", "SHA256", c,
         k2["qx"], k2["qy"], dg, cc["r1"], cc["r2"])
    case("eccverify", "adv", "eccverify", "ECDSA", "SHA256", c,
         k1["qx"], k1["qy"], dg, "-", "-")

    # ValidateSignatureEcdsa truncates the digest to the length of r
    # (CpriECC.c:1909) where FIPS 186-4 truncates to the length of n, and
    # _cpri__ValidateSignatureEcc has already stripped r's leading zeros by
    # then.  So a signature over the 31-byte dg31 is accepted for the 32-byte
    # dg whenever r normalizes to 31 bytes: the verifier quietly throws dg's
    # last byte away.  Both pairs below are genuine signatures over dg31 under
    # k1, harvested from the baseline; they differ only in whether r came out
    # with a leading zero byte.  The full-length pair is the control -- for it
    # the same cross-digest check correctly fails.  Recorded for NIST_P256
    # only, since the vectors are curve- and key-specific.
    if c == "NIST_P256":
        dg31 = msg(31, 61)
        short_r = ("00f74ce5e25738f2ee2888a4f24676b54acbb2e6b6c152082dc4cbcd"
                   "f7a99eb4")
        short_s = ("b660a8ee1330abd4f14a1457010d1b6e7096c76841889584ed1bb010"
                   "67c66fce")
        full_r = ("261d44746d8b00ea9721e565b1876f3feff562854913b458041b3d10"
                  "c0aad265")
        full_s = ("b628d16fe96c78924b9b92c611272648edc20718879a55ff90c0dfcb"
                  "45502240")
        for r_, s_ in ((short_r, short_s), (full_r, full_s)):
            case("eccverify", "adv", "eccverify", "ECDSA", "SHA1", c,
                 k1["qx"], k1["qy"], dg31, r_, s_)
            case("eccverify", "adv", "eccverify", "ECDSA", "SHA1", c,
                 k1["qx"], k1["qy"], dg, r_, s_)

    case("ecccommit", "ok", "ecccommit", c, k1["qx"], k1["qy"],
         k2["qx"], k2["qy"], k1["d"], cc["r1"])
    case("ecccommit", "ok", "ecccommit", c, "NULL", "NULL",
         "NULL", "NULL", k1["d"], cc["r1"])
    case("ecccommit", "ok", "ecccommit", c, k1["qx"], k1["qy"],
         "NULL", "NULL", k2["d"], cc["r2"])
    # B aliased onto M.  _cpri__EccCommitCompute hands B straight to
    # EccInitPoint2B with no on-curve guard in front of it -- unlike
    # _cpri__EccPointMultiply, which guards at CpriECC.c:445 -- so an
    # off-curve B here is answered by OpenSSL rather than by the tree.
    # B == M is on the curve, is a degenerate the protocol has an
    # opinion about (L and E come out equal), and is the tree's own answer.
    case("ecccommit", "adv", "ecccommit", c, k2["qx"], k2["qy"],
         k2["qx"], k2["qy"], k1["d"], cc["r1"])
    case("ecccommit", "adv", "ecccommit", c, k1["qx"], k1["qy"],
         k2["qx"], k2["qy"], zero, cc["r1"])
    case("ecccommit", "adv", "ecccommit", c, k1["qx"], k1["qy"],
         k2["qx"], k2["qy"], k1["d"], zero)

    for scheme in ("ECDH", "SM2"):
        case("ecc22", "ok", "ecc22", c, scheme, k1["d"], cc["r1"],
             k2["qx"], k2["qy"], k1["qx"], k1["qy"])
    case("ecc22", "adv", "ecc22", c, "ECMQV", k1["d"], cc["r1"],
         k2["qx"], k2["qy"], k1["qx"], k1["qy"])
    case("ecc22", "adv", "ecc22", c, "ECDSA", k1["d"], cc["r1"],
         k2["qx"], k2["qy"], k1["qx"], k1["qy"])
    case("ecc22", "adv", "ecc22", c, "ECDH", zero, cc["r1"],
         k2["qx"], k2["qy"], k1["qx"], k1["qy"])
    # The ephemeral peer point aliased onto the static one.  Same reason as
    # ecccommit above: the two-phase code calls EccInitPoint2B on both peer
    # points unguarded, so an off-curve one asks OpenSSL a question, not the
    # tree.
    case("ecc22", "adv", "ecc22", c, "ECDH", k1["d"], cc["r1"],
         k1["qx"], k1["qy"], k1["qx"], k1["qy"])

for c in BAD_CURVES[:4]:
    case("eccpoint", "adv", "eccpoint", c, "00" * 32, "00" * 32)
    case("eccmul", "adv", "eccmul", c, "01", "NULL", "NULL", "NULL")
    case("eccgen", "adv", "eccgen", c, "SHA256", msg(32, 60), "ECC", "-", 0)
    case("eccrange", "adv", "eccrange", c, "01")

# ------------------------------------------------------------------- math

MOD = [("03", "02", "05"), ("0000ffff", "03", "7fffffff"),
       ("02", "0100", "07"), ("00", "03", "05"), ("01", "00", "05"),
       ("0400", "0201", "0503"), ("ff" * 8, "010001", "ff" * 8),
       (msg(32, 70), "010001", "ff" * 32), (msg(64, 71), "03", "ff" * 64),
       (msg(128, 72), "010001", "ff" * 128)]
for i, (m, e, n) in enumerate(MOD):
    for csz in (len(n) // 2, len(n) // 2 + 4):
        case("modexp", "ok", "modexp", csz, m, e, n)
case("modexp", "adv", "modexp", 1, "03", "02", "05")
case("modexp", "adv", "modexp", 4, "03", "02", "00")
case("modexp", "adv", "modexp", 4, "03", "02", "-")
case("modexp", "adv", "modexp", 4, "-", "02", "05")
case("modexp", "adv", "modexp", 4, "03", "-", "05")
case("modexp", "adv", "modexp", 4, "NULL", "02", "05")
case("modexp", "adv", "modexp", 0, "03", "02", "05")

DIV = [("0100", "0003"), ("ff" * 16, "0101"), ("0001", "0002"),
       (msg(32, 73), msg(16, 74)), (msg(32, 73), "01"),
       ("00", "03"), ("0100", "0100")]
for n, d in DIV:
    nb = len(n) // 2
    case("div", "ok", "div", n, d, nb, nb)
    # An output buffer larger than the result is the padding path; one smaller
    # than the result is the BnTo2B underflow and is excluded (see above).
    case("div", "adv", "div", n, d, 200, 200)
case("div", "adv", "div", "0100", "00", 2, 2)
case("div", "adv", "div", "0100", "-", 2, 2)
case("div", "adv", "div", "-", "0003", 2, 2)

# Only the two refusals: the success path of _math__Mul crashes the baseline.
for a, b in (("0100", "0100"), (msg(8, 75), msg(8, 76)), ("ff" * 4, "ff" * 4)):
    n = len(a) // 2 + len(b) // 2
    case("mul", "adv", "mul", a, b, n - 1)
    case("mul", "adv", "mul", a, b, 0)
    case("mul", "adv", "mul", a, b, 257)
    case("mul", "adv", "mul", a, b, 1000)

SUB = [("0200", "0001"), ("ff" * 8, "01"), ("0100", "0100"), ("01", "02"),
       (msg(32, 77), msg(32, 78)), (msg(32, 77), msg(8, 79)),
       ("0000ff", "ff"), ("00", "00")]
for a, b in SUB:
    case("sub", "ok", "sub", a, b)
    case("comp", "ok", "comp", a, b)
    case("comp", "ok", "comp", b, a)
for a, b in (("-", "01"), ("01", "-"), ("NULL", "01"), ("01", "NULL"),
             ("-", "-")):
    case("sub", "adv", "sub", a, b)
    if "NULL" not in (a, b):          # _math__Comp reads a[0] unconditionally
        case("comp", "adv", "comp", a, b)

for a in ("01", "0100", "00", "ff" * 8, "010000", msg(32, 80)):
    case("dec", "ok", "dec", a)
for a in ("-", "NULL"):
    case("dec", "adv", "dec", a)

for p in (2, 3, 4, 5, 9, 17, 101, 1009, 65521, 65537, 65536, 104729, 104730,
          2147483647, 2147483646, 4294967291, 4294967295, 1, 0):
    case("isprime", "ok" if p > 1 else "adv", "isprime", p)

for b, dn in (("000102", 4), ("0102", 4), ("000000", 4), ("00", 1),
              (msg(32, 81), 40), ("ff" * 8, 8)):
    case("norm", "ok", "norm", b, dn)
for b, dn in (("-", 4), ("000102", 1), ("000102", 0), ("000102", 300)):
    case("norm", "adv", "norm", b, dn)

# -------------------------------------------------------------------- rng

for h in HASHES:
    for n in (1, 16, 32, 48, 64, 100, 256, 512):
        case("seededrng", "ok", "seededrandom", n, h, msg(32, 90), "RNG",
             "-", "-")
    case("seededrng", "ok", "seededrandom", 32, h, msg(32, 90), "RNG",
         CTX, CTXV)
    case("seededrng", "ok", "seededrandom", 32, h, msg(32, 91), "RNG",
         "-", "-")
    case("seededrng", "ok", "seededrandom", 32, h, msg(32, 90), "OTHER",
         "-", "-")
    case("seededrng", "adv", "seededrandom", 0, h, msg(32, 90), "RNG",
         "-", "-")
    case("seededrng", "adv", "seededrandom", 32, h, "-", "RNG", "-", "-")
    case("seededrng", "adv", "seededrandom", 32, h, "NULL", "RNG", "-", "-")
for h in BAD_HASHES:
    case("seededrng", "adv", "seededrandom", 32, h, msg(32, 90), "RNG",
         "-", "-")

for n in (1, 16, 32, 64, 256):
    case("rng", "ok", "random", n)
case("rng", "adv", "random", 0)
for e in (msg(8, 92), msg(32, 93), msg(64, 94), "-", "NULL"):
    case("rng", "ok" if e not in ("-", "NULL") else "adv", "stir", e)
for d in ("GET", "PUT"):
    for buf in (msg(32, 95), "-", "NULL"):
        case("rng", "ok", "drbgstate", d, buf)
case("rng", "ok", "startup")


def main():
    out = pathlib.Path(__file__).with_name("cases.txt")
    header = [
        "# Behaviour-suite inputs for the TPM CryptoEngine public API.",
        "# Generated by gencases.py; expectations come from record.sh.",
        "# id = <family>/<ok|adv>/<n>",
    ]
    out.write_text("\n".join(header + CASES) + "\n")
    fam = {}
    for c in CASES:
        f, kind, _ = c.split(" ", 1)[0].split("/")
        fam.setdefault(f, {"ok": 0, "adv": 0})[kind] += 1
    tot_ok = sum(v["ok"] for v in fam.values())
    tot_adv = sum(v["adv"] for v in fam.values())
    for f in sorted(fam):
        print("  %-12s ok=%-5d adv=%d" % (f, fam[f]["ok"], fam[f]["adv"]))
    print("%d cases in %d families; adversarial %d/%d = %.1f%%"
          % (len(CASES), len(fam), tot_adv, len(CASES),
             100.0 * tot_adv / len(CASES)))
    assert tot_adv * 3 >= len(CASES), "adversarial share below one third"
    assert len(set(c.split(" ", 1)[0] for c in CASES)) == len(CASES), \
        "duplicate case id"
    print("ok=%d" % tot_ok)


if __name__ == "__main__":
    main()
