#!/usr/bin/env python3
"""Replay the end-to-end behaviour suite against a built Simulator.

    e2e.py <Simulator> <out> [--port N] [--stall SECONDS] [--quiet]
                             [--sanlog DIR] [--only FAMILY,...]

--sanlog turns the run into a screen rather than a recording: the simulator is
expected to be an ASan+UBSan build, and every case that provokes a report gets
a `# SAN` line naming it, with the raw per-session logs copied into DIR.

The other half of the behaviour suite drives libCryptoEngine's C entry points
directly.  This half never mentions them: it speaks the simulator's TCP
protocol and TPM 2.0 command buffers, which is the interface this repository
actually publishes.  A port that keeps every crypto answer identical but
breaks command dispatch fails here and nowhere else, and a port that quietly
changes a digest fails here too.

Nothing in this file predicts an answer.  Expectations come from recording the
untouched baseline; a case is a fixed input plus a decision about which parts
of the response are reproducible.  Three things in this simulator are not:

  * the primary seeds and hierarchy proofs, drawn from CryptGenerateRandom at
    every process start, so tickets and blobs from a real hierarchy vary.
    Cases stay in the NULL hierarchy, where the ticket is empty by definition.
  * anything built on fresh randomness -- GetRandom, ECDSA's per-signature
    nonce, RSAES and PSS padding, ECDH_KeyGen, Commit.  These record a length
    and a return code, and where a round trip exists the round trip is scored
    as well.  ECDSA is the exception: its round trip is a coin this baseline
    loses about once in 256, because ValidateSignatureEcdsa truncates the
    digest to the length of r (CpriECC.c:1909) after r's leading zeros have
    been stripped, so a signature whose r happens to lead with a zero byte is
    rejected by the very TPM that made it.  The sign step draws again until r
    leads with a non-zero byte.  That makes the round trip reproducible
    without hiding anything: the defect is scored, and deterministically, by
    eccverify/adv/011-014 on the engine half.
  * TPM2_ReadClock, whose answer is the time.  It records its return code and
    nothing else; an expectation that validates against a clock is an
    expectation with an expiry date.

Cases are grouped into sessions.  Each session gets its own simulator process
in its own directory, so a case that takes the TPM down costs its own session
and no other -- main() re-manufactures at every start, which is what makes a
restarted process a clean one rather than one carrying the wreckage.  Cases a
dead session never reached are simply absent from the output, and absent
scores zero.

Exit status is 0 whatever the cases did; the output file is the result.
"""
import hashlib
import os
import shutil
import struct
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tpm2 import (ALG, ATTR_ANY, ATTR_SIGN, ATTR_SYM, CC, CURVE, DIGEST_SIZE,
                  Dead, ENTROPY_TRIES, Flaky,
                  RH_NULL, RH_OWNER, RS_PW, SE_HMAC, SE_POLICY, SE_TRIAL,
                  ST_NO_SESSIONS, ST_SESSIONS, A_SIGN, A_DECRYPT,
                  A_USERWITHAUTH, A_NODA, Tpm, alg, b2, ecc_parms, h,
                  keyedhash_parms, null_ticket, pcr_select, public,
                  pw_session, rsa_parms, sensitive, sym_def_object, u8, u16,
                  u32, unb2)

HASHES = ["SHA1", "SHA256", "SHA384"]
KEYS = None                                    # filled from keys.json
SESSIONS = []


def session(family):
    """Register a case group that owns one simulator process."""
    def deco(fn):
        SESSIONS.append((family, fn))
        return fn
    return deco


# --------------------------------------------------------------------------
# fixed inputs
# --------------------------------------------------------------------------
def msg(n, tag=0):
    """A deterministic n-byte message; tag varies the content, not the size."""
    out = bytearray(n)
    for i in range(n):
        out[i] = (i * 7 + tag * 31 + 0x5A) & 0xFF
    return bytes(out)


def digest(n, tag=0):
    """n deterministic bytes.  sha512, so a 48-byte request is 48 bytes:
    sha256 sliced to 48 silently yields 32, which the TPM then rejects for
    size and the case stops testing what it was written to test."""
    out = b""
    while len(out) < n:
        out += hashlib.sha512(b"e2e|%d|%d|%d" % (n, tag, len(out))).digest()
    return out[:n]


def mgf1(seed, length, name):
    out, c = b"", 0
    while len(out) < length:
        out += hashlib.new(name, seed + struct.pack(">I", c)).digest()
        c += 1
    return out[:length]


def pkcs1v15_pad(m, k):
    """Type-2 padding with fixed non-zero filler: a ciphertext we can reuse."""
    ps = bytes(((i * 11 + 3) & 0xFF) or 0x2A for i in range(k - len(m) - 3))
    return b"\x00\x02" + ps + b"\x00" + m


def oaep_pad(m, k, name, label=b""):
    hlen = hashlib.new(name).digest_size
    lhash = hashlib.new(name, label).digest()
    db = lhash + b"\x00" * (k - len(m) - 2 * hlen - 2) + b"\x01" + m
    seed = hashlib.sha256(b"oaep|%s|%d" % (name.encode(), k)).digest()[:hlen]
    masked_db = bytes(a ^ b for a, b in zip(db, mgf1(seed, k - hlen - 1,
                                                     name)))
    masked_seed = bytes(a ^ b for a, b in zip(seed, mgf1(masked_db, hlen,
                                                         name)))
    return b"\x00" + masked_seed + masked_db


