"""Wire-level probe cases for the SMB 2.0.2 and 2.1 dialects.

Each case drives the server through the SMB2 protocol on loopback and returns a
dictionary of decoded, non-volatile observations. Every expected value is what
Samba 4.17.12-Debian answered, recorded against a real smbd started with the
same smb.conf and the same seeded share.
"""
import struct

import smbprobe as P
from probebase import Ctx, runner

D202 = [0x0202]
D210 = [0x0202, 0x0210]


def _sess(ctx, dialects=D210, share=None, **kw):
    c = ctx.client()
    n = c.smb2_negotiate(dialects, **kw)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(share or ctx.share)
    return c, n, t


def _cap(v, bit):
    return bool(v & bit)


def _ctx(resp, name):
    """One create context out of a CREATE response, or None."""
    for e in resp.get("CreateContexts") or []:
        if e.get("NameBytes") == name:
            return e
    return None


def _chain(resp):
    """Every response of a compound reply, in order."""
    return resp.get("Chain") or [resp]


# --------------------------------------------------------------------------
# NEGOTIATE


def _neg_obs(r):
    caps = r.get("Capabilities", 0)
    return {
        "status": r["status"],
        "DialectRevision": r.get("DialectRevision"),
        "SecurityMode": r.get("SecurityMode"),
        "MaxTransactSize": r.get("MaxTransactSize"),
        "MaxReadSize": r.get("MaxReadSize"),
        "MaxWriteSize": r.get("MaxWriteSize"),
        "ServerGuidLen": len(r.get("ServerGuid", b"")),
        "cap_dfs": _cap(caps, 0x01),
        "cap_leasing": _cap(caps, 0x02),
        "cap_large_mtu": _cap(caps, 0x04),
        "cap_multi_channel": _cap(caps, 0x08),
        "cap_persistent_handles": _cap(caps, 0x10),
        "cap_directory_leasing": _cap(caps, 0x20),
        "cap_encryption": _cap(caps, 0x40),
        "blob_has_ntlmssp": b"\x2b\x06\x01\x04\x01\x82\x37\x02\x02\x0a" in r.get("SecurityBuffer", b""),
    }


def neg_0202(ctx):
    c = ctx.client()
    return _neg_obs(c.smb2_negotiate([0x0202]))


def neg_0210(ctx):
    c = ctx.client()
    return _neg_obs(c.smb2_negotiate([0x0202, 0x0210]))


def neg_only_0210(ctx):
    c = ctx.client()
    return _neg_obs(c.smb2_negotiate([0x0210]))


def neg_future_dialects(ctx):
    c = ctx.client()
    return _neg_obs(c.smb2_negotiate([0x0202, 0x0210, 0x0300, 0x0302, 0x0311]))


def neg_unknown_dialect(ctx):
    c = ctx.client()
    r = c.smb2_negotiate([0x0999])
    return {"status": r["status"]}


def neg_empty_dialects(ctx):
    c = ctx.client()
    r = c.smb2_negotiate([])
    return {"status": r["status"]}


def neg_twice(ctx):
    c = ctx.client()
    c.smb2_negotiate([0x0210])
    try:
        r = c.smb2_negotiate([0x0210])
    except P.SmbProbeError:
        return {"status": "connection-closed"}
    return {"status": r["status"]}


def smb1_multiprotocol_wildcard(ctx):
    """An SMB1 NEGOTIATE offering `SMB 2.???` must revert to SMB2."""
    c = ctx.client()
    r = c.smb1_negotiate(["PC NETWORK PROGRAM 1.0", "LANMAN1.0", "NT LM 0.12",
                          "SMB 2.002", "SMB 2.???"])
    return {"upgraded": bool(r.get("SMB2Upgrade")),
            "DialectRevision": r.get("DialectRevision"),
            "status": r["status"]}


def smb1_multiprotocol_2002(ctx):
    c = ctx.client()
    r = c.smb1_negotiate(["PC NETWORK PROGRAM 1.0", "NT LM 0.12", "SMB 2.002"])
    return {"upgraded": bool(r.get("SMB2Upgrade")),
            "DialectRevision": r.get("DialectRevision"),
            "status": r["status"]}


def smb1_nt1_only_refused(ctx):
    """With the configured minimum at SMB 2.0.2 an NT1-only client gets nothing."""
    c = ctx.client()
    r = c.smb1_negotiate(["PC NETWORK PROGRAM 1.0", "LANMAN1.0", "NT LM 0.12"])
    return {"upgraded": bool(r.get("SMB2Upgrade")),
            "DialectIndex": r.get("DialectIndex"),
            "WordCount": r.get("WordCount")}


def neg_command_before_negotiate(ctx):
    c = ctx.client()
    try:
        r = c.smb2_command(P.SMB2_TREE_CONNECT, struct.pack("<HHHH", 9, 0, 72, 0),
                           session_id=0, tree_id=0)
    except P.SmbProbeError:
        return {"status": "connection-closed"}
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# SESSION SETUP / TREE CONNECT


def ss_success(ctx):
    c = ctx.client()
    c.smb2_negotiate(D210)
    r = c.smb2_session_setup(ctx.user, ctx.password)
    return {"status": r["status"], "SessionFlags": r.get("SessionFlags"),
            "session_nonzero": bool(c.session_id)}


def ss_bad_password(ctx):
    c = ctx.client()
    c.smb2_negotiate(D210)
    r = c.smb2_session_setup(ctx.user, "wrong-password")
    return {"status": r["status"]}


def ss_unknown_user(ctx):
    c = ctx.client()
    c.smb2_negotiate(D210)
    r = c.smb2_session_setup("nosuchuser", ctx.password)
    return {"status": r["status"]}


def ss_signed_requests_verify(ctx):
    c = ctx.client()
    c.verify_signatures = True
    c.smb2_negotiate(D210)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(ctx.share)
    e = c.smb2_echo()
    return {"tree_status": t["status"], "echo_status": e["status"],
            "tree_signed": bool(t.get("Flags", 0) & 0x08),
            "echo_signed": bool(e.get("Flags", 0) & 0x08)}


