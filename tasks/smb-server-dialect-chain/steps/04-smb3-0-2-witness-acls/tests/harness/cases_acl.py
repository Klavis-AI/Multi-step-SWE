"""Wire-level probe cases for the security-descriptor and access-check model.

Every expected value is what Samba 4.17.12-Debian answered under the same
`smb.conf` with `nt acl support = yes`. Descriptors are compared through
`sdutil.parse`, which keeps everything [MS-DTYP] fixes and reduces a
machine-local account identifier to its shape.
"""
import struct

import sdutil
import smbprobe as P
from probebase import Ctx, runner

DIALECTS = [0x0202, 0x0210, 0x0300, 0x0302]
OWNER = 0x00000001
GROUP = 0x00000002
DACL = 0x00000004
SACL = 0x00000008
READ_CONTROL = 0x00020000
WRITE_DAC = 0x00040000
WRITE_OWNER = 0x00080000
ACCESS_SYSTEM_SECURITY = 0x01000000
EVERYONE = sdutil.sid("S-1-1-0")


def _sess(ctx, share=None):
    c = ctx.client()
    c.smb2_negotiate(DIALECTS, capabilities=c.client_capabilities)
    c.smb2_session_setup(ctx.user, ctx.password)
    c.smb2_tree_connect(share or ctx.share)
    return c


def _qsd(c, fid, info=OWNER | GROUP | DACL, length=65536):
    return c.smb2_query_info(fid, P.SMB2_0_INFO_SECURITY, 0,
                             output_buffer_length=length,
                             additional_information=info)


def sd_query_file(ctx):
    c = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | READ_CONTROL)
    r = _qsd(c, o["FileId"])
    return {"status": r["status"], "sd": sdutil.parse(r.get("Buffer", b""))}


def sd_query_directory(ctx):
    c = _sess(ctx)
    o = c.smb2_create("data", create_disposition=P.FILE_OPEN,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | READ_CONTROL)
    r = _qsd(c, o["FileId"])
    return {"status": r["status"], "sd": sdutil.parse(r.get("Buffer", b""))}


def sd_query_owner_only(ctx):
    c = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | READ_CONTROL)
    r = _qsd(c, o["FileId"], info=OWNER)
    return {"status": r["status"], "sd": sdutil.parse(r.get("Buffer", b""))}


def sd_query_dacl_only(ctx):
    c = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | READ_CONTROL)
    r = _qsd(c, o["FileId"], info=DACL)
    return {"status": r["status"], "sd": sdutil.parse(r.get("Buffer", b""))}


def sd_query_without_read_control(ctx):
    c = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA)
    r = _qsd(c, o["FileId"])
    return {"status": r["status"]}


def sd_query_sacl_without_privilege(ctx):
    c = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | READ_CONTROL)
    r = _qsd(c, o["FileId"], info=SACL)
    return {"status": r["status"]}


def sd_query_buffer_too_small(ctx):
    c = _sess(ctx)
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | READ_CONTROL)
    r = _qsd(c, o["FileId"], length=8)
    return {"status": r["status"]}


def _mode_file(c, name, mask_triplet):
    """Create a file and set a DACL of three allow ACEs with the given masks."""
    o = c.smb2_create(name, create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA
                      | READ_CONTROL | WRITE_DAC)
    cur = _qsd(c, o["FileId"])
    parsed = sdutil.parse(cur.get("Buffer", b""))
    raw = cur.get("Buffer", b"")
    # reuse the owner and group the server itself reported
    rev, sbz1, control, o_owner, o_group, o_sacl, o_dacl = struct.unpack_from("<BBHIIII", raw, 0)
    owner = raw[o_owner:o_owner + 8 + 4 * raw[o_owner + 1]] if o_owner else None
    group = raw[o_group:o_group + 8 + 4 * raw[o_group + 1]] if o_group else None
    aces = [(0, 0, mask_triplet[0], owner),
            (0, 0, mask_triplet[1], group),
            (0, 0, mask_triplet[2], EVERYONE)]
    sd = sdutil.build_sd(owner=owner, group=group, dacl=sdutil.build_dacl(aces))
    # DACL only: replacing the owner needs WRITE_OWNER, which this handle does not
    # hold, and asking for it makes the server refuse the whole SET -- which would
    # leave every access-check case below testing a descriptor that never changed.
    s = c.smb2_set_info(o["FileId"], P.SMB2_0_INFO_SECURITY, 0, sd,
                        additional_information=DACL)
    c.smb2_close(o["FileId"])
    return parsed, s