def rsa_raw(padded, key):
    """padded^e mod n as a fixed-width byte string."""
    n = int(key["n"], 16)
    c = pow(int.from_bytes(padded, "big"), key["e"], n)
    return c.to_bytes(key["bits"] // 8, "big")


# --------------------------------------------------------------------------
# object builders
# --------------------------------------------------------------------------
def rsa_pub(k, nalg="SHA256", attrs=ATTR_ANY, bits=None, exponent=None,
            **kw):
    return public("RSA", nalg, attrs,
                  rsa_parms(k["bits"] if bits is None else bits,
                            k["e"] if exponent is None else exponent, **kw),
                  b2(h(k["n"])))


def rsa_sens(k, prime=None):
    return sensitive("RSA", h(k["p"]) if prime is None else prime)


def ecc_pub(k, nalg="SHA256", attrs=ATTR_ANY, qx=None, qy=None, curve=None,
            **kw):
    return public("ECC", nalg, attrs,
                  ecc_parms(k["curve"] if curve is None else curve, **kw),
                  b2(h(k["qx"]) if qx is None else qx)
                  + b2(h(k["qy"]) if qy is None else qy))


def ecc_sens(k, d=None):
    return sensitive("ECC", h(k["d"]) if d is None else d)


def kh_unique(key, seed, nalg):
    """TPMT_PUBLIC.unique for a keyed object: nameAlg over seed || key."""
    return b2(hashlib.new(nalg.lower().replace("sha", "sha"),
                          seed + key).digest())


def hmac_pub(key, halg="SHA256", nalg="SHA256", seed=b"", attrs=ATTR_SIGN):
    return public("KEYEDHASH", nalg, attrs, keyedhash_parms("HMAC", halg),
                  kh_unique(key, seed, nalg))


def hmac_sens(key, seed=b""):
    return sensitive("KEYEDHASH", key, seed=seed)


def sym_pub(key, mode="CFB", nalg="SHA256", seed=b"", algo="AES", bits=None,
            attrs=ATTR_SYM):
    bits = len(key) * 8 if bits is None else bits
    return public("SYMCIPHER", nalg, attrs, sym_def_object(algo, bits, mode),
                  kh_unique(key, seed, nalg))


def sym_sens(key, seed=b""):
    return sensitive("SYMCIPHER", key, seed=seed)


# --------------------------------------------------------------------------
# sessions
# --------------------------------------------------------------------------
@session("e2eboot")
def s_boot(t, emit):
    # Through startup() rather than cmd(): a failed first Startup is the
    # entropy toss, and this session would otherwise record a whole TPM in
    # failure mode as if that were the boot sequence's behaviour.
    emit("ok", "startup-clear", rc=t.startup())
    emit("adv", "startup-twice", rc=t.cmd("Startup", u16(0))[0])
    emit("adv", "startup-state-late", rc=t.cmd("Startup", u16(1))[0])
    emit("adv", "startup-bad-type", rc=t.cmd("Startup", u16(0x1234))[0])
    emit("ok", "selftest-partial", rc=t.cmd("SelfTest", u8(0))[0])
    emit("ok", "selftest-full", rc=t.cmd("SelfTest", u8(1))[0])
    rc, p = t.cmd("GetTestResult")
    emit("ok", "testresult", rc=rc, out=p)
    emit("adv", "selftest-bad-bool", rc=t.cmd("SelfTest", u8(2))[0])
    for i, algs in enumerate([[], ["SHA1"], ["SHA256", "RSA"],
                              ["AES", "ECC", "HMAC"], [0x0099],
                              ["SHA1"] * 8]):
        rc, p = t.cmd("IncrementalSelfTest",
                      u32(len(algs)) + b"".join(u16(alg(a)) for a in algs))
        emit("ok" if i < 4 else "adv", "incremental-%d" % i, rc=rc, out=p)
    rc, p = t.cmd("GetTestResult")
    emit("ok", "testresult-after", rc=rc, out=p)
    emit("ok", "readclock-rc-only", rc=t.cmd("ReadClock")[0])
    for n in [0, 1, 32, 64, 128, 0xFFFF]:
        rc, p = t.cmd("GetRandom", u16(n))
        emit("ok" if n <= 64 else "adv", "getrandom-%d" % n, rc=rc,
             sz=len(p) - 2 if rc == 0 else -1)
    for n in [0, 1, 128, 129, 1024]:
        emit("adv" if n > 128 else "ok", "stir-%d" % n,
             rc=t.cmd("StirRandom", b2(msg(n)))[0])


@session("e2ecap")
def s_cap(t, emit):
    t.startup()
    for cap in range(0, 10):
        for count in [0, 1, 8, 256]:
            rc, p = t.cmd("GetCapability", u32(cap) + u32(0) + u32(count))
            emit("ok", "cap-%d-%d" % (cap, count), rc=rc, out=p)
    for prop, count in [(0x100, 64), (0x200, 64), (0x100, 1), (0x120, 8)]:
        rc, p = t.cmd("GetCapability", u32(6) + u32(prop) + u32(count))
        emit("ok", "prop-%x-%d" % (prop, count), rc=rc, out=p)
    rc, p = t.cmd("GetCapability", u32(7) + u32(0) + u32(64))
    emit("ok", "pcrprops", rc=rc, out=p)
    for cap in [0x99, 0x100, 0xFFFFFFFF]:
        rc, p = t.cmd("GetCapability", u32(cap) + u32(0) + u32(4))
        emit("adv", "cap-bad-%x" % cap, rc=rc, out=p)
    emit("adv", "cap-huge-count",
         rc=t.cmd("GetCapability", u32(0) + u32(0) + u32(0xFFFFFFFF))[0])
    emit("adv", "cap-short", rc=t.cmd("GetCapability", u32(0) + u32(0))[0])

    # TestParms asks "would you accept this algorithm detail?" and answers
    # without touching a key, so it maps the algorithm surface exactly.
    parms = [
        ("sym-aes128-cfb", "SYMCIPHER", sym_def_object("AES", 128, "CFB")),
        ("sym-aes256-cbc", "SYMCIPHER", sym_def_object("AES", 256, "CBC")),
        ("sym-aes64-cfb", "SYMCIPHER", sym_def_object("AES", 64, "CFB")),
        ("sym-sm4-128", "SYMCIPHER", sym_def_object("SM4", 128, "CFB")),
        ("rsa-1024", "RSA", rsa_parms(1024, 65537)),
        ("rsa-2048", "RSA", rsa_parms(2048, 65537)),
        ("rsa-3072", "RSA", rsa_parms(3072, 65537)),
        ("rsa-512", "RSA", rsa_parms(512, 65537)),
        ("rsa-pss", "RSA", rsa_parms(2048, 0, "RSAPSS", "SHA256")),
        ("rsa-oaep-sha384", "RSA", rsa_parms(2048, 0, "OAEP", "SHA384")),
        ("rsa-sha512", "RSA", rsa_parms(2048, 0, "RSASSA", "SHA512")),
        ("ecc-p256", "ECC", ecc_parms("NIST_P256")),
        ("ecc-p384-ecdsa", "ECC", ecc_parms("NIST_P384", "ECDSA", "SHA384")),
        ("ecc-bn256-ecdaa", "ECC", ecc_parms("BN_P256", "ECDSA", "SHA256")),
        ("ecc-p521", "ECC", ecc_parms("NIST_P521")),
        ("ecc-sm2", "ECC", ecc_parms("SM2_P256")),
        ("ecc-bad-curve", "ECC", ecc_parms(0x00FF)),
        ("kh-hmac-sha256", "KEYEDHASH", keyedhash_parms("HMAC", "SHA256")),
        ("kh-hmac-sha512", "KEYEDHASH", keyedhash_parms("HMAC", "SHA512")),
        ("kh-xor", "KEYEDHASH", keyedhash_parms("XOR", "SHA256")),
        ("kh-null", "KEYEDHASH", keyedhash_parms("NULL")),
    ]
    for name, typ, blob in parms:
        kind = "adv" if "bad" in name or name in (
            "sym-aes64-cfb", "rsa-512", "ecc-p521", "rsa-sha512",
            "kh-hmac-sha512") else "ok"
        emit(kind, "testparms-" + name,
             rc=t.cmd("TestParms", u16(alg(typ)) + blob)[0])
    emit("adv", "testparms-bad-type",
         rc=t.cmd("TestParms", u16(0x0099) + sym_def_object())[0])


# The message lengths s_hash hashes and the chunkings s_hashseq feeds through
# a sequence.  Module level because e2echeck.py recomputes both families with
# hashlib and has to hash the same bytes this file asked the TPM to hash; a
# control that carried its own copy of the inputs would stop being a control
# the first time one of these lists changed.
HASH_LENS = [0, 1, 2, 3, 55, 56, 57, 63, 64, 65, 111, 112, 127, 128, 129,
             255, 1000, 1024]
CHUNKINGS = [[], [0], [1], [8], [64], [1, 1, 1], [55, 1], [63, 1, 64],
             [1024], [1024, 1024, 7]]


@session("e2ehash")
def s_hash(t, emit):
    t.startup()
    for a in HASHES:
        for n in HASH_LENS:
            rc, p = t.cmd("Hash", b2(msg(n)) + u16(alg(a)) + u32(RH_NULL))
            d, off = unb2(p)
            emit("ok", "hash-%s-%d" % (a, n), rc=rc, out=d,
                 tk=p[off:] if rc == 0 else b"")
    for a in ["SHA512", "SM3_256", "NULL", 0x0000, 0x0099, 0xFFFF]:
        emit("adv", "hash-alg-%s" % a,
             rc=t.cmd("Hash", b2(msg(8)) + u16(alg(a)) + u32(RH_NULL))[0])
    for n in [1025, 2048, 4096]:
        emit("adv", "hash-oversize-%d" % n,
             rc=t.cmd("Hash",
                      b2(msg(n)) + u16(ALG["SHA256"]) + u32(RH_NULL))[0])
    for hier in [RH_OWNER, 0x40000000, 0x1234, 0x4000000B]:
        # A real hierarchy signs the ticket with a proof drawn at manufacture,
        # so only the return code and the ticket's length survive a restart.
        rc, p = t.cmd("Hash", b2(msg(16)) + u16(ALG["SHA256"]) + u32(hier))
        emit("adv", "hash-hierarchy-%x" % hier, rc=rc, sz=len(p))
    emit("adv", "hash-truncated-2b",
         rc=t.cmd("Hash", u16(64) + msg(8) + u16(ALG["SHA256"])
                  + u32(RH_NULL))[0])


@session("e2ehashseq")
def s_hashseq(t, emit):
    t.startup()
    for a in HASHES:
        for ci, chunks in enumerate(CHUNKINGS):
            rc, p = t.cmd("HMAC_Start" if False else "HashSequenceStart",
                          b2(b"") + u16(alg(a)))
            if rc:
                emit("ok", "seq-%s-%d" % (a, ci), rc=rc)
                continue
            sh = struct.unpack(">I", p[:4])[0]
            for j, n in enumerate(chunks[:-1] if chunks else []):
                t.cmd("SequenceUpdate", b2(msg(n, j)), handles=[sh],
                      auth=pw_session())
            tail = msg(chunks[-1], len(chunks) - 1) if chunks else b""
            rc, p = t.cmd("SequenceComplete", b2(tail) + u32(RH_NULL),
                          handles=[sh], auth=pw_session())
            d, off = unb2(p) if rc == 0 else (b"", 0)
            emit("ok", "seq-%s-%d" % (a, ci), rc=rc, out=d,
                 tk=p[off:] if rc == 0 else b"")
    # A sequence handle is gone once completed; using it again must be
    # rejected rather than reused.
    rc, p = t.cmd("HashSequenceStart", b2(b"") + u16(ALG["SHA256"]))
    sh = struct.unpack(">I", p[:4])[0]
    emit("ok", "seq-start-handle-class", rc=rc, sz=sh >> 24)
    t.cmd("SequenceComplete", b2(b"x") + u32(RH_NULL), handles=[sh],
          auth=pw_session())
    emit("adv", "seq-update-after-complete",
         rc=t.cmd("SequenceUpdate", b2(b"y"), handles=[sh],
                  auth=pw_session())[0])
    emit("adv", "seq-complete-bad-handle",
         rc=t.cmd("SequenceComplete", b2(b"") + u32(RH_NULL),
                  handles=[0x80FFFFFF], auth=pw_session())[0])
    for a in ["SHA512", 0x0099]:
        emit("adv", "seq-start-alg-%s" % a,
             rc=t.cmd("HashSequenceStart", b2(b"") + u16(alg(a)))[0])
    # NULL means "hash for an event", which starts every bank at once.
    rc, p = t.cmd("HashSequenceStart", b2(b"") + u16(ALG["NULL"]))
    emit("ok", "seq-start-null", rc=rc)
    if rc == 0:
        sh = struct.unpack(">I", p[:4])[0]
        t.cmd("SequenceUpdate", b2(msg(40)), handles=[sh], auth=pw_session())
        rc, p = t.cmd("EventSequenceComplete", b2(msg(7, 1)),
                      handles=[0x00000010, sh],
                      auth=pw_session() + pw_session())
        emit("ok", "event-seq-complete", rc=rc, out=p)
        rc, p = t.cmd("PCR_Read", pcr_select([16]))
        emit("ok", "event-pcr16", rc=rc, out=p)
    rc, p = t.cmd("HashSequenceStart", b2(b"") + u16(ALG["SHA256"]))
    sh = struct.unpack(">I", p[:4])[0]
    emit("adv", "seq-update-oversize",
         rc=t.cmd("SequenceUpdate", b2(msg(2048)), handles=[sh],
                  auth=pw_session())[0])
    emit("ok", "seq-flush", rc=t.flush(sh))


# ---------------------------------------------------------------------------
# a sequence that leaves the TPM and comes back
# ---------------------------------------------------------------------------
# TPM2_ContextSave copies the whole internal OBJECT into the context blob with
# one struct assignment -- `*outObject = *object` (ContextSave.c:118) -- and
# for a sequence object that structure *is* the engine's CPRI_HASH_STATE.
# SequenceDataImportExport then runs on the copy and reaches
# _cpri__ImportExportHashState, whose body is #if 0'd out in this revision, so
# the bitwise copy is the whole of the transfer.  TPM2_ContextLoad copies it
# back into a fresh slot the same way.
#
# That is the one place in this repository where a hash state is moved by
# memcpy through the published interface, and until this session existed the
# corpus reached it only from C, through `hashmove` on the engine half.  Here
# it is reached the way a caller reaches it: over the wire, on an object the
# TPM allocated, through the command dispatcher.
#
# What is recorded is digests, return codes and the plaintext head of
# TPMS_CONTEXT.  Not the blob -- it is encrypted under a proof drawn at
# manufacture, so it differs every process -- and not its length, which is
# sizeof(OBJECT) and therefore sizeof(CPRI_HASH_STATE), a number a faithful
# port to opaque contexts is entitled to change.
CTX_CHUNKS = [0, 1, 55, 63, 64, 65, 127, 128, 200, 1024]

# What each relocation shape eats, as (length, tag) pairs into msg().  These are
# named here rather than written inline below because e2echeck.py recomputes
# every digest in this session with hashlib, and a known-answer control that
# keeps its own copy of what was hashed stops being a control the moment the
# suite changes.  One source, two readers.
CTX_MOVE_TAIL = (17, 1)                     # fed to the loaded copy
CTX_FORK_A = ((13, 2), (5, 4))              # first load:  update, then finish
CTX_FORK_B = ((29, 3), (7, 5))              # second load: update, then finish
CTX_CHAIN_PRE = 40                          # eaten before the first save
CTX_CHAIN_STEP = 11                         # after each load; tag is the round
CTX_CHAIN_ROUNDS = 3
CTX_CHAIN_TAIL = (3, 9)
CTX_LIVE_PRE = 48                           # eaten before the save
CTX_LIVE_COPY = ((9, 6), (2, 7))            # the copy, while the original lives
CTX_LIVE_ORIG = (4, 8)                      # the original, afterwards
CTX_AFTER_PRE = 24                          # eaten before the save
CTX_AFTER_TAIL = (8, 1)                     # fed to BOTH: the digests must agree
CTX_AGAIN_PRE = (56, 21)                    # eaten before the blob is saved
CTX_AGAIN_TAIL = (6, 22)                    # fed to each load of that blob


def ctx_save(t, handle):
    """TPM2_ContextSave: one handle in, a marshalled TPMS_CONTEXT out.

    TPMS_CONTEXT is sequence:UINT64, savedHandle:UINT32, hierarchy:UINT32,
    contextBlob:TPM2B -- so bytes 0..15 are plaintext and reproducible and
    everything past 16 is the encrypted blob and is not.
    """
    return t.cmd("ContextSave", handles=[handle])


def ctx_load(t, blob):
    """TPM2_ContextLoad: a TPMS_CONTEXT in, a fresh transient handle out."""
    rc, p = t.cmd("ContextLoad", blob)
    return rc, (struct.unpack(">I", p[:4])[0] if rc == 0 and len(p) >= 4
                else 0)


@session("e2eseqctx")
def s_seqctx(t, emit):
    t.startup()

    def seq(a, n, tag=0):
        """A sequence handle that has already eaten msg(n, tag), or None."""
        rc, p = t.cmd("HashSequenceStart", b2(b"") + u16(alg(a)))
        if rc:
            return None
        sh = struct.unpack(">I", p[:4])[0]
        if n:
            t.cmd("SequenceUpdate", b2(msg(n, tag)), handles=[sh],
                  auth=pw_session())
        return sh

    def finish(sh, tail, tag):
        rc, p = t.cmd("SequenceComplete", b2(msg(tail, tag)) + u32(RH_NULL),
                      handles=[sh], auth=pw_session())
        return rc, (unb2(p)[0] if rc == 0 else b"")

    # --- what the header of a saved sequence says ------------------------
    # savedHandle is 0x80000001 for a sequence and 0x80000000 for an ordinary
    # object (ContextSave.c:129-133); the fingerprint is gr.objectContextID,
    # which counts saves.
    for a in HASHES:
        sh = seq(a, 64)
        rc, blob = ctx_save(t, sh)
        emit("ok", "ctx-head-%s" % a, rc=rc,
             seqno=blob[:8].hex() if rc == 0 else "-",
             saved=blob[8:12].hex() if rc == 0 else "-",
             hier=blob[12:16].hex() if rc == 0 else "-")
        t.flush(sh)

    # --- finished from the copy, with the original evicted ---------------
    for a in HASHES:
        for n in CTX_CHUNKS:
            sh = seq(a, n)
            rc, blob = ctx_save(t, sh)
            t.flush(sh)
            if rc:
                emit("ok", "ctx-move-%s-%d" % (a, n), rc=rc)
                continue
            rc, h2 = ctx_load(t, blob)
            if rc:
                emit("ok", "ctx-move-%s-%d" % (a, n), rc=rc)
                continue
            rc, d = finish(h2, *CTX_MOVE_TAIL)
            emit("ok", "ctx-move-%s-%d" % (a, n), rc=rc, out=d)

    # --- one blob, two continuations -------------------------------------
    # Two loads of one context are two states, not two names for one.  A port
    # that leaves a live library handle inside the object hands both copies
    # the same one, and then the two digests are equal, or wrong, or absent.
    for a in HASHES:
        for n in CTX_CHUNKS:
            sh = seq(a, n)
            rc, blob = ctx_save(t, sh)
            t.flush(sh)
            rca, ha = ctx_load(t, blob) if rc == 0 else (rc, 0)
            rcb, hb = ctx_load(t, blob) if rc == 0 else (rc, 0)
            if rca or rcb:
                emit("ok", "ctx-fork-a-%s-%d" % (a, n), rc=rca)
                emit("ok", "ctx-fork-b-%s-%d" % (a, n), rc=rcb)
                continue
            t.cmd("SequenceUpdate", b2(msg(*CTX_FORK_A[0])), handles=[ha],
                  auth=pw_session())
            t.cmd("SequenceUpdate", b2(msg(*CTX_FORK_B[0])), handles=[hb],
                  auth=pw_session())
            rca, da = finish(ha, *CTX_FORK_A[1])
            rcb, db = finish(hb, *CTX_FORK_B[1])
            emit("ok", "ctx-fork-a-%s-%d" % (a, n), rc=rca, out=da)
            emit("ok", "ctx-fork-b-%s-%d" % (a, n), rc=rcb, out=db)

    # --- relocating a state that has already been relocated --------------
    for a in HASHES:
        sh = seq(a, CTX_CHAIN_PRE)
        for rnd in range(CTX_CHAIN_ROUNDS):
            rc, blob = ctx_save(t, sh)
            t.flush(sh)
            if rc:
                break
            rc, sh = ctx_load(t, blob)
            if rc:
                break
            t.cmd("SequenceUpdate", b2(msg(CTX_CHAIN_STEP, rnd)),
                  handles=[sh], auth=pw_session())
        if rc:
            emit("ok", "ctx-chain-%s" % a, rc=rc)
        else:
            rc, d = finish(sh, *CTX_CHAIN_TAIL)
            emit("ok", "ctx-chain-%s" % a, rc=rc, out=d)

    # --- the original is still loaded while the copy runs ----------------
    # The sharpest shape: two handles, both live, both fed different bytes,
    # both completed.  Completing one must not empty the other.
    for a in HASHES:
        sh = seq(a, CTX_LIVE_PRE)
        rc, blob = ctx_save(t, sh)
        rc2, h2 = ctx_load(t, blob) if rc == 0 else (rc, 0)
        emit("ok", "ctx-live-load-%s" % a, rc=rc2)
        if rc2 == 0:
            t.cmd("SequenceUpdate", b2(msg(*CTX_LIVE_COPY[0])), handles=[h2],
                  auth=pw_session())
            rc3, d3 = finish(h2, *CTX_LIVE_COPY[1])
            emit("ok", "ctx-live-copy-%s" % a, rc=rc3, out=d3)
        rc4, d4 = finish(sh, *CTX_LIVE_ORIG)
        emit("ok", "ctx-live-orig-%s" % a, rc=rc4, out=d4)

    # --- complete the original, then finish the copy the same way --------
    # Same algorithm, same prefix, same tail: the two digests have to agree.
    # If completing the original released state the copy was still using,
    # the second answer is garbage, a failure code, or a dead simulator.
    for a in HASHES:
        sh = seq(a, CTX_AFTER_PRE)
        rc, blob = ctx_save(t, sh)
        rc1, d1 = finish(sh, *CTX_AFTER_TAIL)
        emit("ok", "ctx-after-complete-orig-%s" % a, rc=rc1, out=d1)
        rc2, h2 = ctx_load(t, blob) if rc == 0 else (rc, 0)
        if rc2 == 0:
            rc3, d3 = finish(h2, *CTX_AFTER_TAIL)
            emit("ok", "ctx-after-complete-copy-%s" % a, rc=rc3, out=d3)
        else:
            emit("ok", "ctx-after-complete-copy-%s" % a, rc=rc2)

    # --- an event sequence is HASH_COUNT states in one object ------------
    rc, p = t.cmd("HashSequenceStart", b2(b"") + u16(ALG["NULL"]))
    emit("ok", "ctx-event-start", rc=rc)
    if rc == 0:
        sh = struct.unpack(">I", p[:4])[0]
        t.cmd("SequenceUpdate", b2(msg(70)), handles=[sh], auth=pw_session())
        rc, blob = ctx_save(t, sh)
        t.flush(sh)
        rc2, h2 = ctx_load(t, blob) if rc == 0 else (rc, 0)
        emit("ok", "ctx-event-move", rc=rc2,
             saved=blob[8:12].hex() if rc == 0 else "-")
        if rc2 == 0:
            rc3, p3 = t.cmd("EventSequenceComplete", b2(msg(6, 1)),
                            handles=[0x00000010, h2],
                            auth=pw_session() + pw_session())
            emit("ok", "ctx-event-complete", rc=rc3, out=p3)
            rc4, p4 = t.cmd("PCR_Read", pcr_select([16]))
            emit("ok", "ctx-event-pcr16", rc=rc4, out=p4)

    # --- an HMAC sequence carries a key beside its hash state ------------
    # hashAlg is the caller's, but the key's own scheme has the last word, so
    # the key is loaded once per algorithm: a SHA256-scheme key asked to start
    # a SHA384 sequence answers on parameter 2 and no sequence ever exists.
    # HMAC_Start copies the key into the sequence object, so the key handle is
    # released as soon as the sequence is running.
    key = msg(32, 14)
    rc, hnd, _ = t.load_external(hmac_sens(key), hmac_pub(key))
    emit("ok", "ctx-hmac-key", rc=rc)
    if hnd is not None:
        t.flush(hnd)
    for a in HASHES:
        rc, hnd, _ = t.load_external(hmac_sens(key), hmac_pub(key, halg=a))
        if hnd is None:
            emit("ok", "ctx-hmac-%s" % a, rc=rc)
            continue
        rc, p = t.cmd("HMAC_Start", b2(b"") + u16(alg(a)),
                      handles=[hnd], auth=pw_session(), rhandles=1)
        t.flush(hnd)
        if rc:
            emit("ok", "ctx-hmac-%s" % a, rc=rc)
            continue
        sh = struct.unpack(">I", p[:4])[0]
        t.cmd("SequenceUpdate", b2(msg(50, 15)), handles=[sh],
              auth=pw_session())
        rc, blob = ctx_save(t, sh)
        t.flush(sh)
        rc2, h2 = ctx_load(t, blob) if rc == 0 else (rc, 0)
        if rc2:
            emit("ok", "ctx-hmac-%s" % a, rc=rc2)
            continue
        rc3, d3 = finish(h2, 12, 16)
        emit("ok", "ctx-hmac-%s" % a, rc=rc3, out=d3)

    # --- an ordinary object travels the same path with no sequence data --
    key = msg(32, 17)
    rc, hnd, nm = t.load_external(hmac_sens(key), hmac_pub(key))
    if hnd is not None:
        rc, blob = ctx_save(t, hnd)
        emit("ok", "ctx-object-head", rc=rc,
             saved=blob[8:12].hex() if rc == 0 else "-")
        t.flush(hnd)
        rc2, h2 = ctx_load(t, blob) if rc == 0 else (rc, 0)
        emit("ok", "ctx-object-load", rc=rc2)
        if rc2 == 0:
            rc3, p3 = t.cmd("HMAC", b2(msg(64, 18)) + u16(ALG["SHA256"]),
                            handles=[h2], auth=pw_session())
            emit("ok", "ctx-object-use", rc=rc3,
                 out=unb2(p3)[0] if rc3 == 0 else b"")
            rc4, p4 = t.cmd("ReadPublic", handles=[h2])
            emit("ok", "ctx-object-name", rc=rc4, out=p4)
            t.flush(h2)

    # --- what a blob that is not the one that was saved gets -------------
    # The blob is encrypted and HMAC'd under a proof drawn at manufacture, so
    # every one of these is an integrity failure rather than a parse error;
    # the point is that it stays one.
    sh = seq("SHA256", 64)
    rc, blob = ctx_save(t, sh)
    t.flush(sh)
    if rc == 0:
        for tag, off, mask in [("last", len(blob) - 1, 0x01),
                               ("body", 20, 0x80),
                               ("head", 18, 0x01),
                               ("seqno", 7, 0x01),
                               ("saved", 11, 0x01),
                               ("hier", 15, 0x01)]:
            bad = bytearray(blob)
            bad[off] ^= mask
            emit("adv", "ctx-load-flip-%s" % tag,
                 rc=ctx_load(t, bytes(bad))[0])
        emit("adv", "ctx-load-truncated",
             rc=t.cmd("ContextLoad", blob[:len(blob) // 2])[0])
        # A context blob two bytes long: room for the integrity digest's own
        # size field and nothing after it.  The obvious case -- a blob of
        # length ZERO -- is deliberately not here.  It is a baseline defect:
        # TPM2B_DIGEST_Unmarshal ignores UINT16_Unmarshal's failure
        # and reads target->t.size before anything has written it, so an empty
        # blob's response code is whatever was on the caller's stack.  The same
        # source answers 154 built -O1 -g and 149 built with no flags, which
        # makes it an answer this tree does not own and cannot be held to.
        emit("adv", "ctx-load-stub-blob",
             rc=t.cmd("ContextLoad", blob[:16] + u16(2) + b"\x00\x00")[0])
        emit("adv", "ctx-load-empty", rc=t.cmd("ContextLoad", b"")[0])
        emit("adv", "ctx-load-oversize-2b",
             rc=t.cmd("ContextLoad", blob[:16] + u16(0xFFFF)
                      + blob[18:])[0])
        emit("adv", "ctx-load-trailing",
             rc=t.cmd("ContextLoad", blob + b"\x00")[0])

        # Loading one blob repeatedly runs the slots out.  The count is
        # MAX_LOADED_OBJECTS, a fact about this build that a port must keep.
        held = []
        rcl = 0
        while rcl == 0 and len(held) < 8:
            rcl, hh = ctx_load(t, blob)
            if rcl == 0:
                held.append(hh)
        emit("adv", "ctx-load-until-full", rc=rcl, sz=len(held))
        for hh in held:
            t.flush(hh)

    emit("adv", "ctx-save-unloaded", rc=ctx_save(t, 0x80FFFFFF)[0])
    emit("adv", "ctx-save-permanent", rc=ctx_save(t, RH_OWNER)[0])
    emit("adv", "ctx-save-null", rc=ctx_save(t, RH_NULL)[0])
    emit("adv", "ctx-save-pcr", rc=ctx_save(t, 0x00000010)[0])
    sh = seq("SHA256", 8)
    t.flush(sh)
    emit("adv", "ctx-save-after-flush", rc=ctx_save(t, sh)[0])

    # --- the header is plaintext, and ContextLoad reads it before it checks
    #     anything ---------------------------------------------------------
    # savedHandle picks the branch (session, object, sequence), sequence is
    # carried into the integrity HMAC, and hierarchy picks the proof that HMAC
    # is taken under.  Each of these substitutes a valid-LOOKING value rather
    # than flipping a bit, so the blob still parses and the answer names which
    # check caught it -- a different fact from "a corrupted blob is refused".
    #
    # One case per code path reached, not one per return code.  The
    # three substitutions that are all rejected by the same handle-type switch
    # are one case, not three, and sequence and hierarchy are one each: all of
    # {0,+1,max} and all of {owner,endorsement,platform} were measured and all
    # of them answer exactly what the kept case answers, which makes the rest
    # of them the same behaviour with a different constant.
    sh = seq("SHA256", 64, 19)
    rc, blob = ctx_save(t, sh)
    t.flush(sh)
    if rc == 0:
        def hdr(seqno=None, saved=None, hier=None):
            b = bytearray(blob)
            if seqno is not None:
                b[0:8] = struct.pack(">Q", seqno)
            if saved is not None:
                b[8:12] = struct.pack(">I", saved)
            if hier is not None:
                b[12:16] = struct.pack(">I", hier)
            return bytes(b)

        seqno = struct.unpack(">Q", blob[:8])[0]
        for tag, h in [("object", 0x80000000),      # the object branch
                       ("session", 0x02000000),     # the session branch
                       ("permanent", RH_NULL)]:     # no branch at all
            emit("adv", "ctx-load-saved-%s" % tag,
                 rc=ctx_load(t, hdr(saved=h))[0])
        emit("adv", "ctx-load-seq-plus1",
             rc=ctx_load(t, hdr(seqno=seqno + 1))[0])
        emit("adv", "ctx-load-hier-owner",
             rc=ctx_load(t, hdr(hier=RH_OWNER))[0])

    # --- two saves of one sequence, and the older blob afterwards ---------
    # ContextSave does not invalidate what it saved before.  Whether the stale
    # blob still loads, and what the counter did between the two saves, are
    # properties of this build rather than of the spec.
    sh = seq("SHA256", 96, 20)
    rc1, b1 = ctx_save(t, sh)
    rc2, b2nd = ctx_save(t, sh)
    t.flush(sh)
    emit("adv", "ctx-save-twice", rc=rc1 or rc2,
         seqno=(b1[:8] + b2nd[:8]).hex() if rc1 == 0 and rc2 == 0 else "-")
    for tag, rcS, bb in [("stale", rc1, b1), ("fresh", rc2, b2nd)]:
        if rcS:
            emit("adv", "ctx-load-%s" % tag, rc=rcS)
            continue
        rcL, hL = ctx_load(t, bb)
        emit("adv", "ctx-load-%s" % tag, rc=rcL)
        if rcL == 0:
            t.flush(hL)

    # --- one blob, loaded and finished, then loaded and finished again ----
    # The first copy is gone by the time the second load happens, so a state
    # whose real contents live somewhere the blob does not name has nothing
    # left to point at.  Both digests are over the same bytes and must agree.
    sh = seq("SHA256", *CTX_AGAIN_PRE)
    rc, blob = ctx_save(t, sh)
    t.flush(sh)
    for i in range(2):
        if rc:
            emit("adv", "ctx-load-again-%d" % i, rc=rc)
            continue
        rcL, hL = ctx_load(t, blob)
        if rcL:
            emit("adv", "ctx-load-again-%d" % i, rc=rcL)
            continue
        rcF, dF = finish(hL, *CTX_AGAIN_TAIL)
        emit("adv", "ctx-load-again-%d" % i, rc=rcF, out=dF)

    # --- the slots run out on distinct blobs at the same count ------------
    # ctx-load-until-full reloads one blob; this one holds three different
    # states at once, which is the shape a port that shares storage between
    # copies fails differently from.
    blobs = []
    for i, a in enumerate(HASHES):
        s2 = seq(a, 32, 30 + i)
        rcS, bb = ctx_save(t, s2)
        t.flush(s2)
        if rcS == 0:
            blobs.append(bb)
    held = []
    rcl = 0
    for bb in blobs + blobs:
        rcl, hh = ctx_load(t, bb)
        if rcl:
            break
        held.append(hh)
    emit("adv", "ctx-load-distinct-full", rc=rcl, sz=len(held))
    for hh in held:
        t.flush(hh)

    # --- the handle is gone, and the commands that used it say so ---------
    sh = seq("SHA256", 40, 23)
    ctx_save(t, sh)
    t.flush(sh)
    emit("adv", "ctx-update-flushed",
         rc=t.cmd("SequenceUpdate", b2(msg(4, 24)), handles=[sh],
                  auth=pw_session())[0])
    emit("adv", "ctx-complete-flushed",
         rc=t.cmd("SequenceComplete", b2(b"") + u32(RH_NULL), handles=[sh],
                  auth=pw_session())[0])

    # --- a restored object is still an object -----------------------------
    # ContextLoad puts back what was saved, including what kind of thing it
    # was.  A sequence command aimed at a restored ordinary object has to be
    # refused for being the wrong kind of handle, not attempted on whatever
    # happens to be in the slot.
    key = msg(32, 25)
    rc, hnd, _ = t.load_external(hmac_sens(key), hmac_pub(key))
    if hnd is None:
        emit("adv", "ctx-complete-object", rc=rc)
    else:
        rcS, blob = ctx_save(t, hnd)
        t.flush(hnd)
        rcL, hL = ctx_load(t, blob) if rcS == 0 else (rcS, 0)
        emit("adv", "ctx-complete-object", rc=rcL or t.cmd(
            "SequenceComplete", b2(b"") + u32(RH_NULL), handles=[hL],
            auth=pw_session())[0])
        if rcL == 0:
            t.flush(hL)

@session("e2epcr")
def s_pcr(t, emit):
    t.startup()
    for bank in ["SHA1", "SHA256"]:
        for group in [range(0, 8), range(8, 16), range(16, 24)]:
            rc, p = t.cmd("PCR_Read", pcr_select(list(group), bank))
            emit("ok", "read-reset-%s-%d" % (bank, list(group)[0]), rc=rc,
                 out=p)
    for i in range(8):
        d = digest(32, i)
        rc, _ = t.cmd("PCR_Extend",
                      u32(1) + u16(ALG["SHA256"]) + d, handles=[16],
                      auth=pw_session())
        emit("ok", "extend16-%d" % i, rc=rc)
        rc, p = t.cmd("PCR_Read", pcr_select([16]))
        emit("ok", "read16-%d" % i, rc=rc, out=p)
    rc, p = t.cmd("PCR_Event", b2(msg(37)), handles=[16], auth=pw_session())
    emit("ok", "event16", rc=rc, out=p)
    rc, p = t.cmd("PCR_Read", pcr_select([16]))
    emit("ok", "read16-after-event", rc=rc, out=p)
    emit("ok", "reset16", rc=t.cmd("PCR_Reset", b"", handles=[16],
                                   auth=pw_session())[0])
    rc, p = t.cmd("PCR_Read", pcr_select([16]))
    emit("ok", "read16-after-reset", rc=rc, out=p)
    # PCR 0 is not resettable at locality 0; the refusal is the expectation.
    emit("adv", "reset0", rc=t.cmd("PCR_Reset", b"", handles=[0],
                                   auth=pw_session())[0])
    for i in [0, 1, 7, 15, 23]:
        rc, _ = t.cmd("PCR_Extend", u32(1) + u16(ALG["SHA256"]) + digest(32,
                                                                        i),
                      handles=[i], auth=pw_session())
        emit("ok", "extend-%d" % i, rc=rc)
    rc, p = t.cmd("PCR_Read", pcr_select([0, 1, 7, 15, 23]))
    emit("ok", "read-extended", rc=rc, out=p)
    # Both banks at once, and a selection naming a bank the TPM does not have.
    rc, p = t.cmd("PCR_Read", u32(2) + u16(ALG["SHA1"]) + u8(3)
                  + b"\x01\x00\x00" + u16(ALG["SHA256"]) + u8(3)
                  + b"\x01\x00\x00")
    emit("ok", "read-two-banks", rc=rc, out=p)
    rc, p = t.cmd("PCR_Read", u32(0))
    emit("adv", "read-empty-selection", rc=rc, out=p)
    for bad, tag in [(u32(1) + u16(ALG["SHA512"]) + u8(3) + b"\x01\x00\x00",
                      "bank-sha512"),
                     (u32(1) + u16(ALG["SHA256"]) + u8(0), "zero-sizeofselect"),
                     (u32(1) + u16(ALG["SHA256"]) + u8(2) + b"\x01\x00",
                      "short-select"),
                     (u32(1) + u16(ALG["SHA256"]) + u8(9) + b"\x01" * 9,
                      "long-select"),
                     (u32(99) + u16(ALG["SHA256"]) + u8(3) + b"\x01\x00\x00",
                      "count-lies")]:
        emit("adv", "read-%s" % tag, rc=t.cmd("PCR_Read", bad)[0])
    emit("adv", "extend-24",
         rc=t.cmd("PCR_Extend", u32(1) + u16(ALG["SHA256"]) + digest(32),
                  handles=[24], auth=pw_session())[0])
    emit("adv", "extend-wrong-digest-size",
         rc=t.cmd("PCR_Extend", u32(1) + u16(ALG["SHA256"]) + digest(20),
                  handles=[16], auth=pw_session())[0])
    emit("adv", "extend-no-session",
         rc=t.cmd("PCR_Extend", u32(1) + u16(ALG["SHA256"]) + digest(32),
                  handles=[16])[0])


@session("e2epolicy")
def s_policy(t, emit):
    """Trial policy sessions: a pure hash chain with a readable answer.

    PolicyGetDigest reports the running policy digest, so every step of the
    chain is a byte-exact expectation that never touches a key.  The session
    handle and nonceTPM are not: nonceTPM is fresh randomness, and only its
    length is recorded.
    """
    t.startup()

    def start(authhash="SHA256", kind=SE_TRIAL):
        rc, p = t.cmd("StartAuthSession",
                      b2(bytes(DIGEST_SIZE[authhash])) + b2(b"")
                      + u8(kind) + sym_def_object() + u16(alg(authhash)),
                      handles=[RH_NULL, RH_NULL])
        if rc:
            return rc, None, 0
        hnd = struct.unpack(">I", p[:4])[0]
        return rc, hnd, len(unb2(p, 4)[0])

    def pdigest(hnd):
        rc, p = t.cmd("PolicyGetDigest", b"", handles=[hnd])
        return rc, (unb2(p)[0] if rc == 0 else b"")

    for authhash in HASHES:
        rc, hnd, nlen = start(authhash)
        emit("ok", "start-trial-%s" % authhash, rc=rc, sz=nlen)
        if hnd is None:
            continue
        rc, d = pdigest(hnd)
        emit("ok", "empty-%s" % authhash, rc=rc, out=d)
        t.flush(hnd)

    rc, hnd, _ = start()
    steps = [
        ("cmdcode-sign", "PolicyCommandCode", u32(CC["Sign"])),
        ("authvalue", "PolicyAuthValue", b""),
        ("locality-1c", "PolicyLocality", u8(0x1C)),
        ("cphash", "PolicyCpHash", b2(digest(32, 1))),
        ("pcr-empty", "PolicyPCR", b2(b"") + pcr_select([0, 7, 16])),
        ("physpres", "PolicyPhysicalPresence", b""),
    ]
    for tag, cc, payload in steps:
        rc, _ = t.cmd(cc, payload, handles=[hnd])
        emit("ok", "step-%s-rc" % tag, rc=rc)
        rc, d = pdigest(hnd)
        emit("ok", "step-%s" % tag, rc=rc, out=d)
    emit("ok", "restart", rc=t.cmd("PolicyRestart", b"", handles=[hnd])[0])
    rc, d = pdigest(hnd)
    emit("ok", "after-restart", rc=rc, out=d)
    t.flush(hnd)

    for n in [1, 2, 3, 8, 9]:
        rc, hnd, _ = start()
        lst = u32(n) + b"".join(b2(digest(32, i)) for i in range(n))
        rc2, _ = t.cmd("PolicyOR", lst, handles=[hnd])
        rc3, d = pdigest(hnd)
        emit("ok" if 2 <= n <= 8 else "adv", "or-%d" % n, rc=rc2, out=d)
        t.flush(hnd)

    for tag, cc, payload in [
            ("password", "PolicyPassword", b""),
            ("namehash", "PolicyNameHash", b2(digest(32, 2))),
            ("dupselect", "PolicyDuplicationSelect",
             b2(digest(32, 3)) + b2(digest(32, 4)) + u8(1))]:
        rc, hnd, _ = start()
        rc2, _ = t.cmd(cc, payload, handles=[hnd])
        rc3, d = pdigest(hnd)
        emit("ok", "single-%s" % tag, rc=rc2, out=d)
        t.flush(hnd)

    rc, hnd, _ = start()
    emit("adv", "cmdcode-twice-first",
         rc=t.cmd("PolicyCommandCode", u32(CC["Sign"]), handles=[hnd])[0])
    emit("adv", "cmdcode-twice-second",
         rc=t.cmd("PolicyCommandCode", u32(CC["HMAC"]), handles=[hnd])[0])
    emit("adv", "locality-zero",
         rc=t.cmd("PolicyLocality", u8(0), handles=[hnd])[0])
    emit("adv", "cphash-wrong-size",
         rc=t.cmd("PolicyCpHash", b2(digest(20)), handles=[hnd])[0])
    emit("adv", "or-one-entry",
         rc=t.cmd("PolicyOR", u32(1) + b2(digest(32)), handles=[hnd])[0])
    emit("adv", "getdigest-bad-handle",
         rc=t.cmd("PolicyGetDigest", b"", handles=[0x03FFFFFF])[0])
    t.flush(hnd)
    emit("adv", "flush-twice", rc=t.flush(hnd))

    rc, hnd, nlen = start(kind=SE_POLICY)
    emit("ok", "start-policy-session", rc=rc, sz=nlen)
    if hnd:
        rc, d = pdigest(hnd)
        emit("ok", "policy-session-empty", rc=rc, out=d)
        t.flush(hnd)
    rc, hnd, nlen = start(kind=SE_HMAC)
    emit("ok", "start-hmac-session", rc=rc, sz=nlen)
    if hnd:
        emit("adv", "getdigest-on-hmac-session", rc=pdigest(hnd)[0])
        t.flush(hnd)
    emit("adv", "start-bad-type",
         rc=t.cmd("StartAuthSession", b2(bytes(32)) + b2(b"") + u8(9)
                  + sym_def_object() + u16(ALG["SHA256"]),
                  handles=[RH_NULL, RH_NULL])[0])
    emit("adv", "start-bad-authhash",
         rc=t.cmd("StartAuthSession", b2(bytes(32)) + b2(b"") + u8(SE_TRIAL)
                  + sym_def_object() + u16(0x0099),
                  handles=[RH_NULL, RH_NULL])[0])


def rsa_body(t, emit, name):
    k = KEYS["rsa"][name]
    klen = k["bits"] // 8
    t.startup()

    rc, hpub, nm = t.load_external(b2(b""), rsa_pub(k))
    emit("ok", "load-public-only", rc=rc, out=nm)
    rc, hpriv, nm2 = t.load_external(rsa_sens(k), rsa_pub(k))
    emit("ok", "load-with-private", rc=rc, out=nm2)
    if hpriv is None:
        return
    rc, p = t.cmd("ReadPublic", b"", handles=[hpriv])
    emit("ok", "readpublic", rc=rc, out=p)
    for nalg in HASHES:
        rc, _, nm3 = t.load_external(b2(b""), rsa_pub(k, nalg=nalg))
        emit("ok", "name-%s" % nalg, rc=rc, out=nm3)

    for a in HASHES:
        d = digest(DIGEST_SIZE[a], 1)
        rc, sig = t.cmd("Sign", b2(d) + u16(ALG["RSASSA"]) + u16(alg(a))
                        + null_ticket(), handles=[hpriv], auth=pw_session())
        emit("ok", "sign-rsassa-%s" % a, rc=rc, out=sig)
        if rc == 0:
            emit("ok", "verify-rsassa-%s" % a,
                 rc=t.cmd("VerifySignature", b2(d) + sig,
                          handles=[hpub])[0])
            bad = bytearray(sig)
            bad[-1] ^= 1
            emit("adv", "verify-corrupt-%s" % a,
                 rc=t.cmd("VerifySignature", b2(d) + bytes(bad),
                          handles=[hpub])[0])
        rc, sig = t.cmd("Sign", b2(d) + u16(ALG["RSAPSS"]) + u16(alg(a))
                        + null_ticket(), handles=[hpriv], auth=pw_session())
        emit("ok", "sign-rsapss-%s" % a, rc=rc, sz=len(sig))
        if rc == 0:
            emit("ok", "verify-rsapss-%s" % a,
                 rc=t.cmd("VerifySignature", b2(d) + sig,
                          handles=[hpub])[0])

    # Encryption picks fresh padding, so the ciphertext is not an expectation;
    # decrypting a ciphertext built here is, and it covers the same unpadding.
    for scheme, label in [("RSAES", b""), ("OAEP", b"")]:
        rc, p = t.cmd("RSA_Encrypt", u32(0)[:0] + b2(msg(16, 3))
                      + u16(ALG[scheme]) + b2(label), handles=[hpub])
        emit("ok", "encrypt-%s" % scheme, rc=rc, sz=len(p) - 2)
    plain = msg(24, 4)
    fixtures = [("raw", rsa_raw(b"\x00" + msg(klen - 1, 5), k), "NULL", b""),
                ("pkcs1", rsa_raw(pkcs1v15_pad(plain, klen), k), "RSAES",
                 b""),
                ("oaep-sha1", rsa_raw(oaep_pad(plain, klen, "sha1"), k),
                 "OAEP", b""),
                ("oaep-sha256", rsa_raw(oaep_pad(plain, klen, "sha256"), k),
                 "OAEP", b"")]
    for tag, ct, scheme, label in fixtures:
        rc, p = t.cmd("RSA_Decrypt", b2(ct) + u16(alg(scheme)) + b2(label),
                      handles=[hpriv], auth=pw_session())
        emit("ok", "decrypt-%s" % tag, rc=rc, out=unb2(p)[0] if rc == 0
             else b"")
    for tag, ct in [("garbage", msg(klen, 6)),
                    ("zero", bytes(klen)),
                    ("short", msg(klen - 1, 7)),
                    ("long", msg(klen + 1, 8))]:
        emit("adv", "decrypt-bad-%s" % tag,
             rc=t.cmd("RSA_Decrypt", b2(ct) + u16(ALG["RSAES"]) + b2(b""),
                      handles=[hpriv], auth=pw_session())[0])
    emit("adv", "decrypt-public-only",
         rc=t.cmd("RSA_Decrypt", b2(rsa_raw(pkcs1v15_pad(plain, klen), k))
                  + u16(ALG["RSAES"]) + b2(b""), handles=[hpub],
                  auth=pw_session())[0])
    emit("adv", "sign-public-only",
         rc=t.cmd("Sign", b2(digest(32, 1)) + u16(ALG["RSASSA"])
                  + u16(ALG["SHA256"]) + null_ticket(), handles=[hpub],
                  auth=pw_session())[0])
    emit("adv", "sign-digest-too-short",
         rc=t.cmd("Sign", b2(digest(20, 1)) + u16(ALG["RSASSA"])
                  + u16(ALG["SHA256"]) + null_ticket(), handles=[hpriv],
                  auth=pw_session())[0])
    emit("adv", "sign-scheme-ecdsa",
         rc=t.cmd("Sign", b2(digest(32, 1)) + u16(ALG["ECDSA"])
                  + u16(ALG["SHA256"]) + null_ticket(), handles=[hpriv],
                  auth=pw_session())[0])
    emit("adv", "encrypt-message-too-long",
         rc=t.cmd("RSA_Encrypt", b2(msg(klen, 9)) + u16(ALG["RSAES"])
                  + b2(b""), handles=[hpub])[0])
    emit("adv", "encrypt-oaep-too-long",
         rc=t.cmd("RSA_Encrypt", b2(msg(klen - 2 * 32, 9))
                  + u16(ALG["OAEP"]) + b2(b""), handles=[hpub])[0])
    for tag, sens, pub in [
            ("prime-not-a-factor", sensitive("RSA", msg(klen // 2, 10)),
             rsa_pub(k)),
            ("prime-wrong-size", sensitive("RSA", h(k["p"])[:-2]),
             rsa_pub(k)),
            ("exponent-1", rsa_sens(k), rsa_pub(k, exponent=1)),
            ("exponent-even", rsa_sens(k), rsa_pub(k, exponent=4)),
            ("bits-mismatch", rsa_sens(k), rsa_pub(k, bits=k["bits"] // 2)),
            ("bits-512", rsa_sens(k), rsa_pub(k, bits=512)),
            ("bits-3072", rsa_sens(k), rsa_pub(k, bits=3072)),
            ("restricted-no-parent", rsa_sens(k),
             rsa_pub(k, attrs=ATTR_ANY | (1 << 16)))]:
        emit("adv", "load-%s" % tag, rc=t.load_external(sens, pub)[0])
    emit("ok", "flush", rc=t.flush(hpriv))


for _name in ["k1024e3", "k1024e65537", "k2048e3", "k2048e65537"]:
    session("e2ersa" + _name[1:].replace("e", "x"))(
        (lambda n: lambda t, emit: rsa_body(t, emit, n))(_name))


def ecc_body(t, emit, name, schemes=("ECDSA", "ECSCHNORR")):
    k = KEYS["ecc"][name + "_1"]
    peer = KEYS["ecc"][name + "_2"]
    curve = k["curve"]
    t.startup()

    rc, hpub, nm = t.load_external(b2(b""), ecc_pub(k))
    emit("ok", "load-public-only", rc=rc, out=nm)
    rc, hpriv, nm2 = t.load_external(ecc_sens(k), ecc_pub(k))
    emit("ok", "load-with-private", rc=rc, out=nm2)
    if hpriv is None:
        return
    rc, p = t.cmd("ReadPublic", b"", handles=[hpriv])
    emit("ok", "readpublic", rc=rc, out=p)

    pt = b2(b2(h(peer["qx"])) + b2(h(peer["qy"])))
    rc, p = t.cmd("ECDH_ZGen", pt, handles=[hpriv], auth=pw_session())
    emit("ok", "ecdh-zgen", rc=rc, out=p)
    rc, p = t.cmd("ECDH_KeyGen", b"", handles=[hpub])
    emit("ok", "ecdh-keygen", rc=rc, sz=len(p))

    for a in HASHES:
        d = digest(DIGEST_SIZE[a], 2)
        for scheme in schemes:
            def sign():
                return t.cmd("Sign", b2(d) + u16(ALG[scheme]) + u16(alg(a))
                             + null_ticket(), handles=[hpriv],
                             auth=pw_session())
            rc, sig = sign()

            # ValidateSignatureEcdsa truncates the digest to the length of r
            # (CpriECC.c:1909) after _cpri__ValidateSignatureEcc has stripped
            # r's leading zeros, where FIPS 186-4 truncates to the length of
            # n.  SignEcdsa marshals r zero-padded to the length of
            # n, so one signature in 256 leads with a zero byte, verifies
            # against 31 bytes of a 32-byte digest, and is rejected by the
            # same TPM that made it.  Whether that byte came out zero is a
            # fact about this draw and not about the port, so draw again
            # until it did not; the round trip below then answers the same
            # way in every replay.  Nothing is hidden by this: the defect is
            # scored, and deterministically, by eccverify/adv/011-014 on the
            # engine half.  The signature's marshalled length does not move,
            # so the line recorded here is the same either way.
            tries = 0
            while (rc == 0 and scheme == "ECDSA" and sig[6:7] == b"\x00"
                   and tries < 64):
                rc, sig = sign()
                tries += 1
            emit("ok", "sign-%s-%s" % (scheme, a), rc=rc, sz=len(sig))
            if rc == 0:
                emit("ok", "verify-%s-%s" % (scheme, a),
                     rc=t.cmd("VerifySignature", b2(d) + sig,
                              handles=[hpub])[0])
                bad = bytearray(sig)
                bad[-1] ^= 1
                emit("adv", "verify-corrupt-%s-%s" % (scheme, a),
                     rc=t.cmd("VerifySignature", b2(d) + bytes(bad),
                              handles=[hpub])[0])
    rc, p = t.cmd("Commit", pt + b2(h(peer["qx"])) + b2(h(peer["qy"])),
                  handles=[hpriv], auth=pw_session())
    emit("ok", "commit", rc=rc, sz=len(p))
    rc, p = t.cmd("EC_Ephemeral", u16(CURVE[curve]))
    emit("ok", "ec-ephemeral", rc=rc, sz=len(p))

    for cname, cid in sorted(CURVE.items(), key=lambda kv: kv[1]):
        rc, p = t.cmd("ECC_Parameters", u16(cid))
        emit("ok", "params-%s" % cname, rc=rc, out=p)
    for cid in [0x0000, 0x0099, 0xFFFF]:
        emit("adv", "params-bad-%x" % cid,
             rc=t.cmd("ECC_Parameters", u16(cid))[0])

    cb = KEYS["curves"].get(curve)
    bad_loads = [
        ("off-curve", ecc_sens(k), ecc_pub(k, qy=bytes(
            a ^ 1 for a in h(k["qy"])))),
        ("infinity", sensitive("ECC", b"\x01"),
         ecc_pub(k, qx=b"", qy=b"")),
        ("d-zero", sensitive("ECC", b"\x00" * (len(h(k["d"])))),
         ecc_pub(k)),
        ("d-empty", sensitive("ECC", b""), ecc_pub(k)),
        ("d-too-long", sensitive("ECC", h(k["d"]) + b"\x01"), ecc_pub(k)),
        ("wrong-curve", ecc_sens(k), ecc_pub(k, curve="NIST_P384"
                                             if curve != "NIST_P384"
                                             else "NIST_P256")),
        ("bad-curve-id", ecc_sens(k), ecc_pub(k, curve=0x0099)),
        ("x-too-long", ecc_sens(k), ecc_pub(k, qx=h(k["qx"]) + b"\x00")),
    ]
    if cb:
        bad_loads.append(("generator-point", sensitive("ECC", b"\x01"),
                          ecc_pub(k, qx=h(cb["gx"]), qy=h(cb["gy"]))))
        bad_loads.append(("declared-off-curve", ecc_sens(k),
                          ecc_pub(k, qx=h(cb["offx"]), qy=h(cb["offy"]))))
    for tag, sens, pub in bad_loads:
        emit("adv", "load-%s" % tag, rc=t.load_external(sens, pub)[0])
    emit("adv", "zgen-off-curve",
         rc=t.cmd("ECDH_ZGen",
                  b2(b2(h(peer["qx"]))
                     + b2(bytes(a ^ 1 for a in h(peer["qy"])))),
                  handles=[hpriv], auth=pw_session())[0])
    emit("adv", "sign-public-only",
         rc=t.cmd("Sign", b2(digest(32, 2)) + u16(ALG[schemes[0]])
                  + u16(ALG["SHA256"]) + null_ticket(), handles=[hpub],
                  auth=pw_session())[0])
    emit("adv", "sign-scheme-rsassa",
         rc=t.cmd("Sign", b2(digest(32, 2)) + u16(ALG["RSASSA"])
                  + u16(ALG["SHA256"]) + null_ticket(), handles=[hpriv],
                  auth=pw_session())[0])
    emit("ok", "flush", rc=t.flush(hpriv))


# ECDSA on P-384 takes the whole TPM down, and takes the rest of the session
# with it.  SignEcdsa asserts that the curve order fits MAX_ECC_PARAMETER_BYTES
# (src/OsslCryptoEngine/CpriECC.c:1088), which OsslCryptoEngine.h:80 fixes at
# 32, while Implementation.h:136,144 enable P-384 with MAX_ECC_KEY_BYTES 48;
# the assert calls TpmFail, which recurses until the stack runs out.  Nothing
# is overflowed -- the check fires first, and the sanitizer screen found no
# memory error on any P-384 path -- so the curve keeps every case except the
# one command that cannot return.
ECC_SCHEMES = {"NIST_P384": ("ECSCHNORR",)}

for _name in ["NIST_P256", "NIST_P384", "BN_P256"]:
    session("e2eecc" + _name.replace("_", "").lower())(
        (lambda n: lambda t, emit: ecc_body(
            t, emit, n, ECC_SCHEMES.get(n, ("ECDSA", "ECSCHNORR"))))(_name))


@session("e2ehmac")
def s_hmac(t, emit):
    t.startup()
    for a in HASHES:
        for klen in [1, 16, 20, 32, 48, 64, 65, 128]:
            key = msg(klen, 11)
            rc, hnd, nm = t.load_external(hmac_sens(key),
                                          hmac_pub(key, halg=a))
            emit("ok", "load-%s-%d" % (a, klen), rc=rc, out=nm)
            if hnd is None:
                continue
            for dlen in [0, 1, 64, 1024]:
                rc, p = t.cmd("HMAC", b2(msg(dlen, 12)) + u16(alg(a)),
                              handles=[hnd], auth=pw_session())
                emit("ok", "mac-%s-%d-%d" % (a, klen, dlen), rc=rc,
                     out=unb2(p)[0] if rc == 0 else b"")
            t.flush(hnd)

    key = msg(32, 13)
    rc, hnd, _ = t.load_external(hmac_sens(key), hmac_pub(key))
    for ci, chunks in enumerate([[], [1], [64], [1, 1, 1], [1024, 5]]):
        rc, p = t.cmd("HMAC_Start", b2(b"") + u16(ALG["SHA256"]),
                      handles=[hnd], auth=pw_session(), rhandles=1)
        if rc:
            emit("ok", "seq-%d" % ci, rc=rc)
            continue
        sh = struct.unpack(">I", p[:4])[0]
        for j, n in enumerate(chunks[:-1] if chunks else []):
            t.cmd("SequenceUpdate", b2(msg(n, j)), handles=[sh],
                  auth=pw_session())
        tail = msg(chunks[-1], len(chunks) - 1) if chunks else b""
        rc, p = t.cmd("SequenceComplete", b2(tail) + u32(RH_NULL),
                      handles=[sh], auth=pw_session())
        emit("ok", "seq-%d" % ci, rc=rc, out=unb2(p)[0] if rc == 0 else b"")
    emit("adv", "mac-unsupported-alg",
         rc=t.cmd("HMAC", b2(msg(8)) + u16(ALG["SHA512"]), handles=[hnd],
                  auth=pw_session())[0])
    emit("adv", "mac-oversize",
         rc=t.cmd("HMAC", b2(msg(2048)) + u16(ALG["SHA256"]), handles=[hnd],
                  auth=pw_session())[0])
    emit("adv", "mac-wrong-auth",
         rc=t.cmd("HMAC", b2(msg(8)) + u16(ALG["SHA256"]), handles=[hnd],
                  auth=pw_session(b"wrong"))[0])
    for tag, sens, pub in [
            ("unique-mismatch", hmac_sens(key),
             public("KEYEDHASH", "SHA256", ATTR_SIGN,
                    keyedhash_parms("HMAC", "SHA256"), b2(digest(32, 14)))),
            ("key-too-long", hmac_sens(msg(256, 15)),
             hmac_pub(msg(256, 15))),
            ("no-sign-attr", hmac_sens(key),
             hmac_pub(key, attrs=A_USERWITHAUTH | A_NODA | A_DECRYPT)),
            ("scheme-null", hmac_sens(key),
             public("KEYEDHASH", "SHA256", ATTR_SIGN,
                    keyedhash_parms("NULL"), kh_unique(key, b"", "SHA256"))),
            ("scheme-xor", hmac_sens(key),
             public("KEYEDHASH", "SHA256", ATTR_SIGN,
                    keyedhash_parms("XOR", "SHA256"),
                    kh_unique(key, b"", "SHA256")))]:
        emit("adv", "load-%s" % tag, rc=t.load_external(sens, pub)[0])
    # A seed makes unique = H(seed || key); it must be honoured, not ignored.
    seed = msg(32, 16)
    rc, hnd2, nm = t.load_external(hmac_sens(key, seed),
                                   hmac_pub(key, seed=seed))
    emit("ok", "load-seeded", rc=rc, out=nm)
    if hnd2:
        rc, p = t.cmd("HMAC", b2(msg(20, 17)) + u16(ALG["SHA256"]),
                      handles=[hnd2], auth=pw_session())
        emit("ok", "mac-seeded", rc=rc, out=unb2(p)[0] if rc == 0 else b"")


@session("e2esym")
def s_sym(t, emit):
    t.startup()
    for bits in [128, 192, 256]:
        key = msg(bits // 8, 18)
        for mode in ["CTR", "OFB", "CBC", "CFB", "ECB"]:
            rc, hnd, nm = t.load_external(sym_sens(key),
                                          sym_pub(key, mode=mode))
            emit("ok", "load-%d-%s" % (bits, mode), rc=rc, out=nm)
            if hnd is None:
                continue
            iv = msg(16, 19)
            data = msg(48, 20)
            rc, p = t.cmd("EncryptDecrypt",
                          u8(0) + u16(ALG[mode]) + b2(iv) + b2(data),
                          handles=[hnd], auth=pw_session())
            emit("ok", "enc-%d-%s" % (bits, mode), rc=rc, out=p)
            if rc == 0:
                ct, off = unb2(p)
                rc2, p2 = t.cmd("EncryptDecrypt",
                                u8(1) + u16(ALG[mode]) + b2(iv) + b2(ct),
                                handles=[hnd], auth=pw_session())
                back = unb2(p2)[0] if rc2 == 0 else b""
                emit("ok", "roundtrip-%d-%s" % (bits, mode), rc=rc2,
                     out=b"ok" if back == data else b"differs")
            # A stream mode takes any length; a block mode must refuse a
            # partial block rather than pad it.
            emit("adv", "enc-partial-block-%d-%s" % (bits, mode),
                 rc=t.cmd("EncryptDecrypt",
                          u8(0) + u16(ALG[mode]) + b2(iv) + b2(msg(17, 21)),
                          handles=[hnd], auth=pw_session())[0])
            emit("adv", "enc-empty-%d-%s" % (bits, mode),
                 rc=t.cmd("EncryptDecrypt",
                          u8(0) + u16(ALG[mode]) + b2(iv) + b2(b""),
                          handles=[hnd], auth=pw_session())[0])
            emit("adv", "enc-short-iv-%d-%s" % (bits, mode),
                 rc=t.cmd("EncryptDecrypt",
                          u8(0) + u16(ALG[mode]) + b2(msg(8, 22)) + b2(data),
                          handles=[hnd], auth=pw_session())[0])
            emit("adv", "enc-mode-null-%d-%s" % (bits, mode),
                 rc=t.cmd("EncryptDecrypt",
                          u8(0) + u16(ALG["NULL"]) + b2(iv) + b2(data),
                          handles=[hnd], auth=pw_session())[0])
            t.flush(hnd)
    key = msg(16, 18)
    for tag, sens, pub in [
            ("bits-64", sym_sens(msg(8, 23)), sym_pub(msg(8, 23))),
            ("bits-mismatch", sym_sens(key), sym_pub(key, bits=256)),
            ("alg-sm4", sym_sens(key), sym_pub(key, algo="SM4")),
            ("alg-xor", sym_sens(key), sym_pub(key, algo="XOR")),
            ("sign-attribute-set", sym_sens(key),
             sym_pub(key, attrs=ATTR_ANY)),
            ("unique-mismatch", sym_sens(key),
             public("SYMCIPHER", "SHA256", ATTR_SYM,
                    sym_def_object("AES", 128, "CFB"), b2(digest(32, 24))))]:
        emit("adv", "load-%s" % tag, rc=t.load_external(sens, pub)[0])
    emit("adv", "encdec-unloaded-handle",
         rc=t.cmd("EncryptDecrypt",
                  u8(0) + u16(ALG["CFB"]) + b2(msg(16, 19)) + b2(msg(32, 20)),
                  handles=[0x80FFFFFF], auth=pw_session())[0])


@session("e2eerr")
def s_err(t, emit):
    """Malformed buffers: the dispatcher's answers, with no crypto involved.

    Each case sends a hand-built command buffer through raw(), so the framing
    length stays honest while the command's own header lies.
    """
    t.startup()
    good = u16(ST_NO_SESSIONS) + u32(12) + u32(CC["GetRandom"]) + u16(8)

    def verdict(tag, blob, kind="adv", locality=0):
        try:
            rsp = t.raw(blob, locality)
        except Dead:
            raise
        if len(rsp) < 10:
            emit(kind, tag, sz=len(rsp))
            return
        rtag, size, rc = struct.unpack(">HII", rsp[:10])
        emit(kind, tag, rc=rc, sz=size, tag16=rtag)

    verdict("baseline-getrandom", good, "ok")
    for bad in [0x0000, 0x00C4, 0x1234, 0x8000, 0x8003, 0x8022, 0xFFFF]:
        verdict("tag-%04x" % bad,
                u16(bad) + u32(12) + u32(CC["GetRandom"]) + u16(8))
    for sz in [0, 1, 4, 9, 10, 11, 13, 0xFFFF, 0xFFFFFFFF]:
        verdict("size-%d" % sz,
                u16(ST_NO_SESSIONS) + u32(sz) + u32(CC["GetRandom"]) + u16(8))
    for n in [0, 1, 5, 9]:
        verdict("truncated-%d" % n, good[:n])
    for cc in [0x0000, 0x0123, 0x011E, 0x015A, 0x015F, 0x018F, 0x0999,
               0xFFFFFFFF]:
        verdict("cc-%08x" % cc,
                u16(ST_NO_SESSIONS) + u32(12) + u32(cc) + u16(8))
    # A command that takes no sessions, sent with one, and the reverse.
    verdict("sessions-on-getrandom",
            u16(ST_SESSIONS) + u32(12 + 4 + 9) + u32(CC["GetRandom"])
            + u32(9) + pw_session() + u16(8))
    verdict("no-sessions-on-pcrextend",
            u16(ST_NO_SESSIONS) + u32(10 + 4 + 2 + 32)
            + u32(CC["PCR_Extend"]) + u32(16) + u16(ALG["SHA256"])
            + digest(32))
    for hnd in [0x00000000, 0x80000000, 0x80FFFFFF, 0x02000000, 0x03000000,
                0xFFFFFFFF, 0x40000000]:
        verdict("handle-%08x" % hnd,
                u16(ST_NO_SESSIONS) + u32(14) + u32(CC["ReadPublic"])
                + u32(hnd))
    verdict("readpublic-no-handle",
            u16(ST_NO_SESSIONS) + u32(10) + u32(CC["ReadPublic"]))
    verdict("flush-two-handles",
            u16(ST_NO_SESSIONS) + u32(18) + u32(CC["FlushContext"])
            + u32(0x80000000) + u32(0x80000001))
    body = u32(16) + u16(ALG["SHA256"]) + digest(32)
    for asz in [0, 4, 8, 9, 100, 0xFFFFFFFF]:
        verdict("authsize-%d" % asz,
                u16(ST_SESSIONS) + u32(10 + 4 + 4 + 9 + len(body))
                + u32(CC["PCR_Extend"]) + u32(16) + u32(asz) + pw_session()
                + body)
    for sh in [0x02000000, 0x03000000, 0x40000000, 0x4000000A, 0xFFFFFFFF]:
        auth = u32(sh) + b2(b"") + u8(0) + b2(b"")
        verdict("session-handle-%08x" % sh,
                u16(ST_SESSIONS) + u32(10 + 4 + 4 + len(auth) + len(body))
                + u32(CC["PCR_Extend"]) + u32(16) + u32(len(auth)) + auth
                + body)
    # A TPM2B claiming more bytes than the buffer holds, and one claiming less.
    for n, tag in [(0xFFFF, "huge"), (100, "over"), (2, "under")]:
        verdict("tpm2b-%s" % tag,
                u16(ST_NO_SESSIONS) + u32(10 + 2 + 8 + 2 + 4)
                + u32(CC["Hash"]) + u16(n) + msg(8) + u16(ALG["SHA256"])
                + u32(RH_NULL))
    for loc in [1, 2, 3, 4, 5, 255]:
        verdict("locality-%d" % loc, good, locality=loc)
    verdict("trailing-bytes",
            u16(ST_NO_SESSIONS) + u32(12) + u32(CC["GetRandom"]) + u16(8)
            + b"\xde\xad\xbe\xef")
    verdict("empty-parameters",
            u16(ST_NO_SESSIONS) + u32(10) + u32(CC["GetRandom"]))
    verdict("after-all-that", good, "ok")


# --------------------------------------------------------------------------
# driver
# --------------------------------------------------------------------------
def fmt(value):
    if isinstance(value, (bytes, bytearray)):
        return value.hex() if value else "-"
    if isinstance(value, bool):
        return "1" if value else "0"
    return str(value)


def sanscan(path, seen, cid, tag, out):
    """Attribute anything the simulator printed since the last case to this one.

    A clean simulator writes nothing at all, so new bytes in sim.log are a
    sanitizer report, and the case that produced them is the one that just
    ran.  That attribution is only sound while a single case is in flight at
    a time, which is exactly how a session is driven.
    """
    try:
        size = os.path.getsize(path)
    except OSError:
        return seen
    if size <= seen:
        return seen
    with open(path, "rb") as lf:
        lf.seek(seen)
        blob = lf.read(size - seen).decode("utf-8", "replace")
    for line in blob.splitlines():
        if ("runtime error:" in line or "ERROR: AddressSanitizer" in line
                or "SUMMARY:" in line):
            out.append("# SAN %s t=%s %s" % (cid, tag, line.strip()))
    return size


def ports_from(base):
    """Hand out a port pair that no process in this run has bound before."""
    nxt = [base]

    def take():
        p = nxt[0]
        nxt[0] += 2
        return p
    return take


def run(binary, out, port, stall, quiet, sandir=None, only=None):
    root = tempfile.mkdtemp(prefix="e2e.")
    alloc = ports_from(port)
    deadline = time.time() + stall if stall else None
    written = 0
    try:
        with open(out, "w") as fh:
            fh.write("# End-to-end behaviour suite, recorded from %s\n"
                     % os.path.basename(binary))
            for family, fn in SESSIONS:
                # --only is an authoring convenience: it makes one session
                # replayable on its own while a case is being written.  A
                # recording is never made with it -- record-e2e.sh checks
                # that every registered session is present.
                if only and family not in only:
                    continue
                if deadline and time.time() > deadline:
                    fh.write("# stalled: skipped session %s\n" % family)
                    continue
                wd = os.path.join(root, family)
                simlog = os.path.join(wd, "sim.log")
                # A session is buffered, not streamed: a run that loses the
                # entropy toss part way through has to be thrown away whole,
                # and half a session already on disk cannot be taken back.
                for attempt in range(ENTROPY_TRIES):
                    counters = {}
                    lines = []
                    seen = [0]

                    def emit(kind, tag, _fam=family, _c=counters, _out=lines,
                             _log=simlog, _seen=seen, **fields):
                        _c[kind] = _c.get(kind, 0) + 1
                        cid = "%s/%s/%03d" % (_fam, kind, _c[kind])
                        row = [cid, "t=" + tag]
                        row += ["%s=%s" % (k, fmt(v))
                                for k, v in fields.items()]
                        _out.append(" ".join(row))
                        if sandir:
                            _seen[0] = sanscan(_log, _seen[0], cid, tag, _out)

                    t = Tpm(binary, 0, wd, ports=alloc)
                    retry = None
                    try:
                        t.start()
                        fn(t, emit)
                    except Flaky as e:
                        retry = e
                    except Dead as e:
                        lines.append("# session %s died: %s" % (family, e))
                    except Exception as e:              # a bug in a case
                        lines.append("# session %s raised %s: %s"
                                     % (family, type(e).__name__, e))
                    finally:
                        t.stop()
                        if sandir:
                            # Whatever the last case provoked lands after it
                            # returns, and a fatal report after the death.
                            sanscan(simlog, seen[0], "%s/end" % family, "-",
                                    lines)
                            shutil.copyfile(
                                simlog, os.path.join(sandir, family + ".log"))
                    if retry is None:
                        break
                    if not quiet:
                        print("  %-16s retry %d: %s" % (family, attempt + 1,
                                                        retry))
                else:
                    lines.append("# session %s never got past startup"
                                 % family)
                fh.write("\n".join(lines) + "\n")
                fh.flush()
                if not quiet:
                    n = sum(counters.values())
                    # Retries go to the console, never to the output file: the
                    # file is diffed run against run, and a line that counts
                    # coin tosses would differ every time.
                    print("  %-16s %4d cases%s"
                          % (family, n, "  (%d process starts)" % t.starts
                             if t.starts > 1 else ""))
                written += sum(counters.values())
    finally:
        shutil.rmtree(root, ignore_errors=True)
    if not quiet:
        print("%d cases -> %s" % (written, out))
    return 0


def main(argv):
    global KEYS
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    import json
    KEYS = json.loads(open(os.path.join(os.path.dirname(
        os.path.abspath(__file__)), "keys.json")).read())
    port = int(argv[argv.index("--port") + 1]) if "--port" in argv else 2321
    stall = float(argv[argv.index("--stall") + 1]) if "--stall" in argv \
        else 900.0
    sandir = argv[argv.index("--sanlog") + 1] if "--sanlog" in argv else None
    if sandir:
        os.makedirs(sandir, exist_ok=True)
    only = (argv[argv.index("--only") + 1].split(",")
            if "--only" in argv else None)
    return run(argv[1], argv[2], port, stall, "--quiet" in argv,
               sandir, only)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
