"""Step 6 — spec releases v1.10 and v1.11.

The subject of this step is the move of the content repository behind
authentication, and the handful of other behaviours the two releases add:

* `GET /_matrix/client/v1/media/{config,download,thumbnail}` (v1.11), the
  `animated` thumbnail parameter (v1.11), and the freeze of the deprecated
  `/_matrix/media/v3/{download,thumbnail}` endpoints that the release's
  warning box describes.  `/_matrix/media/v3/upload` and
  `/_matrix/media/v1/create` are explicitly *not* moved by this release.
* `Cross-Origin-Resource-Policy` (v1.4), which the steps before step 4 exclude
  because their releases predate it and which is compared here.
* `unsigned.membership` on client events (v1.11, MSC4115), which steps 1 and 2
  exclude for the same reason.
* `POST /keys/device_signing/upload` without user-interactive authentication
  for a user's *first* cross-signing keys (v1.11, MSC3967).
* `GET /relations` with `recurse` (v1.10, MSC3981).
* `POST /account/deactivate` with `erase` (v1.10).
* `GET /.well-known/matrix/support` (v1.10).

Things deliberately *not* pinned, because the release leaves them to the
implementation:

* the image format a thumbnail is encoded in (the published schema lists five
  content types and does not say which to use), so thumbnails are compared on
  their decoded geometry and their headers are not recorded at all;
* the `recursion_depth` a server reports, only that it is present when
  `recurse` was supplied and absent when it was not - a server may traverse
  further than the three levels the release recommends, and the three levels
  themselves are pinned by the chunk of a three-level chain;
* any `errcode` the release does not fix for the condition that produced it -
  the harness drops those on both sides;
* error text, anywhere.

`Content-Disposition` is *not* compared at this step.  Release v1.11 documents
that a download's `Content-Disposition` carries a file name, but it does not fix
the disposition *type*: the allow-list that decides `inline` against
`attachment` is MSC2702, which lands in release v1.12.  `mxconf/steps.py`
excludes the header through step 6 for that reason, so the download scenarios
below exercise the `/{fileName}` form and the range of content types without
scoring the header itself; step 7's release is where it starts being scored.
"""

from __future__ import annotations

import base64
import hashlib
import json
import struct

from mxconf.ctx import ScenarioAbort
from mxconf.mx import (CS, CS1, MEDIA, User, create_room, fact, invite, join, login,
                       mxc_parts, ok, png, q, register, send, upload)
from mxconf.registry import scenario

V = "10"                      # every room in this module is made explicitly

#: The reference is started with `enable_authenticated_media: true` for the
#: scenarios that carry this capability (runner.ORACLE_CONFIG); the candidate is
#: never configured differently, because for it the behaviour is the release's.
AUTHMEDIA = ("authmedia",)

# A source image whose sides are exact multiples of the thumbnail sizes asked
# for below, so that no scale factor ever lands between two integers and the
# expected geometry is a property of the release's rules rather than of one
# implementation's rounding.
SRC_W, SRC_H = 128, 96


# --------------------------------------------------------------------------- #
# Decoding what an endpoint returned
# --------------------------------------------------------------------------- #
def image_size(data: bytes) -> dict:
    """The geometry of a returned image.

    The thumbnail endpoint may answer with JPEG, PNG, APNG, GIF or WebP, and
    the release does not say which, so a scenario may only compare what the
    picture *is* - never the bytes an encoder happened to produce.  Every
    format the published schema allows is decoded here; anything else is
    reported as undecodable rather than as a byte count, because a byte count
    would be an encoder's property too.
    """
    if len(data) >= 24 and data[:8] == b"\x89PNG\r\n\x1a\n":
        # Also covers APNG: an APNG is a PNG with extra chunks, and its IHDR
        # carries the geometry in the same place.
        w, h = struct.unpack(">II", data[16:24])
        return {"image": True, "width": w, "height": h}
    if len(data) > 4 and data[:2] == b"\xff\xd8":
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            marker = data[i + 1]
            if marker in (0xC0, 0xC1, 0xC2, 0xC3):
                h = struct.unpack(">H", data[i + 5:i + 7])[0]
                w = struct.unpack(">H", data[i + 7:i + 9])[0]
                return {"image": True, "width": w, "height": h}
            i += 2 + struct.unpack(">H", data[i + 2:i + 4])[0]
    if len(data) >= 10 and data[:6] in (b"GIF87a", b"GIF89a"):
        w, h = struct.unpack("<HH", data[6:10])
        return {"image": True, "width": w, "height": h}
    if len(data) >= 30 and data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        tag = data[12:16]
        if tag == b"VP8 ":
            w = struct.unpack("<H", data[26:28])[0] & 0x3FFF
            h = struct.unpack("<H", data[28:30])[0] & 0x3FFF
            return {"image": True, "width": w, "height": h}
        if tag == b"VP8L":
            bits = int.from_bytes(data[21:25], "little")
            return {"image": True, "width": (bits & 0x3FFF) + 1,
                    "height": ((bits >> 14) & 0x3FFF) + 1}
        if tag == b"VP8X":
            return {"image": True,
                    "width": int.from_bytes(data[24:27], "little") + 1,
                    "height": int.from_bytes(data[27:30], "little") + 1}
    return {"image": False}


def ev_tag(e: dict) -> str:
    """A stable name for one event, so a derived list says which events came
    back without quoting their server-minted ids."""
    content = e.get("content") or {}
    etype = e.get("type")
    if etype == "m.room.member":
        return f"member/{e.get('state_key')}/{content.get('membership')}"
    if etype == "m.room.message":
        return content.get("body")
    return etype


def memberships(events) -> list:
    """`[event, unsigned.membership]` for each event, in the order returned."""
    return [[ev_tag(e), (e.get("unsigned") or {}).get("membership")]
            for e in (events or [])]


def joined_timeline(body: dict, key: str = "join") -> dict:
    rooms = ((body.get("rooms") or {}).get(key) or {})
    return {room: memberships((data.get("timeline") or {}).get("events"))
            for room, data in rooms.items()}


# --------------------------------------------------------------------------- #
# Upload helpers.  Release v1.11 moves downloads, not uploads: the upload
# endpoint a scenario uses is still the one under /_matrix/media.
# --------------------------------------------------------------------------- #
def put(c, u: User, data: bytes, content_type: str,
        filename: str | None = None) -> tuple[str, str]:
    """Upload `data` and return `(serverName, mediaId)`."""
    if filename is not None:
        return mxc_parts(upload(c, u, data, content_type, filename))
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=data,
              headers={"Content-Type": content_type}, check=False,
              label="upload without a file name")
    ok(c, r, "upload")
    return mxc_parts(r.json["content_uri"])


def source_image(c, u: User, name: str = "source.png") -> tuple[str, str]:
    return put(c, u, png(SRC_W, SRC_H), "image/png", name)


