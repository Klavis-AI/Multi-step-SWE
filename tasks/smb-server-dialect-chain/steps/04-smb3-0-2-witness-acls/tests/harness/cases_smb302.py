"""Wire-level probe cases for SMB 3.0.2 (dialect 0x0302).

Expectations recorded from Samba 4.17.12-Debian under the same smb.conf.
"""
import struct

import smbprobe as P
from probebase import Ctx, runner

D302 = [0x0202, 0x0210, 0x0300, 0x0302]


def _cap(v, b):
    return bool(v & b)


def _sess(ctx, share=None, dialects=None, caps=None):
    c = ctx.client()
    n = c.smb2_negotiate(dialects or D302,
                         capabilities=c.client_capabilities if caps is None else caps)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(share or ctx.share)
    return c, n, t


def neg_0302(ctx):
    c = ctx.client()
    r = c.smb2_negotiate(D302, capabilities=c.client_capabilities)
    caps = r.get("Capabilities", 0)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision"),
            "MaxTransactSize": r.get("MaxTransactSize"),
            "cap_encryption": _cap(caps, P.SMB2_GLOBAL_CAP_ENCRYPTION),
            "cap_multi_channel": _cap(caps, P.SMB2_GLOBAL_CAP_MULTI_CHANNEL),
            "cap_directory_leasing": _cap(caps, P.SMB2_GLOBAL_CAP_DIRECTORY_LEASING),
            "cap_persistent_handles": _cap(caps, P.SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)}


def neg_0302_and_0311_offered(ctx):
    """A client offering 0x0311 too gets the highest the configuration allows."""
    c = ctx.client()
    r = c.smb2_negotiate([0x0202, 0x0210, 0x0300, 0x0302, 0x0311],
                         capabilities=c.client_capabilities)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision")}


def neg_only_0302(ctx):
    c = ctx.client()
    r = c.smb2_negotiate([0x0302], capabilities=c.client_capabilities)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision")}


def read_unbuffered(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-unbuf.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb2_write(o["FileId"], 0, b"U" * 4096)
    # SMB2_READFLAG_READ_UNBUFFERED, valid from 0x0302 ([MS-SMB2] 2.2.19)
    body = c.body_read(o["FileId"], 0, 4096)
    body = body[:3] + bytes([0x01]) + body[4:]
    r = c.smb2_command(P.SMB2_READ, body)
    return {"status": r["status"], "len": len(r.get("Buffer", b""))}


def write_unbuffered(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-unbufw.bin", create_disposition=P.FILE_OVERWRITE_IF)
    r = c.smb2_write(o["FileId"], 0, b"W" * 2048, flags=0x00000002)
    rr = c.smb2_read(o["FileId"], 0, 2048)
    return {"write": r["status"], "Count": r.get("Count"),
            "read": rr["status"], "len": len(rr.get("Buffer", b""))}


def encrypted_share_roundtrip(ctx):
    c, n, t = _sess(ctx, share="enc")
    o = c.smb2_create("probe-enc302.bin", create_disposition=P.FILE_OVERWRITE_IF)
    w = c.smb2_write(o["FileId"], 0, b"E" * 2048)
    r = c.smb2_read(o["FileId"], 0, 2048)
    return {"encrypt_data": bool(t.get("ShareFlags", 0) & 0x00008000),
            "write": w["status"], "read": r["status"],
            "encrypted": bool(r.get("Encrypted")), "cipher": c.cipher_id}


def encrypted_share_refused_without_capability(ctx):
    c = ctx.client()
    c.smb2_negotiate(D302, capabilities=0)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect("enc")
    return {"status": t["status"]}


def signing_cmac_at_0302(ctx):
    """AES-128-CMAC is still the algorithm at 0x0302, and the MAC is checked.

    The client has to sign for the server to sign back: a reply carries no
    signature when the request did not, and `signature_valid` would then record
    None -- which says nothing about the algorithm.
    """
    c = ctx.client()
    c.verify_signatures = True
    n = c.smb2_negotiate(D302, capabilities=c.client_capabilities, security_mode=0x02)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.signing_required = True
    t = c.smb2_tree_connect(ctx.share)
    e = c.smb2_echo()
    return {"dialect": n.get("DialectRevision"),
            "tree": t["status"], "echo": e["status"],
            "echo_signed": bool(e.get("Flags", 0) & 0x08),
            "signature_valid": e.get("SignatureValid"),
            "signing_algorithm": c.signing_algorithm,
            "signature_len": len(e.get("Signature", b""))}


def validate_negotiate_info(ctx):
    c, _, _ = _sess(ctx)
    req = (struct.pack("<I", c.client_capabilities) + c.client_guid
           + struct.pack("<HH", 1, 4) + struct.pack("<HHHH", 0x0202, 0x0210, 0x0300, 0x0302))
    r = c.smb2_ioctl(b"\xff" * 16, P.FSCTL_VALIDATE_NEGOTIATE_INFO, req, max_out=24)
    b = r.get("Buffer", b"")
    return {"status": r["status"], "len": len(b),
            "Dialect": struct.unpack_from("<H", b, 22)[0] if len(b) >= 24 else None}


def query_network_interface_info(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_ioctl(b"\xff" * 16, P.FSCTL_QUERY_NETWORK_INTERFACE_INFO, b"", max_out=65536)
    b = r.get("Buffer", b"")
    return {"status": r["status"], "nonempty": len(b) > 0}


def app_instance_id_context(ctx):
    """SMB2_CREATE_APP_INSTANCE_ID, added at 0x0302 ([MS-SMB2] 2.2.13.2.13)."""
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-appid.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_BATCH,
                      contexts=[(b"\x6a\xa6\xbc\x45\xa7\x24\xda\x11\xb1\x3e\x00\x21\x5a\xf3\xee\x7c",
                                 struct.pack("<HH", 20, 0) + b"\x91" * 16),
                                (b"DH2Q", P.durable_v2_context(create_guid=b"\x92" * 16))])
    return {"status": o["status"],
            "durable_granted": any(e.get("NameBytes") == b"DH2Q"
                                   for e in (o.get("CreateContexts") or []))}


def tree_connect_flags(ctx):
    c, _, t = _sess(ctx)
    return {"status": t["status"], "ShareType": t.get("ShareType"),
            "ShareFlags": t.get("ShareFlags"), "MaximalAccess": t.get("MaximalAccess")}


CASES = [
    ("neg-0302", neg_0302),
    ("neg-0302-and-0311-offered", neg_0302_and_0311_offered),
    ("neg-only-0302", neg_only_0302),
    ("read-unbuffered", read_unbuffered),
    ("write-unbuffered", write_unbuffered),
    ("encrypted-share-roundtrip", encrypted_share_roundtrip),
    ("encrypted-share-refused-without-capability", encrypted_share_refused_without_capability),
    ("signing-cmac-at-0302", signing_cmac_at_0302),
    ("validate-negotiate-info", validate_negotiate_info),
    ("query-network-interface-info", query_network_interface_info),
    ("app-instance-id-context", app_instance_id_context),
    ("tree-connect-flags", tree_connect_flags),
]

run_all = runner(CASES, record_dialect=True)