def sd_set_dacl(ctx):
    c = _sess(ctx)
    before, s = _mode_file(c, "probe-sd-set.bin", (0x00120089, 0x00120089, 0x00120089))
    o = c.smb2_create("probe-sd-set.bin", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA | READ_CONTROL)
    after = _qsd(c, o["FileId"])
    return {"set_status": s["status"], "sd": sdutil.parse(after.get("Buffer", b""))}


def sd_set_without_write_dac(ctx):
    c = _sess(ctx)
    o = c.smb2_create("probe-sd-nodac.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA | READ_CONTROL)
    sd = sdutil.build_sd(dacl=sdutil.build_dacl([(0, 0, 0x001F01FF, EVERYONE)]))
    s = c.smb2_set_info(o["FileId"], P.SMB2_0_INFO_SECURITY, 0, sd,
                        additional_information=DACL)
    return {"status": s["status"]}


def sd_set_owner_without_write_owner(ctx):
    c = _sess(ctx)
    o = c.smb2_create("probe-sd-noowner.bin", create_disposition=P.FILE_OVERWRITE_IF,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA
                      | READ_CONTROL | WRITE_DAC)
    sd = sdutil.build_sd(owner=sdutil.sid("S-1-5-18"))
    s = c.smb2_set_info(o["FileId"], P.SMB2_0_INFO_SECURITY, 0, sd,
                        additional_information=OWNER)
    return {"status": s["status"]}


def dacl_set_then_open(ctx):
    """The descriptor a read-only DACL leaves behind, and what it then permits."""
    c = _sess(ctx)
    _mode_file(c, "probe-acl-ro.bin", (0x00120089, 0x00120089, 0x00120089))
    rd = c.smb2_create("probe-acl-ro.bin", create_disposition=P.FILE_OPEN,
                       desired_access=P.FILE_READ_DATA)
    wr = c.smb2_create("probe-acl-ro.bin", create_disposition=P.FILE_OPEN,
                       desired_access=P.FILE_WRITE_DATA)
    return {"read": rd["status"], "write": wr["status"]}


def dacl_full_then_open(ctx):
    c = _sess(ctx)
    _mode_file(c, "probe-acl-rw.bin", (0x0012019F, 0x0012019F, 0x0012019F))
    rd = c.smb2_create("probe-acl-rw.bin", create_disposition=P.FILE_OPEN,
                       desired_access=P.FILE_READ_DATA)
    wr = c.smb2_create("probe-acl-rw.bin", create_disposition=P.FILE_OPEN,
                       desired_access=P.FILE_WRITE_DATA)
    return {"read": rd["status"], "write": wr["status"]}


def maximal_access_after_restriction(ctx):
    c = _sess(ctx)
    _mode_file(c, "probe-acl-mxac.bin", (0x00120089, 0x00120089, 0x00120089))
    r = c.smb2_create("probe-acl-mxac.bin", create_disposition=P.FILE_OPEN,
                      desired_access=P.FILE_READ_DATA,
                      contexts=[(b"MxAc", b"")])
    mx = None
    for e in r.get("CreateContexts") or []:
        if e.get("NameBytes") == b"MxAc":
            mx = e
    return {"status": r["status"], "MaximalAccess": mx.get("MaximalAccess") if mx else None,
            "QueryStatus": mx.get("QueryStatus") if mx else None}


def maximum_allowed_open_and_write(ctx):
    c = _sess(ctx)
    _mode_file(c, "probe-acl-ma.bin", (0x00120089, 0x00120089, 0x00120089))
    r = c.smb2_create("probe-acl-ma.bin", create_disposition=P.FILE_OPEN,
                      desired_access=0x02000000)   # MAXIMUM_ALLOWED
    w = c.smb2_write(r["FileId"], 0, b"nope") if r["status"] == 0 else {"status": None}
    return {"open": r["status"], "write": w["status"]}


def new_file_descriptor(ctx):
    c = _sess(ctx)
    o = c.smb2_create("probe-sd-new.bin", create_disposition=P.FILE_CREATE,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA | READ_CONTROL)
    r = _qsd(c, o["FileId"])
    return {"status": r["status"], "sd": sdutil.parse(r.get("Buffer", b""))}


def new_directory_descriptor(ctx):
    c = _sess(ctx)
    o = c.smb2_create("probe-sd-dir", create_disposition=P.FILE_CREATE,
                      create_options=P.FILE_DIRECTORY_FILE,
                      desired_access=P.FILE_READ_DATA | READ_CONTROL)
    r = _qsd(c, o["FileId"])
    return {"status": r["status"], "sd": sdutil.parse(r.get("Buffer", b""))}


def create_with_security_descriptor_context(ctx):
    c = _sess(ctx)
    sd = sdutil.build_sd(dacl=sdutil.build_dacl([(0, 0, 0x001F01FF, EVERYONE)]))
    o = c.smb2_create("probe-sd-ctx.bin", create_disposition=P.FILE_CREATE,
                      desired_access=P.FILE_READ_DATA | P.FILE_WRITE_DATA | READ_CONTROL,
                      contexts=[(b"SecD", sd)])
    r = _qsd(c, o["FileId"]) if o["status"] == 0 else {"status": None, "Buffer": b""}
    return {"create": o["status"], "query": r["status"],
            "sd": sdutil.parse(r.get("Buffer", b"")) if r.get("Buffer") else None}


def access_system_security_without_privilege(ctx):
    c = _sess(ctx)
    r = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=ACCESS_SYSTEM_SECURITY)
    return {"status": r["status"]}