# --------------------------------------------------------------------------- #
# ed25519 (RFC 8032), for the cross-signing scenarios.  Made-up key material is
# derived from a label rather than drawn at random, so that two recordings of
# the same scenario upload exactly the same bytes.
# --------------------------------------------------------------------------- #
_P = 2 ** 255 - 19
_L = 2 ** 252 + 27742317777372353535851937790883648493
_D = (-121665 * pow(121666, _P - 2, _P)) % _P
_I = pow(2, (_P - 1) // 4, _P)


def _recover_x(y: int, sign: int) -> int:
    xx = (y * y - 1) * pow(_D * y * y + 1, _P - 2, _P)
    x = pow(xx, (_P + 3) // 8, _P)
    if (x * x - xx) % _P != 0:
        x = (x * _I) % _P
    if x % 2 != sign:
        x = _P - x
    return x


_BY = (4 * pow(5, _P - 2, _P)) % _P
_BX = _recover_x(_BY, 0)
_B = (_BX, _BY, 1, (_BX * _BY) % _P)


def _pt_add(p, r):
    a = (p[1] - p[0]) * (r[1] - r[0]) % _P
    b = (p[1] + p[0]) * (r[1] + r[0]) % _P
    cc = 2 * p[3] * r[3] * _D % _P
    d = 2 * p[2] * r[2] % _P
    return ((b - a) % _P * ((d - cc) % _P) % _P,
            (b + a) % _P * ((d + cc) % _P) % _P,
            (d - cc) % _P * ((d + cc) % _P) % _P,
            (b - a) % _P * ((b + a) % _P) % _P)


def _pt_mul(p, n: int):
    out = (0, 1, 1, 0)
    while n > 0:
        if n & 1:
            out = _pt_add(out, p)
        p = _pt_add(p, p)
        n >>= 1
    return out


def _pt_enc(p) -> bytes:
    zi = pow(p[2], _P - 2, _P)
    x, y = p[0] * zi % _P, p[1] * zi % _P
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


def _b64(data: bytes) -> str:
    """Unpadded base64: the encoding the specification uses for key material."""
    return base64.b64encode(data).decode("ascii").rstrip("=")


def _canonical(obj) -> bytes:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False).encode("utf-8")


def _seed_for(label: str) -> bytes:
    return hashlib.sha256(f"mxconf/authmedia/{label}".encode()).digest()


def sign_with(obj: dict, user_id: str, master: dict) -> dict:
    """Add `signatures[user_id][master key id]`, per Signing JSON."""
    unsigned = {k: v for k, v in obj.items() if k not in ("signatures", "unsigned")}
    sig = _b64(ed25519_sign(master["seed"], master["public"], _canonical(unsigned)))
    out = dict(obj)
    out["signatures"] = {user_id: {master["key_id"]: sig}}
    return out


def cross_signing_keys(user: User, tag: str = "1") -> dict:
    """A master key plus a self-signing and a user-signing key it has signed."""
    out: dict = {}
    for usage in ("master", "self_signing", "user_signing"):
        seed = _seed_for(f"{tag}/{usage}/{user.localpart}")
        public = ed25519_public(seed)
        key_id = "ed25519:" + _b64(public)
        out[usage] = {
            "seed": seed, "public": public, "key_id": key_id,
            "key": {"user_id": user.user_id, "usage": [usage],
                    "keys": {key_id: _b64(public)}},
        }
    master = out["master"]
    for usage in ("self_signing", "user_signing"):
        out[usage]["key"] = sign_with(out[usage]["key"], user.user_id, master)
    out["body"] = {"master_key": out["master"]["key"],
                   "self_signing_key": out["self_signing"]["key"],
                   "user_signing_key": out["user_signing"]["key"]}
    return out


def device_signing_upload(c, token: str, body: dict, **kw):
    return c.req("POST", f"{CS}/keys/device_signing/upload", token=token,
                 json_body=body, **kw)


def uia_password(session, user: User) -> dict:
    return {"type": "m.login.password", "session": session,
            "identifier": {"type": "m.id.user", "user": user.localpart},
            "password": user.password}


def served_keys(uploaded: dict):
    """Which of a user's cross-signing keys `/keys/query` serves, and whether
    they are the ones this scenario uploaded.  The key *ids* are made-up
    material, so what is compared is the relationship, not the base64."""
    wanted = {usage: (uploaded[usage]["key_id"] if usage in uploaded else None)
              for usage in ("master", "self_signing", "user_signing")}

    def derive(body):
        out = {}
        for usage, field in (("master", "master_keys"),
                             ("self_signing", "self_signing_keys"),
                             ("user_signing", "user_signing_keys")):
            served = body.get(field) or {}
            ids = sorted(k for entry in served.values()
                         for k in (entry.get("keys") or {}))
            out[usage + "_count"] = len(ids)
            out[usage + "_is_the_uploaded_one"] = (
                wanted[usage] is not None and wanted[usage] in ids)
        return out
    return derive


# --------------------------------------------------------------------------- #
# A room whose story gives every membership an event to be observed at
# --------------------------------------------------------------------------- #
def membership_room(c, host: User, guest: User) -> tuple:
    """A room whose last six events are, in order: a message sent before the
    invite, the invite, a message after it, the join, a message after that and
    one from the newcomer.  Fixing the last six means a `/sync` filtered to six
    timeline events sees exactly those, whatever state events `createRoom`
    itself produced."""
    room = create_room(c, host, room_version=V, preset="public_chat",
                       name="Membership")
    ids = {"m1": send(c, host, room, "am1",
                      {"msgtype": "m.text", "body": "before the invite"})}
    invite(c, host, room, guest.user_id)
    ids["m2"] = send(c, host, room, "am2",
                     {"msgtype": "m.text", "body": "after the invite"})
    join(c, guest, room)
    ids["m3"] = send(c, host, room, "am3",
                     {"msgtype": "m.text", "body": "after the join"})
    ids["m4"] = send(c, guest, room, "bm4",
                     {"msgtype": "m.text", "body": "from the newcomer"})
    return room, ids


def history(c, user: User, room: str, limit: int = 40) -> list:
    r = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=user.token,
              query={"dir": "b", "limit": limit}, check=False)
    ok(c, r, "messages")
    return (r.json or {}).get("chunk") or []


def event_id_of(c, user: User, room: str, etype: str) -> str:
    for e in history(c, user, room):
        if e.get("type") == etype:
            return e["event_id"]
    raise ScenarioAbort(f"no {etype} event in the room's history")


def sync_token(c, user: User, filt: str | None = None) -> str:
    query = {"timeout": 0}
    if filt:
        query["filter"] = filt
    r = c.req("GET", f"{CS}/sync", token=user.token, query=query, check=False)
    ok(c, r, "sync")
    return r.json["next_batch"]


SIX = json.dumps({"room": {"timeline": {"limit": 6}}})


def chunk_memberships(body):
    return {"chunk": memberships(body.get("chunk"))}


def one_membership(body):
    return {"event": memberships([body])[0]}


# --------------------------------------------------------------------------- #
# GET /_matrix/client/v1/media/config
# --------------------------------------------------------------------------- #
@scenario("am/media-config", tags=["media", "authmedia"], step=6, needs=AUTHMEDIA)
def media_config(c):
    u = register(c, "amcfg1")
    c.req("GET", f"{CS1}/media/config", token=u.token,
          label="the authenticated media config")
    c.req("GET", f"{CS1}/media/config", record_headers=False,
          label="the config is refused without an access token")
    c.req("GET", f"{CS1}/media/config", token="not-a-real-access-token",
          record_headers=False,
          label="and refused with a token the server never issued")
    c.req("GET", f"{CS1}/media/config", query={"access_token": u.token},
          label="the access_token query parameter is accepted")
    c.req("GET", f"{MEDIA}/config", token=u.token,
          label="the deprecated config endpoint still answers")
    c.req("GET", f"{MEDIA}/config", record_headers=False,
          label="and it too wants a token")


