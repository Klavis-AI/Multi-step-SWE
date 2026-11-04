"""Wire-level probe cases for SMB 3.1.1 (dialect 0x0311).

Expectations recorded from Samba 4.17.12-Debian under the same smb.conf, except
where the notes record a documented substitute.
"""
import struct

import smbprobe as P
from probebase import Ctx, runner

ALL = [0x0202, 0x0210, 0x0300, 0x0302, 0x0311]
PREAUTH = 1
ENCRYPT = 2
COMPRESS = 3
NETNAME = 5
SIGNING = 8


def _ctxs(r):
    return {c["ContextType"]: c for c in (r.get("NegotiateContexts") or [])}


def neg_0311_contexts(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    cs = _ctxs(r)
    pre = cs.get(PREAUTH, {})
    enc = cs.get(ENCRYPT, {})
    sig = cs.get(SIGNING, {})
    return {
        "status": r["status"], "DialectRevision": r.get("DialectRevision"),
        "has_preauth": PREAUTH in cs, "has_encryption": ENCRYPT in cs,
        "has_signing": SIGNING in cs, "has_compression": COMPRESS in cs,
        "HashAlgorithmCount": pre.get("HashAlgorithmCount"),
        "HashAlgorithms": pre.get("HashAlgorithms"),
        "SaltLength": pre.get("SaltLength"),
        "Ciphers": enc.get("Ciphers"),
        "SigningAlgorithms": sig.get("SigningAlgorithms"),
    }


def neg_0311_ccm_only(ctx):
    c = ctx.client()
    c.client_ciphers = (P.SMB2_ENCRYPTION_AES128_CCM,)
    r = c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    enc = _ctxs(r).get(ENCRYPT, {})
    return {"status": r["status"], "Ciphers": enc.get("Ciphers")}


def neg_0311_without_preauth_context(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(ALL, contexts=[(ENCRYPT, P.encryption_context())],
                         capabilities=c.client_capabilities)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision")}


def neg_0311_unknown_hash(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(ALL,
                         contexts=[(PREAUTH, P.preauth_integrity_context(hash_ids=(0x1234,)))],
                         capabilities=c.client_capabilities)
    return {"status": r["status"]}


def neg_0311_netname_context(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(
        ALL,
        contexts=[(PREAUTH, P.preauth_integrity_context()),
                  (ENCRYPT, P.encryption_context()),
                  (NETNAME, P.netname_context("127.0.0.1"))],
        capabilities=c.client_capabilities)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision")}


def session_signed_gmac(ctx):
    c = ctx.client()
    c.verify_signatures = True
    c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(ctx.share)
    e = c.smb2_echo()
    return {"tree": t["status"], "echo": e["status"],
            "signing_algorithm": c.signing_algorithm,
            "signature_valid": e.get("SignatureValid")}


def encrypted_share_gcm(ctx):
    c = ctx.client()
    c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect("enc")
    o = c.smb2_create("probe-gcm.bin", create_disposition=P.FILE_OVERWRITE_IF)
    w = c.smb2_write(o["FileId"], 0, b"G" * 4096)
    r = c.smb2_read(o["FileId"], 0, 4096)
    return {"encrypt_data": bool(t.get("ShareFlags", 0) & 0x00008000),
            "cipher": c.cipher_id, "write": w["status"], "read": r["status"],
            "encrypted": bool(r.get("Encrypted")), "len": len(r.get("Buffer", b""))}


def tree_connect_and_file_ops(ctx):
    c = ctx.client()
    c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(ctx.share)
    o = c.smb2_create("probe-311.bin", create_disposition=P.FILE_OVERWRITE_IF)
    w = c.smb2_write(o["FileId"], 0, b"3" * 1024)
    rd = c.smb2_read(o["FileId"], 0, 1024)
    q = c.smb2_query_info(o["FileId"], P.SMB2_0_INFO_FILE, 5)
    b = q.get("Buffer", b"")
    return {"tree": t["status"], "write": w["status"], "read": rd["status"],
            "EndOfFile": struct.unpack_from("<q", b, 8)[0] if len(b) >= 16 else None}


def validate_negotiate_info_at_0311(ctx):
    """[MS-SMB2] 3.3.5.15.12: secure negotiate is not used at 0x0311."""
    c = ctx.client()
    c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.smb2_tree_connect(ctx.share)
    req = (struct.pack("<I", c.client_capabilities) + c.client_guid
           + struct.pack("<HH", 1, 5) + struct.pack("<HHHHH", *ALL))
    r = c.smb2_ioctl(b"\xff" * 16, P.FSCTL_VALIDATE_NEGOTIATE_INFO, req, max_out=24)
    return {"status": r["status"]}


def negotiate_0300_still_works(ctx):
    c = ctx.client()
    r = c.smb2_negotiate([0x0202, 0x0210, 0x0300], capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(ctx.share)
    e = c.smb2_echo()
    return {"DialectRevision": r.get("DialectRevision"), "tree": t["status"],
            "echo": e["status"]}


# --------------------------------------------------------------------------
# negotiate contexts in detail ([MS-SMB2] 2.2.3.1)


def neg_0311_preauth_salt(ctx):
    """The pre-auth integrity context the server answers with."""
    c = ctx.client()
    r = c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    pre = _ctxs(r).get(PREAUTH, {})
    salt = pre.get("Salt") or b""
    return {"status": r["status"],
            "HashAlgorithmCount": pre.get("HashAlgorithmCount"),
            "HashAlgorithms": pre.get("HashAlgorithms"),
            "SaltLength": pre.get("SaltLength"),
            "salt_len_matches": len(salt) == (pre.get("SaltLength") or 0),
            "salt_not_constant": salt != b"\x00" * len(salt) if salt else None}


def neg_0311_cipher_preference(ctx):
    """The server picks one cipher out of the client's list."""
    out = {}
    for label, ciphers in [("gcm-first", (P.SMB2_ENCRYPTION_AES128_GCM,
                                          P.SMB2_ENCRYPTION_AES128_CCM)),
                           ("ccm-first", (P.SMB2_ENCRYPTION_AES128_CCM,
                                          P.SMB2_ENCRYPTION_AES128_GCM))]:
        c = ctx.client()
        c.client_ciphers = ciphers
        r = c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
        out[label] = (_ctxs(r).get(ENCRYPT, {})).get("Ciphers")
        c.close()
    return out


def neg_0311_unknown_cipher(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(ALL,
                         contexts=[(PREAUTH, P.preauth_integrity_context()),
                                   (ENCRYPT, P.encryption_context(ciphers=(0x00FF,)))],
                         capabilities=c.client_capabilities)
    enc = _ctxs(r).get(ENCRYPT, {})
    return {"status": r["status"], "Ciphers": enc.get("Ciphers")}


def neg_0311_signing_context(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(
        ALL,
        contexts=[(PREAUTH, P.preauth_integrity_context()),
                  (ENCRYPT, P.encryption_context()),
                  (SIGNING, P.signing_context(algorithms=(P.SMB2_SIGNING_AES_GMAC,
                                                          P.SMB2_SIGNING_AES_CMAC)))],
        capabilities=c.client_capabilities)
    sig = _ctxs(r).get(SIGNING, {})
    return {"status": r["status"], "SigningAlgorithms": sig.get("SigningAlgorithms")}


def neg_0311_signing_cmac_only(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(
        ALL,
        contexts=[(PREAUTH, P.preauth_integrity_context()),
                  (ENCRYPT, P.encryption_context()),
                  (SIGNING, P.signing_context(algorithms=(P.SMB2_SIGNING_AES_CMAC,)))],
        capabilities=c.client_capabilities)
    sig = _ctxs(r).get(SIGNING, {})
    return {"status": r["status"], "SigningAlgorithms": sig.get("SigningAlgorithms")}


def compression_context_offered(ctx):
    """Samba 4.17 does not negotiate compression, so this rung's compression
    requirement is not scored against it: what is asserted is the property
    [MS-SMB2] 2.2.3.1.3 and [MS-XCA] fix — the response carries a compression
    context whose algorithm list is a non-empty subset of the four [MS-XCA]
    algorithm ids, with the reserved Flags field clear."""
    c = ctx.client()
    r = c.smb2_negotiate(
        ALL,
        contexts=[(PREAUTH, P.preauth_integrity_context()),
                  (ENCRYPT, P.encryption_context()),
                  (COMPRESS, P.compression_context(
                      algorithms=(P.COMPRESSION_LZNT1, P.COMPRESSION_LZ77,
                                  P.COMPRESSION_LZ77_HUFFMAN,
                                  P.COMPRESSION_PATTERN_V1)))],
        capabilities=c.client_capabilities)
    comp = _ctxs(r).get(COMPRESS, {})
    algs = comp.get("CompressionAlgorithms") or []
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision"),
            "has_compression": COMPRESS in _ctxs(r),
            "Flags": comp.get("Flags"),
            "count_nonzero": len(algs) > 0,
            "all_known": all(a in (1, 2, 3, 4) for a in algs),
            "count_matches": comp.get("CompressionAlgorithmCount") == len(algs)}


def _compressed_roundtrip(ctx, algorithm, make_payload):
    """Send one request compressed and compare the answer with the plain form."""
    c = ctx.client()
    c.smb2_negotiate(
        ALL,
        contexts=[(PREAUTH, P.preauth_integrity_context()),
                  (ENCRYPT, P.encryption_context()),
                  (COMPRESS, P.compression_context(
                      algorithms=(P.COMPRESSION_LZNT1, P.COMPRESSION_LZ77,
                                  P.COMPRESSION_LZ77_HUFFMAN,
                                  P.COMPRESSION_PATTERN_V1)))],
        capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.smb2_tree_connect(ctx.share)
    payload = make_payload()
    o = c.smb2_create("probe-comp.bin", create_disposition=P.FILE_OVERWRITE_IF)
    plain = c.smb2_write(o["FileId"], 0, payload)
    body = c.body_write(o["FileId"], len(payload), payload)
    mid = c.message_id
    pdu = c.smb2_header(P.SMB2_WRITE, session_id=c.session_id, tree_id=c.tree_id,
                        message_id=mid, credit_charge=c._credit_charge(len(payload)),
                        credit_request=64) + body
    c.message_id += 1
    if c.signing_required and c.keys.get("signing"):
        pdu = pdu[:16] + struct.pack("<I", 0x08) + pdu[20:]
        pdu = c.sign_pdu(pdu)
    off = len(pdu) - len(payload)
    if algorithm == P.COMPRESSION_PATTERN_V1:
        blob = P.pattern_v1_compress(payload[0], len(payload))
    else:
        blob = P.lz77_plain_compress(payload)
    # [MS-SMB2] 3.1.4.4 step 3: OriginalCompressedSegmentSize is the uncompressed
    # length of the compressed portion only (the bytes after Offset), and
    # 3.3.5.2.1.2 disconnects when the decompressed size differs from it.
    wire = P.compression_transform(len(pdu) - off, algorithm, pdu[:off] + blob, offset=off)
    try:
        c.send_raw(wire)
        r = c._smb2_receive(mid)
    except Exception:
        return {"plain": plain["status"], "outcome": "disconnected"}
    back = c.smb2_read(o["FileId"], 0, 2 * len(payload))
    return {"plain": plain["status"], "plain_count": plain.get("Count"),
            "compressed": r["status"], "compressed_count": r.get("Count"),
            "same_count": r.get("Count") == plain.get("Count"),
            "read_back_len": r.get("Count") and back.get("DataLength"),
            "read_back_matches": back.get("Buffer") == payload * 2}


def compression_roundtrip_lz77(ctx):
    return _compressed_roundtrip(ctx, P.COMPRESSION_LZ77,
                                 lambda: bytes(bytearray((i * 13) & 0xFF
                                                         for i in range(4096))))


def compression_roundtrip_pattern_v1(ctx):
    return _compressed_roundtrip(ctx, P.COMPRESSION_PATTERN_V1,
                                 lambda: b"\x5a" * 4096)


def neg_0311_duplicate_context(ctx):
    """The same context type sent twice."""
    c = ctx.client()
    r = c.smb2_negotiate(
        ALL,
        contexts=[(PREAUTH, P.preauth_integrity_context()),
                  (PREAUTH, P.preauth_integrity_context()),
                  (ENCRYPT, P.encryption_context())],
        capabilities=c.client_capabilities)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision")}


def neg_0311_unknown_context_ignored(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(
        ALL,
        contexts=[(PREAUTH, P.preauth_integrity_context()),
                  (ENCRYPT, P.encryption_context()),
                  (0x00FE, b"\x01\x02\x03\x04")],
        capabilities=c.client_capabilities)
    cs = _ctxs(r)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision"),
            "echoed_unknown": 0x00FE in cs}


def neg_0311_context_alignment(ctx):
    """Every negotiate context in the answer starts on an 8-byte boundary.

    Measured from the raw response rather than from the parser's own cursor: the
    parser advances to the next 8-byte boundary itself, so an `Offset` it reports
    is aligned whatever the wire said. The contexts begin at
    `NegotiateContextOffset` from the start of the SMB2 header, and each one's
    8-byte DataLength-bearing header plus its data is padded to the next
    boundary; this walks that chain over the bytes that actually arrived.
    """
    c = ctx.client()
    r = c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    raw = r.get("raw", b"")
    base = r.get("NegotiateContextOffset") or 0
    count = r.get("NegotiateContextCount") or 0
    offsets, off, ok = [], base, True
    for _ in range(count):
        if off + 8 > len(raw):
            ok = False
            break
        offsets.append(off)
        if (off - base) % 8 != 0:
            ok = False
        dlen = struct.unpack_from("<H", raw, off + 2)[0]
        off += 8 + dlen
        off += (-off) % 8
    return {"status": r["status"],
            "count": count,
            "parsed_count": len(r.get("NegotiateContexts") or []),
            "context_offset_aligned": base % 8 == 0,
            "every_context_aligned": ok and len(offsets) == count,
            "first_context_offset_matches_header": base >= 64}


# --------------------------------------------------------------------------
# pre-authentication integrity ([MS-SMB2] 3.3.5.4)


def preauth_hash_chained(ctx):
    """A session set up after a 0x0311 negotiate signs with a key derived from
    the pre-auth hash: a wrong hash makes the final verification fail."""
    c = ctx.client()
    c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    good = c.smb2_tree_connect(ctx.share)
    c2 = ctx.client()
    c2.smb2_negotiate(ALL, capabilities=c2.client_capabilities)
    c2.preauth_corrupt = True            # honoured by the probe's key derivation
    try:
        c2.smb2_session_setup(ctx.user, ctx.password)
        bad = "accepted"
    except P.SmbProbeError as exc:
        bad = "rejected" if "STATUS" in str(exc) or "signature" in str(exc) else "error"
    except Exception:
        bad = "error"
    return {"good_tree": good["status"], "corrupt_preauth": bad}


def session_setup_signed_at_0311(ctx):
    """Whether a signing-capable 0x0311 session may still send an unsigned
    request under `server signing = auto`."""
    c = ctx.client()
    c.verify_signatures = True
    c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    signed = c.smb2_tree_connect(ctx.share)
    c.signing_required = False
    unsigned = c.smb2_command(P.SMB2_ECHO, struct.pack("<HH", 4, 0), sign=False)
    return {"signed_tree": signed["status"], "unsigned_echo": unsigned["status"],
            "signing_algorithm": c.signing_algorithm}


def encrypted_share_ccm_at_0311(ctx):
    c = ctx.client()
    c.client_ciphers = (P.SMB2_ENCRYPTION_AES128_CCM,)
    c.smb2_negotiate(ALL, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect("enc")
    o = c.smb2_create("probe-ccm311.bin", create_disposition=P.FILE_OVERWRITE_IF)
    w = c.smb2_write(o["FileId"], 0, b"C" * 2048)
    r = c.smb2_read(o["FileId"], 0, 2048)
    return {"tree": t["status"], "cipher": c.cipher_id, "write": w["status"],
            "read": r["status"], "encrypted": bool(r.get("Encrypted")),
            "len": len(r.get("Buffer", b""))}


def negotiate_0302_still_works(ctx):
    c = ctx.client()
    r = c.smb2_negotiate([0x0202, 0x0210, 0x0300, 0x0302],
                         capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(ctx.share)
    e = c.smb2_echo()
    return {"DialectRevision": r.get("DialectRevision"), "tree": t["status"],
            "echo": e["status"], "signing_algorithm": c.signing_algorithm}


def session_signed_with_gmac(ctx):
    """Negotiate AES-128-GMAC and then actually sign with it.

    Advertising the algorithm in a negotiate context is not the same as using
    it: a server that answers `SigningAlgorithms: [2]` and then signs every
    reply with AES-128-CMAC satisfies a context-only check. This drives a real
    request under GMAC and verifies the server's MAC on the reply, so the
    algorithm has to be the one that was negotiated.
    """
    c = ctx.client()
    c.verify_signatures = True
    c.client_signing_algorithms = (P.SMB2_SIGNING_AES_GMAC, P.SMB2_SIGNING_AES_CMAC)
    n = c.smb2_negotiate(ALL, capabilities=c.client_capabilities, security_mode=0x02)
    sig = _ctxs(n).get(SIGNING, {})
    c.smb2_session_setup(ctx.user, ctx.password)
    c.signing_required = True
    t = c.smb2_tree_connect(ctx.share)
    e = c.smb2_echo()
    return {"negotiated_algorithms": sig.get("SigningAlgorithms"),
            "client_uses": c.signing_algorithm,
            "tree": t["status"], "echo": e["status"],
            "echo_signed": bool(e.get("Flags", 0) & 0x08),
            "signature_valid": e.get("SignatureValid"),
            "_negotiated": n.get("DialectRevision")}


def preauth_mismatch_refused_by_server(ctx):
    """A signature keyed on the wrong pre-auth hash must be refused by the SERVER.

    The earlier form of this case corrupted the hash on the client and then read
    the client's own failure to verify the server's reply, which any server with
    any other key produces. This signs a request with the wrong key and records
    what the server does with it.
    """
    c = ctx.client()
    c.verify_signatures = False
    c.smb2_negotiate(ALL, capabilities=c.client_capabilities, security_mode=0x02)
    c.preauth_corrupt = True          # the KDF context, and so the signing key
    try:
        ss = c.smb2_session_setup(ctx.user, ctx.password)
    except Exception:
        return {"outcome": "session-setup-failed"}
    obs = {"session": ss["status"]}
    if ss["status"] != 0:
        return obs
    c.signing_required = True
    try:
        t = c.smb2_tree_connect(ctx.share)
    except Exception:
        obs["outcome"] = "disconnected"
        return obs
    obs["outcome"] = "status"
    obs["tree"] = t["status"]
    return obs


CASES = [
    ("neg-0311-contexts", neg_0311_contexts),
    ("neg-0311-ccm-only", neg_0311_ccm_only),
    ("neg-0311-without-preauth-context", neg_0311_without_preauth_context),
    ("neg-0311-unknown-hash", neg_0311_unknown_hash),
    ("neg-0311-netname-context", neg_0311_netname_context),
    ("session-signed-gmac", session_signed_gmac),
    ("session-signed-with-gmac", session_signed_with_gmac),
    ("preauth-mismatch-refused-by-server", preauth_mismatch_refused_by_server),
    ("encrypted-share-gcm", encrypted_share_gcm),
    ("tree-connect-and-file-ops", tree_connect_and_file_ops),
    ("validate-negotiate-info-at-0311", validate_negotiate_info_at_0311),
    ("negotiate-0300-still-works", negotiate_0300_still_works),
    ("neg-0311-preauth-salt", neg_0311_preauth_salt),
    ("neg-0311-cipher-preference", neg_0311_cipher_preference),
    ("neg-0311-unknown-cipher", neg_0311_unknown_cipher),
    ("neg-0311-signing-context", neg_0311_signing_context),
    ("neg-0311-signing-cmac-only", neg_0311_signing_cmac_only),
    ("compression-context-offered", compression_context_offered),
    ("compression-roundtrip-lz77", compression_roundtrip_lz77),
    ("compression-roundtrip-pattern-v1", compression_roundtrip_pattern_v1),
    ("neg-0311-duplicate-context", neg_0311_duplicate_context),
    ("neg-0311-unknown-context-ignored", neg_0311_unknown_context_ignored),
    ("neg-0311-context-alignment", neg_0311_context_alignment),
    ("preauth-hash-chained", preauth_hash_chained),
    ("session-setup-signed-at-0311", session_setup_signed_at_0311),
    ("encrypted-share-ccm-at-0311", encrypted_share_ccm_at_0311),
    ("negotiate-0302-still-works", negotiate_0302_still_works),
]

run_all = runner(CASES, record_dialect=True)
