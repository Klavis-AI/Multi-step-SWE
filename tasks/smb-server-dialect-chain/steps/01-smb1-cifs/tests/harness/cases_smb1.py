"""Wire-level probe cases for the SMB1/CIFS dialect.

Each case drives the server through the SMB1 protocol on loopback and returns a
small dictionary of decoded, non-volatile observations. The expected value of
every dictionary is whatever Samba 4.17.12-Debian answered, recorded against
a real smbd started with the same smb.conf and the same seeded share.
"""
import struct

import sdutil
import smbprobe as P
from probebase import Ctx, runner

S = P


def _cap(caps, bit):
    return bool(caps & bit)


# --------------------------------------------------------------------------
# NEGOTIATE


def neg_nt1(ctx):
    c = ctx.client()
    r = c.smb1_negotiate()
    caps = r.get("Capabilities", 0)
    return {
        "DialectName": r.get("DialectName"),
        "WordCount": r.get("WordCount"),
        "ChallengeLength": r.get("ChallengeLength"),
        "ServerGuidLen": len(r.get("ServerGuid", b"")),
        "blob_has_ntlmssp": b"\x2b\x06\x01\x04\x01\x82\x37\x02\x02\x0a" in r.get("SecurityBlob", b""),
        "cap_unicode": _cap(caps, P.CAP_UNICODE),
        "cap_large_files": _cap(caps, P.CAP_LARGE_FILES),
        "cap_nt_smbs": _cap(caps, P.CAP_NT_SMBS),
        "cap_status32": _cap(caps, P.CAP_STATUS32),
        "cap_level_ii_oplocks": _cap(caps, P.CAP_LEVEL_II_OPLOCKS),
        "cap_nt_find": _cap(caps, P.CAP_NT_FIND),
        "cap_extended_security": _cap(caps, P.CAP_EXTENDED_SECURITY),
        "security_user_mode": bool(r.get("SecurityMode", 0) & 0x01),
    }


def neg_no_nt1(ctx):
    c = ctx.client()
    r = c.smb1_negotiate(["PC NETWORK PROGRAM 1.0", "LANMAN1.0", "LANMAN2.1"])
    return {"DialectIndex": r.get("DialectIndex"), "WordCount": r.get("WordCount")}


def neg_empty(ctx):
    c = ctx.client()
    r = c.smb1_negotiate([])
    return {"DialectIndex": r.get("DialectIndex"), "WordCount": r.get("WordCount"),
            "status": r["status"]}


def neg_unknown_only(ctx):
    c = ctx.client()
    r = c.smb1_negotiate(["NOT A DIALECT", "SMB 9.99"])
    return {"DialectIndex": r.get("DialectIndex"), "WordCount": r.get("WordCount")}


def neg_nt1_first(ctx):
    c = ctx.client()
    r = c.smb1_negotiate(["NT LM 0.12", "LANMAN2.1"])
    return {"DialectIndex": r.get("DialectIndex"), "DialectName": r.get("DialectName")}


def neg_nt1_last(ctx):
    c = ctx.client()
    r = c.smb1_negotiate(["PC NETWORK PROGRAM 1.0", "LANMAN1.0",
                          "Windows for Workgroups 3.1a", "LM1.2X002",
                          "LANMAN2.1", "NT LM 0.12"])
    return {"DialectIndex": r.get("DialectIndex"), "DialectName": r.get("DialectName")}


def neg_before_anything(ctx):
    """A command sent before NEGOTIATE."""
    c = ctx.client()
    try:
        r = c.smb1_command(P.SMB_COM_TREE_CONNECT_ANDX, b"", b"")
    except P.SmbProbeError:
        return {"status": "connection-closed"}
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# SESSION SETUP