def ss_logoff_then_use(ctx):
    c, _, _ = _sess(ctx)
    c.smb2_logoff()
    r = c.smb2_tree_connect(ctx.share)
    return {"status": r["status"]}


def tc_disk(ctx):
    c, _, t = _sess(ctx)
    return {"status": t["status"], "ShareType": t.get("ShareType"),
            "ShareFlags": t.get("ShareFlags"), "Capabilities": t.get("Capabilities"),
            "MaximalAccess": t.get("MaximalAccess")}


def tc_readonly(ctx):
    c, _, t = _sess(ctx, share="ro")
    return {"status": t["status"], "ShareType": t.get("ShareType"),
            "MaximalAccess": t.get("MaximalAccess")}


def tc_ipc(ctx):
    c, _, t = _sess(ctx, share="IPC$")
    return {"status": t["status"], "ShareType": t.get("ShareType")}


def tc_missing(ctx):
    c = ctx.client()
    c.smb2_negotiate(D210)
    c.smb2_session_setup(ctx.user, ctx.password)
    r = c.smb2_tree_connect("nosuchshare")
    return {"status": r["status"]}


def tc_disconnect_then_use(ctx):
    c, _, _ = _sess(ctx)
    c.smb2_tree_disconnect()
    r = c.smb2_create("after.txt")
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# CREATE


def _create_obs(r):
    return {"status": r["status"], "CreateAction": r.get("CreateAction"),
            "FileAttributes": r.get("FileAttributes"),
            "EndOfFile": r.get("EndOfFile"), "OplockLevel": r.get("OplockLevel")}


def create_new(ctx):
    c, _, _ = _sess(ctx)
    return _create_obs(c.smb2_create("probe-new.txt", create_disposition=P.FILE_CREATE))


def create_collision(ctx):
    c, _, _ = _sess(ctx)
    c.smb2_create("probe-coll.txt", create_disposition=P.FILE_CREATE)
    r = c.smb2_create("probe-coll.txt", create_disposition=P.FILE_CREATE)
    return {"status": r["status"]}


def create_open_missing(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_create("nosuch.txt", create_disposition=P.FILE_OPEN)
    return {"status": r["status"]}


def create_open_existing(ctx):
    c, _, _ = _sess(ctx)
    return _create_obs(c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                                     desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES))


def create_directory(ctx):
    c, _, _ = _sess(ctx)
    return _create_obs(c.smb2_create("data", create_disposition=P.FILE_OPEN,
                                     create_options=P.FILE_DIRECTORY_FILE,
                                     desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES))


def create_root(ctx):
    c, _, _ = _sess(ctx)
    return _create_obs(c.smb2_create("", create_disposition=P.FILE_OPEN,
                                     create_options=P.FILE_DIRECTORY_FILE,
                                     desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES))


