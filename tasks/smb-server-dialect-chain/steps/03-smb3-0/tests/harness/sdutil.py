"""Decode a self-relative SECURITY_DESCRIPTOR ([MS-DTYP] 2.4.6) into a form two
machines can compare: everything a specification fixes is kept; the identifier
of a local account, which is a property of the host, is reduced to its shape.
"""
import struct

WELL_KNOWN_AUTHORITIES = (1, 2, 3, 5, 16, 22)


def sid_to_string(buf, off):
    rev = buf[off]
    n = buf[off + 1]
    auth = int.from_bytes(buf[off + 2:off + 8], "big")
    subs = [struct.unpack_from("<I", buf, off + 8 + 4 * i)[0] for i in range(n)]
    return "S-%d-%d%s" % (rev, auth, "".join("-%d" % s for s in subs)), 8 + 4 * n


def normalise_sid(s):
    parts = s.split("-")
    if len(parts) < 3:
        return s
    auth = int(parts[2])
    if auth == 5 and len(parts) >= 4 and parts[3] == "21":
        # a machine-local domain SID: keep only its shape and the RID
        return "S-1-5-21-<domain>-%s" % parts[-1]
    if auth in WELL_KNOWN_AUTHORITIES:
        return s
    return "S-1-%d-<local>" % auth


def parse(buf):
    if len(buf) < 20:
        return {"error": "short", "len": len(buf)}
    rev, sbz1, control, o_owner, o_group, o_sacl, o_dacl = struct.unpack_from("<BBHIIII", buf, 0)
    out = {"Revision": rev, "Control": control,
           "owner_present": o_owner != 0, "group_present": o_group != 0,
           "sacl_present": o_sacl != 0, "dacl_present": o_dacl != 0}
    if o_owner:
        s, _ = sid_to_string(buf, o_owner)
        out["Owner"] = normalise_sid(s)
    if o_group:
        s, _ = sid_to_string(buf, o_group)
        out["Group"] = normalise_sid(s)
    if o_dacl:
        out["Dacl"] = parse_acl(buf, o_dacl)
    return out


def parse_acl(buf, off):
    rev, sbz1, size, count, sbz2 = struct.unpack_from("<BBHHH", buf, off)
    aces = []
    p = off + 8
    for _ in range(count):
        if p + 8 > len(buf):
            break
        atype, aflags, asize = struct.unpack_from("<BBH", buf, p)
        mask = struct.unpack_from("<I", buf, p + 4)[0]
        entry = {"Type": atype, "Flags": aflags, "Mask": mask}
        if atype in (0, 1, 2) and p + 8 < len(buf):
            s, _ = sid_to_string(buf, p + 8)
            entry["Sid"] = normalise_sid(s)
        aces.append(entry)
        p += asize if asize else 8
    return {"AclRevision": rev, "AceCount": count, "Aces": aces}


def build_dacl(aces):
    """aces = [(type, flags, mask, sid_bytes)]"""
    body = b""
    for atype, aflags, mask, sid in aces:
        size = 8 + len(sid)
        body += struct.pack("<BBHI", atype, aflags, size, mask) + sid
    return struct.pack("<BBHHH", 2, 0, 8 + len(body), len(aces), 0) + body


def build_sd(owner=None, group=None, dacl=None, control=0x8004):
    parts, off = b"", 20
    o_owner = o_group = o_dacl = 0
    if owner:
        o_owner = off + len(parts)
        parts += owner
    if group:
        o_group = off + len(parts)
        parts += group
    if dacl:
        o_dacl = off + len(parts)
        parts += dacl
    return struct.pack("<BBHIIII", 1, 0, control, o_owner, o_group, 0, o_dacl) + parts


def sid(text):
    parts = text.split("-")
    rev = int(parts[1])
    auth = int(parts[2])
    subs = [int(x) for x in parts[3:]]
    return struct.pack("<BB", rev, len(subs)) + auth.to_bytes(6, "big") \
        + b"".join(struct.pack("<I", s) for s in subs)
