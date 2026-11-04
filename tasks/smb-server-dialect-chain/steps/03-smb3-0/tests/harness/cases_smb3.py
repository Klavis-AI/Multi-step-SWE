"""Wire-level probe cases for SMB 3.0 (dialect 0x0300).

Every expected value is what Samba 4.17.12-Debian answered, recorded against
a real smbd started with the same smb.conf and the same seeded share.
"""
import struct

import smbprobe as P
from probebase import Ctx, runner

D300 = [0x0202, 0x0210, 0x0300]


def _cap(v, bit):
    return bool(v & bit)


def _ctx(resp, name):
    for e in resp.get("CreateContexts") or []:
        if e.get("NameBytes") == name:
            return e
    return None


def _sess(ctx, dialects=D300, share=None, caps=None, **kw):
    c = ctx.client()
    n = c.smb2_negotiate(dialects,
                         capabilities=c.client_capabilities if caps is None else caps,
                         **kw)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(share or ctx.share)
    return c, n, t


# --------------------------------------------------------------------------
# negotiate and capabilities


def neg_0300(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(D300, capabilities=c.client_capabilities)
    caps = r.get("Capabilities", 0)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision"),
            "MaxTransactSize": r.get("MaxTransactSize"),
            "cap_leasing": _cap(caps, P.SMB2_GLOBAL_CAP_LEASING),
            "cap_large_mtu": _cap(caps, P.SMB2_GLOBAL_CAP_LARGE_MTU),
            "cap_directory_leasing": _cap(caps, P.SMB2_GLOBAL_CAP_DIRECTORY_LEASING),
            "cap_encryption": _cap(caps, P.SMB2_GLOBAL_CAP_ENCRYPTION),
            "cap_multi_channel": _cap(caps, P.SMB2_GLOBAL_CAP_MULTI_CHANNEL),
            "cap_persistent_handles": _cap(caps, P.SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)}


def neg_0300_without_encryption_capability(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(D300, capabilities=0)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision"),
            "cap_encryption": _cap(r.get("Capabilities", 0), P.SMB2_GLOBAL_CAP_ENCRYPTION)}


def neg_only_0300(ctx):
    c = ctx.client()
    r = c.smb2_negotiate([0x0300], capabilities=c.client_capabilities)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision")}


# --------------------------------------------------------------------------
# signing: AES-128-CMAC replaces HMAC-SHA256 at 0x0300


