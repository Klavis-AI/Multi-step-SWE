"""Step 3 - end-to-end encryption: device keys, one-time and fallback keys,
key queries and claims, device-list tracking, cross-signing, server-side room
key backups and send-to-device messaging.

Everything here is observed through the Client-Server API.  No real Olm or
Megolm session is ever established: the specification does not make a
homeserver understand the key material, it makes it *store, count, hand out and
account for* opaque key objects, and that is what these scenarios pin.

Key material is therefore made up, but made up *deterministically*: every
public key, one-time key and signature below is derived from a fixed label, so
two recordings of the same scenario carry byte-identical bodies.  The one place
where real cryptography is unavoidable is cross-signing: the reference verifies
that a self-signing or user-signing key is signed by the accompanying master
key, so the three cross-signing keys are real Ed25519 keys from fixed seeds and
the signatures are computed here (Ed25519 is deterministic, so the signature
over the same object is always the same string).  The harness replaces every
`signatures` subtree with a single opaque marker, so no signature value is ever
compared - only whether the server accepted or rejected it.
"""

from __future__ import annotations

import base64
import hashlib
import json
import time

from mxconf.mx import CS, User, join, login, ok, q, register
from mxconf.registry import scenario

# Every room these scenarios create is created at this version, explicitly, so
# the server's default room version cannot move the transcript.
V = "9"

ALGS = ["m.olm.v1.curve25519-aes-sha2", "m.megolm.v1.aes-sha2"]
BACKUP_ALG = "m.megolm_backup.v1.curve25519-aes-sha2"