def ss_success(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    r = c.smb1_session_setup(ctx.user, ctx.password)
    return {"status": r["status"], "guest": bool(r.get("Action", 0) & 1),
            "uid_nonzero": bool(c.smb1_uid)}


def ss_bad_password(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    r = c.smb1_session_setup(ctx.user, "wrong-password")
    return {"status": r["status"]}


def ss_unknown_user(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    r = c.smb1_session_setup("nosuchuser", ctx.password)
    return {"status": r["status"]}


def ss_two_sessions(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    r1 = c.smb1_session_setup(ctx.user, ctx.password)
    uid1 = c.smb1_uid
    r2 = c.smb1_session_setup(ctx.user, ctx.password)
    uid2 = c.smb1_uid
    return {"status1": r1["status"], "status2": r2["status"],
            "distinct": uid1 != uid2}


def ss_logoff_then_use(ctx):
    c, tc = ctx.smb1()
    c.smb1_logoff()
    r = c.smb1_tree_connect(ctx.share)
    return {"status": r["status"]}


def ss_tree_connect_without_session(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    r = c.smb1_tree_connect(ctx.share)
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# TREE CONNECT


def tc_disk(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    c.smb1_session_setup(ctx.user, ctx.password)
    r = c.smb1_tree_connect(ctx.share)
    return {"status": r["status"], "Service": r.get("Service"),
            "NativeFileSystem": r.get("NativeFileSystem")}


def tc_ipc(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    c.smb1_session_setup(ctx.user, ctx.password)
    r = c.smb1_tree_connect("IPC$")
    return {"status": r["status"], "Service": r.get("Service")}


def tc_missing(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    c.smb1_session_setup(ctx.user, ctx.password)
    r = c.smb1_tree_connect("nosuchshare")
    return {"status": r["status"]}


def tc_case_insensitive(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    c.smb1_session_setup(ctx.user, ctx.password)
    r = c.smb1_tree_connect(ctx.share.upper())
    return {"status": r["status"], "Service": r.get("Service")}


def tc_readonly_share(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    c.smb1_session_setup(ctx.user, ctx.password)
    r = c.smb1_tree_connect("ro")
    return {"status": r["status"], "Service": r.get("Service")}


def tc_disconnect_then_use(ctx):
    c, tc = ctx.smb1()
    c.smb1_tree_disconnect()
    r = c.smb1_nt_create("\\after-disconnect.txt")
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# CREATE / OPEN


def _create(ctx, name, **kw):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create(name, **kw)
    return c, r


def create_new(ctx):
    c, r = _create(ctx, "\\probe-new.txt",
                   create_disposition=P.FILE_CREATE)
    out = {"status": r["status"], "CreateAction": r.get("CreateAction"),
           "ExtFileAttributes": r.get("ExtFileAttributes"),
           "EndOfFile": r.get("EndOfFile"), "Directory": r.get("Directory"),
           "OplockLevel": r.get("OplockLevel")}
    return out


def create_collision(ctx):
    c, _ = ctx.smb1()
    c.smb1_nt_create("\\probe-coll.txt", create_disposition=P.FILE_CREATE)
    r = c.smb1_nt_create("\\probe-coll.txt", create_disposition=P.FILE_CREATE)
    return {"status": r["status"]}


def open_missing(ctx):
    c, r = _create(ctx, "\\no-such-file.txt", create_disposition=P.FILE_OPEN)
    return {"status": r["status"]}


def open_existing(ctx):
    c, r = _create(ctx, "\\README.txt", create_disposition=P.FILE_OPEN,
                   desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    return {"status": r["status"], "CreateAction": r.get("CreateAction"),
            "EndOfFile": r.get("EndOfFile"), "Directory": r.get("Directory"),
            "ExtFileAttributes": r.get("ExtFileAttributes")}


def open_directory(ctx):
    c, r = _create(ctx, "\\data", create_disposition=P.FILE_OPEN,
                   create_options=P.FILE_DIRECTORY_FILE,
                   desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    return {"status": r["status"], "Directory": r.get("Directory"),
            "ExtFileAttributes": r.get("ExtFileAttributes"),
            "EndOfFile": r.get("EndOfFile")}


def open_root(ctx):
    c, r = _create(ctx, "", create_disposition=P.FILE_OPEN,
                   create_options=P.FILE_DIRECTORY_FILE,
                   desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    return {"status": r["status"], "Directory": r.get("Directory")}


def open_dir_as_file(ctx):
    c, r = _create(ctx, "\\data", create_disposition=P.FILE_OPEN,
                   create_options=P.FILE_NON_DIRECTORY_FILE)
    return {"status": r["status"]}


def open_file_as_dir(ctx):
    c, r = _create(ctx, "\\README.txt", create_disposition=P.FILE_OPEN,
                   create_options=P.FILE_DIRECTORY_FILE)
    return {"status": r["status"]}


def create_in_missing_dir(ctx):
    c, r = _create(ctx, "\\nodir\\file.txt", create_disposition=P.FILE_CREATE)
    return {"status": r["status"]}


def create_invalid_name(ctx):
    c, r = _create(ctx, "\\bad<name>.txt", create_disposition=P.FILE_CREATE)
    return {"status": r["status"]}


def create_wildcard_name(ctx):
    c, r = _create(ctx, "\\wild*card.txt", create_disposition=P.FILE_CREATE)
    return {"status": r["status"]}


def create_readonly_then_write(ctx):
    c, _ = ctx.smb1()
    c.smb1_nt_create("\\probe-ro.txt", create_disposition=P.FILE_CREATE,
                     file_attributes=P.FILE_ATTRIBUTE_READONLY)
    r = c.smb1_nt_create("\\probe-ro.txt", create_disposition=P.FILE_OPEN,
                         desired_access=P.FILE_WRITE_DATA)
    return {"status": r["status"]}


def create_delete_on_close_without_delete(ctx):
    c, r = _create(ctx, "\\probe-doc.txt", create_disposition=P.FILE_OPEN_IF,
                   desired_access=P.FILE_READ_DATA,
                   create_options=P.FILE_DELETE_ON_CLOSE)
    return {"status": r["status"]}


def create_readonly_delete_on_close(ctx):
    c, r = _create(ctx, "\\probe-rodoc.txt", create_disposition=P.FILE_OVERWRITE_IF,
                   desired_access=P.FILE_READ_DATA | P.DELETE,
                   file_attributes=P.FILE_ATTRIBUTE_READONLY,
                   create_options=P.FILE_DELETE_ON_CLOSE)
    return {"status": r["status"]}


def create_overwrite_if(ctx):
    c, _ = ctx.smb1()
    r1 = c.smb1_nt_create("\\probe-ow.txt", create_disposition=P.FILE_CREATE)
    c.smb1_write(r1["FID"], 0, b"x" * 100)
    c.smb1_close(r1["FID"])
    r = c.smb1_nt_create("\\probe-ow.txt", create_disposition=P.FILE_OVERWRITE_IF)
    return {"status": r["status"], "CreateAction": r.get("CreateAction"),
            "EndOfFile": r.get("EndOfFile"),
            "ExtFileAttributes": r.get("ExtFileAttributes")}


def create_supersede(ctx):
    c, _ = ctx.smb1()
    r1 = c.smb1_nt_create("\\probe-sup.txt", create_disposition=P.FILE_CREATE)
    c.smb1_write(r1["FID"], 0, b"y" * 50)
    c.smb1_close(r1["FID"])
    r = c.smb1_nt_create("\\probe-sup.txt", create_disposition=P.FILE_SUPERSEDE)
    return {"status": r["status"], "CreateAction": r.get("CreateAction"),
            "EndOfFile": r.get("EndOfFile")}


def create_attributes_normal(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-attr1.txt", create_disposition=P.FILE_CREATE,
                         file_attributes=P.FILE_ATTRIBUTE_NORMAL)
    return {"ExtFileAttributes": r.get("ExtFileAttributes")}


def create_attributes_system_hidden(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-attr2.txt", create_disposition=P.FILE_CREATE,
                         file_attributes=P.FILE_ATTRIBUTE_SYSTEM | P.FILE_ATTRIBUTE_HIDDEN)
    return {"ExtFileAttributes": r.get("ExtFileAttributes")}


def create_directory_attributes(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-dir1", create_disposition=P.FILE_CREATE,
                         create_options=P.FILE_DIRECTORY_FILE,
                         file_attributes=P.FILE_ATTRIBUTE_NORMAL)
    return {"status": r["status"], "Directory": r.get("Directory"),
            "ExtFileAttributes": r.get("ExtFileAttributes")}


def sharing_violation(ctx):
    c, _ = ctx.smb1()
    c.smb1_nt_create("\\probe-share.txt", create_disposition=P.FILE_OPEN_IF,
                     share_access=0)
    c2, _ = ctx.smb1()
    r = c2.smb1_nt_create("\\probe-share.txt", create_disposition=P.FILE_OPEN,
                          share_access=0)
    return {"status": r["status"]}


def sharing_allowed(ctx):
    c, _ = ctx.smb1()
    c.smb1_nt_create("\\probe-share2.txt", create_disposition=P.FILE_OPEN_IF,
                     desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES,
                     share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    c2, _ = ctx.smb1()
    r = c2.smb1_nt_create("\\probe-share2.txt", create_disposition=P.FILE_OPEN,
                          desired_access=P.FILE_READ_DATA,
                          share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    return {"status": r["status"]}


def readonly_share_create(ctx):
    c, _ = ctx.smb1(share="ro")
    r = c.smb1_nt_create("\\nope.txt", create_disposition=P.FILE_CREATE)
    return {"status": r["status"]}


def readonly_share_delete(ctx):
    """`read only = yes` refuses SMB_COM_DELETE on a file that is really there."""
    c, _ = ctx.smb1(share="ro")
    r = c.smb1_delete("\\README.txt")
    return {"status": r["status"]}


def readonly_share_rename(ctx):
    c, _ = ctx.smb1(share="ro")
    r = c.smb1_rename("\\README.txt", "\\README.renamed")
    return {"status": r["status"]}


def readonly_share_write(ctx):
    """Opening an existing file for write is refused before any WRITE_ANDX."""
    c, _ = ctx.smb1(share="ro")
    o = c.smb1_nt_create("\\README.txt", create_disposition=P.FILE_OPEN,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA)
    out = {"open_status": o["status"]}
    if o.get("FID") is not None:
        out["write_status"] = c.smb1_write(o["FID"], 0, b"X")["status"]
    return out


def readonly_share_setattr(ctx):
    """TRANS2_SET_PATH_INFORMATION, SMB_SET_FILE_BASIC_INFO."""
    c, _ = ctx.smb1(share="ro")
    params = struct.pack("<HI", 0x0101, 0) + P._u("\\README.txt")
    data = struct.pack("<qqqqII", 0, 0, 0, 0, 0x20, 0)
    r = c.smb1_trans2(0x06, params, data, max_param=2, max_data=0)
    return {"status": r["status"]}


def readonly_share_mkdir(ctx):
    c, _ = ctx.smb1(share="ro")
    r = c.smb1_nt_create("\\nopedir", create_disposition=P.FILE_CREATE,
                         create_options=P.FILE_DIRECTORY_FILE,
                         desired_access=P.FILE_READ_DATA)
    return {"status": r["status"]}


# --------------------------------------------------------------------------
# READ / WRITE


def rw_roundtrip(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-rw.bin", create_disposition=P.FILE_OVERWRITE_IF)
    payload = bytes(range(256)) * 16
    w = c.smb1_write(r["FID"], 0, payload)
    rd = c.smb1_read(r["FID"], 0, len(payload))
    return {"write_status": w["status"], "Count": w.get("Count"),
            "read_status": rd["status"], "read_len": len(rd.get("Data", b"")),
            "identical": rd.get("Data", b"") == payload}


def rw_offset(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-off.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_write(r["FID"], 5000, b"OFFSET")
    rd = c.smb1_read(r["FID"], 5000, 6)
    q = c.smb1_trans2(0x07, struct.pack("<HH", r["FID"], 0x0102))
    eof = struct.unpack_from("<q", q["Data"], 8)[0] if len(q.get("Data", b"")) >= 16 else None
    return {"data": rd.get("Data", b"").decode("latin-1"), "EndOfFile": eof}


def rw_64bit_offset(ctx):
    """WRITE_ANDX / READ_ANDX above 2^32 ([MS-CIFS] 2.2.4.43.1 OffsetHigh).

    A server that ignores OffsetHigh writes at the low 32 bits instead and the
    file ends up 8 KiB long rather than 4 GiB + 8 KiB.
    """
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-64.bin", create_disposition=P.FILE_OVERWRITE_IF)
    off = (1 << 32) + 8192
    w = c.smb1_write(r["FID"], off, b"ABOVE-FOUR-GIB")
    rd = c.smb1_read(r["FID"], off, 14)
    low = c.smb1_read(r["FID"], 8192, 14)
    q = c.smb1_trans2(0x07, struct.pack("<HH", r["FID"], 0x0102))
    eof = struct.unpack_from("<q", q["Data"], 8)[0] if len(q.get("Data", b"")) >= 16 else None
    return {"write_status": w["status"], "Count": w.get("Count"),
            "read_status": rd["status"],
            "data": rd.get("Data", b"").decode("latin-1"),
            "at_low_offset": low.get("Data", b"").decode("latin-1"),
            "EndOfFile": eof}


def rw_large_count(ctx):
    """A single WRITE_ANDX / READ_ANDX far larger than the negotiated buffer.

    32 KiB in one PDU is the large read/write form: it is well past the
    MaxBufferSize the NEGOTIATE response advertises, so it only works if the
    server honours CAP_LARGE_READX / CAP_LARGE_WRITEX and reads the length from
    DataLength plus DataLengthHigh ([MS-CIFS] 2.2.4.42.1, 2.2.4.43.1) rather
    than clamping to the buffer.
    """
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-big.bin", create_disposition=P.FILE_OVERWRITE_IF)
    payload = (bytes(range(256)) * 128)                 # 32768 bytes
    w = c.smb1_write(r["FID"], 0, payload)
    rd = c.smb1_read(r["FID"], 0, len(payload))
    q = c.smb1_trans2(0x07, struct.pack("<HH", r["FID"], 0x0102))
    eof = struct.unpack_from("<q", q["Data"], 8)[0] if len(q.get("Data", b"")) >= 16 else None
    return {"write_status": w["status"], "Count": w.get("Count"),
            "read_status": rd["status"], "read_len": len(rd.get("Data", b"")),
            "identical": rd.get("Data", b"") == payload, "EndOfFile": eof}


def rw_read_past_eof(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-eof.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_write(r["FID"], 0, b"12345")
    rd = c.smb1_read(r["FID"], 100, 50)
    return {"status": rd["status"], "len": len(rd.get("Data", b""))}


def rw_zero_write_keeps_size(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-trunc.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_write(r["FID"], 0, b"A" * 1000)
    c.smb1_write(r["FID"], 400, b"")
    q = c.smb1_trans2(0x07, struct.pack("<HH", r["FID"], 0x0102))
    eof = struct.unpack_from("<q", q["Data"], 8)[0] if len(q.get("Data", b"")) >= 16 else None
    return {"EndOfFile": eof}


def rw_write_to_readonly_handle(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\README.txt", create_disposition=P.FILE_OPEN,
                         desired_access=P.FILE_READ_DATA)
    w = c.smb1_write(r["FID"], 0, b"nope")
    return {"status": w["status"]}


def rw_read_from_writeonly_handle(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-wo.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         desired_access=P.FILE_WRITE_DATA)
    rd = c.smb1_read(r["FID"], 0, 16)
    return {"status": rd["status"]}


def rw_bad_fid(ctx):
    c, _ = ctx.smb1()
    rd = c.smb1_read(0x4242, 0, 16)
    return {"status": rd["status"]}


def rw_close_twice(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_nt_create("\\probe-close.bin", create_disposition=P.FILE_OVERWRITE_IF)
    a = c.smb1_close(r["FID"])
    b = c.smb1_close(r["FID"])
    return {"first": a["status"], "second": b["status"]}


# --------------------------------------------------------------------------
# TRANS2


def t2_qpath_basic(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0) + P._u("\\README.txt") + b"\x00\x00")
    d = r.get("Data", b"")
    return {"status": r["status"], "len": len(d),
            "attributes": struct.unpack_from("<I", d, 32)[0] if len(d) >= 36 else None}


def t2_qpath_standard(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_trans2(0x05, struct.pack("<HI", 0x0102, 0) + P._u("\\README.txt") + b"\x00\x00")
    d = r.get("Data", b"")
    return {"status": r["status"], "len": len(d),
            "EndOfFile": struct.unpack_from("<q", d, 8)[0] if len(d) >= 16 else None,
            "Directory": d[21] if len(d) >= 22 else None}


def t2_qpath_all(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_trans2(0x05, struct.pack("<HI", 0x0107, 0) + P._u("\\README.txt") + b"\x00\x00")
    d = r.get("Data", b"")
    return {"status": r["status"],
            "attributes": struct.unpack_from("<I", d, 32)[0] if len(d) >= 36 else None,
            "EndOfFile": struct.unpack_from("<q", d, 48)[0] if len(d) >= 56 else None,
            "NumberOfLinks": struct.unpack_from("<I", d, 56)[0] if len(d) >= 60 else None,
            "Directory": d[61] if len(d) >= 62 else None,
            "NameLength": struct.unpack_from("<I", d, 68)[0] if len(d) >= 72 else None,
            "Name": d[72:].decode("utf-16le", "replace") if len(d) > 72 else None}


def t2_qpath_missing(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0) + P._u("\\nope.txt") + b"\x00\x00")
    return {"status": r["status"]}


def t2_qpath_bad_level(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_trans2(0x05, struct.pack("<HI", 0x0999, 0) + P._u("\\README.txt") + b"\x00\x00")
    return {"status": r["status"]}


def t2_qpath_directory(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_trans2(0x05, struct.pack("<HI", 0x0102, 0) + P._u("\\data") + b"\x00\x00")
    d = r.get("Data", b"")
    return {"status": r["status"], "Directory": d[21] if len(d) >= 22 else None,
            "EndOfFile": struct.unpack_from("<q", d, 8)[0] if len(d) >= 16 else None}


def t2_qfile_name(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\data\\beta.txt", create_disposition=P.FILE_OPEN,
                         desired_access=P.FILE_READ_ATTRIBUTES | P.FILE_READ_DATA)
    r = c.smb1_trans2(0x07, struct.pack("<HH", o["FID"], 0x0104))
    d = r.get("Data", b"")
    n = struct.unpack_from("<I", d, 0)[0] if len(d) >= 4 else None
    return {"status": r["status"], "NameLength": n,
            "Name": d[4:4 + (n or 0)].decode("utf-16le", "replace")}


def t2_qfs_attribute(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_trans2(0x03, struct.pack("<H", 0x0105))
    d = r.get("Data", b"")
    n = struct.unpack_from("<I", d, 8)[0] if len(d) >= 12 else None
    return {"status": r["status"],
            "MaxFileNameLength": struct.unpack_from("<I", d, 4)[0] if len(d) >= 8 else None,
            "FileSystemName": d[12:12 + (n or 0)].decode("utf-16le", "replace")}


def t2_qfs_volume(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_trans2(0x03, struct.pack("<H", 0x0102))
    d = r.get("Data", b"")
    n = struct.unpack_from("<I", d, 12)[0] if len(d) >= 16 else None
    return {"status": r["status"], "VolumeLabelLength": n,
            "VolumeLabel": d[18:18 + (n or 0)].decode("utf-16le", "replace")}


def t2_qfs_bad_level(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_trans2(0x03, struct.pack("<H", 0x0999))
    return {"status": r["status"]}


def _find_names(data, level=0x0104):
    names, off = [], 0
    while off + 4 <= len(data):
        nxt = struct.unpack_from("<I", data, off)[0]
        nlen = struct.unpack_from("<I", data, off + 60)[0]
        start = off + (94 if level == 0x0104 else 64)
        names.append(data[start:start + nlen].decode("utf-16le", "replace"))
        if nxt == 0:
            break
        off += nxt
    return names


def _seed_dir(c, name, files=("a.txt", "b.txt", "c.bin", "dd.txt")):
    """A directory this case owns, so earlier cases cannot change its contents."""
    d = c.smb1_nt_create(name, create_disposition=P.FILE_OPEN_IF,
                         create_options=P.FILE_DIRECTORY_FILE)
    if "FID" in d:
        c.smb1_close(d["FID"])
    for f in files:
        o = c.smb1_nt_create(name + "\\" + f, create_disposition=P.FILE_OVERWRITE_IF)
        if "FID" in o:
            c.smb1_write(o["FID"], 0, b"x" * len(f))
            c.smb1_close(o["FID"])


def t2_find_all(ctx):
    c, _ = ctx.smb1()
    _seed_dir(c, "\\findall")
    params = struct.pack("<HHHHI", 0x16, 100, 0, 0x0104, 0) + P._u("\\findall\\*") + b"\x00\x00"
    r = c.smb1_trans2(0x01, params, max_data=16384)
    p = r.get("Params", b"")
    return {"status": r["status"],
            "SearchCount": struct.unpack_from("<H", p, 2)[0] if len(p) >= 4 else None,
            "EndOfSearch": struct.unpack_from("<H", p, 4)[0] if len(p) >= 6 else None,
            "names": sorted(_find_names(r.get("Data", b"")))}


def t2_find_pattern(ctx):
    c, _ = ctx.smb1()
    _seed_dir(c, "\\findpat")
    params = struct.pack("<HHHHI", 0x16, 100, 0, 0x0104, 0) + P._u("\\findpat\\?.txt") + b"\x00\x00"
    r = c.smb1_trans2(0x01, params, max_data=16384)
    return {"status": r["status"], "names": sorted(_find_names(r.get("Data", b"")))}


def t2_find_no_match(ctx):
    c, _ = ctx.smb1()
    params = struct.pack("<HHHHI", 0x16, 100, 0, 0x0104, 0) + P._u("\\zzz*") + b"\x00\x00"
    r = c.smb1_trans2(0x01, params, max_data=16384)
    return {"status": r["status"]}


def t2_find_missing_dir(ctx):
    c, _ = ctx.smb1()
    params = struct.pack("<HHHHI", 0x16, 100, 0, 0x0104, 0) + P._u("\\nodir\\*") + b"\x00\x00"
    r = c.smb1_trans2(0x01, params, max_data=16384)
    return {"status": r["status"]}


def t2_find_names_level(ctx):
    c, _ = ctx.smb1()
    _seed_dir(c, "\\findnam")
    params = struct.pack("<HHHHI", 0x16, 100, 0, 0x0103, 0) + P._u("\\findnam\\*") + b"\x00\x00"
    r = c.smb1_trans2(0x01, params, max_data=16384)
    d = r.get("Data", b"")
    names, off = [], 0
    while off + 12 <= len(d):
        nxt = struct.unpack_from("<I", d, off)[0]
        nlen = struct.unpack_from("<I", d, off + 8)[0]
        names.append(d[off + 12:off + 12 + nlen].decode("utf-16le", "replace"))
        if nxt == 0:
            break
        off += nxt
    return {"status": r["status"], "names": sorted(names)}


def t2_find_next(ctx):
    c, _ = ctx.smb1()
    _seed_dir(c, "\\findnext", files=tuple("f%02d.dat" % i for i in range(12)))
    params = struct.pack("<HHHHI", 0x16, 2, 0, 0x0104, 0) + P._u("\\findnext\\*") + b"\x00\x00"
    first = c.smb1_trans2(0x01, params, max_data=16384)
    p = first.get("Params", b"")
    sid = struct.unpack_from("<H", p, 0)[0] if len(p) >= 2 else 0
    got = list(_find_names(first.get("Data", b"")))
    for _ in range(60):
        nxt = c.smb1_trans2(0x02, struct.pack("<HHHIH", sid, 2, 0x0104, 0, 0)
                            + b"\x00\x00", max_data=16384)
        if nxt["status"] != 0:
            break
        more = _find_names(nxt.get("Data", b""))
        got.extend(more)
        p2 = nxt.get("Params", b"")
        if len(p2) >= 4 and struct.unpack_from("<H", p2, 2)[0] == 1:
            break
    return {"first_count": struct.unpack_from("<H", p, 2)[0] if len(p) >= 4 else None,
            "names": sorted(set(got))}


def t2_setpath_basic_attributes(ctx):
    c, _ = ctx.smb1()
    c.smb1_nt_create("\\probe-setattr.txt", create_disposition=P.FILE_OVERWRITE_IF)
    data = struct.pack("<qqqqII", 0, 0, 0, 0,
                       P.FILE_ATTRIBUTE_HIDDEN | P.FILE_ATTRIBUTE_SYSTEM, 0)
    s = c.smb1_trans2(0x06, struct.pack("<HI", 0x0101, 0)
                      + P._u("\\probe-setattr.txt") + b"\x00\x00", data)
    q = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0)
                      + P._u("\\probe-setattr.txt") + b"\x00\x00")
    d = q.get("Data", b"")
    return {"set_status": s["status"],
            "attributes": struct.unpack_from("<I", d, 32)[0] if len(d) >= 36 else None}


def t2_setfile_eof(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-eofset.bin", create_disposition=P.FILE_OVERWRITE_IF)
    s = c.smb1_trans2(0x08, struct.pack("<HHH", o["FID"], 0x0104, 0),
                      struct.pack("<q", 4321))
    q = c.smb1_trans2(0x07, struct.pack("<HH", o["FID"], 0x0102))
    d = q.get("Data", b"")
    return {"set_status": s["status"],
            "EndOfFile": struct.unpack_from("<q", d, 8)[0] if len(d) >= 16 else None}


def t2_setfile_disposition(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-disp.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA | P.DELETE)
    s = c.smb1_trans2(0x08, struct.pack("<HHH", o["FID"], 0x0102, 0), b"\x01")
    q = c.smb1_trans2(0x07, struct.pack("<HH", o["FID"], 0x0102))
    d = q.get("Data", b"")
    c.smb1_close(o["FID"])
    after = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0)
                          + P._u("\\probe-disp.bin") + b"\x00\x00")
    return {"set_status": s["status"],
            "DeletePending": d[20] if len(d) >= 22 else None,
            "NumberOfLinks": struct.unpack_from("<I", d, 16)[0] if len(d) >= 20 else None,
            "after_close": after["status"]}


def t2_setfile_disposition_no_delete_access(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-disp2.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA)
    s = c.smb1_trans2(0x08, struct.pack("<HHH", o["FID"], 0x0102, 0), b"\x01")
    return {"status": s["status"]}


def t2_setfile_allocation(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-alloc.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_write(o["FID"], 0, b"Q" * 900)
    s = c.smb1_trans2(0x08, struct.pack("<HHH", o["FID"], 0x0103, 0),
                      struct.pack("<q", 100))
    q = c.smb1_trans2(0x07, struct.pack("<HH", o["FID"], 0x0102))
    d = q.get("Data", b"")
    return {"set_status": s["status"],
            "EndOfFile": struct.unpack_from("<q", d, 8)[0] if len(d) >= 16 else None}


# --------------------------------------------------------------------------
# delete / rename / directories


def del_file(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-del.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_close(o["FID"])
    d = c.smb1_delete("\\probe-del.bin")
    q = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0)
                      + P._u("\\probe-del.bin") + b"\x00\x00")
    return {"delete_status": d["status"], "after": q["status"]}


def del_missing(ctx):
    c, _ = ctx.smb1()
    d = c.smb1_delete("\\no-such.bin")
    return {"status": d["status"]}


def del_directory(ctx):
    c, _ = ctx.smb1()
    d = c.smb1_delete("\\data")
    return {"status": d["status"]}


def del_open_file(ctx):
    c, _ = ctx.smb1()
    c.smb1_nt_create("\\probe-open-del.bin", create_disposition=P.FILE_OVERWRITE_IF,
                     share_access=P.FILE_SHARE_DELETE)
    d = c.smb1_delete("\\probe-open-del.bin")
    return {"status": d["status"]}


def del_wildcard(ctx):
    c, _ = ctx.smb1()
    for n in ("\\wc1.tmp", "\\wc2.tmp"):
        o = c.smb1_nt_create(n, create_disposition=P.FILE_OVERWRITE_IF)
        c.smb1_close(o["FID"])
    d = c.smb1_delete("\\*.tmp")
    q1 = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0) + P._u("\\wc1.tmp") + b"\x00\x00")
    return {"delete_status": d["status"], "after": q1["status"]}


def rename_file(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-r1.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_close(o["FID"])
    r = c.smb1_rename("\\probe-r1.bin", "\\probe-r2.bin")
    a = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0) + P._u("\\probe-r1.bin") + b"\x00\x00")
    b = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0) + P._u("\\probe-r2.bin") + b"\x00\x00")
    return {"rename_status": r["status"], "old": a["status"], "new": b["status"]}


def rename_onto_existing(ctx):
    c, _ = ctx.smb1()
    for n in ("\\probe-e1.bin", "\\probe-e2.bin"):
        o = c.smb1_nt_create(n, create_disposition=P.FILE_OVERWRITE_IF)
        c.smb1_close(o["FID"])
    r = c.smb1_rename("\\probe-e1.bin", "\\probe-e2.bin")
    return {"status": r["status"]}


def rename_missing(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_rename("\\no-such-src.bin", "\\dst.bin")
    return {"status": r["status"]}


def mkdir_rmdir(ctx):
    c, _ = ctx.smb1()
    a = c.smb1_command(P.SMB_COM_CREATE_DIRECTORY, b"",
                       b"\x04" + P._u("\\probe-md") + b"\x00\x00")
    b = c.smb1_command(P.SMB_COM_DELETE_DIRECTORY, b"",
                       b"\x04" + P._u("\\probe-md") + b"\x00\x00")
    return {"mkdir": a["status"], "rmdir": b["status"]}


def rmdir_nonempty(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_command(P.SMB_COM_DELETE_DIRECTORY, b"",
                       b"\x04" + P._u("\\data") + b"\x00\x00")
    return {"status": r["status"]}


def check_directory(ctx):
    c, _ = ctx.smb1()
    a = c.smb1_command(P.SMB_COM_CHECK_DIRECTORY, b"",
                       b"\x04" + P._u("\\data") + b"\x00\x00")
    b = c.smb1_command(P.SMB_COM_CHECK_DIRECTORY, b"",
                       b"\x04" + P._u("\\README.txt") + b"\x00\x00")
    d = c.smb1_command(P.SMB_COM_CHECK_DIRECTORY, b"",
                       b"\x04" + P._u("\\nodir") + b"\x00\x00")
    return {"dir": a["status"], "file": b["status"], "missing": d["status"]}


# --------------------------------------------------------------------------
# locking


def lock_conflict(ctx):
    c1, _ = ctx.smb1()
    o1 = c1.smb1_nt_create("\\probe-lock.bin", create_disposition=P.FILE_OVERWRITE_IF,
                           desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                           share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    c1.smb1_write(o1["FID"], 0, b"z" * 1024)
    c1.smb1_locking_andx(o1["FID"], locks=[(1, 100, 50)])
    c2, _ = ctx.smb1()
    o2 = c2.smb1_nt_create("\\probe-lock.bin", create_disposition=P.FILE_OPEN,
                           desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                           share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    a = c2.smb1_locking_andx(o2["FID"], locks=[(2, 100, 50)])
    b = c2.smb1_locking_andx(o2["FID"], locks=[(2, 100, 50)])
    return {"first": a["status"], "second": b["status"]}


def lock_shared_ok(ctx):
    c1, _ = ctx.smb1()
    o1 = c1.smb1_nt_create("\\probe-lock2.bin", create_disposition=P.FILE_OVERWRITE_IF,
                           desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                           share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    c1.smb1_write(o1["FID"], 0, b"z" * 1024)
    a = c1.smb1_locking_andx(o1["FID"], locks=[(1, 0, 100)], type_of_lock=0x01)
    c2, _ = ctx.smb1()
    o2 = c2.smb1_nt_create("\\probe-lock2.bin", create_disposition=P.FILE_OPEN,
                           desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                           share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    b = c2.smb1_locking_andx(o2["FID"], locks=[(2, 0, 100)], type_of_lock=0x01)
    return {"first": a["status"], "second": b["status"]}


def lock_unlock(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-lock3.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_write(o["FID"], 0, b"z" * 1024)
    a = c.smb1_locking_andx(o["FID"], locks=[(1, 10, 20)])
    b = c.smb1_locking_andx(o["FID"], unlocks=[(1, 10, 20)])
    d = c.smb1_locking_andx(o["FID"], unlocks=[(1, 10, 20)])
    return {"lock": a["status"], "unlock": b["status"], "unlock_again": d["status"]}


def lock_released_on_close(ctx):
    c1, _ = ctx.smb1()
    o1 = c1.smb1_nt_create("\\probe-lock4.bin", create_disposition=P.FILE_OVERWRITE_IF,
                           desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                           share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    c1.smb1_write(o1["FID"], 0, b"z" * 1024)
    c1.smb1_locking_andx(o1["FID"], locks=[(1, 0, 64)])
    c1.smb1_close(o1["FID"])
    c2, _ = ctx.smb1()
    o2 = c2.smb1_nt_create("\\probe-lock4.bin", create_disposition=P.FILE_OPEN,
                           desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                           share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    a = c2.smb1_locking_andx(o2["FID"], locks=[(2, 0, 64)])
    return {"status": a["status"]}


# --------------------------------------------------------------------------
# misc


def echo_repeat(ctx):
    c = ctx.client()
    c.smb1_negotiate()
    r = c.smb1_echo(count=3, data=b"probe-echo")
    return {"status": r["status"], "responses": len(r.get("Replies", [])),
            "data": r.get("Data", b"").decode("latin-1")}


def unknown_command(ctx):
    c, _ = ctx.smb1()
    r = c.smb1_command(0x77, b"", b"")
    return {"status": r["status"]}


def nttrans_query_security(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\README.txt", create_disposition=P.FILE_OPEN,
                         desired_access=P.FILE_READ_DATA | P.READ_CONTROL)
    r = c.smb1_nt_transact(6, setup=b"", params=struct.pack("<HHI", o["FID"], 0, 7),
                           data=b"", max_param=4, max_data=8192)
    d = r.get("Data", b"")
    return {"status": r["status"], "revision": d[0] if d else None,
            "self_relative": bool(struct.unpack_from("<H", d, 2)[0] & 0x8000) if len(d) >= 4 else None,
            "dacl_present": bool(struct.unpack_from("<H", d, 2)[0] & 0x0004) if len(d) >= 4 else None,
            "nonzero_owner_offset": (struct.unpack_from("<I", d, 4)[0] != 0) if len(d) >= 8 else None}



# --------------------------------------------------------------------------
# the rest of the NT LM 0.12 command set


def open_andx(ctx):
    """SMB_COM_OPEN_ANDX as a file open ([MS-CIFS] 2.2.4.41).

    OpenFunction 0x0011 is "create if absent, open if present", so on a name
    that does not exist yet it creates and reports Action 2 (file created);
    0x0001 is "fail if absent", so on the name just created it opens and
    reports Action 1, and on a name that does not exist it fails.
    """
    c, _ = ctx.smb1()
    a = c.smb1_open_andx("\\probe-openx.bin", open_function=0x0011)
    reopened = c.smb1_open_andx("\\probe-openx.bin", open_function=0x0001)
    b = c.smb1_open_andx("\\no-such-openx.bin", open_function=0x0001)
    return {"create": a["status"], "Action": a.get("Action"),
            "Size": a.get("Size"), "FileAttrs": a.get("FileAttrs"),
            "reopen": reopened["status"], "reopen_Action": reopened.get("Action"),
            "missing": b["status"]}


def open_andx_existing(ctx):
    """OPEN_ANDX on a seeded file reports its real size and opens it for read."""
    c, _ = ctx.smb1()
    a = c.smb1_open_andx("\\README.txt", access_mode=0x0000,
                         open_function=0x0001)
    rd = c.smb1_read(a["FID"], 0, 16) if a.get("FID") is not None else {}
    return {"status": a["status"], "Action": a.get("Action"),
            "Size": a.get("Size"), "FileAttrs": a.get("FileAttrs"),
            "read_status": rd.get("status"),
            "data": rd.get("Data", b"").decode("latin-1")}


def open_andx_truncate(ctx):
    """OpenFunction 0x0012 is "truncate if present, create if absent"."""
    c, _ = ctx.smb1()
    first = c.smb1_nt_create("\\probe-openx-t.bin",
                             create_disposition=P.FILE_OVERWRITE_IF,
                             desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA)
    c.smb1_write(first["FID"], 0, b"Z" * 400)
    c.smb1_close(first["FID"])
    a = c.smb1_open_andx("\\probe-openx-t.bin", open_function=0x0012)
    return {"status": a["status"], "Action": a.get("Action"),
            "Size": a.get("Size")}


def nt_transact_create(ctx):
    c, _ = ctx.smb1()
    name = P._u("\\probe-ntt.bin")
    params = (struct.pack("<IIIQIIIIIIII", 0, 0,
                          P.FILE_READ_DATA | P.FILE_WRITE_DATA | P.FILE_READ_ATTRIBUTES,
                          0, P.FILE_ATTRIBUTE_NORMAL,
                          P.FILE_SHARE_READ | P.FILE_SHARE_WRITE,
                          P.FILE_OVERWRITE_IF, 0, 0, 0, len(name), 2)
              + b"\x00" + name)
    r = c.smb1_nt_transact(1, params=params, max_param=128, max_data=0)
    p = r.get("Params", b"")
    return {"status": r["status"],
            "OplockLevel": p[0] if len(p) >= 1 else None,
            "CreateAction": struct.unpack_from("<I", p, 4)[0] if len(p) >= 8 else None,
            "Directory": p[68] if len(p) >= 69 else None,
            "EndOfFile": struct.unpack_from("<q", p, 56)[0] if len(p) >= 64 else None}


def nt_rename(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-ntr.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_close(o["FID"])
    words = struct.pack("<HHI", 0x0006, 0x0103, 0)
    cur = 32 + 1 + len(words) + 2 + 1
    body = b"\x04" + c._smb1_str("\\probe-ntr.bin", cur)
    cur2 = cur + len(body) - 1 + 1
    body += b"\x04" + c._smb1_str("\\probe-ntr2.bin", cur2)
    r = c.smb1_command(P.SMB_COM_NT_RENAME, words, body)
    q = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0)
                      + P._u("\\probe-ntr2.bin") + b"\x00\x00")
    return {"rename": r["status"], "new_name": q["status"]}


def flush_handle(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-flush.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_write(o["FID"], 0, b"flushed")
    a = c.smb1_command(P.SMB_COM_FLUSH, struct.pack("<H", o["FID"]))
    b = c.smb1_command(P.SMB_COM_FLUSH, struct.pack("<H", 0xFFFF))
    d = c.smb1_command(P.SMB_COM_FLUSH, struct.pack("<H", 0x4242))
    return {"handle": a["status"], "all": b["status"], "bad": d["status"]}


def seek_handle(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-seek.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_write(o["FID"], 0, b"S" * 500)
    a = c.smb1_command(P.SMB_COM_SEEK, struct.pack("<HHi", o["FID"], 0, 120))
    b = c.smb1_command(P.SMB_COM_SEEK, struct.pack("<HHi", o["FID"], 2, 0))
    return {"set": a["status"],
            "set_offset": struct.unpack_from("<I", a["Words"], 0)[0] if a["WordCount"] >= 2 else None,
            "end": b["status"],
            "end_offset": struct.unpack_from("<I", b["Words"], 0)[0] if b["WordCount"] >= 2 else None}


def query_information(ctx):
    c, _ = ctx.smb1()
    cur = 32 + 1 + 0 + 2 + 1
    body = b"\x04" + c._smb1_str("\\README.txt", cur)
    r = c.smb1_command(P.SMB_COM_QUERY_INFORMATION, b"", body)
    w = r["Words"]
    body2 = b"\x04" + c._smb1_str("\\nosuch.txt", cur)
    m = c.smb1_command(P.SMB_COM_QUERY_INFORMATION, b"", body2)
    return {"status": r["status"], "WordCount": r["WordCount"],
            "FileAttributes": struct.unpack_from("<H", w, 0)[0] if r["WordCount"] >= 10 else None,
            "FileSize": struct.unpack_from("<I", w, 6)[0] if r["WordCount"] >= 10 else None,
            "missing": m["status"]}


def set_information(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-setinfo.bin", create_disposition=P.FILE_OVERWRITE_IF)
    c.smb1_close(o["FID"])
    words = struct.pack("<HI10s", P.FILE_ATTRIBUTE_HIDDEN, 0, b"\x00" * 10)
    cur = 32 + 1 + len(words) + 2 + 1
    body = b"\x04" + c._smb1_str("\\probe-setinfo.bin", cur)
    a = c.smb1_command(P.SMB_COM_SET_INFORMATION, words, body)
    q = c.smb1_trans2(0x05, struct.pack("<HI", 0x0101, 0)
                      + P._u("\\probe-setinfo.bin") + b"\x00\x00")
    d = q.get("Data", b"")
    return {"set": a["status"],
            "attributes": struct.unpack_from("<I", d, 32)[0] if len(d) >= 36 else None}


def query_information2(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\README.txt", create_disposition=P.FILE_OPEN,
                         desired_access=P.FILE_READ_DATA | P.FILE_READ_ATTRIBUTES)
    r = c.smb1_command(P.SMB_COM_QUERY_INFORMATION2, struct.pack("<H", o["FID"]))
    w = r["Words"]
    return {"status": r["status"], "WordCount": r["WordCount"],
            "FileDataSize": struct.unpack_from("<I", w, 12)[0] if r["WordCount"] >= 11 else None,
            "FileAttributes": struct.unpack_from("<H", w, 20)[0] if r["WordCount"] >= 11 else None}


def query_information_disk(ctx):
    """The five words of SMB_COM_QUERY_INFORMATION_DISK; the volume's actual
    size is a property of the host filesystem, so only shape is compared."""
    c, _ = ctx.smb1()
    r = c.smb1_command(P.SMB_COM_QUERY_INFORMATION_DISK, b"")
    w = r["Words"]
    ok = r["WordCount"] >= 5
    return {"status": r["status"], "WordCount": r["WordCount"],
            "total_units_nonzero": bool(struct.unpack_from("<H", w, 0)[0]) if ok else None,
            "blocks_per_unit_nonzero": bool(struct.unpack_from("<H", w, 2)[0]) if ok else None,
            "block_size_nonzero": bool(struct.unpack_from("<H", w, 4)[0]) if ok else None}


def process_exit_closes_handles(ctx):
    """SMB_COM_PROCESS_EXIT closes every file this PID has open."""
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-pexit.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         share_access=0)
    a = c.smb1_command(P.SMB_COM_PROCESS_EXIT, b"")
    b = c.smb1_nt_create("\\probe-pexit.bin", create_disposition=P.FILE_OPEN,
                         share_access=0)
    return {"exit": a["status"], "reopen": b["status"]}


def find_close2(ctx):
    c, _ = ctx.smb1()
    _seed_dir(c, "\\fclose")
    params = struct.pack("<HHHHI", 0x16, 2, 0, 0x0104, 0) + P._u("\\fclose\\*") + b"\x00\x00"
    first = c.smb1_trans2(0x01, params, max_data=8192)
    p = first.get("Params", b"")
    sid = struct.unpack_from("<H", p, 0)[0] if len(p) >= 2 else 0
    a = c.smb1_command(P.SMB_COM_FIND_CLOSE2, struct.pack("<H", sid))
    b = c.smb1_trans2(0x02, struct.pack("<HHHIH", sid, 2, 0x0104, 0, 0) + b"\x00\x00",
                      max_data=8192)
    return {"close": a["status"], "next_after_close": b["status"]}


def t2_create_directory(ctx):
    c, _ = ctx.smb1()
    params = struct.pack("<I", 0) + P._u("\\probe-t2dir") + b"\x00\x00"
    a = c.smb1_trans2(0x0D, params)
    b = c.smb1_trans2(0x0D, params)
    return {"first": a["status"], "again": b["status"]}


def lock_byte_range(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-lbr.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                         share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    c.smb1_write(o["FID"], 0, b"L" * 512)
    a = c.smb1_command(P.SMB_COM_LOCK_BYTE_RANGE,
                       struct.pack("<HII", o["FID"], 64, 32))
    c2, _ = ctx.smb1()
    o2 = c2.smb1_nt_create("\\probe-lbr.bin", create_disposition=P.FILE_OPEN,
                           desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                           share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    b = c2.smb1_command(P.SMB_COM_LOCK_BYTE_RANGE,
                        struct.pack("<HII", o2["FID"], 64, 32))
    d = c.smb1_command(P.SMB_COM_UNLOCK_BYTE_RANGE,
                       struct.pack("<HII", o["FID"], 64, 32))
    e = c2.smb1_command(P.SMB_COM_LOCK_BYTE_RANGE,
                        struct.pack("<HII", o2["FID"], 64, 32))
    return {"lock": a["status"], "conflict": b["status"], "unlock": d["status"],
            "after_unlock": e["status"]}


def lock_large_offset(ctx):
    """The large-file LOCKING_ANDX form, with a 64-bit offset."""
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-lbig.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                         share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    big = 0x100000000
    a = c.smb1_locking_andx(o["FID"], locks=[(1, big, 64)], type_of_lock=0x10)
    c2, _ = ctx.smb1()
    o2 = c2.smb1_nt_create("\\probe-lbig.bin", create_disposition=P.FILE_OPEN,
                           desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                           share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    b = c2.smb1_locking_andx(o2["FID"], locks=[(2, big, 64)], type_of_lock=0x10)
    d = c2.smb1_locking_andx(o2["FID"], locks=[(2, big + 64, 64)], type_of_lock=0x10)
    return {"lock": a["status"], "conflict": b["status"], "adjacent": d["status"]}


def lock_timed_blocking(ctx):
    """A non-zero Timeout makes LOCKING_ANDX block; it must still answer."""
    import time
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\probe-ltime.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                         share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    c.smb1_write(o["FID"], 0, b"T" * 128)
    c.smb1_locking_andx(o["FID"], locks=[(1, 0, 64)])
    c2, _ = ctx.smb1()
    o2 = c2.smb1_nt_create("\\probe-ltime.bin", create_disposition=P.FILE_OPEN,
                           desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                           share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    start = time.time()
    r = c2.smb1_locking_andx(o2["FID"], locks=[(2, 0, 64)], timeout=1500)
    waited = time.time() - start
    return {"status": r["status"], "waited_at_least_1s": waited >= 1.0}


# --- oplocks ---------------------------------------------------------------


def oplock_exclusive_grant(ctx):
    c, _ = ctx.smb1()
    # NT_CREATE_REQUEST_OPLOCK ([MS-CIFS] 2.2.4.64.1)
    r = c.smb1_nt_create("\\probe-opl1.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         flags=0x00000002)
    return {"status": r["status"], "OplockLevel": r.get("OplockLevel")}


def oplock_batch_grant(ctx):
    c, _ = ctx.smb1()
    # NT_CREATE_REQUEST_OPLOCK | NT_CREATE_REQUEST_OPBATCH
    r = c.smb1_nt_create("\\probe-opl2.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         flags=0x00000006)
    return {"status": r["status"], "OplockLevel": r.get("OplockLevel")}


def oplock_second_open_level_ii(ctx):
    """A second opener gets a level II oplock, not an exclusive one."""
    c, _ = ctx.smb1()
    a = c.smb1_nt_create("\\probe-opl3.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                         share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    c2, _ = ctx.smb1()
    b = c2.smb1_nt_create("\\probe-opl3.bin", create_disposition=P.FILE_OPEN,
                          desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                          share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE,
                          flags=0x00000006)
    return {"first": a["status"], "second": b["status"],
            "second_oplock": b.get("OplockLevel")}


def oplock_break_on_conflicting_open(ctx):
    """A batch oplock is broken by a second open; the holder acknowledges with
    LOCKING_ANDX and the second open then completes."""
    import threading
    c, _ = ctx.smb1()
    a = c.smb1_nt_create("\\probe-oplbrk.bin", create_disposition=P.FILE_OVERWRITE_IF,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA,
                         share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE,
                         flags=0x00000006)
    seen = {}

    def _reader():
        # The case closes its connections as soon as the observation is made, so
        # the acknowledgement may race the teardown; that is not a failure.
        try:
            brk = c.smb1_wait_notification(timeout=15.0)
            if brk is None or brk["Command"] != P.SMB_COM_LOCKING_ANDX:
                seen["break"] = None
                return
            w = brk["Words"]
            seen["break"] = {"mid": brk["MID"],
                             "type_of_lock": w[6] if len(w) > 6 else None,
                             "new_level": w[7] if len(w) > 7 else None,
                             "unlocks": struct.unpack_from("<H", w, 12)[0] if len(w) >= 14 else None,
                             "locks": struct.unpack_from("<H", w, 14)[0] if len(w) >= 16 else None}
            fid = struct.unpack_from("<H", w, 4)[0]
            c.smb1_locking_andx(fid, type_of_lock=0x02,
                                new_oplock_level=w[7] if len(w) > 7 else 1)
        except Exception:
            seen.setdefault("break", None)

    th = threading.Thread(target=_reader, daemon=True)
    th.start()
    c2, _ = ctx.smb1()
    c2.sock.settimeout(20.0)
    try:
        b = c2.smb1_nt_create("\\probe-oplbrk.bin", create_disposition=P.FILE_OPEN,
                              desired_access=P.FILE_READ_DATA,
                              share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
        second = b["status"]
    except Exception:
        second = "timeout"
    th.join(18.0)
    return {"granted": a.get("OplockLevel"), "second_open": second,
            "break": seen.get("break")}


def nttrans_set_security(ctx):
    """NT_TRANSACT_SET_SECURITY_DESC needs WRITE_DAC on the handle."""
    c, _ = ctx.smb1()
    sd = (struct.pack("<BBHIIII", 1, 0, 0x8004, 20, 36, 0, 52)
          + bytes([1, 2, 0, 0, 0, 0, 0, 5, 0x20, 0, 0, 0, 0x21, 2, 0, 0]) * 2
          + struct.pack("<BBHHH", 2, 0, 8, 0, 0))
    ok = c.smb1_nt_create("\\probe-sd.bin", create_disposition=P.FILE_OVERWRITE_IF,
                          desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA
                          | P.READ_CONTROL | 0x00040000)
    a = c.smb1_nt_transact(3, params=struct.pack("<HHI", ok["FID"], 0, 4),
                           data=sd, max_param=0, max_data=0)
    no = c.smb1_nt_create("\\probe-sd2.bin", create_disposition=P.FILE_OVERWRITE_IF,
                          desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA)
    b = c.smb1_nt_transact(3, params=struct.pack("<HHI", no["FID"], 0, 4),
                           data=sd, max_param=0, max_data=0)
    return {"with_write_dac": a["status"], "without_write_dac": b["status"]}


def nttrans_set_security_owner(ctx):
    """Replacing the owner needs WRITE_OWNER, not WRITE_DAC ([MS-DTYP] 2.4.6).

    The owner written back is the one the server itself reports for the object,
    so the only thing separating the two handles is the access mask: which SIDs
    a server will accept as a new owner is a question of its own, and not this
    case's.
    """
    c, _ = ctx.smb1()
    read = c.smb1_nt_create("\\probe-sdo.bin", create_disposition=P.FILE_OVERWRITE_IF,
                            desired_access=P.FILE_READ_DATA | P.READ_CONTROL)
    q = c.smb1_nt_transact(6, params=struct.pack("<HHI", read["FID"], 0, 1),
                           max_param=4, max_data=8192)
    raw = q.get("Data") or b""
    if len(raw) < 20:
        return {"query": q["status"], "owner_bytes": len(raw)}
    o_owner = struct.unpack_from("<I", raw, 4)[0]
    owner_sid = raw[o_owner:o_owner + 8 + 4 * raw[o_owner + 1]]
    sd = sdutil.build_sd(owner=owner_sid, control=0x8000)
    dac_only = c.smb1_nt_create("\\probe-sdo2.bin",
                                create_disposition=P.FILE_OVERWRITE_IF,
                                desired_access=P.FILE_READ_DATA | P.READ_CONTROL
                                | 0x00040000)
    a = c.smb1_nt_transact(3, params=struct.pack("<HHI", dac_only["FID"], 0, 1),
                           data=sd, max_param=0, max_data=0)
    with_owner = c.smb1_nt_create("\\probe-sdo3.bin",
                                  create_disposition=P.FILE_OVERWRITE_IF,
                                  desired_access=P.FILE_READ_DATA | P.READ_CONTROL
                                  | 0x00080000)
    b = c.smb1_nt_transact(3, params=struct.pack("<HHI", with_owner["FID"], 0, 1),
                           data=sd, max_param=0, max_data=0)
    return {"without_write_owner": a["status"], "with_write_owner": b["status"]}


def nttrans_set_security_roundtrip(ctx):
    """A DACL that is set is read back through the mode it maps onto.

    NT_TRANSACT_SET_SECURITY_DESC followed by NT_TRANSACT_QUERY_SECURITY_DESC on
    the same handle, so a handler that checks WRITE_DAC and then drops the body
    is visible: what comes back is the descriptor the resulting UNIX bits imply.
    """
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\data\\beta.txt", create_disposition=P.FILE_OPEN,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA
                         | P.READ_CONTROL | 0x00040000)
    dacl = sdutil.build_dacl([(0, 0, 0x001F01FF, sdutil.sid("S-1-1-0"))])
    sd = sdutil.build_sd(dacl=dacl)
    a = c.smb1_nt_transact(3, params=struct.pack("<HHI", o["FID"], 0, 4),
                           data=sd, max_param=0, max_data=0)
    q = c.smb1_nt_transact(6, params=struct.pack("<HHI", o["FID"], 0, 4),
                           max_param=4, max_data=8192)
    out = {"set_status": a["status"], "query_status": q["status"]}
    if q.get("Data"):
        parsed = sdutil.parse(q["Data"])
        out["descriptor"] = parsed
    return out


def nttrans_query_security_no_read_control(ctx):
    c, _ = ctx.smb1()
    o = c.smb1_nt_create("\\README.txt", create_disposition=P.FILE_OPEN,
                         desired_access=P.FILE_READ_DATA)
    r = c.smb1_nt_transact(6, params=struct.pack("<HHI", o["FID"], 0, 7),
                           max_param=4, max_data=8192)
    return {"status": r["status"]}


CASES = [
    ("neg-nt1", neg_nt1),
    ("neg-no-nt1", neg_no_nt1),
    ("neg-empty", neg_empty),
    ("neg-unknown-only", neg_unknown_only),
    ("neg-nt1-first", neg_nt1_first),
    ("neg-nt1-last", neg_nt1_last),
    ("neg-before-anything", neg_before_anything),
    ("ss-success", ss_success),
    ("ss-bad-password", ss_bad_password),
    ("ss-unknown-user", ss_unknown_user),
    ("ss-two-sessions", ss_two_sessions),
    ("ss-logoff-then-use", ss_logoff_then_use),
    ("ss-tree-connect-without-session", ss_tree_connect_without_session),
    ("tc-disk", tc_disk),
    ("tc-ipc", tc_ipc),
    ("tc-missing", tc_missing),
    ("tc-case-insensitive", tc_case_insensitive),
    ("tc-readonly-share", tc_readonly_share),
    ("tc-disconnect-then-use", tc_disconnect_then_use),
    ("create-new", create_new),
    ("create-collision", create_collision),
    ("open-missing", open_missing),
    ("open-existing", open_existing),
    ("open-directory", open_directory),
    ("open-root", open_root),
    ("open-dir-as-file", open_dir_as_file),
    ("open-file-as-dir", open_file_as_dir),
    ("create-in-missing-dir", create_in_missing_dir),
    ("create-invalid-name", create_invalid_name),
    ("create-wildcard-name", create_wildcard_name),
    ("create-readonly-then-write", create_readonly_then_write),
    ("create-delete-on-close-without-delete", create_delete_on_close_without_delete),
    ("create-readonly-delete-on-close", create_readonly_delete_on_close),
    ("create-overwrite-if", create_overwrite_if),
    ("create-supersede", create_supersede),
    ("create-attributes-normal", create_attributes_normal),
    ("create-attributes-system-hidden", create_attributes_system_hidden),
    ("create-directory-attributes", create_directory_attributes),
    ("sharing-violation", sharing_violation),
    ("sharing-allowed", sharing_allowed),
    ("readonly-share-create", readonly_share_create),
    ("readonly-share-delete", readonly_share_delete),
    ("readonly-share-rename", readonly_share_rename),
    ("readonly-share-write", readonly_share_write),
    ("readonly-share-setattr", readonly_share_setattr),
    ("readonly-share-mkdir", readonly_share_mkdir),
    ("rw-roundtrip", rw_roundtrip),
    ("rw-offset", rw_offset),
    ("rw-read-past-eof", rw_read_past_eof),
    ("rw-64bit-offset", rw_64bit_offset),
    ("rw-large-count", rw_large_count),
    ("rw-zero-write-keeps-size", rw_zero_write_keeps_size),
    ("rw-write-to-readonly-handle", rw_write_to_readonly_handle),
    ("rw-read-from-writeonly-handle", rw_read_from_writeonly_handle),
    ("rw-bad-fid", rw_bad_fid),
    ("rw-close-twice", rw_close_twice),
    ("t2-qpath-basic", t2_qpath_basic),
    ("t2-qpath-standard", t2_qpath_standard),
    ("t2-qpath-all", t2_qpath_all),
    ("t2-qpath-missing", t2_qpath_missing),
    ("t2-qpath-bad-level", t2_qpath_bad_level),
    ("t2-qpath-directory", t2_qpath_directory),
    ("t2-qfile-name", t2_qfile_name),
    ("t2-qfs-attribute", t2_qfs_attribute),
    ("t2-qfs-volume", t2_qfs_volume),
    ("t2-qfs-bad-level", t2_qfs_bad_level),
    ("t2-find-all", t2_find_all),
    ("t2-find-pattern", t2_find_pattern),
    ("t2-find-no-match", t2_find_no_match),
    ("t2-find-missing-dir", t2_find_missing_dir),
    ("t2-find-names-level", t2_find_names_level),
    ("t2-find-next", t2_find_next),
    ("t2-setpath-basic-attributes", t2_setpath_basic_attributes),
    ("t2-setfile-eof", t2_setfile_eof),
    ("t2-setfile-disposition", t2_setfile_disposition),
    ("t2-setfile-disposition-no-delete-access", t2_setfile_disposition_no_delete_access),
    ("t2-setfile-allocation", t2_setfile_allocation),
    ("del-file", del_file),
    ("del-missing", del_missing),
    ("del-directory", del_directory),
    ("del-open-file", del_open_file),
    ("rename-file", rename_file),
    ("rename-onto-existing", rename_onto_existing),
    ("rename-missing", rename_missing),
    ("mkdir-rmdir", mkdir_rmdir),
    ("rmdir-nonempty", rmdir_nonempty),
    ("check-directory", check_directory),
    ("lock-conflict", lock_conflict),
    ("lock-shared-ok", lock_shared_ok),
    ("lock-unlock", lock_unlock),
    ("lock-released-on-close", lock_released_on_close),
    ("echo-repeat", echo_repeat),
    ("unknown-command", unknown_command),
    ("nttrans-query-security", nttrans_query_security),
    ("open-andx", open_andx),
    ("open-andx-existing", open_andx_existing),
    ("open-andx-truncate", open_andx_truncate),
    ("nt-transact-create", nt_transact_create),
    ("nt-rename", nt_rename),
    ("flush-handle", flush_handle),
    ("seek-handle", seek_handle),
    ("query-information", query_information),
    ("set-information", set_information),
    ("query-information2", query_information2),
    ("query-information-disk", query_information_disk),
    ("process-exit-closes-handles", process_exit_closes_handles),
    ("find-close2", find_close2),
    ("t2-create-directory", t2_create_directory),
    ("lock-byte-range", lock_byte_range),
    ("lock-large-offset", lock_large_offset),
    ("lock-timed-blocking", lock_timed_blocking),
    ("oplock-exclusive-grant", oplock_exclusive_grant),
    ("oplock-batch-grant", oplock_batch_grant),
    ("oplock-second-open-level-ii", oplock_second_open_level_ii),
    ("oplock-break-on-conflicting-open", oplock_break_on_conflicting_open),
    ("nttrans-set-security", nttrans_set_security),
    ("nttrans-set-security-owner", nttrans_set_security_owner),
    ("nttrans-set-security-roundtrip", nttrans_set_security_roundtrip),
    ("nttrans-query-security-no-read-control", nttrans_query_security_no_read_control),
]

run_all = runner(CASES)