# --------------------------------------------------------------------------- #
# GET /_matrix/client/v1/media/download/{serverName}/{mediaId}
# --------------------------------------------------------------------------- #
@scenario("am/download-bytes", tags=["media", "authmedia"], step=6, needs=AUTHMEDIA)
def download_bytes(c):
    u = register(c, "amdl1")
    cases = [
        (b"hello media content\n", "text/plain", "hello.txt", "a text file"),
        (png(64, 48), "image/png", "pic.png", "a PNG"),
        (b"\x00\x01\x02\x03\xff\xfe", "application/octet-stream", "blob.bin",
         "arbitrary bytes"),
        (b"", "text/plain", "empty.txt", "an empty file"),
        (bytes(range(256)) * 400, "application/octet-stream", "chunky.bin",
         "a hundred kilobytes"),
    ]
    for data, ctype, name, what in cases:
        server, media_id = put(c, u, data, ctype, name)
        c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}",
              token=u.token, binary=True,
              label=f"{what} comes back unchanged")
    server, media_id = put(c, u, b"no file name was given", "text/plain")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}",
          token=u.token, binary=True,
          label="an upload made without a file name comes back too")


@scenario("am/download-content-types", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def download_content_types(c):
    """A download carries back the content type the upload was made with, and
    the release's cross-origin header, whatever the type is."""
    u = register(c, "amdl2")
    cases = [
        (b"plain text\n", "text/plain", "note.txt"),
        (b"one,two\n", "text/csv", "table.csv"),
        (b"body { }", "text/css", "sheet.css"),
        (b'{"a":1}', "application/json", "doc.json"),
        (png(8, 8), "image/png", "dot.png"),
        (b"<b>markup</b>", "text/html", "page.html"),
        (b"<svg/>", "image/svg+xml", "vector.svg"),
        (b"%PDF-1.4\n", "application/pdf", "paper.pdf"),
        (b"\x00\x01\x02", "application/octet-stream", "opaque.bin"),
    ]
    for data, ctype, name in cases:
        server, media_id = put(c, u, data, ctype, name)
        c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}",
              token=u.token, binary=True,
              label=f"{ctype} comes back as {ctype}")


@scenario("am/download-file-name", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def download_file_name(c):
    """The `/{fileName}` form of the authenticated download.

    The name the caller puts in the path reaches the client through
    `Content-Disposition`, which this step does not score (see the module
    docstring), so what is pinned here is the rest: the form exists, it serves
    the same bytes and the same content type as the plain form whatever name is
    asked for, and it is behind the same authentication.
    """
    u = register(c, "amdl3")
    pic = put(c, u, png(32, 24), "image/png", "original.png")
    page = put(c, u, b"<b>markup</b>", "text/html", "page.html")
    bare = put(c, u, b"this upload has no name", "text/plain")
    c.req("GET", f"{CS1}/media/download/{q(pic[0])}/{q(pic[1])}/renamed.png",
          token=u.token, binary=True,
          label="a name the caller chose serves the stored bytes")
    c.req("GET", f"{CS1}/media/download/{q(pic[0])}/{q(pic[1])}/original.png",
          token=u.token, binary=True,
          label="so does the name the upload was given")
    c.req("GET", f"{CS1}/media/download/{q(pic[0])}/{q(pic[1])}",
          token=u.token, binary=True,
          label="and so does no name at all")
    c.req("GET", f"{CS1}/media/download/{q(pic[0])}/{q(pic[1])}/"
                 f"{q('a name with spaces.png')}",
          token=u.token, binary=True,
          label="a name that has to be escaped in the path is accepted")
    c.req("GET", f"{CS1}/media/download/{q(page[0])}/{q(page[1])}/renamed.png",
          token=u.token, binary=True,
          label="the name in the path does not change the content type")
    c.req("GET", f"{CS1}/media/download/{q(bare[0])}/{q(bare[1])}/given.txt",
          token=u.token, binary=True,
          label="an upload made without a name can still be asked for by one")
    c.req("GET", f"{CS1}/media/download/{q(pic[0])}/{q(pic[1])}/renamed.png",
          binary=True, record_headers=False,
          label="the file name form is refused without a token")
    c.req("GET", f"{CS1}/media/download/{q(pic[0])}/{q(pic[1])}/renamed.png",
          query={"access_token": u.token}, binary=True,
          label="and it takes the access_token query parameter")


@scenario("am/download-needs-a-token", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def download_needs_a_token(c):
    u = register(c, "amdl4")
    v = register(c, "amdl5")
    server, media_id = put(c, u, b"only for the logged in\n", "text/plain",
                           "secret.txt")
    path = f"{CS1}/media/download/{q(server)}/{q(media_id)}"
    c.req("GET", path, binary=True, record_headers=False,
          label="a download without an access token is refused")
    c.req("GET", path, token="not-a-real-access-token", binary=True,
          record_headers=False,
          label="a token the server never issued is refused")
    c.req("GET", path, query={"access_token": "not-a-real-access-token"},
          binary=True, record_headers=False,
          label="the same through the access_token query parameter")
    c.req("GET", path, query={"access_token": u.token}, binary=True,
          label="a good token in the query string downloads the content")
    c.req("GET", path, token=v.token, binary=True,
          label="any authenticated user may download it, not just the uploader")
    c.req("GET", path, token=u.token, binary=True,
          label="and so may the uploader")


@scenario("am/download-unknown-media", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def download_unknown_media(c):
    u = register(c, "amdl6")
    server, media_id = put(c, u, b"the real one\n", "text/plain", "real.txt")
    unknown = "thismediaidwasneveruploaded"
    c.req("GET", f"{CS1}/media/download/{q(server)}/{unknown}", token=u.token,
          binary=True, record_headers=False,
          label="an unknown media id is not found")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{unknown}/name.txt",
          token=u.token, binary=True, record_headers=False,
          label="nor is it found through the file name form")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q('../../etc/passwd')}",
          token=u.token, binary=True, record_headers=False,
          label="a media id built to escape the store is not found")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{unknown}", binary=True,
          record_headers=False,
          label="an unknown media id without a token is refused for the token")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{unknown}",
          query={"access_token": u.token}, binary=True, record_headers=False,
          label="an unknown media id through the query parameter")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}",
          token=u.token, binary=True,
          label="while the media id that does exist still serves its bytes")


# --------------------------------------------------------------------------- #
# GET /_matrix/client/v1/media/thumbnail/{serverName}/{mediaId}
# --------------------------------------------------------------------------- #
def fits(asked_w, asked_h, src_w=None, src_h=None, method="crop"):
    """What the release fixes about a thumbnail's geometry.

    "The dimensions ... are a *guide*, and the server may return a thumbnail
    larger than the requested size ... Servers must never upscale."  So the
    exact pixels a server returns are its own choice among the sizes it keeps,
    and what can be compared is the shape of the answer: at least what was
    asked for (unless the source is smaller), never larger than the source, and
    the aspect the method promises.
    """
    src_w = src_w if src_w is not None else SRC_W
    src_h = src_h if src_h is not None else SRC_H
    # `scale` keeps the source's aspect, so for a box of another aspect inside
    # the source the server picks which side meets the box.  The release says
    # it must not return less than was asked; the reference rounds the other
    # way.  Neither reading is pinned there: that fact is compared only where
    # it is not in dispute.
    disputed = (method == "scale" and asked_w * src_h != asked_h * src_w
                and (asked_w < src_w or asked_h < src_h))

    def derive(data, _w=asked_w, _h=asked_h):
        got = image_size(data)
        gw, gh = got.get("width"), got.get("height")
        if not isinstance(gw, int) or not isinstance(gh, int):
            return dict(got, decodable=False)
        out = {
            "decodable": True,
            "never_upscaled": gw <= src_w and gh <= src_h,
            "is_an_image": got.get("image") is True,
        }
        if not disputed:
            out["at_least_asked_or_the_whole_source"] = (
                gw >= min(_w, src_w) and gh >= min(_h, src_h))
        return out
    return derive