def sd_same_over_smb1_and_smb2(ctx):
    """The same file's descriptor, read over SMB1 and over SMB2.

    The two are read by different code paths and nothing else in the set compares
    them. Normalising a machine-local domain SID to its shape (which every other
    case does, because the domain half is a property of the host) would hide a
    server that derived a different domain SID in one path than the other, so
    this case compares the two **raw** descriptors to each other instead of
    either of them to a recording.
    """
    c2 = _sess(ctx)
    o = c2.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                       desired_access=P.FILE_READ_DATA | READ_CONTROL)
    smb2_raw = _qsd(c2, o["FileId"]).get("Buffer", b"")

    c1, _ = ctx.smb1(dialects=["NT LM 0.12"])
    n = c1.smb1_nt_create("\\README.txt", create_disposition=P.FILE_OPEN,
                          desired_access=P.FILE_READ_DATA | READ_CONTROL,
                          share_access=P.FILE_SHARE_READ | P.FILE_SHARE_WRITE)
    obs = {"smb2_len_nonzero": len(smb2_raw) > 0, "smb1_create": n["status"]}
    if n["status"] != 0:
        return obs
    # [MS-CIFS] 2.2.7.6: FID, reserved and the security-information selector are
    # the request PARAMETERS, not the setup words
    r = c1.smb1_nt_transact(6,                       # NT_TRANSACT_QUERY_SECURITY_DESC
                            params=struct.pack("<HHI", n["FID"], 0,
                                               OWNER | GROUP | DACL),
                            max_param=4, max_data=65536)
    smb1_raw = r.get("Data", b"")
    obs["smb1_len_nonzero"] = len(smb1_raw) > 0
    obs["identical"] = bool(smb1_raw) and smb1_raw == smb2_raw
    if not obs["identical"] and smb1_raw and smb2_raw:
        a, b = sdutil.parse(smb1_raw), sdutil.parse(smb2_raw)
        obs["owner_same"] = a.get("Owner") == b.get("Owner")
        obs["group_same"] = a.get("Group") == b.get("Group")
    return obs


CASES = [
    ("sd-query-file", sd_query_file),
    ("sd-same-over-smb1-and-smb2", sd_same_over_smb1_and_smb2),
    ("sd-query-directory", sd_query_directory),
    ("sd-query-owner-only", sd_query_owner_only),
    ("sd-query-dacl-only", sd_query_dacl_only),
    ("sd-query-without-read-control", sd_query_without_read_control),
    ("sd-query-sacl-without-privilege", sd_query_sacl_without_privilege),
    ("sd-query-buffer-too-small", sd_query_buffer_too_small),
    ("sd-set-dacl", sd_set_dacl),
    ("sd-set-without-write-dac", sd_set_without_write_dac),
    ("sd-set-owner-without-write-owner", sd_set_owner_without_write_owner),
    ("dacl-set-then-open", dacl_set_then_open),
    ("dacl-full-then-open", dacl_full_then_open),
    ("maximal-access-after-restriction", maximal_access_after_restriction),
    ("maximum-allowed-open-and-write", maximum_allowed_open_and_write),
    ("new-file-descriptor", new_file_descriptor),
    ("new-directory-descriptor", new_directory_descriptor),
    ("create-with-security-descriptor-context", create_with_security_descriptor_context),
    ("access-system-security-without-privilege", access_system_security_without_privilege),
]

run_all = runner(CASES)