def create_dir_as_file(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_create("data", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_NON_DIRECTORY_FILE)
    return {"status": r["status"]}


def create_file_as_dir(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE)
    return {"status": r["status"]}


def create_invalid_name(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_create("bad<name>.txt", create_disposition=P.FILE_CREATE)
    return {"status": r["status"]}


def create_leading_backslash(ctx):
    """[MS-SMB2] 2.2.13: the name is share-relative and must not start with \\."""
    c, _, _ = _sess(ctx)
    r = c.smb2_create("\\README.txt", create_disposition=P.FILE_OPEN)
    return {"status": r["status"]}


def create_missing_path(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_create("nodir\\file.txt", create_disposition=P.FILE_CREATE)
    return {"status": r["status"]}


def create_overwrite_if(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-ow.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb2_write(o["FileId"], 0, b"x" * 400)
    c.smb2_close(o["FileId"])
    return _create_obs(c.smb2_create("probe-ow.bin", create_disposition=P.FILE_OVERWRITE_IF))


def create_attributes(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_create("probe-attr.txt", create_disposition=P.FILE_CREATE,
                      file_attributes=P.FILE_ATTRIBUTE_SYSTEM | P.FILE_ATTRIBUTE_HIDDEN)
    return {"FileAttributes": r.get("FileAttributes"), "status": r["status"]}


def create_delete_on_close(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-doc.txt", create_disposition=P.FILE_OVERWRITE_IF,
                      create_options=P.FILE_DELETE_ON_CLOSE)
    c.smb2_close(o["FileId"])
    after = c.smb2_create("probe-doc.txt", create_disposition=P.FILE_OPEN)
    return {"create": o["status"], "after_close": after["status"]}


def create_sharing_violation(ctx):
    c, _, _ = _sess(ctx)
    c.smb2_create("probe-share.bin", create_disposition=P.FILE_OVERWRITE_IF,
                  share_access=0)
    c2, _, _ = _sess(ctx)
    r = c2.smb2_create("probe-share.bin", create_disposition=P.FILE_OPEN,
                       share_access=0)
    return {"status": r["status"]}


def create_context_maximal_access(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES,
                      contexts=[(b"MxAc", b"")])
    mxac = _ctx(r, b"MxAc")
    return {"status": r["status"], "has_mxac": mxac is not None,
            "MaximalAccess": mxac.get("MaximalAccess") if mxac else None,
            "QueryStatus": mxac.get("QueryStatus") if mxac else None}


def create_context_query_on_disk_id(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES,
                      contexts=[(b"QFid", b"")])
    q = _ctx(r, b"QFid")
    return {"status": r["status"], "has_qfid": q is not None,
            "qfid_len": q.get("DataLength") if q else None}


def create_readonly_share(ctx):
    c, _, _ = _sess(ctx, share="ro")
    r = c.smb2_create("nope.txt", create_disposition=P.FILE_CREATE)
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# READ / WRITE / CLOSE / FLUSH


def rw_roundtrip(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-rw.bin", create_disposition=P.FILE_OVERWRITE_IF)
    payload = bytes(range(256)) * 32
    w = c.smb2_write(o["FileId"], 0, payload)
    r = c.smb2_read(o["FileId"], 0, len(payload))
    return {"write_status": w["status"], "Count": w.get("Count"),
            "read_status": r["status"], "DataLength": len(r.get("Buffer", b"")),
            "identical": r.get("Buffer", b"") == payload}


def rw_large(ctx):
    c, n, _ = _sess(ctx)
    o = c.smb2_create("probe-large.bin", create_disposition=P.FILE_OVERWRITE_IF)
    payload = b"L" * 262144
    w = c.smb2_write(o["FileId"], 0, payload)
    r = c.smb2_read(o["FileId"], 0, len(payload))
    return {"write_status": w["status"], "Count": w.get("Count"),
            "read_status": r["status"], "len": len(r.get("Buffer", b""))}


def rw_read_past_eof(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-eof.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb2_write(o["FileId"], 0, b"12345")
    r = c.smb2_read(o["FileId"], 100, 50)
    return {"status": r["status"], "len": len(r.get("Buffer", b""))}


def rw_read_zero(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-z.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb2_write(o["FileId"], 0, b"abcdef")
    r = c.smb2_read(o["FileId"], 0, 0)
    return {"status": r["status"]}


def rw_write_readonly_handle(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA)
    w = c.smb2_write(o["FileId"], 0, b"nope")
    return {"status": w["status"]}


def rw_read_writeonly_handle(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-wo.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_WRITE_DATA)
    r = c.smb2_read(o["FileId"], 0, 16)
    return {"status": r["status"]}


def rw_bad_file_id(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_read(b"\x11" * 16, 0, 16)
    return {"status": r["status"]}


def close_and_flush(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-cf.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb2_write(o["FileId"], 0, b"payload")
    f = c.smb2_flush(o["FileId"])
    a = c.smb2_close(o["FileId"], flags=1)
    b = c.smb2_close(o["FileId"])
    return {"flush": f["status"], "close": a["status"],
            "EndOfFile": a.get("EndOfFile"), "FileAttributes": a.get("FileAttributes"),
            "close_again": b["status"]}


# --------------------------------------------------------------------------
# QUERY_INFO / SET_INFO


def _qi(c, fid, cls, itype=P.SMB2_0_INFO_FILE):
    return c.smb2_query_info(fid, itype, cls)


def qi_basic(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = _qi(c, o["FileId"], 4)
    b = r.get("Buffer", b"")
    return {"status": r["status"], "len": len(b),
            "FileAttributes": struct.unpack_from("<I", b, 32)[0] if len(b) >= 36 else None}


def qi_standard(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = _qi(c, o["FileId"], 5)
    b = r.get("Buffer", b"")
    return {"status": r["status"], "len": len(b),
            "EndOfFile": struct.unpack_from("<q", b, 8)[0] if len(b) >= 16 else None,
            "NumberOfLinks": struct.unpack_from("<I", b, 16)[0] if len(b) >= 20 else None,
            "DeletePending": b[20] if len(b) >= 22 else None,
            "Directory": b[21] if len(b) >= 22 else None}


def qi_all(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("data\\beta.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = _qi(c, o["FileId"], 18)
    b = r.get("Buffer", b"")
    nl = struct.unpack_from("<I", b, 92)[0] if len(b) >= 96 else None
    return {"status": r["status"],
            "FileAttributes": struct.unpack_from("<I", b, 32)[0] if len(b) >= 36 else None,
            "EndOfFile": struct.unpack_from("<q", b, 48)[0] if len(b) >= 56 else None,
            "NameLength": nl,
            "Name": b[96:96 + (nl or 0)].decode("utf-16le", "replace") if nl else None}


def qi_network_open(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = _qi(c, o["FileId"], 34)
    b = r.get("Buffer", b"")
    return {"status": r["status"], "len": len(b),
            "EndOfFile": struct.unpack_from("<q", b, 40)[0] if len(b) >= 48 else None,
            "FileAttributes": struct.unpack_from("<I", b, 48)[0] if len(b) >= 52 else None}


def qi_internal(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = _qi(c, o["FileId"], 6)
    return {"status": r["status"], "len": len(r.get("Buffer", b"")),
            "nonzero": any(r.get("Buffer", b""))}


def qi_alternate_name(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("longfilename-that-needs-mangling.txt",
                      create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = _qi(c, o["FileId"], 21)
    b = r.get("Buffer", b"")
    n = struct.unpack_from("<I", b, 0)[0] if len(b) >= 4 else 0
    name = b[4:4 + n].decode("utf-16le", "replace")
    return {"status": r["status"], "is_8dot3": bool(name) and len(name.split(".")[0]) <= 8}


def qi_attribute_tag(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = _qi(c, o["FileId"], 35)
    b = r.get("Buffer", b"")
    return {"status": r["status"], "len": len(b),
            "FileAttributes": struct.unpack_from("<I", b, 0)[0] if len(b) >= 4 else None,
            "ReparseTag": struct.unpack_from("<I", b, 4)[0] if len(b) >= 8 else None}


def qi_invalid_class(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = _qi(c, o["FileId"], 250)
    return {"status": r["status"]}


def qi_quota_unsupported(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = c.smb2_query_info(o["FileId"], P.SMB2_0_INFO_QUOTA, 0)
    return {"status": r["status"]}


def qfs_volume(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = c.smb2_query_info(o["FileId"], P.SMB2_0_INFO_FILESYSTEM, 1)
    b = r.get("Buffer", b"")
    n = struct.unpack_from("<I", b, 12)[0] if len(b) >= 16 else 0
    return {"status": r["status"], "VolumeLabelLength": n,
            "VolumeLabel": b[18:18 + n].decode("utf-16le", "replace")}


def qfs_attribute(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = c.smb2_query_info(o["FileId"], P.SMB2_0_INFO_FILESYSTEM, 5)
    b = r.get("Buffer", b"")
    n = struct.unpack_from("<I", b, 8)[0] if len(b) >= 12 else 0
    return {"status": r["status"],
            "MaximumComponentNameLength": struct.unpack_from("<I", b, 4)[0] if len(b) >= 8 else None,
            "FileSystemName": b[12:12 + n].decode("utf-16le", "replace")}


def qfs_device(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = c.smb2_query_info(o["FileId"], P.SMB2_0_INFO_FILESYSTEM, 4)
    b = r.get("Buffer", b"")
    return {"status": r["status"], "len": len(b),
            "DeviceType": struct.unpack_from("<I", b, 0)[0] if len(b) >= 4 else None}


def si_basic_attributes(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-si.txt", create_disposition=P.FILE_OVERWRITE_IF)
    buf = struct.pack("<qqqqII", 0, 0, 0, 0,
                      P.FILE_ATTRIBUTE_HIDDEN | P.FILE_ATTRIBUTE_SYSTEM, 0)
    s = c.smb2_set_info(o["FileId"], P.SMB2_0_INFO_FILE, 4, buf)
    q = _qi(c, o["FileId"], 4)
    b = q.get("Buffer", b"")
    return {"set_status": s["status"],
            "FileAttributes": struct.unpack_from("<I", b, 32)[0] if len(b) >= 36 else None}


def si_end_of_file(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-eofset.bin", create_disposition=P.FILE_OVERWRITE_IF)
    s = c.smb2_set_info(o["FileId"], P.SMB2_0_INFO_FILE, 20, struct.pack("<q", 7777))
    q = _qi(c, o["FileId"], 5)
    b = q.get("Buffer", b"")
    return {"set_status": s["status"],
            "EndOfFile": struct.unpack_from("<q", b, 8)[0] if len(b) >= 16 else None}


def si_disposition(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-disp.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA | P.DELETE)
    s = c.smb2_set_info(o["FileId"], P.SMB2_0_INFO_FILE, 13, b"\x01")
    q = _qi(c, o["FileId"], 5)
    b = q.get("Buffer", b"")
    c.smb2_close(o["FileId"])
    after = c.smb2_create("probe-disp.bin", create_disposition=P.FILE_OPEN)
    return {"set_status": s["status"],
            "DeletePending": b[20] if len(b) >= 22 else None,
            "NumberOfLinks": struct.unpack_from("<I", b, 16)[0] if len(b) >= 20 else None,
            "after_close": after["status"]}


def si_rename(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-ren.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_READ_DATA | P.DELETE)
    name = P._u("probe-ren2.bin")
    buf = struct.pack("<BBBBBBBBQI", 0, 0, 0, 0, 0, 0, 0, 0, 0, len(name)) + name
    s = c.smb2_set_info(o["FileId"], P.SMB2_0_INFO_FILE, 10, buf)
    c.smb2_close(o["FileId"])
    a = c.smb2_create("probe-ren.bin", create_disposition=P.FILE_OPEN)
    b = c.smb2_create("probe-ren2.bin", create_disposition=P.FILE_OPEN)
    return {"set_status": s["status"], "old": a["status"], "new": b["status"]}


def si_invalid_class(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-si2.bin", create_disposition=P.FILE_OVERWRITE_IF)
    r = c.smb2_set_info(o["FileId"], P.SMB2_0_INFO_FILE, 250, b"\x00" * 8)
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# QUERY_DIRECTORY


def _seed_dir(c, name, files=("a.txt", "b.txt", "c.bin", "dd.txt")):
    d = c.smb2_create(name, create_disposition=P.FILE_OPEN_IF,
                      create_options=P.FILE_DIRECTORY_FILE)
    if "FileId" in d:
        c.smb2_close(d["FileId"])
    for f in files:
        o = c.smb2_create(name + "\\" + f, create_disposition=P.FILE_OVERWRITE_IF)
        if "FileId" in o:
            c.smb2_write(o["FileId"], 0, b"x" * len(f))
            c.smb2_close(o["FileId"])


def _names(entries):
    return sorted(e.get("FileName", "") for e in entries or [])


def qd_both(ctx):
    c, _, _ = _sess(ctx)
    _seed_dir(c, "qdboth")
    d = c.smb2_create("qdboth", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb2_query_directory(d["FileId"], 3, "*")
    return {"status": r["status"], "names": _names(r.get("Entries"))}


def qd_id_both(ctx):
    c, _, _ = _sess(ctx)
    _seed_dir(c, "qdid")
    d = c.smb2_create("qdid", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb2_query_directory(d["FileId"], 37, "*")
    return {"status": r["status"], "names": _names(r.get("Entries"))}


def qd_names(ctx):
    c, _, _ = _sess(ctx)
    _seed_dir(c, "qdnames")
    d = c.smb2_create("qdnames", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb2_query_directory(d["FileId"], 12, "*")
    return {"status": r["status"], "names": _names(r.get("Entries"))}


def qd_pattern(ctx):
    c, _, _ = _sess(ctx)
    _seed_dir(c, "qdpat")
    d = c.smb2_create("qdpat", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb2_query_directory(d["FileId"], 3, "?.txt")
    return {"status": r["status"], "names": _names(r.get("Entries"))}


def qd_no_match(ctx):
    c, _, _ = _sess(ctx)
    _seed_dir(c, "qdnone")
    d = c.smb2_create("qdnone", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb2_query_directory(d["FileId"], 3, "zzz*")
    return {"status": r["status"]}


def qd_continuation(ctx):
    c, _, _ = _sess(ctx)
    _seed_dir(c, "qdcont", tuple("f%02d.dat" % i for i in range(10)))
    d = c.smb2_create("qdcont", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    got, statuses = [], []
    for _ in range(30):
        r = c.smb2_query_directory(d["FileId"], 3, "*", output_buffer_length=1024)
        statuses.append(r["status"])
        if r["status"] != 0:
            break
        got.extend(_names(r.get("Entries")))
    return {"names": sorted(set(got)), "final_status": statuses[-1],
            "calls_more_than_one": len(statuses) > 1}


def qd_restart(ctx):
    c, _, _ = _sess(ctx)
    _seed_dir(c, "qdrestart")
    d = c.smb2_create("qdrestart", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    first = c.smb2_query_directory(d["FileId"], 3, "*")
    again = c.smb2_query_directory(d["FileId"], 3, "*", flags=0x01)
    return {"first": _names(first.get("Entries")), "restart": _names(again.get("Entries"))}


def qd_single_entry(ctx):
    c, _, _ = _sess(ctx)
    _seed_dir(c, "qdsingle")
    d = c.smb2_create("qdsingle", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb2_query_directory(d["FileId"], 3, "*", flags=0x02)
    return {"status": r["status"], "count": len(r.get("Entries") or [])}


def qd_on_file(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb2_query_directory(o["FileId"], 3, "*")
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# LOCK / IOCTL / ECHO


def lock_conflict(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-lock.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                      share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    c.smb2_write(o["FileId"], 0, b"z" * 1024)
    a = c.smb2_lock(o["FileId"], [(100, 50, 0x02 | 0x10)])
    c2, _, _ = _sess(ctx)
    o2 = c2.smb2_create("probe-lock.bin", create_disposition=P.FILE_OPEN,
                        desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                        share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    b = c2.smb2_lock(o2["FileId"], [(100, 50, 0x02 | 0x10)])
    return {"first": a["status"], "second": b["status"]}


def lock_shared(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-lock2.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                      share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    c.smb2_write(o["FileId"], 0, b"z" * 1024)
    a = c.smb2_lock(o["FileId"], [(0, 100, 0x01 | 0x10)])
    c2, _, _ = _sess(ctx)
    o2 = c2.smb2_create("probe-lock2.bin", create_disposition=P.FILE_OPEN,
                        desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                        share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    b = c2.smb2_lock(o2["FileId"], [(0, 100, 0x01 | 0x10)])
    return {"first": a["status"], "second": b["status"]}


def lock_unlock(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-lock3.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb2_write(o["FileId"], 0, b"z" * 1024)
    a = c.smb2_lock(o["FileId"], [(10, 20, 0x02 | 0x10)])
    b = c.smb2_lock(o["FileId"], [(10, 20, 0x04)])
    d = c.smb2_lock(o["FileId"], [(10, 20, 0x04)])
    return {"lock": a["status"], "unlock": b["status"], "unlock_again": d["status"]}


def ioctl_dfs(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_ioctl(b"\xff" * 16, P.FSCTL_DFS_GET_REFERRALS, b"\x00" * 4)
    return {"status": r["status"]}


def ioctl_unknown(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb2_ioctl(o["FileId"], 0x00090000, b"")
    return {"status": r["status"]}


def ioctl_pipe_on_disk(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb2_ioctl(o["FileId"], P.FSCTL_PIPE_WAIT, b"\x00" * 16)
    return {"status": r["status"]}


def echo(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_echo()
    return {"status": r["status"], "StructureSize": r.get("StructureSize"),
            "BodyStructureSize": r.get("BodyStructureSize")}


def cancel_change_notify(ctx):
    c, _, _ = _sess(ctx)
    d = c.smb2_create("data", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    sent = c.smb2_change_notify(d["FileId"], expect_reply=False)
    mid = sent["MessageId"]
    c.smb2_cancel(mid)
    r = c.smb2_receive_reply(mid)
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# compound requests, credits, oplocks, leases, durable handles


def compound_unrelated(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_compound([
        (P.SMB2_CREATE, c.body_create("probe-comp.bin",
                                      create_disposition=P.FILE_OVERWRITE_IF)),
        (P.SMB2_ECHO, struct.pack("<HH", 4, 0)),
    ], related=False)
    rs = _chain(r)
    return {"count": len(rs), "statuses": [x["status"] for x in rs],
            "commands": [x["Command"] for x in rs]}


def compound_related(ctx):
    c, _, _ = _sess(ctx)
    last = c.SMB2_LAST_FILE_ID
    r = c.smb2_compound([
        (P.SMB2_CREATE, c.body_create("probe-comp2.bin",
                                      create_disposition=P.FILE_OVERWRITE_IF)),
        (P.SMB2_QUERY_INFO, c.body_query_info(last, P.SMB2_0_INFO_FILE, 5)),
        (P.SMB2_CLOSE, c.body_close(last)),
    ], related=True)
    rs = _chain(r)
    return {"count": len(rs), "statuses": [x["status"] for x in rs],
            "commands": [x["Command"] for x in rs]}


def compound_related_first_fails(ctx):
    c, _, _ = _sess(ctx)
    last = c.SMB2_LAST_FILE_ID
    r = c.smb2_compound([
        (P.SMB2_CREATE, c.body_create("nosuch-compound.bin",
                                      create_disposition=P.FILE_OPEN)),
        (P.SMB2_QUERY_INFO, c.body_query_info(last, P.SMB2_0_INFO_FILE, 5)),
        (P.SMB2_CLOSE, c.body_close(last)),
    ], related=True)
    rs = _chain(r)
    return {"count": len(rs), "statuses": [x["status"] for x in rs]}


def credits_granted(ctx):
    c, _, _ = _sess(ctx)
    r = c.smb2_echo()
    return {"credit_response_nonzero": r.get("CreditResponse", 0) > 0}


def oplock_batch_grant(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-opl.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_BATCH)
    return {"status": o["status"], "OplockLevel": o.get("OplockLevel")}


def oplock_break_notification(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-oplbrk.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_BATCH,
                      share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE | P.FILE_SHARE_DELETE)
    granted = o.get("OplockLevel")
    c2, _, _ = _sess(ctx)
    c2.smb2_create("probe-oplbrk.bin", create_disposition=P.FILE_OPEN,
                   share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE | P.FILE_SHARE_DELETE,
                   expect_reply=False)
    brk = c.wait_for_notification(timeout=10.0)
    return {"granted": granted,
            "break_command": brk.get("Command") if brk else None,
            "new_level": brk.get("OplockLevel") if brk else None}


def lease_grant(ctx):
    c, _, _ = _sess(ctx)
    key = b"\x21" * 16
    o = c.smb2_create("probe-lease.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_LEASE,
                      contexts=[(b"RqLs", P.lease_context_v1(key, 0x07))])
    rq = _ctx(o, b"RqLs")
    d = rq.get("Data", b"") if rq else b""
    return {"status": o["status"], "OplockLevel": o.get("OplockLevel"),
            "has_lease_context": rq is not None,
            "LeaseState": struct.unpack_from("<I", d, 16)[0] if len(d) >= 20 else None}


def lease_same_key_no_break(ctx):
    c, _, _ = _sess(ctx)
    key = b"\x33" * 16
    a = c.smb2_create("probe-lease2.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_LEASE,
                      share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE | P.FILE_SHARE_DELETE,
                      contexts=[(b"RqLs", P.lease_context_v1(key, 0x07))])
    b = c.smb2_create("probe-lease2.bin", create_disposition=P.FILE_OPEN,
                      oplock=P.SMB2_OPLOCK_LEVEL_LEASE,
                      share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE | P.FILE_SHARE_DELETE,
                      contexts=[(b"RqLs", P.lease_context_v1(key, 0x07))])
    rq = _ctx(b, b"RqLs")
    d = rq.get("Data", b"") if rq else b""
    return {"first": a["status"], "second": b["status"],
            "LeaseState": struct.unpack_from("<I", d, 16)[0] if len(d) >= 20 else None}


def durable_v1_grant(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-dur.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_BATCH,
                      contexts=[(b"DHnQ", b"\x00" * 16)])
    return {"status": o["status"], "granted": _ctx(o, b"DHnQ") is not None}


def durable_v2_grant(ctx):
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-dur2.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_BATCH,
                      contexts=[(b"DH2Q", P.durable_v2_context(create_guid=b"\x44" * 16))])
    d = _ctx(o, b"DH2Q")
    return {"status": o["status"], "granted": d is not None,
            "len": d.get("DataLength") if d else None}


def _outcome(fn):
    """Run fn and describe the answer: a status, or a dropped connection."""
    try:
        r = fn()
    except P.SmbProbeError as exc:
        text = str(exc)
        if "closed by peer" in text or "reset" in text.lower():
            return {"outcome": "disconnected"}
        return {"outcome": "error"}
    except (ConnectionResetError, BrokenPipeError):
        return {"outcome": "disconnected"}
    return {"outcome": "status", "status": r["status"],
            "BodyStructureSize": r.get("BodyStructureSize")}


def _everyone_sd():
    """A self-relative security descriptor whose DACL grants everyone full
    access ([MS-DTYP] 2.4.6); used as SecD create-context data."""
    sid = struct.pack("<BB", 1, 1) + (1).to_bytes(6, "big") + struct.pack("<I", 0)
    ace = struct.pack("<BBHI", 0, 0, 8 + len(sid), 0x001F01FF) + sid
    acl = struct.pack("<BBHHH", 2, 0, 8 + len(ace), 1, 0) + ace
    return struct.pack("<BBHIIII", 1, 0, 0x8004, 0, 0, 0, 20) + acl


# --------------------------------------------------------------------------
# Signing ([MS-SMB2] 3.3.5.2.4)


def ss_signed_session(ctx):
    """A session that asks for signing gets signed replies with a valid MAC."""
    c = ctx.client()
    c.verify_signatures = False
    n = c.smb2_negotiate(D210, security_mode=0x02)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.signing_required = True
    t = c.smb2_tree_connect(ctx.share)
    e = c.smb2_echo()
    return {"neg_status": n["status"],
            "tree_status": t["status"], "echo_status": e["status"],
            "tree_signed": bool(t.get("Flags", 0) & 0x08),
            "echo_signed": bool(e.get("Flags", 0) & 0x08),
            "tree_signature_valid": t.get("SignatureValid"),
            "echo_signature_valid": e.get("SignatureValid"),
            "signature_len": len(e.get("Signature", b""))}


def ss_bad_signature(ctx):
    """A signed request whose MAC does not verify is refused."""
    c = ctx.client()
    c.verify_signatures = False
    c.smb2_negotiate(D210, security_mode=0x02)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.signing_required = True
    c.smb2_tree_connect(ctx.share)
    c.smb2_echo()
    pdu = bytearray(c.last_request)
    pdu[48] ^= 0xFF                      # corrupt the first signature byte

    def send_corrupt():
        c.send_raw(bytes(pdu))
        return c._smb2_split(c.recv_raw(), False, b"")[0]

    return _outcome(send_corrupt)


def ss_unsigned_on_signed_session(ctx):
    """A session that negotiated signing may still send unsigned requests when
    the server's own policy is not `mandatory`."""
    c = ctx.client()
    c.verify_signatures = False
    c.smb2_negotiate(D210, security_mode=0x02)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.signing_required = False
    t = c.smb2_tree_connect(ctx.share)
    return {"tree_status": t["status"], "tree_signed": bool(t.get("Flags", 0) & 0x08)}


# --------------------------------------------------------------------------
# The 9-byte error response ([MS-SMB2] 2.2.2)


def error_response_shape(ctx):
    """Every failure carries the 9-byte SMB2 ERROR Response body."""
    c, _, _ = _sess(ctx)
    miss = c.smb2_create("no-such-file-at-all.bin", create_disposition=P.FILE_OPEN)
    bad_read = c.smb2_read(b"\xee" * 16, 0, 16)
    return {"create_status": miss["status"],
            "create_body_size": miss.get("BodyStructureSize"),
            "create_byte_count": miss.get("ByteCount"),
            "create_error_data": miss.get("ErrorData", b"").hex(),
            "create_context_count": miss.get("ErrorContextCount"),
            "read_status": bad_read["status"],
            "read_body_size": bad_read.get("BodyStructureSize"),
            "read_byte_count": bad_read.get("ByteCount")}


# --------------------------------------------------------------------------
# Credits and multi-credit I/O ([MS-SMB2] 3.2.4.1.5)


def rw_multi_credit(ctx):
    """A read and a write larger than one 64 KiB credit unit."""
    c, n, _ = _sess(ctx)
    size = 393216                        # 6 credit units
    payload = bytes(bytearray((i * 7) & 0xFF for i in range(size)))
    o = c.smb2_create("probe-multicredit.bin", create_disposition=P.FILE_OVERWRITE_IF)
    w = c.smb2_write(o["FileId"], 0, payload)
    r = c.smb2_read(o["FileId"], 0, size)
    c.smb2_close(o["FileId"])
    return {"MaxWriteSize": n.get("MaxWriteSize"), "MaxReadSize": n.get("MaxReadSize"),
            "write_status": w["status"], "written": w.get("Count"),
            "read_status": r["status"], "read_len": r.get("DataLength"),
            "read_matches": r.get("Buffer") == payload,
            "charge_echoed": w.get("CreditCharge")}


def rw_over_max_read(ctx):
    """A read longer than the advertised MaxReadSize."""
    c, n, _ = _sess(ctx)
    o = c.smb2_create("probe-overmax.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb2_write(o["FileId"], 0, b"z" * 4096)
    big = (n.get("MaxReadSize") or 65536) + 1
    return _outcome(lambda: c.smb2_read(o["FileId"], 0, big))


# --------------------------------------------------------------------------
# Create contexts ([MS-SMB2] 2.2.13.2)


def create_context_allocation_size(ctx):
    """AlSi sets the allocation size of a newly created file."""
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-alsi.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      contexts=[(b"AlSi", struct.pack("<q", 65536))])
    q = c.smb2_query_info(o["FileId"], 1, 0x05)   # FileStandardInformation
    buf = q.get("Buffer") or b""
    alloc = struct.unpack_from("<q", buf, 0)[0] if len(buf) >= 8 else None
    eof = struct.unpack_from("<q", buf, 8)[0] if len(buf) >= 16 else None
    c.smb2_close(o["FileId"])
    return {"status": o["status"], "response_context": _ctx(o, b"AlSi") is not None,
            "AllocationSize": alloc, "EndOfFile": eof}


def create_context_extended_attributes(ctx):
    """ExtA carries FILE_FULL_EA_INFORMATION on the create."""
    c, _, _ = _sess(ctx)
    name, value = b"PROBEEA", b"probe-value"
    ea = struct.pack("<IBBH", 0, 0, len(name), len(value)) + name + b"\x00" + value
    ea += b"\x00" * ((-len(ea)) % 4)
    o = c.smb2_create("probe-exta.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA
                      | P.FILE_READ_EA | P.FILE_WRITE_EA | P.SYNCHRONIZE,
                      contexts=[(b"ExtA", ea)])
    obs = {"status": o["status"]}
    if o["status"] == 0:
        q = c.smb2_query_info(o["FileId"], 1, 0x0F)   # FileFullEaInformation
        buf = q.get("Buffer") or b""
        obs["ea_status"] = q["status"]
        obs["ea_contains_name"] = name in buf
        obs["ea_contains_value"] = value in buf
        c.smb2_close(o["FileId"])
    return obs


def create_context_security_descriptor(ctx):
    """SecD applies a security descriptor at create time."""
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-secd.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA
                      | P.READ_CONTROL | P.DELETE,
                      contexts=[(b"SecD", _everyone_sd())])
    obs = {"status": o["status"], "response_context": _ctx(o, b"SecD") is not None}
    if o["status"] == 0:
        q = c.smb2_query_info(o["FileId"], 3, 0x00, additional_information=0x00000004)
        obs["sd_status"] = q["status"]
        obs["sd_len_nonzero"] = len(q.get("Buffer") or b"") > 0
        c.smb2_close(o["FileId"])
    return obs


def create_context_unknown_ignored(ctx):
    """An unrecognised create-context tag is ignored, not refused."""
    c, _, _ = _sess(ctx)
    o = c.smb2_create("probe-unkctx.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      contexts=[(b"ZzQq", b"\x01\x02\x03\x04")])
    obs = {"status": o["status"], "echoed_back": _ctx(o, b"ZzQq") is not None}
    if o["status"] == 0:
        c.smb2_close(o["FileId"])
    return obs


# --------------------------------------------------------------------------
# Leases ([MS-SMB2] 3.3.4.7)


def lease_break_on_conflict(ctx):
    """A conflicting open from another lease key breaks the lease."""
    share_all = P.FILE_SHARE_READ | P.FILE_SHARE_WRITE | P.FILE_SHARE_DELETE
    c, _, _ = _sess(ctx)
    key = b"\x55" * 16
    a = c.smb2_create("probe-leasebrk.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      oplock=P.SMB2_OPLOCK_LEVEL_LEASE, share_access=share_all,
                      contexts=[(b"RqLs", P.lease_context_v1(key, 0x07))])
    rq = _ctx(a, b"RqLs")
    d = rq.get("Data", b"") if rq else b""
    granted = struct.unpack_from("<I", d, 16)[0] if len(d) >= 20 else None
    c2, _, _ = _sess(ctx)
    c2.smb2_create("probe-leasebrk.bin", create_disposition=P.FILE_OPEN,
                   share_access=share_all, expect_reply=False)
    try:
        brk = c.wait_for_notification(timeout=15.0)
    except Exception:
        return {"granted": granted, "break": "none"}
    obs = {"granted": granted,
           "break_command": brk.get("Command"),
           "break_mid_is_max": brk.get("MessageId") == 0xFFFFFFFFFFFFFFFF,
           "break_type": brk.get("BreakType"),
           "break_key_matches": brk.get("LeaseKey") == key,
           "CurrentLeaseState": brk.get("CurrentLeaseState"),
           "NewLeaseState": brk.get("NewLeaseState")}
    ack = c.smb2_lease_break_ack(key, brk.get("NewLeaseState", 0))
    obs["ack_status"] = ack["status"]
    return obs


def lease_break_ack_wrong_key(ctx):
    """A lease-break acknowledgement for a key the session does not hold."""
    c, _, _ = _sess(ctx)
    c.smb2_create("probe-leaseack.bin", create_disposition=P.FILE_OVERWRITE_IF,
                  oplock=P.SMB2_OPLOCK_LEVEL_LEASE,
                  contexts=[(b"RqLs", P.lease_context_v1(b"\x66" * 16, 0x07))])
    r = c.smb2_lease_break_ack(b"\x99" * 16, 0x01)
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# CHANGE_NOTIFY that actually fires ([MS-SMB2] 3.3.5.19)


def notify_fires_on_change(ctx):
    """A watched directory reports a name that appears in it."""
    share_all = P.FILE_SHARE_READ | P.FILE_SHARE_WRITE | P.FILE_SHARE_DELETE
    c, _, _ = _sess(ctx)
    d = c.smb2_create("data", create_disposition=P.FILE_OPEN, share_access=share_all,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=0x00000001 | P.SYNCHRONIZE)
    pending = c.smb2_change_notify(d["FileId"], completion_filter=0x00000001,
                                   expect_reply=False)
    mid = pending["MessageId"]
    c2, _, _ = _sess(ctx)
    n = c2.smb2_create("data\\probe-notify.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c2.smb2_close(n["FileId"])
    r = c.smb2_receive_reply(mid)
    buf = r.get("Buffer") or b""
    action = struct.unpack_from("<I", buf, 4)[0] if len(buf) >= 8 else None
    nlen = struct.unpack_from("<I", buf, 8)[0] if len(buf) >= 12 else 0
    return {"status": r["status"], "Action": action,
            "FileName": buf[12:12 + nlen].decode("utf-16le", "replace"),
            "interim_pending": (r.get("InterimResponse") or {}).get("status")}


CASES = [
    ("neg-0202", neg_0202),
    ("neg-0210", neg_0210),
    ("neg-only-0210", neg_only_0210),
    ("neg-future-dialects", neg_future_dialects),
    ("neg-unknown-dialect", neg_unknown_dialect),
    ("neg-empty-dialects", neg_empty_dialects),
    ("neg-twice", neg_twice),
    ("smb1-multiprotocol-wildcard", smb1_multiprotocol_wildcard),
    ("smb1-multiprotocol-2002", smb1_multiprotocol_2002),
    ("smb1-nt1-only-refused", smb1_nt1_only_refused),
    ("neg-command-before-negotiate", neg_command_before_negotiate),
    ("ss-success", ss_success),
    ("ss-bad-password", ss_bad_password),
    ("ss-unknown-user", ss_unknown_user),
    ("ss-signed-requests-verify", ss_signed_requests_verify),
    ("ss-signed-session", ss_signed_session),
    ("ss-bad-signature", ss_bad_signature),
    ("ss-unsigned-on-signed-session", ss_unsigned_on_signed_session),
    ("ss-logoff-then-use", ss_logoff_then_use),
    ("tc-disk", tc_disk),
    ("tc-readonly", tc_readonly),
    ("tc-ipc", tc_ipc),
    ("tc-missing", tc_missing),
    ("tc-disconnect-then-use", tc_disconnect_then_use),
    ("create-new", create_new),
    ("create-collision", create_collision),
    ("create-open-missing", create_open_missing),
    ("create-open-existing", create_open_existing),
    ("create-directory", create_directory),
    ("create-root", create_root),
    ("create-dir-as-file", create_dir_as_file),
    ("create-file-as-dir", create_file_as_dir),
    ("create-invalid-name", create_invalid_name),
    ("create-leading-backslash", create_leading_backslash),
    ("create-missing-path", create_missing_path),
    ("create-overwrite-if", create_overwrite_if),
    ("create-attributes", create_attributes),
    ("create-delete-on-close", create_delete_on_close),
    ("create-sharing-violation", create_sharing_violation),
    ("create-context-maximal-access", create_context_maximal_access),
    ("create-context-query-on-disk-id", create_context_query_on_disk_id),
    ("create-context-allocation-size", create_context_allocation_size),
    ("create-context-extended-attributes", create_context_extended_attributes),
    ("create-context-security-descriptor", create_context_security_descriptor),
    ("create-context-unknown-ignored", create_context_unknown_ignored),
    ("create-readonly-share", create_readonly_share),
    ("rw-roundtrip", rw_roundtrip),
    ("rw-large", rw_large),
    ("rw-multi-credit", rw_multi_credit),
    ("rw-over-max-read", rw_over_max_read),
    ("rw-read-past-eof", rw_read_past_eof),
    ("rw-read-zero", rw_read_zero),
    ("rw-write-readonly-handle", rw_write_readonly_handle),
    ("rw-read-writeonly-handle", rw_read_writeonly_handle),
    ("rw-bad-file-id", rw_bad_file_id),
    ("close-and-flush", close_and_flush),
    ("qi-basic", qi_basic),
    ("qi-standard", qi_standard),
    ("qi-all", qi_all),
    ("qi-network-open", qi_network_open),
    ("qi-internal", qi_internal),
    ("qi-alternate-name", qi_alternate_name),
    ("qi-attribute-tag", qi_attribute_tag),
    ("qi-invalid-class", qi_invalid_class),
    ("qi-quota-unsupported", qi_quota_unsupported),
    ("qfs-volume", qfs_volume),
    ("qfs-attribute", qfs_attribute),
    ("qfs-device", qfs_device),
    ("si-basic-attributes", si_basic_attributes),
    ("si-end-of-file", si_end_of_file),
    ("si-disposition", si_disposition),
    ("si-rename", si_rename),
    ("si-invalid-class", si_invalid_class),
    ("qd-both", qd_both),
    ("qd-id-both", qd_id_both),
    ("qd-names", qd_names),
    ("qd-pattern", qd_pattern),
    ("qd-no-match", qd_no_match),
    ("qd-continuation", qd_continuation),
    ("qd-restart", qd_restart),
    ("qd-single-entry", qd_single_entry),
    ("qd-on-file", qd_on_file),
    ("lock-conflict", lock_conflict),
    ("lock-shared", lock_shared),
    ("lock-unlock", lock_unlock),
    ("ioctl-dfs", ioctl_dfs),
    ("ioctl-unknown", ioctl_unknown),
    ("ioctl-pipe-on-disk", ioctl_pipe_on_disk),
    ("echo", echo),
    ("error-response-shape", error_response_shape),
    ("cancel-change-notify", cancel_change_notify),
    ("notify-fires-on-change", notify_fires_on_change),
    ("compound-unrelated", compound_unrelated),
    ("compound-related", compound_related),
    ("compound-related-first-fails", compound_related_first_fails),
    ("credits-granted", credits_granted),
    ("oplock-batch-grant", oplock_batch_grant),
    ("oplock-break-notification", oplock_break_notification),
    ("lease-grant", lease_grant),
    ("lease-same-key-no-break", lease_same_key_no_break),
    ("lease-break-on-conflict", lease_break_on_conflict),
    ("lease-break-ack-wrong-key", lease_break_ack_wrong_key),
    ("durable-v1-grant", durable_v1_grant),
    ("durable-v2-grant", durable_v2_grant),
]

run_all = runner(CASES)