@scenario("am/thumbnail-crop", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def thumbnail_crop(c):
    """"crop" returns the requested aspect, at least at the requested size."""
    u = register(c, "amth1")
    server, media_id = source_image(c, u)
    for w, h in [(32, 32), (96, 96), (48, 24), (64, 48), (16, 16),
                 (SRC_W, SRC_H)]:
        c.req("GET", f"{CS1}/media/thumbnail/{q(server)}/{q(media_id)}",
              token=u.token, query={"width": w, "height": h, "method": "crop"},
              binary=True, derive=fits(w, h), record_headers=False,
              label=f"crop {w}x{h} of a {SRC_W}x{SRC_H} image")


@scenario("am/thumbnail-scale", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def thumbnail_scale(c):
    """"scale" keeps the original aspect ratio, and never upscales."""
    u = register(c, "amth2")
    server, media_id = source_image(c, u)
    for w, h in [(32, 24), (64, 48), (64, 64), (96, 96), (16, 12),
                 (SRC_W, SRC_H), (640, 480)]:
        c.req("GET", f"{CS1}/media/thumbnail/{q(server)}/{q(media_id)}",
              token=u.token, query={"width": w, "height": h, "method": "scale"},
              binary=True, derive=fits(w, h, method="scale"), record_headers=False,
              label=f"scale {w}x{h} of a {SRC_W}x{SRC_H} image")


@scenario("am/thumbnail-animated", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def thumbnail_animated(c):
    """The `animated` parameter of release v1.11.

    The source cannot be animated, so the release says the server should behave
    as though `animated` were false.  What that fixes is the picture, not the
    container: the schema lets the server answer in any of five content types,
    so the headers are not recorded -- and not the exact pixels either, since
    the requested dimensions are a guide (`fits`).  What is compared is that
    both values of `animated` get the same still picture, at a geometry the
    release allows.
    """
    u = register(c, "amth3")
    server, media_id = source_image(c, u)
    cases = [(32, 32, "crop", True), (32, 32, "crop", False),
             (64, 48, "scale", True), (64, 48, "scale", False),
             (96, 96, "crop", True), (SRC_W, SRC_H, "scale", True)]
    for w, h, method, animated in cases:
        c.req("GET", f"{CS1}/media/thumbnail/{q(server)}/{q(media_id)}",
              token=u.token,
              query={"width": w, "height": h, "method": method,
                     "animated": animated},
              binary=True, derive=fits(w, h), record_headers=False,
              label=f"{method} {w}x{h} with animated={str(animated).lower()}")


@scenario("am/thumbnail-refused", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def thumbnail_refused(c):
    u = register(c, "amth4")
    server, media_id = source_image(c, u)
    path = f"{CS1}/media/thumbnail/{q(server)}/{q(media_id)}"
    crop = {"width": 32, "height": 32, "method": "crop"}
    c.req("GET", path, query=crop, binary=True, record_headers=False,
          label="a thumbnail without an access token is refused")
    c.req("GET", path, token="not-a-real-access-token", query=crop, binary=True,
          record_headers=False,
          label="a token the server never issued is refused")
    c.req("GET", path, query=dict(crop, access_token=u.token), binary=True,
          derive=fits(32, 32), record_headers=False,
          label="the access_token query parameter is accepted")
    c.req("GET",
          f"{CS1}/media/thumbnail/{q(server)}/thismediaidwasneveruploaded",
          token=u.token, query=crop, binary=True, record_headers=False,
          label="a thumbnail of an unknown media id is not found")
    c.req("GET", path, token=u.token,
          query={"width": 32, "height": 32, "method": "sideways"},
          binary=True, record_headers=False,
          label="a resizing method outside the published set is rejected")
    c.req("GET", path, token=u.token, query={"height": 32, "method": "crop"},
          binary=True, record_headers=False,
          label="width is a required parameter")
    c.req("GET", path, token=u.token, query={"width": 32, "method": "crop"},
          binary=True, record_headers=False,
          label="and so is height")


# --------------------------------------------------------------------------- #
# The freeze of the deprecated, unauthenticated endpoints
# --------------------------------------------------------------------------- #
@scenario("am/unauthenticated-download-frozen", tags=["media", "authmedia"],
          step=6, needs=AUTHMEDIA)
def unauthenticated_download_frozen(c):
    """Content uploaded after the freeze is no longer served by
    `/_matrix/media/v3/download`, whether or not a token is offered."""
    u = register(c, "amfz1")
    server, media_id = put(c, u, b"uploaded after the freeze\n", "text/plain",
                           "frozen.txt")
    old = f"{MEDIA}/download/{q(server)}/{q(media_id)}"
    c.req("GET", old, binary=True, record_headers=False,
          label="the deprecated download does not serve it")
    c.req("GET", old, token=u.token, binary=True, record_headers=False,
          label="not even to a client that offers a token")
    c.req("GET", old, query={"access_token": u.token}, binary=True,
          record_headers=False,
          label="nor through the access_token query parameter")
    c.req("GET", f"{old}/frozen.txt", binary=True, record_headers=False,
          label="nor through the deprecated file name form")
    c.req("GET", f"{old}/frozen.txt", token=u.token, binary=True,
          record_headers=False,
          label="nor through the deprecated file name form with a token")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}",
          token=u.token, binary=True,
          label="while the authenticated endpoint serves the same bytes")


@scenario("am/unauthenticated-thumbnail-frozen", tags=["media", "authmedia"],
          step=6, needs=AUTHMEDIA)
def unauthenticated_thumbnail_frozen(c):
    u = register(c, "amfz2")
    server, media_id = source_image(c, u, "frozen.png")
    old = f"{MEDIA}/thumbnail/{q(server)}/{q(media_id)}"
    new = f"{CS1}/media/thumbnail/{q(server)}/{q(media_id)}"
    crop = {"width": 32, "height": 32, "method": "crop"}
    scale = {"width": 64, "height": 48, "method": "scale"}
    c.req("GET", old, query=crop, binary=True, record_headers=False,
          label="the deprecated thumbnail does not serve it")
    c.req("GET", old, token=u.token, query=crop, binary=True,
          record_headers=False,
          label="not even to a client that offers a token")
    c.req("GET", old, token=u.token, query=scale, binary=True,
          record_headers=False,
          label="and not for the scale method either")
    c.req("GET", old, query=dict(crop, access_token=u.token), binary=True,
          record_headers=False,
          label="nor through the access_token query parameter")
    c.req("GET", new, token=u.token, query=crop, binary=True,
          derive=fits(32, 32), record_headers=False,
          label="while the authenticated endpoint crops it")
    c.req("GET", new, token=u.token, query=scale, binary=True,
          derive=fits(64, 48), record_headers=False,
          label="and scales it")


@scenario("am/upload-endpoint-unmoved", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def upload_endpoint_unmoved(c):
    """The release moves the download endpoints and leaves upload where it is."""
    u = register(c, "amup1")
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=b"still here\n",
              headers={"Content-Type": "text/plain"},
              query={"filename": "here.txt"},
              label="upload is still POST /_matrix/media/v3/upload")
    ok(c, r, "upload")
    server, media_id = mxc_parts(r.json["content_uri"])
    c.req("POST", f"{MEDIA}/upload", body=b"no token\n",
          headers={"Content-Type": "text/plain"}, record_headers=False,
          label="and it still refuses an upload without a token")
    c.req("POST", f"{CS1}/media/upload", token=u.token, body=b"moved?\n",
          headers={"Content-Type": "text/plain"}, record_headers=False,
          label="upload has not moved under /_matrix/client/v1/media")
    c.req("GET", f"{CS1}/media/upload", token=u.token, record_headers=False,
          label="there is no such endpoint to GET either")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}",
          token=u.token, binary=True,
          label="what was uploaded is downloaded through the new endpoint")
    c.req("GET", f"{MEDIA}/download/{q(server)}/{q(media_id)}", binary=True,
          record_headers=False,
          label="and not through the old one")


