"""Policy probe cases: the same exchanges under different `smb.conf` policies.

The module is run once per scenario; what each case observes is whatever the
configuration in force makes the server do, and the expected value is what
Samba 4.17.12-Debian did under the same configuration.
"""
import struct

import smbprobe as P
from probebase import Ctx, runner

ALL = [0x0202, 0x0210, 0x0300, 0x0302, 0x0311]


def _neg(c, dialects=None):
    return c.smb2_negotiate(dialects or ALL, capabilities=c.client_capabilities)


# --- which dialects are on offer -------------------------------------------


def negotiate_smb1_nt1_only(ctx):
    c = ctx.client()
    r = c.smb1_negotiate(["PC NETWORK PROGRAM 1.0", "LANMAN1.0", "NT LM 0.12"])
    return {"upgraded": bool(r.get("SMB2Upgrade")),
            "DialectIndex": r.get("DialectIndex"),
            "WordCount": r.get("WordCount"), "status": r["status"]}


def negotiate_smb1_multiprotocol(ctx):
    c = ctx.client()
    r = c.smb1_negotiate(["PC NETWORK PROGRAM 1.0", "NT LM 0.12", "SMB 2.002", "SMB 2.???"])
    return {"upgraded": bool(r.get("SMB2Upgrade")),
            "DialectRevision": r.get("DialectRevision"),
            "DialectIndex": r.get("DialectIndex")}


def negotiate_smb2_all(ctx):
    c = ctx.client()
    r = _neg(c)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision")}


def negotiate_smb2_202_only(ctx):
    c = ctx.client()
    r = c.smb2_negotiate([0x0202], capabilities=c.client_capabilities)
    return {"status": r["status"], "DialectRevision": r.get("DialectRevision")}


def negotiate_security_mode(ctx):
    c = ctx.client()
    r = _neg(c)
    m = r.get("SecurityMode", 0)
    return {"signing_enabled": bool(m & 0x01), "signing_required": bool(m & 0x02)}


# --- signing policy ---------------------------------------------------------


def session_then_unsigned_request(ctx):
    c = ctx.client()
    _neg(c)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(ctx.share)
    body = struct.pack("<HH", 4, 0)
    mid = c.message_id
    pdu = c.smb2_header(P.SMB2_ECHO, flags=0, session_id=c.session_id,
                        tree_id=c.tree_id, message_id=mid)
    c.message_id += 1
    c.send_raw(pdu + body)
    try:
        r = c.smb2_receive_reply(mid)
        return {"tree": t["status"], "unsigned_echo": r["status"]}
    except P.SmbProbeError:
        return {"tree": t["status"], "unsigned_echo": "connection-closed"}


def session_signed_request(ctx):
    c = ctx.client()
    c.verify_signatures = True
    _neg(c)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(ctx.share)
    e = c.smb2_echo()
    return {"tree": t["status"], "echo": e["status"],
            "echo_signed": bool(e.get("Flags", 0) & 0x08)}


# --- encryption policy ------------------------------------------------------


def tree_connect_share_flags(ctx):
    c = ctx.client()
    _neg(c)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(ctx.share)
    return {"status": t["status"],
            "encrypt_data": bool(t.get("ShareFlags", 0) & 0x00008000)}


def tree_connect_encrypted_share(ctx):
    c = ctx.client()
    _neg(c)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect("enc")
    return {"status": t["status"],
            "encrypt_data": bool(t.get("ShareFlags", 0) & 0x00008000)}


def client_without_encryption(ctx):
    c = ctx.client()
    c.smb2_negotiate(ALL, capabilities=0)
    c.smb2_session_setup(ctx.user, ctx.password)
    a = c.smb2_tree_connect(ctx.share)
    b = c.smb2_tree_connect("enc")
    return {"tmp": a["status"], "enc": b["status"]}


def session_flags_encrypt_data(ctx):
    c = ctx.client()
    _neg(c)
    r = c.smb2_session_setup(ctx.user, ctx.password)
    return {"status": r["status"], "SessionFlags": r.get("SessionFlags")}


# --- account policy ---------------------------------------------------------


def unknown_user(ctx):
    c = ctx.client()
    _neg(c)
    r = c.smb2_session_setup("nosuchuser", "whatever")
    return {"status": r["status"], "SessionFlags": r.get("SessionFlags")}


def bad_password(ctx):
    c = ctx.client()
    _neg(c)
    r = c.smb2_session_setup(ctx.user, "wrong-password")
    return {"status": r["status"]}