# --------------------------------------------------------------------------- #
# Ed25519 (RFC 8032), pure Python
#
# Only cross-signing needs it, and it needs it at run time because the object
# being signed carries the user id, which carries the server name, which
# carries the port this recording happened to get.  The implementation is the
# reference one from the RFC; `_selftest` checks it against the RFC's own test
# vector on import.
# --------------------------------------------------------------------------- #
_P = 2**255 - 19
_L = 2**252 + 27742317777372353535851937790883648493
_D = -121665 * pow(121666, _P - 2, _P) % _P
_I = pow(2, (_P - 1) // 4, _P)


def _inv(x: int) -> int:
    return pow(x, _P - 2, _P)


def _xrecover(y: int) -> int:
    xx = (y * y - 1) * _inv(_D * y * y + 1)
    x = pow(xx, (_P + 3) // 8, _P)
    if (x * x - xx) % _P != 0:
        x = (x * _I) % _P
    if x % 2 != 0:
        x = _P - x
    return x


_By = 4 * _inv(5) % _P
_Bx = _xrecover(_By)
_B = (_Bx, _By, 1, _Bx * _By % _P)


def _pt_add(p, q):
    """Unified addition in extended coordinates (a = -1), so it doubles too."""
    x1, y1, z1, t1 = p
    x2, y2, z2, t2 = q
    a = (y1 - x1) * (y2 - x2) % _P
    b = (y1 + x1) * (y2 + x2) % _P
    c = t1 * 2 * _D * t2 % _P
    d = z1 * 2 * z2 % _P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % _P, g * h % _P, f * g % _P, e * h % _P)


def _pt_mul(p, e: int):
    q = (0, 1, 1, 0)
    while e > 0:
        if e & 1:
            q = _pt_add(q, p)
        p = _pt_add(p, p)
        e >>= 1
    return q


def _pt_enc(p) -> bytes:
    x, y, z, _t = p
    zi = _inv(z)
    x = x * zi % _P
    y = y * zi % _P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


def _clamp(h32: bytes) -> int:
    a = int.from_bytes(h32, "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a


def ed25519_public(seed: bytes) -> bytes:
    return _pt_enc(_pt_mul(_B, _clamp(hashlib.sha512(seed).digest()[:32])))


def ed25519_sign(seed: bytes, public: bytes, msg: bytes) -> bytes:
    h = hashlib.sha512(seed).digest()
    a = _clamp(h[:32])
    r = int.from_bytes(hashlib.sha512(h[32:] + msg).digest(), "little") % _L
    rr = _pt_enc(_pt_mul(_B, r))
    k = int.from_bytes(hashlib.sha512(rr + public + msg).digest(), "little") % _L
    return rr + ((r + k * a) % _L).to_bytes(32, "little")


def _selftest() -> None:
    seed = bytes.fromhex(
        "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb")
    pub = ed25519_public(seed)
    assert pub.hex() == (
        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c")
    assert ed25519_sign(seed, pub, bytes([0x72])).hex() == (
        "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
        "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")


_selftest()


# --------------------------------------------------------------------------- #
# Deterministic made-up key material
# --------------------------------------------------------------------------- #
def b64(data: bytes) -> str:
    """Unpadded base64, the encoding the specification uses for key material."""
    return base64.b64encode(data).decode("ascii").rstrip("=")


def material(label: str, size: int = 32) -> str:
    """A fixed, made-up public key / signature for `label`.

    Deriving it from a label rather than from randomness is what makes a
    transcript reproducible: the same scenario always uploads the same bytes.
    """
    out = b""
    i = 0
    while len(out) < size:
        out += hashlib.sha512(f"mxconf/e2ee/{label}/{i}".encode()).digest()
        i += 1
    return b64(out[:size])


def seed_for(label: str) -> bytes:
    return hashlib.sha256(f"mxconf/e2ee/seed/{label}".encode()).digest()


def canonical(obj) -> bytes:
    """Canonical JSON as [Signing JSON] defines it, for the signature input."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False).encode("utf-8")


def sign_json(obj: dict, user_id: str, key_id: str, seed: bytes,
              public: bytes) -> dict:
    """Add `signatures[user_id][key_id]` over the object, per Signing JSON."""
    unsigned = {k: v for k, v in obj.items() if k not in ("signatures", "unsigned")}
    sig = b64(ed25519_sign(seed, public, canonical(unsigned)))
    out = dict(obj)
    sigs = {u: dict(v) for u, v in (obj.get("signatures") or {}).items()}
    sigs.setdefault(user_id, {})[key_id] = sig
    out["signatures"] = sigs
    return out


def device_keys(user: User, device_id: str, variant: str = "a") -> dict:
    """A `DeviceKeys` object: the identity keys of one device."""
    return {
        "user_id": user.user_id,
        "device_id": device_id,
        "algorithms": list(ALGS),
        "keys": {
            f"curve25519:{device_id}": material(f"curve/{device_id}/{variant}"),
            f"ed25519:{device_id}": material(f"ed/{device_id}/{variant}"),
        },
        "signatures": {
            user.user_id: {
                f"ed25519:{device_id}": material(f"devsig/{device_id}/{variant}", 64)
            }
        },
    }


def otk(user: User, device_id: str, name: str) -> dict:
    """A signed one-time key object."""
    return {
        "key": material(f"otk/{device_id}/{name}"),
        "signatures": {
            user.user_id: {
                f"ed25519:{device_id}": material(f"otksig/{device_id}/{name}", 64)
            }
        },
    }


def fallback(user: User, device_id: str, name: str) -> dict:
    out = otk(user, device_id, name)
    out["fallback"] = True
    return out


def cross_signing_keys(user: User, tag: str = "1") -> dict:
    """A master key plus a self-signing and a user-signing key it signs.

    Returns the `POST /keys/device_signing/upload` body together with the
    seeds, so that a later `/keys/signatures/upload` can sign with them.
    """
    out = {}
    for usage in ("master", "self_signing", "user_signing"):
        seed = seed_for(f"{tag}/{usage}/{user.localpart}")
        public = ed25519_public(seed)
        key_id = "ed25519:" + b64(public)
        out[usage] = {
            "seed": seed,
            "public": public,
            "key_id": key_id,
            "key": {"user_id": user.user_id, "usage": [usage],
                    "keys": {key_id: b64(public)}},
        }
    master = out["master"]
    for usage in ("self_signing", "user_signing"):
        out[usage]["key"] = sign_json(out[usage]["key"], user.user_id,
                                      master["key_id"], master["seed"],
                                      master["public"])
    out["body"] = {
        "master_key": out["master"]["key"],
        "self_signing_key": out["self_signing"]["key"],
        "user_signing_key": out["user_signing"]["key"],
    }
    return out


# --------------------------------------------------------------------------- #
# Request helpers
# --------------------------------------------------------------------------- #
def upload_keys(c, token, *, keys=None, one_time=None, fallback_keys=None,
                check=False, label=None, **kw):
    body = {}
    if keys is not None:
        body["device_keys"] = keys
    if one_time is not None:
        body["one_time_keys"] = one_time
    if fallback_keys is not None:
        body["fallback_keys"] = fallback_keys
    return c.req("POST", f"{CS}/keys/upload", token=token, json_body=body,
                 check=check, label=label, **kw)


def query_keys(c, token, wanted, *, check=True, label=None, **kw):
    return c.req("POST", f"{CS}/keys/query", token=token,
                 json_body={"device_keys": wanted}, check=check, label=label, **kw)


def claim_keys(c, token, wanted, *, check=True, label=None, **kw):
    return c.req("POST", f"{CS}/keys/claim", token=token,
                 json_body={"one_time_keys": wanted}, check=check, label=label, **kw)


def syncer(c, token):
    """A `/sync` caller that never issues the same request twice.

    The reference caches a `/sync` response for two minutes under a key that
    includes the device, the `since` token and the `timeout`, so repeating a
    request returns the first response again instead of observing the server a
    second time.  Every call here therefore gets its own timeout, which keeps
    the requests distinct; `wait` additionally turns a call into a long poll,
    so a scenario can ask for the consequence of something that has just
    happened without racing it.
    """
    # Two counters: scored requests take their nonce from the first, unscored
    # settling polls from the second.  Sharing one would let a candidate that
    # needs a different number of polls than the reference build a *different
    # URL* for the scored request that follows, which is a difference in the
    # scenario's bookkeeping rather than in the server.
    state = {"scored": 0, "quiet": 0}

    def do(*, since=None, wait=0, check=False, label=None, **kw):
        if check or label:
            state["scored"] += 1
            nonce = state["scored"]
        else:
            state["quiet"] += 1
            nonce = 500 + state["quiet"]
        return c.req("GET", f"{CS}/sync", token=token,
                     query={"timeout": wait + nonce, "since": since},
                     check=check, label=label, **kw)

    return do


def encrypted_room(c, user):
    """A room with end-to-end encryption turned on, at a pinned room version.

    `device_lists` tracks users you share an *encrypted* room with, so the
    room these scenarios share has to be one.
    """
    r = c.req("POST", f"{CS}/createRoom", token=user.token, json_body={
        "room_version": V,
        "preset": "public_chat",
        "initial_state": [{"type": "m.room.encryption", "state_key": "",
                           "content": {"algorithm": "m.megolm.v1.aes-sha2"}}],
    }, check=False, label="createRoom")
    ok(c, r, "createRoom")
    return (r.json or {})["room_id"]


def backup_auth_data(tag: str = "1") -> dict:
    return {"public_key": material(f"backup/{tag}")}


def create_backup(c, token, tag="1", *, check=False, label=None):
    r = c.req("POST", f"{CS}/room_keys/version", token=token,
              json_body={"algorithm": BACKUP_ALG,
                         "auth_data": backup_auth_data(tag)},
              check=check, label=label)
    ok(c, r, "create backup version")
    return (r.json or {})["version"]


def session_key(name, *, index=1, forwarded=0, verified=False) -> dict:
    """A `KeyBackupData`: opaque session data plus the three metadata fields
    the replacement rules are decided on."""
    return {
        "first_message_index": index,
        "forwarded_count": forwarded,
        "is_verified": verified,
        "session_data": {
            "ephemeral": material(f"eph/{name}"),
            "ciphertext": material(f"ct/{name}", 48),
            "mac": material(f"mac/{name}", 16),
        },
    }


def put_key(c, token, version, room, session, data, *, check=True, label=None):
    # The etag is "an opaque string": what it *is* belongs to the
    # implementation, so it is dropped here and only its movement is scored,
    # in the scenario devoted to it.
    return c.req("PUT", f"{CS}/room_keys/keys/{q(room)}/{q(session)}",
                 token=token, query={"version": version}, json_body=data,
                 check=check, label=label, drop=("etag",))


def get_key(c, token, version, room, session, *, check=True, label=None):
    return c.req("GET", f"{CS}/room_keys/keys/{q(room)}/{q(session)}",
                 token=token, query={"version": version}, check=check,
                 label=label)


def poll(make, ready, tries: int = 80, delay: float = 0.05):
    """Repeat an *unscored* request until the server has caught up.

    Nothing here is recorded, so a scenario that needs a background stream
    (device-list updates, send-to-device delivery) to have landed can wait for
    it without the waiting becoming part of the transcript.  The scored request
    that follows then observes a settled server on every run.
    """
    r = make()
    for _ in range(tries):
        if ready(r):
            return r
        time.sleep(delay)
        r = make()
    return r


# --------------------------------------------------------------------------- #
# Derivations: the facts a response must carry, without its volatile parts
# --------------------------------------------------------------------------- #
def claimed(b):
    """`/keys/claim` keyed by user and device.

    The published schema stops one level short of the key object, so a
    projected body would compare an empty object; the derivation keeps the key
    ids and the key objects themselves, which is the point of the call.
    """
    return {
        "one_time_keys": b.get("one_time_keys") or {},
        "failures": b.get("failures") or {},
    }


def claimed_ids(b):
    """Just the key ids that came back, per user and device."""
    return {
        u: {d: sorted(keys) for d, keys in sorted(devs.items())}
        for u, devs in sorted((b.get("one_time_keys") or {}).items())
    }


def one_claimed_id(b):
    ids = [k for devs in (b.get("one_time_keys") or {}).values()
           for keys in devs.values() for k in keys]
    return ids[0] if len(ids) == 1 else None


OTK_ALGS = ("signed_curve25519", "curve25519")


def otk_counts(b, algs=OTK_ALGS):
    """The counts for the algorithms a scenario uploaded, absent read as zero.

    "If an algorithm is not listed, the count for that algorithm is zero", so a
    server that omits a zero and one that reports it are saying the same thing,
    and which *other* algorithms a server chooses to name is its own business.
    Only the algorithms these scenarios actually upload are compared.
    """
    counts = b.get("one_time_key_counts") or {}
    return {a: int(counts.get(a) or 0) for a in algs}


def sync_e2ee(b):
    """The end-to-end-encryption extensions to `/sync`, and nothing else."""
    return {
        "device_one_time_keys_count": {
            a: int((b.get("device_one_time_keys_count") or {}).get(a) or 0)
            for a in OTK_ALGS},
        "device_unused_fallback_key_types": sorted(
            b.get("device_unused_fallback_key_types") or []),
    }


def sync_device_lists(b):
    lists = b.get("device_lists") or {}
    return {
        "device_lists": {
            "changed": sorted(lists.get("changed") or []),
            "left": sorted(lists.get("left") or []),
        }
    }


def sync_to_device(b):
    return {
        "to_device": {
            "events": [
                {"type": e.get("type"), "sender": e.get("sender"),
                 "content": e.get("content")}
                for e in ((b.get("to_device") or {}).get("events") or [])
            ]
        }
    }


def changes(b):
    return {"changed": sorted(b.get("changed") or []),
            "left": sorted(b.get("left") or [])}


def failure_codes(b):
    """`failures` reduced to its error codes.

    The reference puts an HTTP status and a human-readable message in each
    failure; neither is specified, only the `errcode` is.
    """
    return {
        "failures": {
            u: {k: {"errcode": (v or {}).get("errcode")}
                for k, v in sorted(entries.items())}
            for u, entries in sorted((b.get("failures") or {}).items())
        }
    }


def device_key_shape(b):
    """`/keys/query` device keys, with the signature subtrees left out.

    A user with no device keys at all is dropped rather than recorded as an
    empty map: the specification says an unknown user or device is "missing
    from the `device_keys` result", while the reference lists the user with an
    empty object, and neither reading is worth failing a homeserver over.
    """
    out = {}
    for uid, devs in sorted((b.get("device_keys") or {}).items()):
        if not devs:
            continue
        out[uid] = {}
        for did, info in sorted(devs.items()):
            out[uid][did] = {
                "user_id": info.get("user_id"),
                "device_id": info.get("device_id"),
                "algorithms": info.get("algorithms"),
                "keys": info.get("keys"),
                "display_name": (info.get("unsigned") or {}).get(
                    "device_display_name"),
            }
    return out


def cross_signing_shape(b):
    """Which cross-signing keys `/keys/query` returned, and their usage."""
    def one(section):
        return {
            uid: {"usage": key.get("usage"), "keys": key.get("keys"),
                  "user_id": key.get("user_id")}
            for uid, key in sorted((b.get(section) or {}).items())
        }

    return {
        "master_keys": one("master_keys"),
        "self_signing_keys": one("self_signing_keys"),
        "user_signing_keys": one("user_signing_keys"),
        "device_ids": sorted(
            d for devs in (b.get("device_keys") or {}).values() for d in devs),
    }



# --------------------------------------------------------------------------- #
# POST /keys/upload
# --------------------------------------------------------------------------- #
@scenario("e2ee/keys-upload-device-and-one-time-keys", tags=["e2ee", "keys"], step=3)
def keys_upload_device_and_otks(c):
    u = register(c, "ku1", device_id="KUONE", display_name="upload device")
    s = syncer(c, u.token)
    upload_keys(c, u.token, keys=device_keys(u, "KUONE"),
                label="upload device keys and no one-time keys", check=True,
                derive=otk_counts)
    upload_keys(c, u.token, one_time={
        "signed_curve25519:AAAAAA": otk(u, "KUONE", "a"),
        "signed_curve25519:AAAAAB": otk(u, "KUONE", "b"),
    }, label="add two one-time keys", check=True, derive=otk_counts)
    upload_keys(c, u.token, one_time={
        "signed_curve25519:AAAAAC": otk(u, "KUONE", "c"),
        "signed_curve25519:AAAAAD": otk(u, "KUONE", "d"),
        "signed_curve25519:AAAAAE": otk(u, "KUONE", "e"),
    }, label="add three more, the count accumulates", check=True,
       derive=otk_counts)
    upload_keys(c, u.token, one_time={"curve25519:BBBBBA": material("plain/a")},
                label="a second algorithm is counted separately", check=True,
                derive=otk_counts)
    upload_keys(c, u.token, label="an upload with nothing new in it", check=True,
                derive=otk_counts)
    query_keys(c, u.token, {u.user_id: []},
               label="the identity keys come back from /keys/query",
               derive=device_key_shape)
    s(check=True, label="/sync reports the same counts", derive=sync_e2ee)


@scenario("e2ee/keys-upload-same-device-key-again", tags=["e2ee", "keys"], step=3)
def keys_upload_same_device_key_again(c):
    """"If the uploaded keys are the same as the existing keys, the device list
    is not considered to have changed"." """
    u = register(c, "ku2", device_id="KUTWO", display_name="stable device")
    dk = device_keys(u, "KUTWO")
    s = syncer(c, u.token)
    upload_keys(c, u.token, keys=dk, label="upload device keys", check=True)
    settled = s()
    since = (settled.json or {})["next_batch"]
    upload_keys(c, u.token, keys=dk, label="upload exactly the same keys again",
                check=True)
    s(since=since, wait=1000, check=True,
      label="re-uploading identical keys moves nobody's device list",
      derive=sync_device_lists)
    c.req("GET", f"{CS}/keys/changes", token=u.token,
          query={"from": since, "to": (s().json or {})["next_batch"]},
          label="and /keys/changes agrees that nothing moved",
          derive=lambda b: {"changed": sorted(b.get("changed") or []),
                            "left": sorted(b.get("left") or [])})
    query_keys(c, u.token, {u.user_id: []},
               label="one device, with the keys that were uploaded",
               derive=device_key_shape)
    upload_keys(c, u.token, keys=dk,
                one_time={"signed_curve25519:AAAAAA": otk(u, "KUTWO", "a")},
                label="the same keys once more, with a one-time key", check=True)
    query_keys(c, u.token, {u.user_id: []},
               label="still one device, still the same keys",
               derive=device_key_shape)


@scenario("e2ee/keys-upload-different-device-key", tags=["e2ee", "keys"], step=3)
def keys_upload_different_device_key(c):
    u = register(c, "ku3", device_id="KUTHREE", display_name="changing device")
    first = device_keys(u, "KUTHREE", "a")
    second = device_keys(u, "KUTHREE", "b")
    upload_keys(c, u.token, keys=first)
    upload_keys(c, u.token, keys=second,
                label="upload a different identity key for the same device",
                check=True)
    # Both outcomes are recorded as facts rather than as one asserted value:
    # what a homeserver does with a replacement identity key is exactly the
    # thing under test, so the derivation says which of the two uploads is
    # being served rather than assuming.
    query_keys(c, u.token, {u.user_id: []},
               label="which identity key the server now serves",
               derive=lambda b, f=first, s=second, uid=u.user_id: {
                   "devices": sorted((b.get("device_keys") or {}).get(uid, {})),
                   "serves_the_first_upload": (
                       (b.get("device_keys") or {}).get(uid, {})
                       .get("KUTHREE", {}).get("keys") == f["keys"]),
                   "serves_the_second_upload": (
                       (b.get("device_keys") or {}).get(uid, {})
                       .get("KUTHREE", {}).get("keys") == s["keys"]),
               })
    c.req("GET", f"{CS}/devices/KUTHREE", token=u.token,
          label="the device record itself is untouched",
          derive=lambda b: {"device_id": b.get("device_id"),
                            "display_name": b.get("display_name")})
    upload_keys(c, u.token, keys=second,
                label="re-uploading whatever is current is accepted", check=True)
    query_keys(c, u.token, {u.user_id: []},
               label="and one device is still all there is",
               derive=device_key_shape)


@scenario("e2ee/keys-upload-one-time-keys-only", tags=["e2ee", "keys"], step=3)
def keys_upload_one_time_keys_only(c):
    u = register(c, "ku4", device_id="KUFOUR", display_name="otk only")
    s = syncer(c, u.token)
    upload_keys(c, u.token,
                one_time={"signed_curve25519:AAAAAA": otk(u, "KUFOUR", "a")},
                label="one-time keys without ever uploading identity keys",
                check=True)
    query_keys(c, u.token, {u.user_id: []},
               label="a device with no identity keys is not in /keys/query",
               derive=device_key_shape)
    s(check=True, label="the count is still reported to its own device",
      derive=sync_e2ee)
    claim_keys(c, u.token, {u.user_id: {"KUFOUR": "signed_curve25519"}},
               label="the one-time key can still be claimed", derive=claimed)
    upload_keys(c, u.token, label="and the count is back to nothing", check=True)


@scenario("e2ee/keys-upload-duplicate-one-time-key-id", tags=["e2ee", "keys"],
          step=3)
def keys_upload_duplicate_otk_id(c):
    u = register(c, "ku5", device_id="KUFIVE", display_name="duplicate ids")
    upload_keys(c, u.token, keys=device_keys(u, "KUFIVE"))
    same = otk(u, "KUFIVE", "a")
    upload_keys(c, u.token, one_time={"signed_curve25519:AAAAAA": same},
                label="upload a one-time key", check=True)
    upload_keys(c, u.token, one_time={"signed_curve25519:AAAAAA": same},
                label="the identical key under the identical id", check=True)
    # A *different* key under an id already in use: the release documents only
    # a 200 for this endpoint and never says what a server does with it.  The
    # reference refuses it; ignoring it or replacing it are equally inside the
    # release.  So the upload is issued unscored and what *is* scored is the
    # outcome every reading has to agree on: the id is served once, and the key
    # served under it is one of the two that were uploaded under it.
    second = otk(u, "KUFIVE", "b")
    upload_keys(c, u.token,
                one_time={"signed_curve25519:AAAAAA": second}, check=False)
    upload_keys(c, u.token, label="the count after that upload", check=True,
                derive=otk_counts)
    claim_keys(c, u.token, {u.user_id: {"KUFIVE": "signed_curve25519"}},
               label="and the key that is served",
               derive=lambda b, a=same, z=second: {
                   "key_ids": claimed_ids(b),
                   "is_one_of_the_two_uploaded_under_that_id": [
                       v for devs in (b.get("one_time_keys") or {}).values()
                       for keys in devs.values() for v in keys.values()
                   ] in ([a], [z]),
               })


@scenario("e2ee/keys-upload-fallback-key", tags=["e2ee", "keys", "fallback"],
          step=3)
def keys_upload_fallback_key(c):
    u = register(c, "ku6", device_id="KUSIX", display_name="fallback device")
    s = syncer(c, u.token)
    upload_keys(c, u.token, keys=device_keys(u, "KUSIX"))
    upload_keys(c, u.token,
                fallback_keys={"signed_curve25519:FBAAAA": fallback(u, "KUSIX", "f1")},
                label="a fallback key is not counted as a one-time key", check=True)
    s(check=True, label="/sync advertises the unused fallback key type",
      derive=sync_e2ee)
    upload_keys(c, u.token,
                fallback_keys={"signed_curve25519:FBAAAB": fallback(u, "KUSIX", "f2")},
                label="replacing the fallback key", check=True)
    claim_keys(c, u.token, {u.user_id: {"KUSIX": "signed_curve25519"}},
               label="with no one-time keys left, the fallback key is served",
               derive=claimed)
    s(check=True, label="the fallback key type after the key has been used",
      derive=sync_e2ee)
    claim_keys(c, u.token, {u.user_id: {"KUSIX": "signed_curve25519"}},
               label="and the same fallback key is served again", derive=claimed)


# --------------------------------------------------------------------------- #
# POST /keys/query
# --------------------------------------------------------------------------- #
@scenario("e2ee/keys-query-self-and-other-user", tags=["e2ee", "query"], step=3)
def keys_query_self_and_other(c):
    u = register(c, "kq1", device_id="KQONE", display_name="mine")
    v = register(c, "kq2", device_id="KQTWO", display_name="theirs")
    upload_keys(c, u.token, keys=device_keys(u, "KQONE"))
    upload_keys(c, v.token, keys=device_keys(v, "KQTWO"))
    query_keys(c, u.token, {u.user_id: []}, label="query your own keys",
               derive=device_key_shape)
    query_keys(c, u.token, {v.user_id: []}, label="query another user's keys",
               derive=device_key_shape)
    query_keys(c, u.token, {u.user_id: [], v.user_id: []},
               label="query both at once", derive=device_key_shape)
    query_keys(c, u.token, {}, label="an empty query",
               derive=device_key_shape)
    query_keys(c, u.token, {u.user_id: []},
               label="the failures object is there and empty",
               derive=lambda b: {"failures": b.get("failures") or {}})


@scenario("e2ee/keys-query-one-device", tags=["e2ee", "query"], step=3)
def keys_query_one_device(c):
    u = register(c, "kq3", device_id="KQTHREE", display_name="first")
    second = login(c, u, device_id="KQFOUR")
    upload_keys(c, u.token, keys=device_keys(u, "KQTHREE"))
    upload_keys(c, second, keys=device_keys(u, "KQFOUR"))
    query_keys(c, u.token, {u.user_id: []}, label="both devices",
               derive=device_key_shape)
    query_keys(c, u.token, {u.user_id: ["KQTHREE"]},
               label="one named device", derive=device_key_shape)
    query_keys(c, u.token, {u.user_id: ["KQFOUR"]},
               label="the other named device", derive=device_key_shape)
    query_keys(c, u.token, {u.user_id: ["KQTHREE", "KQFOUR"]},
               label="both named devices", derive=device_key_shape)
    query_keys(c, u.token, {u.user_id: ["KQTHREE", "KQTHREE"]},
               label="the same device named twice", derive=device_key_shape)


@scenario("e2ee/keys-query-unknown-targets", tags=["e2ee", "query"], step=3)
def keys_query_unknown_targets(c):
    u = register(c, "kq5", device_id="KQFIVE", display_name="known")
    upload_keys(c, u.token, keys=device_keys(u, "KQFIVE"))
    query_keys(c, u.token, {u.user_id: ["NOSUCHDEVICE"]},
               label="a device that does not exist", derive=device_key_shape)
    query_keys(c, u.token, {"@nobody:" + c.server_name: []},
               label="a local user that does not exist",
               derive=device_key_shape)
    query_keys(c, u.token, {"@nobody:" + c.server_name: ["NOSUCHDEVICE"]},
               label="an unknown device of an unknown user",
               derive=device_key_shape)
    query_keys(c, u.token,
               {u.user_id: [], "@nobody:" + c.server_name: []},
               label="one known and one unknown user",
               derive=device_key_shape)
    query_keys(c, u.token, {u.user_id: ["NOSUCHDEVICE"]},
               label="an unknown device records no failure",
               derive=lambda b: {"failures": b.get("failures") or {}})


@scenario("e2ee/keys-query-device-display-name", tags=["e2ee", "query"], step=3)
def keys_query_device_display_name(c):
    u = register(c, "kq6", device_id="KQSIX", display_name="original name")
    no_name = login(c, u, device_id="KQSEVEN")
    upload_keys(c, u.token, keys=device_keys(u, "KQSIX"))
    upload_keys(c, no_name, keys=device_keys(u, "KQSEVEN"))
    query_keys(c, u.token, {u.user_id: []},
               label="the display name set at login is in unsigned",
               derive=device_key_shape)
    c.req("PUT", f"{CS}/devices/KQSIX", token=u.token,
          json_body={"display_name": "renamed device"}, check=False)
    query_keys(c, u.token, {u.user_id: []},
               label="renaming the device changes it",
               derive=device_key_shape)
    v = register(c, "kq8", device_id="KQEIGHT")
    query_keys(c, v.token, {u.user_id: []},
               label="another user sees the same display name",
               derive=device_key_shape)
    c.req("PUT", f"{CS}/devices/KQSEVEN", token=no_name,
          json_body={"display_name": "given a name late"}, check=False)
    query_keys(c, u.token, {u.user_id: ["KQSEVEN"]},
               label="a device that had no name until now",
               derive=device_key_shape)


# --------------------------------------------------------------------------- #
# POST /keys/claim
# --------------------------------------------------------------------------- #
@scenario("e2ee/keys-claim-one-key", tags=["e2ee", "claim"], step=3)
def keys_claim_one_key(c):
    u = register(c, "kc1", device_id="KCONE", display_name="holder")
    v = register(c, "kc2", device_id="KCTWO", display_name="claimer")
    upload_keys(c, u.token, keys=device_keys(u, "KCONE"),
                one_time={"signed_curve25519:AAAAAA": otk(u, "KCONE", "a")})
    claim_keys(c, v.token, {u.user_id: {"KCONE": "signed_curve25519"}},
               label="another user claims the only key", derive=claimed)
    upload_keys(c, u.token, label="the holder's count has dropped", check=True)
    claim_keys(c, v.token, {u.user_id: {"KCONE": "signed_curve25519"}},
               label="claiming again returns nothing", derive=claimed)
    claim_keys(c, v.token, {u.user_id: {"KCONE": "curve25519"}},
               label="claiming an algorithm that was never uploaded",
               derive=claimed)
    claim_keys(c, v.token, {}, label="an empty claim", derive=claimed)


@scenario("e2ee/keys-claim-twice-gives-different-keys", tags=["e2ee", "claim"],
          step=3)
def keys_claim_twice(c):
    u = register(c, "kc3", device_id="KCTHREE", display_name="two keys")
    v = register(c, "kc4", device_id="KCFOUR", display_name="claimer")
    uploaded = {"signed_curve25519:AAAAAA": otk(u, "KCTHREE", "a"),
                "signed_curve25519:AAAAAB": otk(u, "KCTHREE", "b")}
    upload_keys(c, u.token, keys=device_keys(u, "KCTHREE"), one_time=uploaded)
    # Which of two equally eligible keys is handed out first is the server's
    # choice, so the scenario pins that each claim yields one of the uploaded
    # keys and that the second differs from the first - never which is which.
    r1 = claim_keys(c, v.token, {u.user_id: {"KCTHREE": "signed_curve25519"}},
                    label="the first claim",
                    derive=lambda b, up=uploaded: {
                        "count": len(claimed_ids(b).get(u.user_id, {})
                                     .get("KCTHREE", [])),
                        "is_one_of_the_uploaded_keys":
                            one_claimed_id(b) in up,
                    })
    first = one_claimed_id(r1.json or {})
    claim_keys(c, v.token, {u.user_id: {"KCTHREE": "signed_curve25519"}},
               label="the second claim never repeats the first",
               derive=lambda b, up=uploaded, f=first: {
                   "is_one_of_the_uploaded_keys": one_claimed_id(b) in up,
                   "differs_from_the_first_claim": one_claimed_id(b) != f,
               })
    claim_keys(c, v.token, {u.user_id: {"KCTHREE": "signed_curve25519"}},
               label="the third claim has nothing left to give", derive=claimed)
    upload_keys(c, u.token, label="and the holder's count is zero", check=True)


@scenario("e2ee/keys-claim-falls-back", tags=["e2ee", "claim", "fallback"], step=3)
def keys_claim_falls_back(c):
    u = register(c, "kc5", device_id="KCFIVE", display_name="has a fallback")
    v = register(c, "kc6", device_id="KCSIX", display_name="claimer")
    upload_keys(c, u.token, keys=device_keys(u, "KCFIVE"),
                one_time={"signed_curve25519:AAAAAA": otk(u, "KCFIVE", "a")},
                fallback_keys={
                    "signed_curve25519:FBAAAA": fallback(u, "KCFIVE", "f")})
    claim_keys(c, v.token, {u.user_id: {"KCFIVE": "signed_curve25519"}},
               label="a one-time key is preferred while one is left",
               derive=claimed)
    claim_keys(c, v.token, {u.user_id: {"KCFIVE": "signed_curve25519"}},
               label="then the fallback key is handed out", derive=claimed)
    claim_keys(c, v.token, {u.user_id: {"KCFIVE": "signed_curve25519"}},
               label="and handed out again, because it is a fallback",
               derive=claimed)
    upload_keys(c, u.token,
                one_time={"signed_curve25519:AAAAAB": otk(u, "KCFIVE", "b")},
                label="uploading another one-time key", check=True)
    claim_keys(c, v.token, {u.user_id: {"KCFIVE": "signed_curve25519"}},
               label="which is preferred over the fallback again",
               derive=claimed)


@scenario("e2ee/keys-claim-unknown-device", tags=["e2ee", "claim"], step=3)
def keys_claim_unknown_device(c):
    u = register(c, "kc7", device_id="KCSEVEN", display_name="known")
    v = register(c, "kc8", device_id="KCEIGHT", display_name="claimer")
    upload_keys(c, u.token, keys=device_keys(u, "KCSEVEN"),
                one_time={"signed_curve25519:AAAAAA": otk(u, "KCSEVEN", "a")})
    claim_keys(c, v.token, {u.user_id: {"NOSUCHDEVICE": "signed_curve25519"}},
               label="claim for a device that does not exist", derive=claimed)
    claim_keys(c, v.token,
               {"@nobody:" + c.server_name: {"KCSEVEN": "signed_curve25519"}},
               label="claim for a user that does not exist", derive=claimed)
    claim_keys(c, v.token, {u.user_id: {"KCSEVEN": "signed_curve25519",
                                        "NOSUCHDEVICE": "signed_curve25519"}},
               label="one known and one unknown device", derive=claimed)
    claim_keys(c, v.token, {u.user_id: {"KCSEVEN": "no.such.algorithm"}},
               label="an algorithm nobody has heard of", derive=claimed)


@scenario("e2ee/keys-claim-moves-sync-counts", tags=["e2ee", "claim", "sync"],
          step=3)
def keys_claim_moves_sync_counts(c):
    u = register(c, "kc9", device_id="KCNINE", display_name="counted")
    v = register(c, "kc10", device_id="KCTEN", display_name="claimer")
    s = syncer(c, u.token)
    upload_keys(c, u.token, keys=device_keys(u, "KCNINE"))
    since = (s().json or {})["next_batch"]
    upload_keys(c, u.token, one_time={
        "signed_curve25519:AAAAAA": otk(u, "KCNINE", "a"),
        "signed_curve25519:AAAAAB": otk(u, "KCNINE", "b"),
        "signed_curve25519:AAAAAC": otk(u, "KCNINE", "c"),
    })
    r = s(since=since, wait=5000, check=True,
          label="the count the device's own /sync reports", derive=sync_e2ee)
    claim_keys(c, v.token, {u.user_id: {"KCNINE": "signed_curve25519"}},
               label="someone claims one of them", derive=claimed_ids)
    r2 = s(since=(r.json or {}).get("next_batch"), wait=5000, check=True,
           label="/sync now reports one fewer", derive=sync_e2ee)
    claim_keys(c, v.token, {u.user_id: {"KCNINE": "signed_curve25519"}},
               check=False)
    claim_keys(c, v.token, {u.user_id: {"KCNINE": "signed_curve25519"}},
               check=False)
    s(since=(r2.json or {}).get("next_batch"), wait=5000, check=True,
      label="and zero once they are all gone", derive=sync_e2ee)
    upload_keys(c, u.token, label="which /keys/upload agrees with", check=True)


# --------------------------------------------------------------------------- #
# GET /keys/changes and the /sync device lists
# --------------------------------------------------------------------------- #
def delete_device(c, user, device_id, token=None):
    """Remove a device through the user-interactive-auth password flow."""
    tok = token or user.token
    r = c.req("DELETE", f"{CS}/devices/{q(device_id)}", token=tok, json_body={},
              check=False)
    c.req("DELETE", f"{CS}/devices/{q(device_id)}", token=tok, json_body={
        "auth": {"type": "m.login.password", "session": (r.json or {}).get("session"),
                 "identifier": {"type": "m.id.user", "user": user.localpart},
                 "password": user.password}}, check=False)


def wait_for_changed(s, since, who):
    """Sync until the device list of `who` is reported as changed."""
    return poll(lambda: s(since=since, wait=2000),
                lambda r: who in (((r.json or {}).get("device_lists") or {})
                                  .get("changed") or []),
                tries=20, delay=0.0)


def wait_for_left(s, since, who):
    return poll(lambda: s(since=since, wait=2000),
                lambda r: who in (((r.json or {}).get("device_lists") or {})
                                  .get("left") or []),
                tries=20, delay=0.0)


@scenario("e2ee/keys-changes-added-device", tags=["e2ee", "device-lists"], step=3)
def keys_changes_added_device(c):
    u = register(c, "kh1", device_id="KHONE", display_name="watched")
    v = register(c, "kh2", device_id="KHTWO", display_name="watcher")
    upload_keys(c, u.token, keys=device_keys(u, "KHONE"))
    upload_keys(c, v.token, keys=device_keys(v, "KHTWO"))
    room = encrypted_room(c, u)
    join(c, v, room)
    sv = syncer(c, v.token)
    start = (sv().json or {})["next_batch"]
    extra = login(c, u, device_id="KHTHREE")
    upload_keys(c, extra, keys=device_keys(u, "KHTHREE"))
    seen = wait_for_changed(sv, start, u.user_id)
    middle = (seen.json or {})["next_batch"]
    c.req("GET", f"{CS}/keys/changes", token=v.token,
          query={"from": start, "to": middle},
          label="the login that added a device is reported", derive=changes)
    c.req("GET", f"{CS}/keys/changes", token=v.token,
          query={"from": middle, "to": middle},
          label="an empty window reports nothing", derive=changes)
    su = syncer(c, u.token)
    own = (su().json or {})["next_batch"]
    c.req("GET", f"{CS}/keys/changes", token=u.token,
          query={"from": own, "to": own},
          label="and neither does the user's own empty window", derive=changes)
    query_keys(c, v.token, {u.user_id: []},
               label="the new device is the one /keys/query now returns",
               derive=device_key_shape)


@scenario("e2ee/keys-changes-deleted-device", tags=["e2ee", "device-lists"], step=3)
def keys_changes_deleted_device(c):
    u = register(c, "kh3", device_id="KHFOUR", display_name="watched")
    v = register(c, "kh4", device_id="KHFIVE", display_name="watcher")
    upload_keys(c, u.token, keys=device_keys(u, "KHFOUR"))
    extra = login(c, u, device_id="KHSIX")
    upload_keys(c, extra, keys=device_keys(u, "KHSIX"))
    room = encrypted_room(c, u)
    join(c, v, room)
    sv = syncer(c, v.token)
    start = (sv().json or {})["next_batch"]
    delete_device(c, u, "KHSIX")
    seen = wait_for_changed(sv, start, u.user_id)
    end = (seen.json or {})["next_batch"]
    c.req("GET", f"{CS}/keys/changes", token=v.token,
          query={"from": start, "to": end},
          label="deleting a device counts as a device-list change",
          derive=changes)
    query_keys(c, v.token, {u.user_id: []},
               label="and the deleted device is gone from /keys/query",
               derive=device_key_shape)
    claim_keys(c, v.token, {u.user_id: {"KHSIX": "signed_curve25519"}},
               label="its one-time keys are unreachable", derive=claimed)
    c.req("GET", f"{CS}/keys/changes", token=v.token,
          query={"from": end, "to": end},
          label="nothing more has changed since", derive=changes)


@scenario("e2ee/sync-device-lists-changed", tags=["e2ee", "device-lists", "sync"],
          step=3)
def sync_device_lists_changed(c):
    u = register(c, "dl1", device_id="DLONE", display_name="changing")
    v = register(c, "dl2", device_id="DLTWO", display_name="watching")
    upload_keys(c, u.token, keys=device_keys(u, "DLONE"))
    upload_keys(c, v.token, keys=device_keys(v, "DLTWO"))
    room = encrypted_room(c, u)
    join(c, v, room)
    sv = syncer(c, v.token)
    start = (sv().json or {})["next_batch"]
    extra = login(c, u, device_id="DLTHREE")
    upload_keys(c, extra, keys=device_keys(u, "DLTHREE"))
    seen = wait_for_changed(sv, start, u.user_id)
    after_add = (seen.json or {})["next_batch"]
    sv(since=start, wait=2000, check=True,
       label="adding a device puts its owner in device_lists.changed",
       derive=sync_device_lists)
    quiet = sv(since=after_add, wait=200, check=True,
               label="and nothing is reported when nothing changes",
               derive=sync_device_lists)
    delete_device(c, u, "DLTHREE", token=extra)
    seen2 = wait_for_changed(sv, (quiet.json or {})["next_batch"], u.user_id)
    sv(since=(quiet.json or {})["next_batch"], wait=2000, check=True,
       label="removing a device reports the owner again",
       derive=sync_device_lists)
    sv(since=(seen2.json or {})["next_batch"], wait=200, check=True,
       label="and then falls quiet again", derive=sync_device_lists)
    query_keys(c, v.token, {u.user_id: []},
               label="leaving one device behind", derive=device_key_shape)


@scenario("e2ee/sync-device-lists-left", tags=["e2ee", "device-lists", "sync"],
          step=3)
def sync_device_lists_left(c):
    u = register(c, "dl3", device_id="DLFOUR", display_name="leaving")
    v = register(c, "dl4", device_id="DLFIVE", display_name="staying")
    upload_keys(c, u.token, keys=device_keys(u, "DLFOUR"))
    upload_keys(c, v.token, keys=device_keys(v, "DLFIVE"))
    room = encrypted_room(c, u)
    join(c, v, room)
    sv = syncer(c, v.token)
    start = (sv().json or {})["next_batch"]
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=u.token, json_body={},
          check=False)
    seen = wait_for_left(sv, start, u.user_id)
    gone = (seen.json or {})["next_batch"]
    sv(since=start, wait=2000, check=True,
       label="the user we no longer share an encrypted room with is in left",
       derive=sync_device_lists)
    c.req("GET", f"{CS}/keys/changes", token=v.token,
          query={"from": start, "to": gone},
          label="/keys/changes reports the same window", derive=changes)
    join(c, u, room)
    seen2 = wait_for_changed(sv, gone, u.user_id)
    sv(since=gone, wait=2000, check=True,
       label="coming back puts them in changed, not in left",
       derive=sync_device_lists)
    c.req("GET", f"{CS}/keys/changes", token=v.token,
          query={"from": gone, "to": (seen2.json or {})["next_batch"]},
          label="and /keys/changes agrees", derive=changes)
    query_keys(c, v.token, {u.user_id: []},
               label="their keys are queryable throughout",
               derive=device_key_shape)


# --------------------------------------------------------------------------- #
# POST /keys/device_signing/upload and POST /keys/signatures/upload
# --------------------------------------------------------------------------- #
def first_cross_signing(c, u, body, *, label=None):
    """The *first* cross-signing upload, done so that either reading passes.

    Release v1.3 puts `/keys/device_signing/upload` behind user-interactive
    authentication with no exception for the first upload; the reference lets a
    first upload through unchallenged (the behaviour MSC3967 later
    standardised).  Both are defensible readings of the release this step
    targets, so the setup supplies complete password auth up front and answers
    a challenge if one comes back.  A server that challenges and a server that
    does not both end up with the keys stored, and nothing here scores which
    one it is.
    """
    with_auth = dict(body)
    with_auth["auth"] = {"type": "m.login.password",
                         "identifier": {"type": "m.id.user", "user": u.localpart},
                         "password": u.password}
    r = upload_cross_signing(c, u.token, with_auth, label=label)
    if r.status == 401 and (r.json or {}).get("session"):
        again = dict(with_auth)
        again["auth"] = dict(with_auth["auth"], session=r.json["session"])
        r = upload_cross_signing(c, u.token, again, label=label)
    return r


def uia_offer(b):
    """What a user-interactive challenge offers, without pinning how much.

    Which flows a server advertises is its own business -- a server that
    supports both of this task's login types is within its rights to offer
    both -- so the number of flows and their order are not compared.  What the
    challenge has to carry is a session to retry against, and at least one
    flow the client in this scenario can actually complete.
    """
    flows = [f.get("stages") for f in (b.get("flows") or [])]
    return {"a_session_to_retry_against": bool(b.get("session")),
            "offers_a_password_flow": ["m.login.password"] in flows,
            "nothing_completed_yet": not b.get("completed")}


def upload_cross_signing(c, token, body, *, check=False, label=None, **kw):
    return c.req("POST", f"{CS}/keys/device_signing/upload", token=token,
                 json_body=body, check=check, label=label, **kw)


@scenario("e2ee/cross-signing-upload-and-query", tags=["e2ee", "cross-signing"],
          step=3)
def cross_signing_upload_and_query(c):
    u = register(c, "cs1", device_id="CSONE", display_name="signing device")
    v = register(c, "cs2", device_id="CSTWO", display_name="other user")
    upload_keys(c, u.token, keys=device_keys(u, "CSONE"))
    upload_keys(c, v.token, keys=device_keys(v, "CSTWO"))
    ks = cross_signing_keys(u)
    # Whether a *first* cross-signing upload is challenged is left to the
    # replacement scenario: the reference does not challenge one, so scoring
    # it here would pin that choice rather than the specification.
    first_cross_signing(c, u, ks["body"])
    query_keys(c, u.token, {u.user_id: []},
               label="all three cross-signing keys come back to their owner",
               derive=cross_signing_shape)
    query_keys(c, v.token, {u.user_id: []},
               label="another user sees the master and self-signing keys only",
               derive=cross_signing_shape)
    query_keys(c, u.token, {v.user_id: []},
               label="a user who has uploaded none has none",
               derive=cross_signing_shape)
    query_keys(c, u.token, {u.user_id: ["CSONE"]},
               label="they accompany a single-device query too",
               derive=cross_signing_shape)
    upload_cross_signing(c, u.token, ks["body"],
                         label="re-uploading the identical keys is not a change")
    query_keys(c, u.token, {u.user_id: []},
               label="and has changed nothing", derive=cross_signing_shape)


@scenario("e2ee/cross-signing-replace-with-uia",
          tags=["e2ee", "cross-signing", "uia"], step=3)
def cross_signing_replace_with_uia(c):
    u = register(c, "cs3", device_id="CSTHREE", display_name="replacer")
    upload_keys(c, u.token, keys=device_keys(u, "CSTHREE"))
    first_cross_signing(c, u, cross_signing_keys(u, "1")["body"])
    second = cross_signing_keys(u, "2")["body"]
    r = upload_cross_signing(c, u.token, second, check=True,
                             label="replacing a cross-signing key is challenged",
                             drop=("flows",), derive=uia_offer)
    done = dict(second)
    done["auth"] = {"type": "m.login.password",
                    "session": (r.json or {}).get("session"),
                    "identifier": {"type": "m.id.user", "user": u.localpart},
                    "password": u.password}
    upload_cross_signing(c, u.token, done, check=True,
                         label="and completes with password auth")
    query_keys(c, u.token, {u.user_id: []},
               label="the replacement keys are the ones served",
               derive=cross_signing_shape)
    third = cross_signing_keys(u, "3")["body"]
    r2 = upload_cross_signing(c, u.token, third)
    wrong = dict(third)
    wrong["auth"] = {"type": "m.login.password",
                     "session": (r2.json or {}).get("session"),
                     "identifier": {"type": "m.id.user", "user": u.localpart},
                     "password": "not-the-password"}
    upload_cross_signing(c, u.token, wrong, check=True,
                         label="a wrong password does not complete it",
                         drop=("completed", "params", "flows"))
    query_keys(c, u.token, {u.user_id: []},
               label="so the served keys have not moved",
               derive=cross_signing_shape)


@scenario("e2ee/cross-signing-malformed", tags=["e2ee", "cross-signing"], step=3)
def cross_signing_malformed(c):
    """Key objects the endpoint must not accept.

    Each upload carries complete password auth, so that a server which
    challenges before it validates and one which validates before it challenges
    both reach the same question: is this key object acceptable?  Only the
    refusal is compared -- which code a server picks for a malformed body is not
    something the release settles for this endpoint.
    """
    u = register(c, "cs4", device_id="CSFOUR", display_name="malformed")
    upload_keys(c, u.token, keys=device_keys(u, "CSFOUR"))
    ks = cross_signing_keys(u, "m")
    wrong_user = dict(ks["master"]["key"])
    wrong_user["user_id"] = "@someone-else:" + c.server_name
    auth = {"type": "m.login.password",
            "identifier": {"type": "m.id.user", "user": u.localpart},
            "password": u.password}
    refuse = lambda body, label: upload_cross_signing(          # noqa: E731
        c, u.token, dict(body, auth=auth), check=True,
        drop=("errcode", "flows", "session", "params", "completed"),
        label=label)
    refuse({"master_key": wrong_user},
           "a key claiming to belong to another user")
    refuse({"master_key": {"user_id": u.user_id, "usage": ["master"],
                           "keys": {}}},
           "a key object with no key in it")
    refuse({"master_key": {
        "user_id": u.user_id, "usage": ["master"],
        "keys": {ks["master"]["key_id"]: b64(ks["master"]["public"]),
                 ks["self_signing"]["key_id"]: b64(ks["self_signing"]["public"])},
    }}, "a key object carrying two keys")
    refuse({
        "master_key": ks["master"]["key"],
        "self_signing_key": {"user_id": u.user_id, "usage": ["self_signing"],
                             "keys": {ks["self_signing"]["key_id"]:
                                      b64(ks["self_signing"]["public"])}},
    }, "a self-signing key the master key has not signed")
    refuse({"self_signing_key": ks["self_signing"]["key"]},
           "a self-signing key with no master key at all")
    query_keys(c, u.token, {u.user_id: []},
               label="none of that was stored", derive=cross_signing_shape)


@scenario("e2ee/signatures-upload-device", tags=["e2ee", "cross-signing"], step=3)
def signatures_upload_device(c):
    u = register(c, "sg1", device_id="SGONE", display_name="signed device")
    v = register(c, "sg2", device_id="SGTWO", display_name="onlooker")
    dk = device_keys(u, "SGONE")
    upload_keys(c, u.token, keys=dk)
    ks = cross_signing_keys(u, "s")
    first_cross_signing(c, u, ks["body"])
    signed = sign_json({k: val for k, val in dk.items() if k != "signatures"},
                       u.user_id, ks["self_signing"]["key_id"],
                       ks["self_signing"]["seed"], ks["self_signing"]["public"])

    def signing_ids(b, uid=u.user_id, kid=ks["self_signing"]["key_id"]):
        sigs = ((b.get("device_keys") or {}).get(uid, {})
                .get("SGONE", {}).get("signatures", {}).get(uid, {}))
        return {"signing_key_ids": sorted(sigs),
                "self_signing_key_id_present": kid in sigs}

    query_keys(c, u.token, {u.user_id: []},
               label="before: only the device's own signature", derive=signing_ids)
    c.req("POST", f"{CS}/keys/signatures/upload", token=u.token,
          json_body={u.user_id: {"SGONE": signed}},
          label="a device signed by its owner's self-signing key",
          derive=failure_codes)
    query_keys(c, u.token, {u.user_id: []},
               label="the new signature is served with the device",
               derive=signing_ids)
    query_keys(c, v.token, {u.user_id: []},
               label="and another user sees it too", derive=signing_ids)
    c.req("POST", f"{CS}/keys/signatures/upload", token=u.token,
          json_body={u.user_id: {"SGONE": signed}},
          label="uploading the same signature again", derive=failure_codes)
    c.req("POST", f"{CS}/keys/signatures/upload", token=u.token, json_body={},
          label="an upload with no signatures in it", derive=failure_codes)


@scenario("e2ee/signatures-upload-rejected", tags=["e2ee", "cross-signing"], step=3)
def signatures_upload_rejected(c):
    u = register(c, "sg3", device_id="SGTHREE", display_name="badly signed")
    dk = device_keys(u, "SGTHREE")
    upload_keys(c, u.token, keys=dk)
    ks = cross_signing_keys(u, "s")
    first_cross_signing(c, u, ks["body"])
    stripped = {k: val for k, val in dk.items() if k != "signatures"}
    forged = dict(stripped)
    forged["signatures"] = {u.user_id: {
        ks["self_signing"]["key_id"]: material("forged/signature", 64)}}
    c.req("POST", f"{CS}/keys/signatures/upload", token=u.token,
          json_body={u.user_id: {"SGTHREE": forged}},
          label="a device signature that does not verify", derive=failure_codes)
    c.req("POST", f"{CS}/keys/signatures/upload", token=u.token,
          json_body={u.user_id: {"SGTHREE": stripped}},
          label="a device object carrying no signature at all",
          derive=failure_codes)
    altered = dict(stripped)
    altered["algorithms"] = ["m.megolm.v1.aes-sha2"]
    altered = sign_json(altered, u.user_id, ks["self_signing"]["key_id"],
                        ks["self_signing"]["seed"], ks["self_signing"]["public"])
    c.req("POST", f"{CS}/keys/signatures/upload", token=u.token,
          json_body={u.user_id: {"SGTHREE": altered}},
          label="a correctly signed object that is not the stored device",
          derive=failure_codes)
    unknown = sign_json(
        {k: val for k, val in device_keys(u, "SGNOPE").items()
         if k != "signatures"},
        u.user_id, ks["self_signing"]["key_id"], ks["self_signing"]["seed"],
        ks["self_signing"]["public"])
    c.req("POST", f"{CS}/keys/signatures/upload", token=u.token,
          json_body={u.user_id: {"SGNOPE": unknown}},
          label="a signature for a device that does not exist",
          derive=failure_codes)
    query_keys(c, u.token, {u.user_id: []},
               label="no rejected signature was stored",
               derive=lambda b, uid=u.user_id: {
                   "devices": sorted((b.get("device_keys") or {}).get(uid, {})),
                   "signing_key_ids": sorted(
                       (b.get("device_keys") or {}).get(uid, {})
                       .get("SGTHREE", {}).get("signatures", {}).get(uid, {})),
                   "algorithms": ((b.get("device_keys") or {}).get(uid, {})
                                  .get("SGTHREE", {}).get("algorithms")),
               })


# --------------------------------------------------------------------------- #
# Server-side key backups
# --------------------------------------------------------------------------- #
@scenario("e2ee/room-keys-version-lifecycle", tags=["e2ee", "backup"], step=3)
def room_keys_version_lifecycle(c):
    """A backup version is "an opaque string" the server chooses.

    So the name is never written down here: each request quotes back whatever
    the server returned, and the transcript compares the symbol rather than the
    spelling.  A server that counts from "1" and a server that mints a UUID both
    have to answer the same *sequence* of questions the same way.
    """
    u = register(c, "rk1", device_id="RKONE", display_name="backer-up")
    c.req("GET", f"{CS}/room_keys/version", token=u.token,
          label="there is no current backup version yet")
    first = create_backup(c, u.token, "1", label="create a backup version")
    c.req("GET", f"{CS}/room_keys/version", token=u.token,
          label="the current version describes what was created",
          drop=("etag",))
    c.req("GET", f"{CS}/room_keys/version/{q(first)}", token=u.token,
          label="and so does the version fetched by name", drop=("etag",))
    c.req("GET", f"{CS}/room_keys/version/{q('nosuchversion')}", token=u.token,
          label="a version that does not exist")
    create_backup(c, u.token, "2", label="create a second version")
    c.req("GET", f"{CS}/room_keys/version", token=u.token,
          label="the newest version is the current one", drop=("etag",))
    c.req("GET", f"{CS}/room_keys/version/{q(first)}", token=u.token,
          label="the older version is still described", drop=("etag",))


@scenario("e2ee/room-keys-version-update-and-delete", tags=["e2ee", "backup"],
          step=3)
def room_keys_version_update_and_delete(c):
    u = register(c, "rk2", device_id="RKTWO", display_name="version editor")
    version = create_backup(c, u.token, "1")
    c.req("PUT", f"{CS}/room_keys/version/{q(version)}", token=u.token,
          json_body={"algorithm": BACKUP_ALG, "auth_data": backup_auth_data("2"),
                     "version": version},
          label="replace the auth_data of an existing version")
    c.req("GET", f"{CS}/room_keys/version/{q(version)}", token=u.token,
          label="which is what is served afterwards", drop=("etag",))
    c.req("PUT", f"{CS}/room_keys/version/{q(version)}", token=u.token,
          json_body={"algorithm": BACKUP_ALG, "auth_data": backup_auth_data("3"),
                     "version": "9000"},
          label="a version in the body that is not the one in the path")
    c.req("PUT", f"{CS}/room_keys/version/9000", token=u.token,
          json_body={"algorithm": BACKUP_ALG, "auth_data": backup_auth_data("3")},
          label="updating a version that does not exist")
    c.req("DELETE", f"{CS}/room_keys/version/{q(version)}", token=u.token,
          label="delete the version")
    c.req("GET", f"{CS}/room_keys/version/{q(version)}", token=u.token,
          label="after which it cannot be fetched")
    c.req("GET", f"{CS}/room_keys/version", token=u.token,
          label="and there is no current version again")
    c.req("DELETE", f"{CS}/room_keys/version/{q(version)}", token=u.token,
          label="deleting it a second time")


@scenario("e2ee/room-keys-store-and-fetch", tags=["e2ee", "backup"], step=3)
def room_keys_store_and_fetch(c):
    u = register(c, "rk3", device_id="RKTHREE", display_name="key storer")
    version = create_backup(c, u.token)
    room1 = "!bkone:" + c.server_name
    room2 = "!bktwo:" + c.server_name
    put_key(c, u.token, version, room1, "sess1", session_key("one"),
            label="store one session key")
    get_key(c, u.token, version, room1, "sess1",
            label="read it back by room and session")
    c.req("PUT", f"{CS}/room_keys/keys/{q(room1)}", token=u.token,
          query={"version": version},
          json_body={"sessions": {"sess2": session_key("two", index=4)}},
          label="store a second session for the same room", drop=("etag",))
    c.req("PUT", f"{CS}/room_keys/keys", token=u.token,
          query={"version": version},
          json_body={"rooms": {room2: {"sessions": {
              "sess3": session_key("three", index=7, verified=True)}}}},
          label="store a session for another room", drop=("etag",))
    c.req("GET", f"{CS}/room_keys/keys/{q(room1)}", token=u.token,
          query={"version": version}, label="read a whole room back")
    c.req("GET", f"{CS}/room_keys/keys", token=u.token,
          query={"version": version}, label="read the whole backup back")
    c.req("GET", f"{CS}/room_keys/keys/{q(room1)}/nosuchsession", token=u.token,
          query={"version": version}, label="a session that was never stored")
    c.req("GET", f"{CS}/room_keys/keys", token=u.token,
          query={"version": "9000"},
          label="reading a backup version that does not exist")


@scenario("e2ee/room-keys-replace-verified-wins", tags=["e2ee", "backup"], step=3)
def room_keys_replace_verified_wins(c):
    u = register(c, "rk4", device_id="RKFOUR", display_name="verified rule")
    version = create_backup(c, u.token)
    room = "!bkver:" + c.server_name
    # The three rules are applied in order, so a case that isolates one of
    # them has to leave the others pointing the same way or saying nothing:
    # the replacement below is verified *and* the loser that follows is worse
    # on every count, so only the is_verified rule decides anything here.
    unverified = session_key("unverified", index=6, forwarded=3, verified=False)
    verified = session_key("verified", index=9, forwarded=5, verified=True)
    put_key(c, u.token, version, room, "sess1", unverified,
            label="store an unverified key")
    get_key(c, u.token, version, room, "sess1", label="which is what is stored")
    put_key(c, u.token, version, room, "sess1", verified,
            label="a verified key replaces it even with a higher index")
    get_key(c, u.token, version, room, "sess1",
            label="the verified key is the one kept")
    put_key(c, u.token, version, room, "sess1",
            session_key("loser", index=12, forwarded=8, verified=False),
            label="an unverified key does not replace a verified one")
    get_key(c, u.token, version, room, "sess1",
            label="the verified key is still the one kept")


@scenario("e2ee/room-keys-replace-lower-index-wins", tags=["e2ee", "backup"],
          step=3)
def room_keys_replace_lower_index_wins(c):
    u = register(c, "rk5", device_id="RKFIVE", display_name="index rule")
    version = create_backup(c, u.token)
    room = "!bkidx:" + c.server_name
    put_key(c, u.token, version, room, "sess1",
            session_key("high", index=5, forwarded=6),
            label="store a key whose first message index is five")
    get_key(c, u.token, version, room, "sess1", label="which is what is stored")
    put_key(c, u.token, version, room, "sess1",
            session_key("low", index=2, forwarded=6),
            label="a lower first_message_index replaces it")
    get_key(c, u.token, version, room, "sess1",
            label="the lower index is the one kept")
    put_key(c, u.token, version, room, "sess1",
            session_key("higher", index=7, forwarded=6),
            label="and a higher one does not")
    get_key(c, u.token, version, room, "sess1",
            label="the loser did not overwrite the winner")


@scenario("e2ee/room-keys-replace-lower-forwarded-count-wins",
          tags=["e2ee", "backup"], step=3)
def room_keys_replace_lower_forwarded_count_wins(c):
    u = register(c, "rk6", device_id="RKSIX", display_name="forwarded rule")
    version = create_backup(c, u.token)
    room = "!bkfwd:" + c.server_name
    put_key(c, u.token, version, room, "sess1",
            session_key("many", index=3, forwarded=4),
            label="store a key forwarded four times")
    get_key(c, u.token, version, room, "sess1", label="which is what is stored")
    put_key(c, u.token, version, room, "sess1",
            session_key("few", index=3, forwarded=1),
            label="at the same index, fewer forwards replaces it")
    get_key(c, u.token, version, room, "sess1",
            label="the less forwarded key is the one kept")
    put_key(c, u.token, version, room, "sess1",
            session_key("most", index=3, forwarded=9),
            label="and a more forwarded key does not")
    get_key(c, u.token, version, room, "sess1",
            label="the loser did not overwrite the winner")


@scenario("e2ee/room-keys-count-and-etag", tags=["e2ee", "backup"], step=3)
def room_keys_count_and_etag(c):
    u = register(c, "rk7", device_id="RKSEVEN", display_name="etag watcher")
    version = create_backup(c, u.token)
    room = "!bketag:" + c.server_name
    seen = {"etag": (c.req("GET", f"{CS}/room_keys/version", token=u.token,
                           check=False).json or {}).get("etag")}

    def stored(b, seen=seen):
        # The etag is "an opaque string": only that it is a string, and that it
        # moves when the backup's contents move, is specified.
        return {"count": b.get("count"),
                "etag_is_a_string": isinstance(b.get("etag"), str),
                "etag_moved": b.get("etag") != seen["etag"]}

    def matches(b, seen=seen):
        return {"count": b.get("count"),
                "etag_is_the_one_the_last_store_returned":
                    b.get("etag") == seen["etag"]}

    r = c.req("PUT", f"{CS}/room_keys/keys/{q(room)}/sess1", token=u.token,
              query={"version": version}, json_body=session_key("one"),
              label="storing the first key moves the etag", derive=stored)
    seen["etag"] = (r.json or {}).get("etag")
    c.req("GET", f"{CS}/room_keys/version/{q(version)}", token=u.token,
          label="the version reports the same etag", derive=matches)
    r = c.req("PUT", f"{CS}/room_keys/keys/{q(room)}/sess2", token=u.token,
              query={"version": version}, json_body=session_key("two"),
              label="storing a second key moves it again", derive=stored)
    seen["etag"] = (r.json or {}).get("etag")
    c.req("GET", f"{CS}/room_keys/version/{q(version)}", token=u.token,
          label="and the version agrees again", derive=matches)
    r = c.req("DELETE", f"{CS}/room_keys/keys/{q(room)}/sess1", token=u.token,
              query={"version": version},
              label="deleting a key moves it too", derive=stored)
    seen["etag"] = (r.json or {}).get("etag")
    c.req("GET", f"{CS}/room_keys/version/{q(version)}", token=u.token,
          label="leaving one key in the backup", derive=matches)


@scenario("e2ee/room-keys-delete", tags=["e2ee", "backup"], step=3)
def room_keys_delete(c):
    u = register(c, "rk8", device_id="RKEIGHT", display_name="deleter")
    version = create_backup(c, u.token)
    room1 = "!bkdone:" + c.server_name
    room2 = "!bkdtwo:" + c.server_name
    put_key(c, u.token, version, room1, "sess1", session_key("one"), check=False)
    put_key(c, u.token, version, room1, "sess2", session_key("two"), check=False)
    put_key(c, u.token, version, room2, "sess3", session_key("three"), check=False)
    c.req("DELETE", f"{CS}/room_keys/keys/{q(room1)}/sess1", token=u.token,
          query={"version": version}, label="delete one session",
          drop=("etag",))
    get_key(c, u.token, version, room1, "sess1", label="which is then not found")
    get_key(c, u.token, version, room1, "sess2", label="its neighbour survives")
    c.req("DELETE", f"{CS}/room_keys/keys/{q(room1)}", token=u.token,
          query={"version": version}, label="delete a whole room", drop=("etag",))
    get_key(c, u.token, version, room1, "sess2",
            label="both of its sessions are gone")
    c.req("GET", f"{CS}/room_keys/keys", token=u.token,
          query={"version": version}, label="the other room is untouched")
    c.req("DELETE", f"{CS}/room_keys/keys", token=u.token,
          query={"version": version}, label="delete everything",
          drop=("etag",))
    c.req("GET", f"{CS}/room_keys/keys", token=u.token,
          query={"version": version}, label="and the backup is empty")


@scenario("e2ee/room-keys-wrong-version", tags=["e2ee", "backup"], step=3)
def room_keys_wrong_version(c):
    u = register(c, "rk9", device_id="RKNINE", display_name="stale version")
    old = create_backup(c, u.token, "1")
    room = "!bkstale:" + c.server_name
    put_key(c, u.token, old, room, "sess1", session_key("one"), check=False)
    new = create_backup(c, u.token, "2")
    put_key(c, u.token, new, room, "sess2", session_key("two"),
            label="storing into the current version works")
    c.req("PUT", f"{CS}/room_keys/keys/{q(room)}/sess3", token=u.token,
          query={"version": old}, json_body=session_key("three"),
          label="storing into a superseded version is refused")
    c.req("PUT", f"{CS}/room_keys/keys/{q(room)}", token=u.token,
          query={"version": old},
          json_body={"sessions": {"sess4": session_key("four")}},
          label="and so is the whole-room form")
    c.req("PUT", f"{CS}/room_keys/keys", token=u.token, query={"version": old},
          json_body={"rooms": {room: {"sessions": {
              "sess5": session_key("five")}}}},
          label="and the whole-backup form")
    c.req("PUT", f"{CS}/room_keys/keys/{q(room)}/sess6", token=u.token,
          query={"version": "9000"}, json_body=session_key("six"),
          label="storing into a version that never existed")
    get_key(c, u.token, old, room, "sess1",
            label="the superseded version can still be read")
    c.req("GET", f"{CS}/room_keys/keys", token=u.token, query={"version": new},
          label="and the current one holds only its own key")


# --------------------------------------------------------------------------- #
# PUT /sendToDevice/{eventType}/{txnId}
# --------------------------------------------------------------------------- #
TO_DEVICE = "com.example.e2ee_test"


def send_to_device(c, token, txn, messages, *, etype=TO_DEVICE, check=True,
                   label=None):
    return c.req("PUT", f"{CS}/sendToDevice/{etype}/{txn}", token=token,
                 json_body={"messages": messages}, check=check, label=label)


def wait_to_device(s, since, count=1):
    return poll(lambda: s(since=since, wait=2000),
                lambda r: len((((r.json or {}).get("to_device") or {})
                               .get("events") or [])) >= count,
                tries=20, delay=0.0)


@scenario("e2ee/to-device-single-device", tags=["e2ee", "to-device"], step=3)
def to_device_single_device(c):
    sender = register(c, "td1", device_id="TDSEND", display_name="sender")
    recv = register(c, "td2", device_id="TDONE", display_name="first device")
    other = login(c, recv, device_id="TDTWO")
    s1 = syncer(c, recv.token)
    s2 = syncer(c, other)
    start1 = (s1().json or {})["next_batch"]
    start2 = (s2().json or {})["next_batch"]
    send_to_device(c, sender.token, "txn-one",
                   {recv.user_id: {"TDONE": {"body": "for the first device"}}},
                   label="send to exactly one device")
    wait_to_device(s1, start1)
    delivered = s1(since=start1, wait=2000, check=True,
                   label="it arrives at that device", derive=sync_to_device)
    s1(since=(delivered.json or {})["next_batch"], wait=200, check=True,
       label="and is not delivered a second time", derive=sync_to_device)
    s2(since=start2, wait=200, check=True,
       label="the user's other device never saw it", derive=sync_to_device)
    send_to_device(c, sender.token, "txn-two",
                   {recv.user_id: {"TDTWO": {"body": "for the second device"}}},
                   label="send to the other device")
    wait_to_device(s2, start2)
    s2(since=start2, wait=2000, check=True,
       label="which arrives there instead", derive=sync_to_device)


@scenario("e2ee/to-device-wildcard", tags=["e2ee", "to-device"], step=3)
def to_device_wildcard(c):
    sender = register(c, "td3", device_id="TDSENDB", display_name="sender")
    recv = register(c, "td4", device_id="TDTHREE", display_name="first device")
    other = login(c, recv, device_id="TDFOUR")
    s1 = syncer(c, recv.token)
    s2 = syncer(c, other)
    start1 = (s1().json or {})["next_batch"]
    start2 = (s2().json or {})["next_batch"]
    send_to_device(c, sender.token, "txn-star",
                   {recv.user_id: {"*": {"body": "for every device"}}},
                   label="send to every device of a user")
    wait_to_device(s1, start1)
    wait_to_device(s2, start2)
    got1 = s1(since=start1, wait=2000, check=True,
              label="the first device gets it", derive=sync_to_device)
    got2 = s2(since=start2, wait=2000, check=True,
              label="and so does the second", derive=sync_to_device)
    s1(since=(got1.json or {})["next_batch"], wait=200, check=True,
       label="once each, for the first", derive=sync_to_device)
    s2(since=(got2.json or {})["next_batch"], wait=200, check=True,
       label="once each, for the second", derive=sync_to_device)


@scenario("e2ee/to-device-across-users", tags=["e2ee", "to-device"], step=3)
def to_device_across_users(c):
    a = register(c, "td5", device_id="TDFIVE", display_name="a")
    b = register(c, "td6", device_id="TDSIX", display_name="b")
    sa = syncer(c, a.token)
    sb = syncer(c, b.token)
    starta = (sa().json or {})["next_batch"]
    startb = (sb().json or {})["next_batch"]
    send_to_device(c, a.token, "txn-a2b",
                   {b.user_id: {"TDSIX": {"body": "a to b"}}},
                   label="one user sends to another")
    wait_to_device(sb, startb)
    gotb = sb(since=startb, wait=2000, check=True,
              label="the recipient sees the sender", derive=sync_to_device)
    sa(since=starta, wait=200, check=True,
       label="the sender's own device gets nothing", derive=sync_to_device)
    send_to_device(c, b.token, "txn-b2a",
                   {a.user_id: {"TDFIVE": {"body": "b to a"}},
                    b.user_id: {"TDSIX": {"body": "b to itself"}}},
                   label="and a single request can address two users")
    wait_to_device(sa, starta)
    sa(since=starta, wait=2000, check=True,
       label="which reaches the other user", derive=sync_to_device)
    wait_to_device(sb, (gotb.json or {})["next_batch"])
    sb(since=(gotb.json or {})["next_batch"], wait=2000, check=True,
       label="and the sender's own device", derive=sync_to_device)


@scenario("e2ee/to-device-transaction-replay", tags=["e2ee", "to-device"], step=3)
def to_device_transaction_replay(c):
    sender = register(c, "td7", device_id="TDSENDC", display_name="sender")
    recv = register(c, "td8", device_id="TDSEVEN", display_name="receiver")
    s = syncer(c, recv.token)
    start = (s().json or {})["next_batch"]
    send_to_device(c, sender.token, "txn-repeat",
                   {recv.user_id: {"TDSEVEN": {"body": "the first body"}}},
                   label="send a message")
    wait_to_device(s, start)
    send_to_device(c, sender.token, "txn-repeat",
                   {recv.user_id: {"TDSEVEN": {"body": "a different body"}}},
                   label="replay the same transaction id with new content")
    got = s(since=start, wait=2000, check=True,
            label="the replay was not delivered as a second message",
            derive=sync_to_device)
    send_to_device(c, sender.token, "txn-fresh",
                   {recv.user_id: {"TDSEVEN": {"body": "a new transaction"}}},
                   label="a fresh transaction id is a new message")
    wait_to_device(s, (got.json or {})["next_batch"])
    s(since=(got.json or {})["next_batch"], wait=2000, check=True,
      label="and is delivered", derive=sync_to_device)


@scenario("e2ee/to-device-unknown-destination", tags=["e2ee", "to-device"], step=3)
def to_device_unknown_destination(c):
    sender = register(c, "td9", device_id="TDSENDD", display_name="sender")
    recv = register(c, "td10", device_id="TDEIGHT", display_name="receiver")
    s = syncer(c, recv.token)
    start = (s().json or {})["next_batch"]
    send_to_device(c, sender.token, "txn-nodevice",
                   {recv.user_id: {"TDNOSUCH": {"body": "into the void"}}},
                   label="send to a device that does not exist")
    send_to_device(c, sender.token, "txn-nouser",
                   {"@nobody:" + c.server_name: {"*": {"body": "into the void"}}},
                   label="send to a user that does not exist")
    send_to_device(c, sender.token, "txn-empty", {},
                   label="send to nobody at all")
    s(since=start, wait=500, check=True,
      label="none of which reached the real device", derive=sync_to_device)
    send_to_device(c, sender.token, "txn-mixed",
                   {recv.user_id: {"TDNOSUCH": {"body": "into the void"},
                                   "TDEIGHT": {"body": "to the real device"}}},
                   label="one real device and one unknown in the same request")
    wait_to_device(s, start)
    s(since=start, wait=2000, check=True,
      label="only the real device is delivered to", derive=sync_to_device)

# --------------------------------------------------------------------------- #
# The pre-flight, on the routes this milestone adds
# --------------------------------------------------------------------------- #
# Every step's ticket restates the release's CORS requirement -- every endpoint
# answers an `OPTIONS` request, and answering it performs none of the endpoint's
# own logic -- and step 1 scores it on the routes that existed then.  A route
# added later is a route that can be added without it, so each milestone asks
# the same two questions of its own.  Nothing here needs a room, a user or an
# upload to exist: that is the point of the second question.


@scenario("e2ee/options-preflight", tags=["cors"], step=3)
def e2ee_options_preflight(c):
    """`OPTIONS` on the endpoints this release adds."""
    empty = lambda b: {"body_carries_nothing_of_substance": not b}   # noqa: E731
    for path, method, label in (
            (f"{CS}/keys/query", "POST", "the device-key query endpoint"),
            (f"{CS}/keys/upload", "POST", "the device-key upload endpoint"),
            (f"{CS}/sendToDevice/com.example.pre/tx1", "PUT", "a send-to-device path"),
            (f"{CS}/room_keys/version", "POST", "the key-backup version endpoint")
    ):
        c.req("OPTIONS", path,
              headers={"Origin": "https://example.invalid",
                       "Access-Control-Request-Method": method},
              label=f"OPTIONS preflight on {label}", derive=empty)