@scenario("am/async-upload-unmoved", tags=["media", "authmedia"], step=6,
          needs=AUTHMEDIA)
def async_upload_unmoved(c):
    """`POST /_matrix/media/v1/create` and its `PUT` are not moved either."""
    u = register(c, "amup2")
    r = c.req("POST", "/_matrix/media/v1/create", token=u.token, json_body={},
              label="create is still POST /_matrix/media/v1/create",
              drop=("unused_expires_at",))
    if not r.ok or not (r.json or {}).get("content_uri"):
        raise ScenarioAbort("the reference did not mint a media id to fill")
    server, media_id = mxc_parts(r.json["content_uri"])
    put_path = f"{MEDIA}/upload/{q(server)}/{q(media_id)}"
    c.req("PUT", put_path, token=u.token, body=b"filled in later\n",
          headers={"Content-Type": "text/plain"},
          query={"filename": "async.txt"},
          label="the reserved media id is filled by PUT")
    c.req("PUT", put_path, token=u.token, body=b"a second time\n",
          headers={"Content-Type": "text/plain"},
          query={"filename": "async.txt"}, record_headers=False,
          label="and cannot be filled twice")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}",
          token=u.token, binary=True,
          label="the content is downloaded through the new endpoint")
    c.req("GET", f"{MEDIA}/download/{q(server)}/{q(media_id)}", binary=True,
          record_headers=False,
          label="and the frozen endpoint does not serve it")
    c.req("PUT", f"{MEDIA}/upload/{q(server)}/thismediaidwasneverreserved",
          token=u.token, body=b"x", headers={"Content-Type": "text/plain"},
          record_headers=False,
          label="a media id that was never reserved cannot be filled")
    c.req("POST", f"{CS1}/media/create", token=u.token, json_body={},
          record_headers=False,
          label="create has not moved under /_matrix/client/v1/media")


# --------------------------------------------------------------------------- #
# unsigned.membership
# --------------------------------------------------------------------------- #
@scenario("am/unsigned-membership-sync", tags=["events", "membership"], step=6)
def unsigned_membership_sync(c):
    a = register(c, "amum1")
    b = register(c, "amum2")
    room, ids = membership_room(c, a, b)
    c.req("GET", f"{CS}/sync", token=a.token,
          query={"timeout": 0, "filter": SIX},
          label="the room's host sees their own membership at every event",
          derive=joined_timeline)
    since_b = sync_token(c, b, SIX)
    c.req("GET", f"{CS}/sync", token=b.token,
          query={"timeout": 0, "filter": SIX},
          label="the newcomer sees theirs change across the same events",
          derive=joined_timeline)
    since_a = sync_token(c, a, SIX)
    send(c, a, room, "am9", {"msgtype": "m.text", "body": "and one more"})
    r = c.req("GET", f"{CS}/sync", token=b.token,
              query={"timeout": 0, "since": since_b, "filter": SIX},
              label="an incremental sync carries it too",
              derive=joined_timeline)
    c.req("GET", f"{CS}/sync", token=a.token,
          query={"timeout": 0, "since": since_a, "filter": SIX},
          label="and carries it for the host",
          derive=joined_timeline)
    # The window for the departure starts where that sync ended, so the leave
    # is the only thing in it and the poll below has something definite to wait
    # for: a `timeout=0` sync taken straight after the leave would be a race
    # between the request and the server's own stream, and would score whichever
    # side of it the reference happened to land on.
    since_b2 = (r.json or {}).get("next_batch")
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=b.token, json_body={},
          check=False)
    c.req("GET", f"{CS}/sync", token=b.token,
          query={"timeout": 8000, "since": since_b2, "filter": SIX},
          timeout=60.0,
          label="a departure arrives in the leave section as a leave",
          derive=lambda body: joined_timeline(body, "leave"))


@scenario("am/unsigned-membership-messages", tags=["events", "membership"],
          step=6)
def unsigned_membership_messages(c):
    a = register(c, "amum3")
    b = register(c, "amum4")
    room, ids = membership_room(c, a, b)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token,
          query={"dir": "b", "limit": 6},
          label="the newcomer's history carries their membership at each event",
          derive=chunk_memberships)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 6},
          label="the host's history carries theirs",
          derive=chunk_memberships)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token,
          query={"dir": "b", "limit": 3},
          label="a shorter page is no different",
          derive=chunk_memberships)
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=b.token, json_body={},
          check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token,
          query={"dir": "b", "limit": 7},
          label="after leaving, the departure is a leave and the rest unchanged",
          derive=chunk_memberships)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 7},
          label="and the host still reads join throughout",
          derive=chunk_memberships)


@scenario("am/unsigned-membership-context", tags=["events", "membership"],
          step=6)
def unsigned_membership_context(c):
    a = register(c, "amum5")
    b = register(c, "amum6")
    room, ids = membership_room(c, a, b)
    create = event_id_of(c, a, room, "m.room.create")

    def ctx(body):
        return {
            "event": memberships([body.get("event") or {}]),
            "before": memberships(body.get("events_before")),
            "after": memberships(body.get("events_after")),
        }

    # `limit` bounds the *sum* of events_before and events_after and the release
    # does not say how a server splits it between them, so the only figure that
    # is the release's rather than an implementation's is zero - which the
    # release does fix, together with the requested event being returned anyway.
    for who, user, eid, what in [
        ("the newcomer", b, ids["m1"], "an event from before their invite"),
        ("the newcomer", b, ids["m2"], "an event from while they were invited"),
        ("the newcomer", b, ids["m3"], "an event from after they joined"),
        ("the newcomer", b, ids["m4"], "an event they sent themselves"),
        ("the host", a, ids["m1"], "an event of their own"),
        ("the host", a, create, "the event that created the room"),
    ]:
        c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(eid)}", token=user.token,
              query={"limit": 0},
              label=f"{who} in context around {what}", derive=ctx)
    c.req("GET", f"{CS}/rooms/{q(room)}/context/"
                 "$noSuchEventWasEverCreatedInThisRoom", token=a.token,
          query={"limit": 0}, record_headers=False,
          label="context around an event that does not exist")