def good_user(ctx):
    c = ctx.client()
    _neg(c)
    r = c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect(ctx.share)
    return {"session": r["status"], "SessionFlags": r.get("SessionFlags"),
            "tree": t["status"]}


def readonly_share_write(ctx):
    c = ctx.client()
    _neg(c)
    c.smb2_session_setup(ctx.user, ctx.password)
    t = c.smb2_tree_connect("ro")
    o = c.smb2_create("nope.bin", create_disposition=P.FILE_CREATE)
    return {"tree": t["status"], "create": o["status"],
            "MaximalAccess": t.get("MaximalAccess")}


# --- the policy matrix in more detail --------------------------------------


def _session(ctx, dialects=None, user=None, password=None, caps=None,
             verify=True):
    c = ctx.client()
    c.verify_signatures = verify
    if caps is not None:
        c.client_capabilities = caps
    n = c.smb2_negotiate(dialects or ALL, capabilities=c.client_capabilities)
    ss = c.smb2_session_setup(user if user is not None else ctx.user,
                              password if password is not None else ctx.password)
    return c, n, ss


def max_protocol_clamp(ctx):
    """A client offering every dialect gets exactly what the policy allows."""
    c = ctx.client()
    r = _neg(c)
    c2 = ctx.client()
    r2 = c2.smb2_negotiate([0x0311], capabilities=c2.client_capabilities)
    c3 = ctx.client()
    r3 = c3.smb2_negotiate([0x0202], capabilities=c3.client_capabilities)
    return {"all": r.get("DialectRevision"), "all_status": r["status"],
            "only_0311": r2.get("DialectRevision"), "only_0311_status": r2["status"],
            "only_0202": r3.get("DialectRevision"), "only_0202_status": r3["status"]}


def negotiate_smb1_nt1_then_use(ctx):
    """Whether an SMB1 session can actually be established under this policy."""
    c = ctx.client()
    n = c.smb1_negotiate(["PC NETWORK PROGRAM 1.0", "LANMAN1.0", "NT LM 0.12"])
    obs = {"upgraded": bool(n.get("SMB2Upgrade")), "neg_status": n["status"]}
    if n.get("SMB2Upgrade") or n["status"] != 0 or n.get("DialectIndex") in (None, 0xFFFF):
        return obs
    try:
        ss = c.smb1_session_setup(ctx.user, ctx.password)
        obs["session"] = ss["status"]
        t = c.smb1_tree_connect(ctx.share)
        obs["tree"] = t["status"]
    except Exception:
        obs["session"] = "error"
    return obs


def session_setup_flags(ctx):
    """SessionFlags and the security mode the session setup reports."""
    c, n, ss = _session(ctx)
    return {"status": ss["status"], "SessionFlags": ss.get("SessionFlags"),
            "neg_SecurityMode": n.get("SecurityMode"),
            "dialect": n.get("DialectRevision")}


def unsigned_request_after_setup(ctx):
    """An unsigned request on an established session.

    Asked at 0x0210, because from 0x0311 the pre-auth integrity chain makes
    signing mandatory whatever `server signing` says, and the answer would then
    be the same under every policy.
    """
    try:
        c, n, ss = _session(ctx, dialects=[0x0202, 0x0210], verify=False)
    except Exception:
        return {"status": "error"}
    if ss["status"] != 0:
        return {"dialect": n.get("DialectRevision"), "session": ss["status"]}
    c.signing_required = False
    obs = {"dialect": n.get("DialectRevision")}
    try:
        t = c.smb2_tree_connect(ctx.share)
    except Exception:
        # a policy that requires encryption leaves a session with no usable key,
        # so there is nothing to send the tree connect on; record the shape, not
        # the exception, which would pin this file's own line numbers
        obs["tree"] = "unusable-session"
        return obs
    obs["tree"] = t["status"]
    if t["status"] == 0:
        try:
            e = c.smb2_command(P.SMB2_ECHO, struct.pack("<HH", 4, 0), sign=False)
        except Exception:
            obs["unsigned_echo"] = "disconnected"
        else:
            obs["unsigned_echo"] = e["status"]
    return obs