def signing_cmac_verifies(ctx):
    c = ctx.client()
    c.verify_signatures = True
    c.smb2_negotiate(D300, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.signing_required = True          # this client chooses to sign
    t = c.smb2_tree_connect(ctx.share)
    e = c.smb2_echo()
    return {"tree": t["status"], "echo": e["status"],
            "echo_signed": bool(e.get("Flags", 0) & 0x08),
            "signature_valid": e.get("SignatureValid")}


def signing_hmac_sha256_rejected(ctx):
    """A 2.x-style HMAC-SHA256 signature is not authentic at 0x0300."""
    import hashlib
    import hmac as _hmac
    c = ctx.client()
    c.smb2_negotiate(D300, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.signing_required = True
    c.smb2_tree_connect(ctx.share)
    body = struct.pack("<HH", 4, 0)
    mid = c.message_id
    pdu = c.smb2_header(P.SMB2_ECHO, flags=0x08, session_id=c.session_id,
                        tree_id=c.tree_id, message_id=mid)
    c.message_id += 1
    blank = pdu[:48] + b"\x00" * 16 + pdu[64:] + body
    sig = _hmac.new(c.keys["signing"], blank, hashlib.sha256).digest()[:16]
    c.send_raw(blank[:48] + sig + blank[64:])
    prev, c.verify_signatures = c.verify_signatures, False
    try:
        r = c.smb2_receive_reply(mid)
        return {"status": r["status"]}
    except P.SmbProbeError:
        return {"status": "connection-closed"}
    finally:
        c.verify_signatures = prev


def signing_wrong_signature_rejected(ctx):
    c, _, _ = _sess(ctx)
    c.signing_required = True
    body = struct.pack("<HH", 4, 0)
    pdu = c.smb2_header(P.SMB2_ECHO, flags=0x08, session_id=c.session_id,
                        tree_id=c.tree_id, message_id=c.message_id)
    c.message_id += 1
    pdu = bytearray(pdu + body)
    pdu[48:64] = b"\x00" * 15 + b"\x01"
    c.send_raw(bytes(pdu))
    r = c.smb2_receive_reply(c.message_id - 1)
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# encryption


def encrypted_share_flags(ctx):
    c, n, t = _sess(ctx, share="enc")
    return {"status": t["status"],
            "encrypt_data": bool(t.get("ShareFlags", 0) & 0x00008000)}


def encrypted_share_roundtrip(ctx):
    c, n, t = _sess(ctx, share="enc")
    o = c.smb2_create("probe-enc.bin", create_disposition=P.FILE_OVERWRITE_IF)
    w = c.smb2_write(o["FileId"], 0, b"E" * 4096)
    r = c.smb2_read(o["FileId"], 0, 4096)
    return {"create": o["status"], "write": w["status"], "read": r["status"],
            "len": len(r.get("Buffer", b"")), "encrypted": bool(r.get("Encrypted")),
            "cipher": c.cipher_id}


def encrypted_share_plaintext_refused(ctx):
    """A client that will not encrypt cannot use a share that requires it."""
    c = ctx.client()
    c.smb2_negotiate(D300, capabilities=0)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect("enc")
    return {"status": t["status"]}


def plain_share_not_encrypted(ctx):
    c, n, t = _sess(ctx)
    e = c.smb2_echo()
    return {"tree": t["status"], "encrypt_data": bool(t.get("ShareFlags", 0) & 0x00008000),
            "echo_encrypted": bool(e.get("Encrypted"))}


# --------------------------------------------------------------------------
# secure negotiate and the interface list


def validate_negotiate_info(ctx):
    c, n, t = _sess(ctx)
    # VALIDATE_NEGOTIATE_INFO request ([MS-SMB2] 2.2.31.4): Capabilities, Guid,
    # SecurityMode, DialectCount, Dialects[].
    req = (struct.pack("<I", c.client_capabilities) + c.client_guid
           + struct.pack("<HH", 1, 3) + struct.pack("<HHH", 0x0202, 0x0210, 0x0300))
    r = c.smb2_ioctl(b"\xff" * 16, P.FSCTL_VALIDATE_NEGOTIATE_INFO, req, max_out=24)
    b = r.get("Buffer", b"")
    if len(b) < 24:
        return {"status": r["status"], "len": len(b)}
    caps = struct.unpack_from("<I", b, 0)[0]
    return {"status": r["status"], "len": len(b),
            "Capabilities": caps,
            "guid_matches_negotiate": b[4:20] == n.get("ServerGuid", b""),
            "guid_nonzero": b[4:20] != b"\x00" * 16,
            "SecurityMode": struct.unpack_from("<H", b, 20)[0],
            "Dialect": struct.unpack_from("<H", b, 22)[0],
            "dialect_matches_negotiate": struct.unpack_from("<H", b, 22)[0]
                                         == n.get("DialectRevision")}


def validate_negotiate_info_mismatch(ctx):
    """[MS-SMB2] 3.3.5.15.12: a request that does not match what was negotiated
    makes the server drop the connection."""
    c, n, t = _sess(ctx)
    req = (struct.pack("<I", 0) + b"\x00" * 16 + struct.pack("<HH", 0, 1)
           + struct.pack("<H", 0x0202))
    try:
        r = c.smb2_ioctl(b"\xff" * 16, P.FSCTL_VALIDATE_NEGOTIATE_INFO, req, max_out=24)
    except P.SmbProbeError:
        return {"status": "connection-closed"}
    return {"status": r["status"]}


def query_network_interface_info(ctx):
    """NETWORK_INTERFACE_INFO ([MS-SMB2] 2.2.32.5), decoded rather than weighed:
    eight zero bytes would satisfy a length check."""
    c, n, t = _sess(ctx)
    r = c.smb2_ioctl(b"\xff" * 16, P.FSCTL_QUERY_NETWORK_INTERFACE_INFO, b"", max_out=65536)
    b = r.get("Buffer", b"")
    entries, off, guard = [], 0, 0
    while off + 152 <= len(b) and guard < 32:
        nxt, ifindex, cap, _res, speed = struct.unpack_from("<IIIIQ", b, off)
        family = struct.unpack_from("<H", b, off + 24)[0]
        entries.append({"ifindex_nonzero": ifindex != 0,
                        "capability": cap,
                        "link_speed_nonzero": speed != 0,
                        "sockaddr_family": family,
                        "aligned": off % 8 == 0})
        if not nxt:
            break
        off += nxt
        guard += 1
    loopback = [e for e in entries
                if e["sockaddr_family"] in (2, 23)]      # AF_INET / AF_INET6
    return {"status": r["status"],
            "entry_count_nonzero": len(entries) > 0,
            "every_entry_has_index": all(e["ifindex_nonzero"] for e in entries),
            "every_entry_aligned": all(e["aligned"] for e in entries),
            "families": sorted({e["sockaddr_family"] for e in entries}),
            "has_inet_entry": len(loopback) > 0}


# --------------------------------------------------------------------------
# leases v2, directory leases, persistent handles


def lease_v2_epoch_advances(ctx):
    """The epoch moves when the lease state moves, and not otherwise.

    [MS-SMB2] 2.2.13.2.10: a v2 lease carries an epoch. Granting a read lease
    and then raising the same key to read/write/handle is one state change, so
    the second grant reports a higher epoch than the first; asking again for
    what the lease already holds changes neither.
    """
    c, _, _ = _sess(ctx)
    key = b"\x52" * 16

    def grant(state, disp):
        o = c.smb2_create("probe-lv2e.bin", create_disposition=disp,
                          oplock=P.SMB2_OPLOCK_LEVEL_LEASE,
                          contexts=[(b"RqLs", key + struct.pack("<IIQ16sHH", state, 0, 0,
                                                                b"\x00" * 16, 0, 0))])
        e = _ctx(o, b"RqLs")
        d = e.get("Data", b"") if e else b""
        if len(d) < 52:
            return {"status": o["status"], "context_len": len(d)}
        return {"status": o["status"],
                "LeaseState": struct.unpack_from("<I", d, 16)[0],
                "Epoch": struct.unpack_from("<H", d, 48)[0]}

    first = grant(0x01, P.FILE_OVERWRITE_IF)
    raised = grant(0x07, P.FILE_OPEN)
    again = grant(0x07, P.FILE_OPEN)
    return {"read_lease": first, "raised_to_rwh": raised, "unchanged": again}


def lease_v2_epoch(ctx):
    c, _, _ = _sess(ctx)
    key = b"\x51" * 16
    o = c.smb2_create("probe-lv2.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_LEASE,
                      contexts=[(b"RqLs", key + struct.pack("<IIQ16sHH", 0x07, 0, 0,
                                                            b"\x00" * 16, 0, 0))])
    e = _ctx(o, b"RqLs")
    d = e.get("Data", b"") if e else b""
    obs = {"status": o["status"], "OplockLevel": o.get("OplockLevel"),
           "context_len": len(d),
           "LeaseState": struct.unpack_from("<I", d, 16)[0] if len(d) >= 20 else None}
    if len(d) >= 52:
        obs["LeaseFlags"] = struct.unpack_from("<I", d, 20)[0]
        obs["LeaseDuration"] = struct.unpack_from("<Q", d, 24)[0]
        obs["ParentLeaseKey_zero"] = d[32:48] == b"\x00" * 16
        obs["Epoch"] = struct.unpack_from("<H", d, 48)[0]
        obs["key_echoed"] = d[0:16] == key
    return obs


def directory_lease(ctx):
    c, _, _ = _sess(ctx)
    key = b"\x61" * 16
    o = c.smb2_create("data", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES,
                      oplock=P.SMB2_OPLOCK_LEVEL_LEASE,
                      contexts=[(b"RqLs", P.lease_context_v1(key, 0x03))])
    e = _ctx(o, b"RqLs")
    d = e.get("Data", b"") if e else b""
    return {"status": o["status"], "OplockLevel": o.get("OplockLevel"),
            "LeaseState": struct.unpack_from("<I", d, 16)[0] if len(d) >= 20 else None}


def persistent_handle_request(ctx):
    """SMB2_DHANDLE_FLAG_PERSISTENT on a share that is not continuously
    available: the handle is granted, persistence is not."""
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-persist.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_BATCH,
                      contexts=[(b"DH2Q", P.durable_v2_context(
                          timeout=0, flags=0x00000002, create_guid=b"\x71" * 16))])
    e = _ctx(o, b"DH2Q")
    d = e.get("Data", b"") if e else b""
    return {"status": o["status"], "granted": e is not None,
            "Flags": struct.unpack_from("<I", d, 4)[0] if len(d) >= 8 else None}


def durable_v2_reconnect(ctx):
    c, _, _ = _sess(ctx)
    guid = b"\x81" * 16
    o = c.smb2_create("probe-dur3.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_BATCH,
                      contexts=[(b"DH2Q", P.durable_v2_context(timeout=16000,
                                                               create_guid=guid))])
    granted = _ctx(o, b"DH2Q") is not None
    c.smb2_write(o["FileId"], 0, b"durable")
    c.close()
    c2 = ctx.client()
    c2.client_guid = c.client_guid
    c2.smb2_negotiate(D300, capabilities=c2.client_capabilities,
                      client_guid=c.client_guid)
    c2.smb2_session_setup(ctx.user, ctx.password)
    c2.smb2_tree_connect(ctx.share)
    dh2c = o.get("FileId", b"\x00" * 16)[:16] + guid + struct.pack("<I", 0)
    r = c2.smb2_create("probe-dur3.bin", create_disposition=P.FILE_OPEN,
                       oplock=P.SMB2_OPLOCK_LEVEL_BATCH,
                       contexts=[(b"DH2C", dh2c)])
    data = c2.smb2_read(r["FileId"], 0, 7) if r["status"] == 0 else {"Buffer": b""}
    return {"granted": granted, "reconnect": r["status"],
            "context_len": len(dh2c),
            "data": data.get("Buffer", b"").decode("latin-1")}


# --------------------------------------------------------------------------
# server-side copy ([MS-SMB2] 2.2.31.1, 3.3.5.15.6)


def _seed_source(c, name="probe-copysrc.bin", payload=None):
    payload = payload or bytes(bytearray((i * 11) & 0xFF for i in range(8192)))
    o = c.smb2_create(name, create_disposition=P.FILE_OVERWRITE_IF)
    c.smb2_write(o["FileId"], 0, payload)
    return o, payload


def copychunk_resume_key(ctx):
    c, _, _ = _sess(ctx)
    o, _ = _seed_source(c)
    r = c.smb2_ioctl(o["FileId"], P.FSCTL_SRV_REQUEST_RESUME_KEY, b"", max_out=32)
    b = r.get("Buffer", b"")
    return {"status": r["status"], "len": len(b), "key_nonzero": b[:24] != b"\x00" * 24}


def copychunk_copy(ctx):
    c, _, _ = _sess(ctx)
    src, payload = _seed_source(c)
    rk = c.smb2_ioctl(src["FileId"], P.FSCTL_SRV_REQUEST_RESUME_KEY, b"", max_out=32)
    key = (rk.get("Buffer") or b"")[:24]
    dst = c.smb2_create("probe-copydst.bin", create_disposition=P.FILE_OVERWRITE_IF)
    req = key + struct.pack("<II", 1, 0) + struct.pack("<QQII", 0, 0, len(payload), 0)
    r = c.smb2_ioctl(dst["FileId"], P.FSCTL_SRV_COPYCHUNK, req, max_out=12)
    b = r.get("Buffer", b"")
    back = c.smb2_read(dst["FileId"], 0, len(payload))
    return {"resume_key": rk["status"], "copychunk": r["status"],
            "ChunksWritten": struct.unpack_from("<I", b, 0)[0] if len(b) >= 4 else None,
            "TotalBytesWritten": struct.unpack_from("<I", b, 8)[0] if len(b) >= 12 else None,
            "read_back_matches": back.get("Buffer") == payload}


def copychunk_bad_resume_key(ctx):
    c, _, _ = _sess(ctx)
    dst = c.smb2_create("probe-copybad.bin", create_disposition=P.FILE_OVERWRITE_IF)
    req = b"\xcd" * 24 + struct.pack("<II", 1, 0) + struct.pack("<QQII", 0, 0, 16, 0)

    def call():
        return c.smb2_ioctl(dst["FileId"], P.FSCTL_SRV_COPYCHUNK, req, max_out=12)

    try:
        r = call()
    except Exception:
        return {"outcome": "disconnected"}
    return {"outcome": "status", "status": r["status"]}


def fsctl_set_sparse_and_zero(ctx):
    c, _, _ = _sess(ctx)
    o, payload = _seed_source(c, "probe-sparse.bin")
    sp = c.smb2_ioctl(o["FileId"], P.FSCTL_SET_SPARSE, b"\x01", max_out=0)
    zd = c.smb2_ioctl(o["FileId"], P.FSCTL_SET_ZERO_DATA,
                      struct.pack("<qq", 1024, 2048), max_out=0)
    back = c.smb2_read(o["FileId"], 1024, 1024)
    return {"set_sparse": sp["status"], "set_zero_data": zd["status"],
            "hole_is_zero": back.get("Buffer") == b"\x00" * 1024}


def fsctl_unknown_refused(ctx):
    c, _, _ = _sess(ctx)
    o, _ = _seed_source(c, "probe-fsctl.bin")
    r = c.smb2_ioctl(o["FileId"], 0x00090000, b"", max_out=64)
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# the transform header itself ([MS-SMB2] 2.2.41)


def transform_header_fields(ctx):
    c, n, t = _sess(ctx, share="enc")
    e = c.smb2_echo()
    th = c.last_transform or {}
    return {"echo": e["status"], "encrypted": bool(e.get("Encrypted")),
            "cipher": c.cipher_id,
            "Flags": th.get("Flags"),
            "session_matches": th.get("SessionId") == c.session_id,
            "nonce_len_nonzero": bool(th.get("Nonce")) and th["Nonce"] != b"\x00" * 16,
            "OriginalMessageSize_matches": th.get("OriginalMessageSize") == len(e.get("raw", b""))}


def transform_bad_signature(ctx):
    """A transform header whose AEAD tag does not verify."""
    c, n, t = _sess(ctx, share="enc")
    body = struct.pack("<HH", 4, 0)
    mid = c.message_id
    pdu = c.smb2_header(P.SMB2_ECHO, session_id=c.session_id, tree_id=c.tree_id,
                        message_id=mid) + body
    c.message_id += 1
    wire = bytearray(c.encrypt_pdu(pdu))
    wire[4] ^= 0xFF                       # first byte of the AEAD tag

    def send_bad():
        c.send_raw(bytes(wire))
        raw = c.recv_raw()
        return c._smb2_split(c.decrypt_pdu(raw), True, raw)[0]

    try:
        r = send_bad()
    except Exception:
        return {"outcome": "disconnected"}
    return {"outcome": "status", "status": r["status"]}


def encrypted_compound(ctx):
    """A compounded, related chain inside one transform header."""
    c, n, t = _sess(ctx, share="enc")
    r = c.smb2_compound([
        (P.SMB2_CREATE, c.body_create("probe-enc-compound.bin",
                                      create_disposition=P.FILE_OVERWRITE_IF)),
        (P.SMB2_WRITE, c.body_write(b"\xff" * 16, 0, b"compound")),
        (P.SMB2_CLOSE, c.body_close(b"\xff" * 16)),
    ], related=True)
    chain = r.get("Chain") or [r]
    return {"encrypted": bool(r.get("Encrypted")),
            "statuses": [m["status"] for m in chain],
            "count": len(chain)}


def session_bind_refused(ctx):
    """SMB2_SESSION_FLAG_BINDING without multi-channel support."""
    c, n, t = _sess(ctx)
    blob = P.spnego_neg_token_init(P.ntlm_negotiate())
    body = struct.pack("<HBBIIHHQ", 25, 0x01, 0x01, 0, 0, 88, len(blob), 0) + blob
    r = c.smb2_command(P.SMB2_SESSION_SETUP, body, session_id=c.session_id)
    return {"status": r["status"]}


CASES = [
    ("neg-0300", neg_0300),
    ("neg-0300-without-encryption-capability", neg_0300_without_encryption_capability),
    ("neg-only-0300", neg_only_0300),
    ("signing-cmac-verifies", signing_cmac_verifies),
    ("signing-hmac-sha256-rejected", signing_hmac_sha256_rejected),
    ("signing-wrong-signature-rejected", signing_wrong_signature_rejected),
    ("encrypted-share-flags", encrypted_share_flags),
    ("encrypted-share-roundtrip", encrypted_share_roundtrip),
    ("encrypted-share-plaintext-refused", encrypted_share_plaintext_refused),
    ("plain-share-not-encrypted", plain_share_not_encrypted),
    ("validate-negotiate-info", validate_negotiate_info),
    ("validate-negotiate-info-mismatch", validate_negotiate_info_mismatch),
    ("query-network-interface-info", query_network_interface_info),
    ("lease-v2-epoch", lease_v2_epoch),
    ("lease-v2-epoch-advances", lease_v2_epoch_advances),
    ("directory-lease", directory_lease),
    ("persistent-handle-request", persistent_handle_request),
    ("durable-v2-reconnect", durable_v2_reconnect),
    ("copychunk-resume-key", copychunk_resume_key),
    ("copychunk-copy", copychunk_copy),
    ("copychunk-bad-resume-key", copychunk_bad_resume_key),
    ("fsctl-set-sparse-and-zero", fsctl_set_sparse_and_zero),
    ("fsctl-unknown-refused", fsctl_unknown_refused),
    ("transform-header-fields", transform_header_fields),
    ("transform-bad-signature", transform_bad_signature),
    ("encrypted-compound", encrypted_compound),
    ("session-bind-refused", session_bind_refused),
]

run_all = runner(CASES, record_dialect=True)