@scenario("am/unsigned-membership-event", tags=["events", "membership"], step=6)
def unsigned_membership_event(c):
    a = register(c, "amum7")
    b = register(c, "amum8")
    room, ids = membership_room(c, a, b)
    create = event_id_of(c, a, room, "m.room.create")

    def one(body):
        return {"event": memberships([body])[0], "sender": body.get("sender")}

    for who, user, eid, what in [
        ("the newcomer", b, ids["m1"], "an event from before their invite"),
        ("the newcomer", b, ids["m2"], "an event from while they were invited"),
        ("the newcomer", b, ids["m3"], "an event from after they joined"),
        ("the newcomer", b, ids["m4"], "an event they sent themselves"),
        ("the newcomer", b, create, "the event that created the room"),
        ("the host", a, ids["m1"], "their own earliest message"),
        ("the host", a, create, "the event that created the room"),
    ]:
        c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(eid)}", token=user.token,
              label=f"{who} fetching {what}", derive=one)


@scenario("am/unsigned-membership-rejoin", tags=["events", "membership"], step=6)
def unsigned_membership_rejoin(c):
    """A user who joins, leaves and joins again reads a different membership on
    either side of each transition."""
    a = register(c, "amum9")
    b = register(c, "amuma")
    room = create_room(c, a, room_version=V, preset="public_chat", name="Rejoin")
    first = send(c, a, room, "r1", {"msgtype": "m.text", "body": "before"})
    join(c, b, room)
    during = send(c, a, room, "r2", {"msgtype": "m.text", "body": "during"})
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=b.token, json_body={},
          check=False)
    away = send(c, a, room, "r3", {"msgtype": "m.text", "body": "while away"})
    join(c, b, room)
    after = send(c, a, room, "r4", {"msgtype": "m.text", "body": "after"})
    # Seven is exactly the number of events this scenario sent, so the page
    # never reaches back into the ones `createRoom` produced.
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token,
          query={"dir": "b", "limit": 7},
          label="every transition shows in the history of the one who made it",
          derive=chunk_memberships)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 7},
          label="while the host reads join throughout",
          derive=chunk_memberships)
    for eid, what in [(first, "before the first join"),
                      (during, "between the join and the leave"),
                      (away, "between the leave and the second join"),
                      (after, "after the second join")]:
        c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(eid)}", token=b.token,
              label=f"an event sent {what}", derive=one_membership)


# --------------------------------------------------------------------------- #
# POST /keys/device_signing/upload
# --------------------------------------------------------------------------- #
@scenario("am/cross-signing-first-upload", tags=["cross-signing", "uia"], step=6)
def cross_signing_first_upload(c):
    """Release v1.11: a user's first cross-signing keys need no user-interactive
    authentication, and re-uploading exactly the same keys needs none either."""
    u = register(c, "amxs1")
    v = register(c, "amxs2")
    w = register(c, "amxs3")
    ks = cross_signing_keys(u, "1")
    device_signing_upload(c, u.token, ks["body"],
                          label="a first upload of all three keys is not "
                                "challenged")
    device_signing_upload(c, u.token, ks["body"],
                          label="and the identical upload again is not either")
    device_signing_upload(c, u.token, {"master_key": ks["master"]["key"]},
                          label="nor is the same master key on its own")
    c.req("POST", f"{CS}/keys/query", token=u.token,
          json_body={"device_keys": {u.user_id: []}},
          label="all three keys are served", derive=served_keys(ks))
    vk = cross_signing_keys(v, "1")
    device_signing_upload(c, v.token, {"master_key": vk["master"]["key"]},
                          label="another user's first master key alone is not "
                                "challenged")
    wk = cross_signing_keys(w, "1")
    device_signing_upload(c, w.token,
                          {"self_signing_key": wk["self_signing"]["key"]},
                          label="a self-signing key with no master key behind "
                                "it is rejected", record_headers=False)
    c.req("POST", f"{CS}/keys/query", token=w.token,
          json_body={"device_keys": {w.user_id: []}},
          label="so that user has none", derive=served_keys({}))


@scenario("am/cross-signing-replacement", tags=["cross-signing", "uia"], step=6)
def cross_signing_replacement(c):
    """Replacing a key that is already stored still goes through
    user-interactive authentication."""
    u = register(c, "amxs4")
    first = cross_signing_keys(u, "1")
    device_signing_upload(c, u.token, first["body"],
                          label="the first upload goes through unchallenged")
    second = cross_signing_keys(u, "2")
    r = device_signing_upload(c, u.token, second["body"],
                              label="replacing the master key is challenged")
    session = (r.json or {}).get("session")
    device_signing_upload(
        c, u.token,
        dict(second["body"],
             auth=dict(uia_password(session, u), password="not-the-password")),
        label="a wrong password does not complete the challenge",
        # `completed` and `params` are optional in the published auth response,
        # so a server that leaves them out is not wrong; `flows` is required.
        drop=("completed", "params"))
    device_signing_upload(
        c, u.token, dict(second["body"], auth=uia_password(session, u)),
        label="and the right one does")
    c.req("POST", f"{CS}/keys/query", token=u.token,
          json_body={"device_keys": {u.user_id: []}},
          label="the replacement is what is served now",
          derive=served_keys(second))
    third = cross_signing_keys(u, "3")
    mixed = {"master_key": second["master"]["key"],
             "self_signing_key": sign_with(
                 {k: v for k, v in third["self_signing"]["key"].items()
                  if k != "signatures"}, u.user_id, second["master"])}
    device_signing_upload(
        c, u.token, mixed,
        label="an unchanged master key with a new self-signing key is "
              "challenged too")
    c.req("POST", f"{CS}/keys/query", token=u.token,
          json_body={"device_keys": {u.user_id: []}},
          label="so an unanswered challenge has moved nothing",
          derive=served_keys(second))
    device_signing_upload(
        c, u.token, second["body"],
        label="while re-uploading the keys already stored is not challenged")


# --------------------------------------------------------------------------- #
# GET /relations with recurse
# --------------------------------------------------------------------------- #
def relation_chain(c, u: User, room: str, depth: int) -> list:
    """`depth` messages, each a reference to the one before it, under a root."""
    ids = [send(c, u, room, "rel0", {"msgtype": "m.text", "body": "root"})]
    for i in range(1, depth + 1):
        ids.append(send(c, u, room, f"rel{i}", {
            "msgtype": "m.text", "body": f"level {i}",
            "m.relates_to": {"rel_type": "m.reference", "event_id": ids[-1]},
        }))
    return ids


def relations(body) -> dict:
    """What the release fixes about a relations page.

    Not `recursion_depth`: the release says it "gives the actual depth to which
    the server recursed", so a server that traverses further than the three
    levels it recommends reports a larger number, and the published schema
    marks the field optional where the prose calls it required whenever
    `recurse` was supplied at all.  Neither its value nor its presence is
    therefore a difference between two conforming servers.  What *is* fixed is
    which events come back, which a chain three levels deep shows directly, and
    the ticket asks for the recursion rather than for the field.
    """
    return {
        "chunk": [(e.get("content") or {}).get("body")
                  for e in body.get("chunk") or []],
        "has_next_batch": "next_batch" in body,
    }


