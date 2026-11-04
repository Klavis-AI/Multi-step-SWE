"""Wire-level probe cases for the witness service ([MS-SWN]) over DCE/RPC on the
`\\pipe\\witness` named pipe of `IPC$`.

Samba only runs its witness service on a clustered server, so there is no Samba
answer to record. The oracle used instead is Samba's own NDR implementation:
every response stub is handed to `ndrdump witness <op> out --validate`, which
re-marshals what it parsed and compares. A stub that survives that is encoded
the way Samba would encode it.
"""
import os
import re
import struct
import subprocess
import tempfile
import uuid

import smbprobe as P
from probebase import Ctx, runner

DIALECTS = [0x0202, 0x0210, 0x0300, 0x0302]
WITNESS_UUID = uuid.UUID("ccd8c074-d0e5-4a40-92b4-d074faa6ba28")
NDR_UUID = uuid.UUID("8a885d04-1ceb-11c9-9fe8-08002b104860")
FSCTL_PIPE_TRANSCEIVE = 0x0011C017


def _ipc(ctx):
    c = ctx.client()
    c.smb2_negotiate(DIALECTS, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect("IPC$")
    return c, t


def _bind_pdu(call_id=1, ctx_id=0):
    body = struct.pack("<HHIHHI", 4280, 4280, 0, 1, 0, 0)
    body = struct.pack("<HHI", 4280, 4280, 0)          # max xmit, max recv, assoc group
    body += struct.pack("<BBBB", 1, 0, 0, 0)           # 1 context element
    body += struct.pack("<HBB", ctx_id, 1, 0)          # context id, 1 transfer syntax
    body += WITNESS_UUID.bytes_le + struct.pack("<HH", 1, 1)
    body += NDR_UUID.bytes_le + struct.pack("<I", 2)
    head = struct.pack("<BBBBBBBBHHI", 5, 0, 11, 0x03, 0x10, 0, 0, 0,
                       16 + len(body), 0, call_id)
    return head + body


def _request_pdu(opnum, stub, call_id=2, ctx_id=0):
    body = struct.pack("<IHH", len(stub), ctx_id, opnum) + stub
    head = struct.pack("<BBBBBBBBHHI", 5, 0, 0, 0x03, 0x10, 0, 0, 0,
                       16 + len(body), 0, call_id)
    return head + body


def _parse_pdu(buf):
    if len(buf) < 16:
        return {"short": True, "len": len(buf)}
    (ver, minor, ptype, pfc, d0, d1, d2, d3, frag, auth, call_id) = \
        struct.unpack_from("<BBBBBBBBHHI", buf, 0)
    out = {"ptype": ptype, "pfc_flags": pfc, "frag_length": frag, "call_id": call_id,
           "version": ver, "version_minor": minor}
    if ptype == 12 and len(buf) >= 24:
        out["max_xmit"], out["max_recv"], out["assoc_group"] = \
            struct.unpack_from("<HHI", buf, 16)
        sa = buf.find(b"\x00", 24)
        nres = buf[24 + (sa - 24)] if False else None
        # the secondary address is a counted, NUL-terminated string
        salen = struct.unpack_from("<H", buf, 24)[0]
        off = 26 + salen
        off += (-off) % 4
        if off + 8 <= len(buf):
            nres, = struct.unpack_from("<B", buf, off)
            out["n_results"] = nres
            out["ack_result"] = struct.unpack_from("<H", buf, off + 4)[0]
            out["ack_reason"] = struct.unpack_from("<H", buf, off + 6)[0]
    elif ptype == 2 and len(buf) >= 24:
        alloc, cid, cancel, _r = struct.unpack_from("<IHBB", buf, 16)
        out["alloc_hint"] = alloc
        out["stub"] = buf[24:frag]
    elif ptype == 3 and len(buf) >= 32:
        out["fault_status"] = struct.unpack_from("<I", buf, 24)[0]
    return out


def _ndrdump(op, direction, blob):
    """Samba's own NDR decoder, used as the oracle.

    `ndrdump <pipe> <op> <in|out> <file> --validate` parses the stub and then
    re-marshals what it parsed and compares the two, so a stub that survives it
    is encoded the way Samba would encode it. The blob is passed as a file:
    `--hex-input --input=` segfaults in 4.17.12.
    """
    if not blob:
        return {"ran": False}
    fd, path = tempfile.mkstemp(prefix="ndrdump")
    try:
        os.write(fd, blob)
        os.close(fd)
        p = subprocess.run(["ndrdump", "witness", op, direction, path, "--validate"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return {"ran": False}
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass
    out = p.stdout.decode("utf-8", "replace")
    res = {"ran": True, "valid": p.returncode == 0}
    m = re.search(r"^\s*result\s*:\s*(\S+)", out, re.M)
    if m:
        res["result"] = m.group(1)
    return res


def _open_pipe(c, name="witness"):
    return c.smb2_create(name, create_disposition=P.FILE_OPEN,
                         desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA
                         | P.FILE_READ_ATTRIBUTES | 0x00100000,
                         share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE
                         | P.FILE_SHARE_DELETE,
                         create_options=0)


def _transceive(c, fid, pdu):
    r = c.smb2_ioctl(fid, FSCTL_PIPE_TRANSCEIVE, pdu, max_out=65536)
    return r


def pipe_open_witness(ctx):
    c, t = _ipc(ctx)
    o = _open_pipe(c)
    return {"tree": t["status"], "ShareType": t.get("ShareType"),
            "create": o["status"]}


def pipe_open_unknown(ctx):
    c, t = _ipc(ctx)
    o = _open_pipe(c, "nosuchpipe")
    return {"status": o["status"]}


def pipe_open_on_disk_share(ctx):
    c = ctx.client()
    c.smb2_negotiate(DIALECTS, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.smb2_tree_connect(ctx.share)
    o = _open_pipe(c)
    return {"status": o["status"]}


def dcerpc_bind(ctx):
    c, _ = _ipc(ctx)
    o = _open_pipe(c)
    if o["status"] != 0:
        return {"create": o["status"]}
    r = _transceive(c, o["FileId"], _bind_pdu())
    p = _parse_pdu(r.get("Buffer", b""))
    return {"ioctl": r["status"], "ptype": p.get("ptype"),
            "call_id": p.get("call_id"), "ack_result": p.get("ack_result"),
            "version": p.get("version"), "version_minor": p.get("version_minor")}


def _call(ctx, opnum, stub=b""):
    c, _ = _ipc(ctx)
    o = _open_pipe(c)
    if o["status"] != 0:
        return None, {"create": o["status"]}
    b = _transceive(c, o["FileId"], _bind_pdu())
    bp = _parse_pdu(b.get("Buffer", b""))
    r = _transceive(c, o["FileId"], _request_pdu(opnum, stub))
    rp = _parse_pdu(r.get("Buffer", b""))
    return rp, {"bind_ptype": bp.get("ptype"), "bind_result": bp.get("ack_result"),
                "ioctl": r["status"], "ptype": rp.get("ptype"),
                "call_id": rp.get("call_id")}


def witness_get_interface_list(ctx):
    rp, obs = _call(ctx, 0)
    if rp is None:
        return obs
    obs["ndr"] = _ndrdump("witness_GetInterfaceList", "out", rp.get("stub", b""))
    return obs


def _wstr(s):
    """A conformant-varying UTF-16 string: max_count, offset, actual_count, data."""
    b = (s + "\x00").encode("utf-16le")
    return (struct.pack("<III", len(b) // 2, 0, len(b) // 2) + b
            + b"\x00" * ((-len(b)) % 4))


def _register_stub(version=0x00010001, netname="SMBSRV", ipaddr="127.0.0.1",
                   client="probe"):
    """WitnessrRegister [in] ([MS-SWN] 3.1.4.2).

    Each [unique][string] parameter is its referent id followed immediately by
    the string: verified against `ndrdump witness witness_Register in --validate`,
    which rejects the deferred arrangement with "non-zero array offset".
    """
    out = struct.pack("<I", version)
    for i, s in enumerate((netname, ipaddr, client)):
        out += struct.pack("<I", 0x00020000 + i * 4) + _wstr(s)
    return out


def _register_ex_stub(version=0x00020000, netname="SMBSRV", sharename="tmp",
                      ipaddr="127.0.0.1", client="probe", flags=0, keepalive=0):
    """WitnessrRegisterEx [in] ([MS-SWN] 3.1.4.5): the same four-pointer shape
    plus a share name, and two trailing DWORDs."""
    out = struct.pack("<I", version)
    for i, s in enumerate((netname, sharename, ipaddr, client)):
        out += struct.pack("<I", 0x00020000 + i * 4) + _wstr(s)
    return out + struct.pack("<II", flags, keepalive)


def witness_register(ctx):
    rp, obs = _call(ctx, 1, _register_stub())
    if rp is None:
        return obs
    obs["ndr"] = _ndrdump("witness_Register", "out", rp.get("stub", b""))
    return obs


def witness_register_bad_version(ctx):
    rp, obs = _call(ctx, 1, _register_stub(version=0x00090009))
    if rp is None:
        return obs
    obs["ndr"] = _ndrdump("witness_Register", "out", rp.get("stub", b""))
    return obs


def witness_unregister(ctx):
    rp, obs = _call(ctx, 2, b"\x00" * 20)
    if rp is None:
        return obs
    obs["ndr"] = _ndrdump("witness_UnRegister", "out", rp.get("stub", b""))
    return obs


def witness_register_ex(ctx):
    rp, obs = _call(ctx, 4, _register_ex_stub())
    if rp is None:
        return obs
    obs["ndr"] = _ndrdump("witness_RegisterEx", "out", rp.get("stub", b""))
    return obs


def witness_unknown_opnum(ctx):
    rp, obs = _call(ctx, 99)
    keep = {k: obs[k] for k in ("create", "bind_ptype", "ioctl", "ptype") if k in obs}
    if rp is not None:
        keep["fault_status"] = rp.get("fault_status")
    return keep


def pipe_write_then_read(ctx):
    """The same bind driven with WRITE and READ instead of the FSCTL."""
    c, _ = _ipc(ctx)
    o = _open_pipe(c)
    if o["status"] != 0:
        return {"create": o["status"]}
    w = c.smb2_write(o["FileId"], 0, _bind_pdu(call_id=7))
    r = c.smb2_read(o["FileId"], 0, 4096)
    p = _parse_pdu(r.get("Buffer", b""))
    return {"write": w["status"], "read": r["status"],
            "ptype": p.get("ptype"), "call_id": p.get("call_id")}


def witness_async_notify(ctx):
    """WitnessrAsyncNotify (opnum 3) with a context handle no client map holds.

    The call itself is a long poll and has no deterministic answer; what is
    deterministic is what the server does with a handle it never issued.
    """
    rp, obs = _call(ctx, 3, b"\x00" * 20)
    if rp is None:
        return obs
    obs["ndr"] = _ndrdump("witness_AsyncNotify", "out", rp.get("stub", b""))
    return obs


def _call_on(c, fid, opnum, stub=b"", call_id=2):
    """One more request on a pipe handle that is already bound."""
    r = _transceive(c, fid, _request_pdu(opnum, stub, call_id=call_id))
    return _parse_pdu(r.get("Buffer", b"")), r["status"]


def witness_register_then_async_notify(ctx):
    """AsyncNotify on a handle the server itself just issued.

    Without this, a service that implements nothing but "a handle I never issued
    is WERR_NOT_FOUND" satisfies every other witness case: the registration
    handle would never have to be real, and the long-poll would never have to
    exist. [MS-SWN] 3.1.4.4.
    """
    c, _ = _ipc(ctx)
    o = _open_pipe(c)
    if o["status"] != 0:
        return {"create": o["status"]}
    fid = o["FileId"]
    b = _transceive(c, fid, _bind_pdu())
    bp = _parse_pdu(b.get("Buffer", b""))
    obs = {"bind_ptype": bp.get("ptype"), "bind_result": bp.get("ack_result")}

    reg, st = _call_on(c, fid, 1, _register_stub(), call_id=2)
    obs["register_ioctl"] = st
    obs["register_ptype"] = reg.get("ptype")
    reg_stub = reg.get("stub", b"")
    obs["register_ndr"] = _ndrdump("witness_Register", "out", reg_stub)
    if len(reg_stub) < 24:
        obs["handle_len"] = len(reg_stub)
        return obs
    handle = reg_stub[:20]
    obs["handle_nonzero"] = handle != b"\x00" * 20

    # the handle the server issued must be usable: unregistering it succeeds,
    # and unregistering it a second time does not
    un1, _ = _call_on(c, fid, 2, handle, call_id=3)
    obs["unregister_first"] = _ndrdump("witness_UnRegister", "out", un1.get("stub", b""))
    un2, _ = _call_on(c, fid, 2, handle, call_id=4)
    obs["unregister_again"] = _ndrdump("witness_UnRegister", "out", un2.get("stub", b""))
    return obs


def witness_register_wrong_netname(ctx):
    """Register naming a server this is not ([MS-SWN] 3.1.4.2).

    The NetName check was briefly disabled in the reference implementation so
    that this probe's registrations would be accepted -- a one-line workaround
    that removed the whole validation path from what the scored set can see.
    This case is why it is on.
    """
    rp, obs = _call(ctx, 1, _register_stub(netname="NOT-THIS-SERVER"))
    if rp is None:
        return obs
    obs["ndr"] = _ndrdump("witness_Register", "out", rp.get("stub", b""))
    return obs


CASES = [
    ("pipe-open-witness", pipe_open_witness),
    ("pipe-open-unknown", pipe_open_unknown),
    ("pipe-open-on-disk-share", pipe_open_on_disk_share),
    ("dcerpc-bind", dcerpc_bind),
    ("witness-get-interface-list", witness_get_interface_list),
    ("witness-register", witness_register),
    ("witness-register-bad-version", witness_register_bad_version),
    ("witness-register-wrong-netname", witness_register_wrong_netname),
    ("witness-unregister", witness_unregister),
    ("witness-async-notify", witness_async_notify),
    ("witness-register-then-async-notify", witness_register_then_async_notify),
    ("witness-register-ex", witness_register_ex),
    ("witness-unknown-opnum", witness_unknown_opnum),
    ("pipe-write-then-read", pipe_write_then_read),
]

run_all = runner(CASES)