def guest_fallback(ctx):
    """An unknown user under `map to guest`, and what that session may reach.

    A guest session has no session key, so the tree connects are sent unsigned:
    signing them with a key derived from the password that was rejected would
    make every tree connect fail for that reason and the case would say nothing
    about `guest ok`.
    """
    try:
        c, n, ss = _session(ctx, user="definitely-not-a-user", password="x",
                            verify=False)
    except Exception:
        return {"status": "error"}
    obs = {"status": ss["status"], "SessionFlags": ss.get("SessionFlags"),
           "is_guest": bool((ss.get("SessionFlags") or 0) & 0x0001)}
    if ss["status"] == 0:
        c.signing_required = False
        for label, share in (("tree_guest_ok", ctx.share), ("tree_no_guest", "ro")):
            try:
                obs[label] = c.smb2_tree_connect(share, sign=False)["status"]
            except Exception:
                obs[label] = "disconnected"
    return obs


def anonymous_session(ctx):
    """An empty-credential session setup."""
    try:
        c, n, ss = _session(ctx, user="", password="", verify=False)
    except Exception:
        return {"status": "error"}
    return {"status": ss["status"], "SessionFlags": ss.get("SessionFlags"),
            "is_null": bool((ss.get("SessionFlags") or 0) & 0x0002)}


def tree_connect_ipc(ctx):
    c, n, ss = _session(ctx)
    if ss["status"] != 0:
        return {"session": ss["status"]}
    t = c.smb2_tree_connect("IPC$")
    return {"status": t["status"], "ShareType": t.get("ShareType"),
            "ShareFlags": t.get("ShareFlags"),
            "encrypt_data": bool((t.get("ShareFlags") or 0) & 0x00008000)}


def plain_share_encryption(ctx):
    """Whether a share with no `smb encrypt` of its own is encrypted, which the
    global policy decides."""
    c, n, ss = _session(ctx)
    if ss["status"] != 0:
        return {"session": ss["status"]}
    t = c.smb2_tree_connect(ctx.share)
    obs = {"tree": t["status"],
           "encrypt_data": bool((t.get("ShareFlags") or 0) & 0x00008000),
           "session_encrypt": bool((ss.get("SessionFlags") or 0) & 0x0004)}
    if t["status"] == 0:
        o = c.smb2_create("probe-policy.bin", create_disposition=P.FILE_OVERWRITE_IF)
        obs["create"] = o["status"]
        obs["create_encrypted"] = bool(o.get("Encrypted"))
        if o["status"] == 0:
            c.smb2_close(o["FileId"])
    return obs


def readonly_share_mkdir_and_delete(ctx):
    c, n, ss = _session(ctx)
    if ss["status"] != 0:
        return {"session": ss["status"]}
    t = c.smb2_tree_connect("ro")
    obs = {"tree": t["status"], "MaximalAccess": t.get("MaximalAccess")}
    if t["status"] != 0:
        return obs
    d = c.smb2_create("probe-ro-dir", create_disposition=P.FILE_CREATE,
                      create_options=P.FILE_DIRECTORY_FILE)
    obs["mkdir"] = d["status"]
    o = c.smb2_create("README.txt", create_disposition=P.FILE_OPEN,
                      desired_access=P.DELETE | P.FILE_READ_ATTRIBUTES,
                      create_options=0x00001000)      # FILE_DELETE_ON_CLOSE
    obs["delete_on_close_open"] = o["status"]
    return obs


CASES = [
    ("negotiate-smb1-nt1-only", negotiate_smb1_nt1_only),
    ("negotiate-smb1-multiprotocol", negotiate_smb1_multiprotocol),
    ("negotiate-smb2-all", negotiate_smb2_all),
    ("negotiate-smb2-202-only", negotiate_smb2_202_only),
    ("negotiate-security-mode", negotiate_security_mode),
    ("session-then-unsigned-request", session_then_unsigned_request),
    ("session-signed-request", session_signed_request),
    ("tree-connect-share-flags", tree_connect_share_flags),
    ("tree-connect-encrypted-share", tree_connect_encrypted_share),
    ("client-without-encryption", client_without_encryption),
    ("session-flags-encrypt-data", session_flags_encrypt_data),
    ("unknown-user", unknown_user),
    ("bad-password", bad_password),
    ("good-user", good_user),
    ("readonly-share-write", readonly_share_write),
    ("max-protocol-clamp", max_protocol_clamp),
    ("negotiate-smb1-nt1-then-use", negotiate_smb1_nt1_then_use),
    ("session-setup-flags", session_setup_flags),
    ("unsigned-request-after-setup", unsigned_request_after_setup),
    ("guest-fallback", guest_fallback),
    ("anonymous-session", anonymous_session),
    ("tree-connect-ipc", tree_connect_ipc),
    ("plain-share-encryption", plain_share_encryption),
    ("readonly-share-mkdir-and-delete", readonly_share_mkdir_and_delete),
]

run_all = runner(CASES)