@scenario("am/relations-recurse", tags=["relations"], step=6)
def relations_recurse(c):
    """A two-level chain: `recurse=true` reaches the indirect child, and
    leaving it out does not."""
    u = register(c, "amrl1")
    room = create_room(c, u, room_version=V, name="Relations")
    root, child, grandchild = relation_chain(c, u, room, 2)
    base = f"{CS1}/rooms/{q(room)}/relations"
    c.req("GET", f"{base}/{q(root)}", token=u.token,
          label="without recurse only the direct child is returned",
          derive=relations)
    c.req("GET", f"{base}/{q(root)}", token=u.token, query={"recurse": False},
          label="recurse=false is the same", derive=relations)
    c.req("GET", f"{base}/{q(root)}", token=u.token, query={"recurse": True},
          label="recurse=true reaches the indirect child as well",
          derive=relations)
    c.req("GET", f"{base}/{q(child)}", token=u.token, query={"recurse": True},
          label="from the middle of the chain only what is below it",
          derive=relations)
    c.req("GET", f"{base}/{q(grandchild)}", token=u.token,
          query={"recurse": True},
          label="and from the bottom, nothing", derive=relations)
    c.req("GET", f"{base}/{q(root)}/m.reference", token=u.token,
          query={"recurse": True},
          label="filtering by relation type keeps both", derive=relations)
    c.req("GET", f"{base}/{q(root)}/m.reference/m.room.message", token=u.token,
          query={"recurse": True},
          label="and filtering by event type as well", derive=relations)
    c.req("GET", f"{base}/{q(root)}/m.annotation", token=u.token,
          query={"recurse": True},
          label="a relation type nothing uses returns an empty chunk",
          derive=relations)
    c.req("GET", f"{base}/{q(root)}/m.reference", token=u.token,
          label="without recurse the type filter sees the direct child only",
          derive=relations)


@scenario("am/relations-recurse-depth", tags=["relations"], step=6)
def relations_recurse_depth(c):
    """Three levels of relationship - the depth the release says servers SHOULD
    traverse - and what paginating through them looks like."""
    u = register(c, "amrl2")
    v = register(c, "amrl3")
    room = create_room(c, u, room_version=V, name="Deep")
    ids = relation_chain(c, u, room, 3)
    root = ids[0]
    base = f"{CS1}/rooms/{q(room)}/relations"
    c.req("GET", f"{base}/{q(root)}", token=u.token, query={"recurse": True},
          label="three levels of relationship are all reached",
          derive=relations)
    c.req("GET", f"{base}/{q(root)}", token=u.token,
          label="while a direct query sees one", derive=relations)
    c.req("GET", f"{base}/{q(ids[1])}", token=u.token, query={"recurse": True},
          label="two levels below the second event", derive=relations)
    c.req("GET", f"{base}/{q(root)}", token=u.token,
          query={"recurse": True, "limit": 1},
          label="a page of one carries a token for the next", derive=relations)
    c.req("GET", f"{base}/{q(root)}", token=u.token,
          query={"recurse": True, "limit": 2},
          label="and a page of two", derive=relations)
    c.req("GET", f"{base}/$nosuchparenteventwasevercreatedhere", token=u.token,
          query={"recurse": True}, record_headers=False,
          label="an unknown parent event is not found")
    c.req("GET", f"{base}/{q(root)}", token=v.token, query={"recurse": True},
          drop=("errcode",), record_headers=False,
          label="and a user who cannot read the room gets nothing")


# --------------------------------------------------------------------------- #
# POST /account/deactivate with erase
# --------------------------------------------------------------------------- #
@scenario("am/deactivate-erase", tags=["account"], step=6)
def deactivate_erase(c):
    u = register(c, "amdz1", device_id="FIRST")
    second = login(c, u, device_id="SECOND")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/com.example.kept",
          token=u.token, json_body={"kept": True}, check=False)
    r = c.req("POST", f"{CS}/account/deactivate", token=u.token,
              json_body={"erase": True},
              label="deactivation is challenged before anything happens")
    session = (r.json or {}).get("session")
    c.req("POST", f"{CS}/account/deactivate", token=u.token,
          json_body={"erase": True,
                     "auth": dict(uia_password(session, u),
                                  password="not-the-password")},
          label="a wrong password does not complete it",
          drop=("completed", "params"))
    c.req("GET", f"{CS}/account/whoami", token=u.token,
          label="so the account is still there")
    c.req("POST", f"{CS}/account/deactivate", token=u.token,
          json_body={"erase": True, "auth": uia_password(session, u)},
          label="and with the password the account is deactivated and erased")
    c.req("GET", f"{CS}/account/whoami", token=u.token,
          label="the token that did it stops working")
    c.req("GET", f"{CS}/account/whoami", token=second,
          label="and so does the account's other device")
    c.req("POST", f"{CS}/login", json_body={
        "type": "m.login.password",
        "identifier": {"type": "m.id.user", "user": u.localpart},
        "password": u.password}, drop=("errcode",),
        label="logging in again is refused")
    c.req("GET", f"{CS}/register/available", query={"username": u.localpart},
          label="the localpart is not handed back out")
    c.req("POST", f"{CS}/register", json_body={
        "username": u.localpart, "password": "a-brand-new-password",
        "auth": {"type": "m.login.dummy"}},
        label="and it cannot be registered again")


@scenario("am/deactivate-erase-content", tags=["account"], step=6)
def deactivate_erase_content(c):
    """`erase: true` means someone who joins the room after the request is
    served redacted copies of what the erased account sent, while a member who
    could already see them still sees them."""
    a = register(c, "amdz2")
    e = register(c, "amdz3")
    late = register(c, "amdz4")
    room = create_room(c, a, room_version=V, preset="public_chat", name="Erase")
    join(c, e, room)
    kept = send(c, a, room, "k1", {"msgtype": "m.text", "body": "from a member"})
    erased = send(c, e, room, "e1",
                  {"msgtype": "m.text", "body": "from the account to be erased"})
    r = c.req("POST", f"{CS}/account/deactivate", token=e.token,
              json_body={"erase": True}, check=False)
    c.req("POST", f"{CS}/account/deactivate", token=e.token,
          json_body={"erase": True,
                     "auth": uia_password((r.json or {}).get("session"), e)},
          label="the account is deactivated with erasure asked for")
    join(c, late, room)

    def body_of(ev):
        return {"content": ev.get("content"), "sender": ev.get("sender"),
                "type": ev.get("type")}

    def message_bodies(b):
        return {"messages": [[ev.get("sender"),
                              (ev.get("content") or {}).get("body")]
                             for ev in b.get("chunk") or []
                             if ev.get("type") == "m.room.message"]}

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(erased)}", token=a.token,
          label="a member who was there still reads what was sent",
          derive=body_of)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(erased)}", token=late.token,
          label="someone who joined afterwards is served a redacted copy",
          derive=body_of)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(kept)}", token=late.token,
          label="which does not touch anybody else's events", derive=body_of)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=late.token,
          query={"dir": "b", "limit": 20},
          label="the same through the newcomer's history",
          derive=message_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 20},
          label="and through the older member's", derive=message_bodies)


def _no_support(b):
    """v1.10 documents the 404 and says nothing whatever about its body,
    so what is compared is that no support information is served -- not the
    reference's incidental choice to send no body at all.  A server that
    routes unknown paths through a Matrix handler answering M_UNRECOGNIZED
    is conforming and must not lose these checks (NOTES 196).
    """
    d = b if isinstance(b, dict) else {}
    return {"no_support_information_is_served":
            not d.get("contacts") and not d.get("support_page")}

# --------------------------------------------------------------------------- #
# GET /.well-known/matrix/support
# --------------------------------------------------------------------------- #
@scenario("am/well-known-support", tags=["discovery"], step=6)
def well_known_support(c):
    """The support endpoint of release v1.10.  Nothing here configures a
    support page, so the release's answer is that there is none; the endpoint
    carries no security scheme, so a token must not change that into a refusal.
    """
    u = register(c, "amwk1")
    c.req("GET", "/.well-known/matrix/support", record_headers=False,
          label="no support information is configured", derive=_no_support)
    c.req("GET", "/.well-known/matrix/support", token=u.token,
          record_headers=False,
          label="offering a token does not change the answer", derive=_no_support)
    c.req("GET", "/.well-known/matrix/support", token="not-a-real-access-token",
          record_headers=False,
          label="and neither does offering a bad one, since none is required", derive=_no_support)
    c.req("GET", "/_matrix/client/versions", record_headers=False,
          label="the releases of this step are advertised",
          derive=lambda b: {
              "advertises_v1.10": "v1.10" in (b.get("versions") or []),
              "advertises_v1.11": "v1.11" in (b.get("versions") or []),
          })


# --------------------------------------------------------------------------- #
# The federation media endpoints, which answer multipart/mixed
# --------------------------------------------------------------------------- #
@scenario("am/fed-media-multipart", tags=["media", "authmedia", "federation"],
          step=6, needs=AUTHMEDIA + ("tls",))
def fed_media_multipart(c):
    """`GET /_matrix/federation/v1/media/download/{mediaId}`.

    Release v1.11 gives the federation media endpoints a shape of their own:
    "the response is a multipart/mixed ... the first part being a JSON object
    ... and the second part being the media itself".  No client request reaches
    them, so the request is made by a server; and what is scored is the shape of
    the answer -- two parts, the first JSON, the second the bytes that were
    uploaded -- never the boundary string, which every implementation picks for
    itself.
    """
    from mxconf.fakefed import (FakeFederation, federation_url, pick_port,
                                port_hint)

    server = c.extra["server"]
    fake = FakeFederation(port=pick_port(port_hint(server) + 6))
    c.extra["fake"] = fake
    fake.start()
    c.syms.aliases[fake.server_name] = "<FAKE>"
    url, name = federation_url(server), server.server_name

    u = register(c, "amfm1")
    content = png(SRC_W, SRC_H)
    server_name, media_id = put(c, u, content, "image/png", "federated.png")

    status, headers, raw = fake.request(
        url, name, "GET",
        f"/_matrix/federation/v1/media/download/{q(media_id)}", None, raw=True)
    ctype = (headers.get("Content-Type") or headers.get("content-type") or "")
    boundary = ""
    for piece in ctype.split(";"):
        piece = piece.strip()
        if piece.startswith("boundary="):
            boundary = piece[len("boundary="):].strip('"')
    parts = raw.split(b"--" + boundary.encode()) if boundary else []
    bodies = []
    for part in parts:
        if b"\r\n\r\n" in part:
            bodies.append(part.split(b"\r\n\r\n", 1)[1].rstrip(b"\r\n"))
        elif b"\n\n" in part:
            bodies.append(part.split(b"\n\n", 1)[1].rstrip(b"\n"))
    first_is_json = False
    if bodies:
        try:
            first_is_json = isinstance(json.loads(bodies[0].decode()), dict)
        except Exception:                                        # noqa: BLE001
            first_is_json = False

    fact(c, {
        "status": status,
        "content_type_is_multipart_mixed": ctype.split(";")[0].strip()
                                           == "multipart/mixed",
        "it_names_a_boundary": bool(boundary),
        "there_are_two_parts": len(bodies) == 2,
        "the_first_part_is_a_json_object": first_is_json,
        "the_second_part_is_the_uploaded_bytes":
            len(bodies) > 1 and bodies[1] == content,
    }, label="the federation download is a multipart answer", token=u.token)

    status2, headers2, raw2 = fake.request(
        url, name, "GET",
        f"/_matrix/federation/v1/media/thumbnail/{q(media_id)}"
        "?width=32&height=32&method=crop", None, raw=True)
    ctype2 = (headers2.get("Content-Type") or headers2.get("content-type") or "")
    # The download half above parses its own parts inline and is left exactly as
    # it was, so its recording cannot shift.  This half parses the same way and
    # then asks what the release actually fixes about the answer's shape: two
    # parts, the first JSON, the second an image no larger than the source.
    # Asserting only multipart/mixed and a non-empty body let '{}' plus one byte
    # pass (NOTES 196).
    _b2 = ""
    for _piece in ctype2.split(";"):
        _piece = _piece.strip()
        if _piece.startswith("boundary="):
            _b2 = _piece[len("boundary="):].strip('"')
    _parts2 = raw2.split(b"--" + _b2.encode()) if _b2 else []
    _bodies2 = []
    for _part in _parts2:
        if b"\r\n\r\n" in _part:
            _bodies2.append(_part.split(b"\r\n\r\n", 1)[1].rstrip(b"\r\n"))
        elif b"\n\n" in _part:
            _bodies2.append(_part.split(b"\n\n", 1)[1].rstrip(b"\n"))
    _first2_is_json = False
    if _bodies2:
        try:
            _first2_is_json = isinstance(json.loads(_bodies2[0].decode()), dict)
        except Exception:                                    # noqa: BLE001
            _first2_is_json = False
    _thumb = image_size(_bodies2[1]) if len(_bodies2) > 1 else {}
    _tw, _th = _thumb.get("width"), _thumb.get("height")
    fact(c, {
        "status": status2,
        "content_type_is_multipart_mixed": ctype2.split(";")[0].strip()
                                           == "multipart/mixed",
        "it_names_a_boundary": bool(_b2),
        "there_are_two_parts": len(_bodies2) == 2,
        "the_first_part_is_a_json_object": _first2_is_json,
        "the_second_part_is_an_image": _thumb.get("image") is True,
        "at_least_asked_or_the_whole_source":
            isinstance(_tw, int) and isinstance(_th, int)
            and _tw >= min(32, SRC_W) and _th >= min(32, SRC_H),
        "never_upscaled":
            isinstance(_tw, int) and isinstance(_th, int)
            and _tw <= SRC_W and _th <= SRC_H,
    }, label="and so is the federation thumbnail", token=u.token)

    status3, _h3, _r3 = fake.request(
        url, name, "GET",
        "/_matrix/federation/v1/media/download/nosuchmediaid", None, raw=True)
    fact(c, {"status_class": status3 // 100},
         label="media this server does not have is refused", token=u.token)

# --------------------------------------------------------------------------- #
# The pre-flight, on the routes this milestone adds
# --------------------------------------------------------------------------- #
# Every step's ticket restates the release's CORS requirement -- every endpoint
# answers an `OPTIONS` request, and answering it performs none of the endpoint's
# own logic -- and step 1 scores it on the routes that existed then.  A route
# added later is a route that can be added without it, so each milestone asks
# the same two questions of its own.  Nothing here needs a room, a user or an
# upload to exist: that is the point of the second question.


@scenario("am/options-preflight", tags=["cors"], step=6)
def am_options_preflight(c):
    """`OPTIONS` on the endpoints this release adds."""
    empty = lambda b: {"body_carries_nothing_of_substance": not b}   # noqa: E731
    for path, method, label in (
            (f"{CS1}/media/config", "GET", "the authenticated media config endpoint"),
            (f"{CS1}/media/download/example.invalid/nothing", "GET",
          "an authenticated media download path"),
            (f"{CS1}/media/thumbnail/example.invalid/nothing", "GET",
          "an authenticated thumbnail path")
    ):
        c.req("OPTIONS", path,
              headers={"Origin": "https://example.invalid",
                       "Access-Control-Request-Method": method},
              label=f"OPTIONS preflight on {label}", derive=empty)
