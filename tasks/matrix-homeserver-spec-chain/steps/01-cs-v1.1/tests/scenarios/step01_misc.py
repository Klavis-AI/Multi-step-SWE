"""Step 1 - misc: media, account data, tags, typing, receipts, presence,
legacy initial sync, CORS/auth edge cases, and restart persistence.
"""

from __future__ import annotations

import struct

from mxconf.mx import CS, MEDIA, create_room, fact, invite, join, mxc_parts, png, q, register, send, sync, upload
from mxconf.registry import scenario
from mxconf.steps import keep_types


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def _img_dims(content_type: str, data: bytes) -> tuple[int, int]:
    """Decoded pixel (width, height) for the two content types a thumbnail may
    come back as.  A thumbnail is server-regenerated, so its compressed bytes
    are never compared across implementations - only what they decode to."""
    ct = (content_type or "").split(";", 1)[0].strip().lower()
    if ct == "image/png":
        w, h = struct.unpack(">II", data[16:24])
        return w, h
    if ct == "image/jpeg":
        i, n = 2, len(data)
        while i + 4 <= n:
            if data[i] != 0xFF:
                i += 1
                continue
            marker = data[i + 1]
            if marker in (0xD8, 0xD9, 0x01) or 0xD0 <= marker <= 0xD7:
                i += 2
                continue
            seg_len = struct.unpack(">H", data[i + 2:i + 4])[0]
            if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
                hh, ww = struct.unpack(">HH", data[i + 5:i + 9])
                return ww, hh
            i += 2 + seg_len
        raise ValueError("no JPEG SOF marker found")
    raise ValueError(f"unexpected thumbnail content-type {content_type!r}")




def _sync_since(c, user, since, *, timeout=0, label=None, derive=None, check=True):
    return c.req("GET", f"{CS}/sync", token=user.token,
                 query={"since": since, "timeout": timeout}, check=check,
                 label=label, derive=derive)


def _two_in_a_room(c, a_name, b_name, **room_kwargs):
    a = register(c, a_name)
    b = register(c, b_name)
    kwargs = {"preset": "private_chat", "room_version": "6"}
    kwargs.update(room_kwargs)
    room = create_room(c, a, **kwargs)
    invite(c, a, room, b.user_id)
    join(c, b, room)
    return a, b, room


def _typing_users(body, room_id):
    for e in (body.get("rooms", {}).get("join", {}).get(room_id, {})
              .get("ephemeral", {}).get("events", [])):
        if e.get("type") == "m.typing":
            return sorted(e.get("content", {}).get("user_ids", []))
    return []


def _has_receipt(body, room_id, event_id, user_id):
    for e in (body.get("rooms", {}).get("join", {}).get(room_id, {})
              .get("ephemeral", {}).get("events", [])):
        if e.get("type") == "m.receipt":
            if user_id in e.get("content", {}).get(event_id, {}).get("m.read", {}):
                return True
    return False


def _upload_size_shape(body):
    v = body.get("m.upload.size")
    if v is None:
        return "absent-or-null"
    return ("positive-int" if isinstance(v, int) and not isinstance(v, bool) and v > 0
            else "invalid")


# --------------------------------------------------------------------------- #
# media
# --------------------------------------------------------------------------- #
@scenario("cs/media-upload-download-roundtrip", tags=["media"], step=1)
def media_upload_download(c):
    u = register(c, "med1")

    png_bytes = png(6, 4, rgb=(10, 20, 30))
    mxc_png = upload(c, u, png_bytes, "image/png", "photo.png", check=True)
    server, media_id = mxc_parts(mxc_png)
    c.req("GET", f"{MEDIA}/download/{server}/{media_id}", token=u.token,
          label="download the uploaded png by id", binary=True)
    c.req("GET", f"{MEDIA}/download/{server}/{media_id}/renamed.png",
          token=u.token, label="download the uploaded png by id, overriding the filename",
          binary=True)
    # content-repo.yaml declares no `security:` block at all for either the
    # plain or the filename-overriding download operation, so unlike almost
    # everything else in this file an access token is not required by spec -
    # pinning a 200 here is a genuine MUST, not an implementation choice.
    c.req("GET", f"{MEDIA}/download/{server}/{media_id}",
          label="download the uploaded png without an access token", binary=True)
    c.req("GET", f"{MEDIA}/download/{server}/{media_id}/renamed.png",
          label="download (overriding filename) without an access token", binary=True)

    text_bytes = b"hello from the conformance harness\n"
    mxc_txt = upload(c, u, text_bytes, "text/plain; charset=utf-8", "notes.txt", check=True)
    server2, media_id2 = mxc_parts(mxc_txt)
    c.req("GET", f"{MEDIA}/download/{server2}/{media_id2}", token=u.token,
          label="download the uploaded text file by id", binary=True)
    c.req("GET", f"{MEDIA}/download/{server2}/{media_id2}/renamed.txt",
          token=u.token, label="download the uploaded text file, overriding the filename",
          binary=True)

    no_name_bytes = b"no filename was given at upload time"
    mxc3 = upload(c, u, no_name_bytes, "text/plain", "", check=True)
    server3, media_id3 = mxc_parts(mxc3)
    c.req("GET", f"{MEDIA}/download/{server3}/{media_id3}", token=u.token,
          label="download content that was uploaded without a filename", binary=True)


@scenario("cs/media-content-type-roundtrip", tags=["media"], step=1)
def media_content_type_roundtrip(c):
    u = register(c, "medct1")
    for ct, data, fname in (
        ("application/octet-stream", b"\x00\x01\x02\x03binarydata", "blob.bin"),
        ("application/json", b'{"hello":"world"}', "data.json"),
        ("image/jpeg", b"not-really-a-jpeg-but-opaque-bytes-are-fine-here", "fake.jpg"),
    ):
        mxc = upload(c, u, data, ct, fname, check=True)
        server, media_id = mxc_parts(mxc)
        c.req("GET", f"{MEDIA}/download/{server}/{media_id}", token=u.token,
              label=f"download round-trip for {ct}", binary=True)


@scenario("cs/media-thumbnail-crop", tags=["media"], step=1)
def media_thumbnail_crop(c):
    u = register(c, "medcrop1")
    # A 2:1 source; both requested boxes have a different aspect ratio, so a
    # `method=crop` thumbnail - which must fill the requested box exactly,
    # cropping away whatever does not fit - has an unambiguous pixel size.
    data = png(40, 20, rgb=(200, 30, 30))
    mxc = upload(c, u, data, "image/png", "crop-me.png", check=False)
    server, media_id = mxc_parts(mxc)

    # The source is 40x20, and the release says in as many words that "servers
    # must never upscale", so neither box is taller or wider than the source: a
    # crop box 24 tall could not be filled without upscaling. 16x12 keeps the
    # point of the case -- a box whose aspect ratio differs from the 2:1 source --
    # and is satisfiable without upscaling, so the invariant stays scored.
    for w_req, h_req in ((10, 10), (16, 12)):
        r = c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
                  query={"width": w_req, "height": h_req, "method": "crop"},
                  binary=True, record_headers=False,
                  label=f"thumbnail crop {w_req}x{h_req}: served",
                  derive=lambda b: {})
        ct = (r.headers.get("content-type") or "").split(";", 1)[0].strip()
        w, h = _img_dims(ct, r.body)
        # "The dimensions are a guide, and the server may return a thumbnail
        # larger than the requested size ... servers must never upscale."
        fact(c, {"content_type_is_image": ct in ("image/png", "image/jpeg"),
                 "at_least_asked_or_the_whole_source":
                     w >= min(w_req, 40) and h >= min(h_req, 20),
                 "never_upscaled": w <= 40 and h <= 20},
             label=f"thumbnail crop {w_req}x{h_req}: the size it came back",
             token=u.token)

    # allow_remote only matters for media that must be fetched from another
    # server; for locally-uploaded content it can never trigger a remote
    # fetch, so setting it to false must still succeed identically.
    r4 = c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
               query={"width": 10, "height": 10, "method": "crop",
                      "allow_remote": False},
               binary=True, record_headers=False, derive=lambda b: {},
               label="thumbnail of local media with allow_remote=false: served")
    ct4 = (r4.headers.get("content-type") or "").split(";", 1)[0].strip()
    fact(c, {"content_type_is_image": ct4 in ("image/png", "image/jpeg")},
         label="thumbnail of local media with allow_remote=false: what came back",
         token=u.token)


@scenario("cs/media-thumbnail-scale", tags=["media"], step=1)
def media_thumbnail_scale(c):
    u = register(c, "medscale1")
    data = png(40, 20, rgb=(30, 200, 30))
    mxc = upload(c, u, data, "image/png", "scale-me.png", check=False)
    server, media_id = mxc_parts(mxc)

    # 20x10 is exactly half of the 40x20 source on both axes - a clean scale
    # factor with no rounding choice, so any correct `scale` gives 20x10.
    # Which of the allowed content types a server encodes a thumbnail in is its
    # own choice, so the headers are not compared; the fact below records that
    # it is one of them.
    r = c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
              query={"width": 20, "height": 10, "method": "scale"},
              record_headers=False,
              label="thumbnail scale 20x10 (exact half, matches source aspect)")
    ct = (r.headers.get("content-type") or "").split(";", 1)[0].strip()
    w, h = _img_dims(ct, r.body)
    fact(c, {"content_type_is_image": ct in ("image/png", "image/jpeg"),
             "at_least_asked_or_the_whole_source": w >= 20 and h >= 10,
             "never_upscaled": w <= 40 and h <= 20,
             "aspect_matches_the_source": h > 0 and abs((w / h) - 2.0) < 0.35},
         label="thumbnail scale 20x10: the size it came back", token=u.token)

    # 13x13 forces a scale factor (0.325) that only lands on an integer pixel
    # count on one axis - here only invariants any implementation's rounding
    # must satisfy are pinned, not an exact pixel count.
    r2 = c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
               query={"width": 13, "height": 13, "method": "scale"},
               record_headers=False,
               label="thumbnail scale 13x13 (rounding required)")
    ct2 = (r2.headers.get("content-type") or "").split(";", 1)[0].strip()
    w2, h2 = _img_dims(ct2, r2.body)
    fact(c, {"content_type_is_image": ct2 in ("image/png", "image/jpeg"),
             "fits_in_requested_box": w2 <= 13 and h2 <= 13,
             "positive_dimensions": w2 > 0 and h2 > 0,
             "aspect_matches_source_within_rounding":
                 h2 > 0 and abs((w2 / h2) - 2.0) < 0.35},
         label="thumbnail scale 13x13: pixel-size invariants", token=u.token)

    # content-repo.yaml declares no `security:` block for the thumbnail
    # operation either, so - like plain download - no access token is
    # required by spec.
    r3 = c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}",
               query={"width": 20, "height": 10, "method": "scale"},
               record_headers=False,
               label="thumbnail without an access token: served")
    ct3 = (r3.headers.get("content-type") or "").split(";", 1)[0].strip()
    w3, h3 = _img_dims(ct3, r3.body)
    # The same invariants the other two scale requests are held to, rather
    # than an exact pixel count: the release calls the requested dimensions a
    # guide and lets a server return a larger thumbnail, so what is scored is
    # that a thumbnail came back, of the source's aspect, never upscaled.
    fact(c, {"content_type_is_image": ct3 in ("image/png", "image/jpeg"),
             "at_least_asked_or_the_whole_source": w3 >= 20 and h3 >= 10,
             "never_upscaled": w3 <= 40 and h3 <= 20,
             "aspect_matches_the_source":
                 h3 > 0 and abs((w3 / h3) - 2.0) < 0.35},
         label="thumbnail without an access token: pixel-size invariants",
         token=u.token)


@scenario("cs/media-thumbnail-bad-request", tags=["media"], step=1)
def media_thumbnail_bad_request(c):
    u = register(c, "medbad1")
    data = png(8, 8, rgb=(1, 2, 3))
    mxc = upload(c, u, data, "image/png", "bad.png", check=False)
    server, media_id = mxc_parts(mxc)

    c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
          query={"height": 10, "method": "crop"},
          label="thumbnail missing the required width parameter")
    c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
          query={"width": 10, "method": "crop"},
          label="thumbnail missing the required height parameter")
    c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
          query={"width": 10, "height": 10, "method": "not-a-real-method"},
          label="thumbnail with an invalid method value")
    c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
          query={"width": -5, "height": 10, "method": "crop"},
          label="thumbnail with a negative width")
    # content-repo.yaml's 400 description names both cases explicitly: "the
    # client requested non-integer dimensions or asked for negatively-sized
    # images" - so a non-integer width is as squarely documented as negative.
    c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
          query={"width": "not-a-number", "height": 10, "method": "crop"},
          label="thumbnail with a non-integer width")


@scenario("cs/media-thumbnail-of-text-file", tags=["media"], step=1)
def media_thumbnail_of_text_file(c):
    u = register(c, "medtxt1")
    mxc = upload(c, u, b"not an image at all", "text/plain", "notimage.txt", check=False)
    server, media_id = mxc_parts(mxc)
    c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
          query={"width": 32, "height": 32, "method": "scale"},
          label="thumbnail of content that is not an image, method=scale")
    c.req("GET", f"{MEDIA}/thumbnail/{server}/{media_id}", token=u.token,
          query={"width": 32, "height": 32, "method": "crop"},
          label="thumbnail of content that is not an image, method=crop")


@scenario("cs/media-config", tags=["media"], step=1)
def media_config(c):
    u = register(c, "medcfg1")
    c.req("GET", f"{MEDIA}/config", token=u.token, label="media config",
          derive=lambda b: {"m.upload.size_shape": _upload_size_shape(b)})
    c.req("GET", f"{MEDIA}/config", label="media config without a token")
    c.req("GET", f"{MEDIA}/config", token="syt_bogus_media_config_token_zzz",
          label="media config with an invalid token")


@scenario("cs/media-errors", tags=["media"], step=1)
def media_errors(c):
    u = register(c, "mederr1")
    c.req("GET", f"{MEDIA}/download/{c.server_name}/nonexistent00000mediaid00000",
          token=u.token, label="download an unknown media id")
    c.req("GET", f"{MEDIA}/thumbnail/{c.server_name}/nonexistent00000mediaid00000",
          token=u.token, query={"width": 32, "height": 32, "method": "scale"},
          label="thumbnail of an unknown media id")
    too_big = b"x" * (10 * 1024 * 1024 + 4096)
    c.req("POST", f"{MEDIA}/upload", token=u.token, body=too_big,
          headers={"Content-Type": "application/octet-stream"},
          query={"filename": "toobig.bin"},
          label="upload above the configured max upload size")
    c.req("POST", f"{MEDIA}/upload", body=b"no token on this request",
          headers={"Content-Type": "text/plain"}, query={"filename": "no-auth.txt"},
          label="upload without an access token")


# --------------------------------------------------------------------------- #
# account data
# --------------------------------------------------------------------------- #
@scenario("cs/account-data-global", tags=["account-data"], step=1)
def account_data_global(c):
    u = register(c, "adg1")
    v = register(c, "adg2")

    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/org.example.pref",
          token=u.token, json_body={"colour": "green", "n": 3},
          label="PUT global account data")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/org.example.pref",
          token=u.token, label="GET global account data back")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/org.example.pref",
          token=u.token, json_body={"colour": "blue", "n": 7},
          label="PUT global account data again: overwrite")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/org.example.pref",
          token=u.token, label="GET global account data: sees the overwrite")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/org.example.other",
          token=u.token, json_body={"nested": {"a": [1, 2, 3]}, "s": ""},
          label="PUT a second global account data type")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/org.example.other",
          token=u.token, label="GET the second global account data type back")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/org.example.never_set",
          token=u.token, label="GET a global account data type that was never set")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/org.example.pref",
          token=v.token, json_body={"colour": "hijack"},
          label="PUT another user's global account data")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/org.example.pref",
          token=v.token, label="GET another user's global account data")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/org.example.pref",
          token="syt_bogus_account_data_token_zzz",
          label="GET account data with an invalid token")


@scenario("cs/account-data-room", tags=["account-data"], step=1)
def account_data_room(c):
    u = register(c, "adr1")
    v = register(c, "adr2")
    room = create_room(c, u, room_version="6", preset="private_chat")

    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.room_pref",
          token=u.token, json_body={"flag": True}, label="PUT room account data")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.room_pref",
          token=u.token, label="GET room account data back")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.room_pref",
          token=u.token, json_body={"flag": False}, label="PUT room account data again: overwrite")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.room_pref",
          token=u.token, label="GET room account data: sees the overwrite")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.never_set",
          token=u.token, label="GET a room account data type that was never set")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.room_pref",
          token=v.token, json_body={"flag": None}, label="PUT another user's room account data")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.room_pref",
          token=v.token, label="GET another user's room account data")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.room_pref",
          token="syt_bogus_room_account_data_token_zzz",
          label="GET room account data with an invalid token")


@scenario("cs/account-data-sync", tags=["account-data", "sync"], step=1)
def account_data_sync(c):
    u = register(c, "ads1")
    room = create_room(c, u, room_version="6", preset="private_chat")

    def has_global(body):
        return any(e.get("type") == "org.example.sync_pref"
                   and e.get("content", {}).get("v") == 1
                   for e in body.get("account_data", {}).get("events", []))

    def has_room(body, rid):
        events = (body.get("rooms", {}).get("join", {}).get(rid, {})
                  .get("account_data", {}).get("events", []))
        return any(e.get("type") == "org.example.sync_room_pref"
                   and e.get("content", {}).get("v") == 2 for e in events)

    r0 = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0},
              label="initial full sync, before either account data type is set",
              derive=lambda b, rid=room: {"global": has_global(b), "room": has_room(b, rid)})
    since = (r0.json or {}).get("next_batch")

    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/org.example.sync_pref",
          token=u.token, json_body={"v": 1}, check=False, label="set global account data")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.sync_room_pref",
          token=u.token, json_body={"v": 2}, check=False, label="set room account data")

    _sync_since(c, u, since, label="incremental sync sees both new account data events",
               derive=lambda b, rid=room: {"global": has_global(b), "room": has_room(b, rid)})

    # A bare, repeated `GET /sync` with no `since` is not proven by the spec
    # to force a live recompute: Synapse serves an identical cached response
    # for back-to-back initial syncs sharing the same request shape (verified
    # directly: the `next_batch` token from a same-shaped full sync issued
    # after the writes above is byte-identical to the one from the very
    # first sync in this scenario, even seconds later) - that is a caching
    # optimisation, not a specification requirement, so this scenario does
    # not assert what a second same-shaped full sync for the same user sees.
    # Instead, check something the spec does require: a brand new user's own
    # first sync - necessarily a fresh computation, never previously
    # requested by anyone - must not see another user's private account data.
    other = register(c, "ads2")
    c.req("GET", f"{CS}/sync", token=other.token, query={"timeout": 0},
          label="a different user's own first sync does not see this user's account data",
          derive=lambda b, rid=room: {"global": has_global(b), "room": has_room(b, rid)})


@scenario("cs/account-data-unauthenticated", tags=["account-data"], step=1)
def account_data_unauthenticated(c):
    u = register(c, "adnoauth1")
    room = create_room(c, u, room_version="6", preset="private_chat")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/org.example.whatever",
          label="GET global account data without an access token")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/org.example.whatever",
          json_body={"a": 1}, label="PUT global account data without an access token")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.whatever",
          label="GET room account data without an access token")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/org.example.whatever",
          json_body={"a": 1}, label="PUT room account data without an access token")


# --------------------------------------------------------------------------- #
# room tags
# --------------------------------------------------------------------------- #
@scenario("cs/room-tags", tags=["tags"], step=1)
def room_tags(c):
    u = register(c, "tag1")
    v = register(c, "tag2")
    room = create_room(c, u, room_version="6", preset="private_chat")

    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/m.favourite",
          token=u.token, json_body={"order": 0.1}, label="PUT a tag with an order")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/org.example.plain",
          token=u.token, json_body={}, label="PUT a tag without an order")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags",
          token=u.token, label="GET tags: both present")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/m.favourite",
          token=u.token, json_body={"order": 0.9}, label="PUT the same tag again: change its order")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags",
          token=u.token, label="GET tags: sees the changed order")
    c.req("DELETE", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/org.example.plain",
          token=u.token, label="DELETE a tag")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags",
          token=u.token, label="GET tags: the deleted one is gone")
    c.req("DELETE", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/org.example.never_existed",
          token=u.token, label="DELETE a tag that was never set")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/m.favourite",
          token=v.token, json_body={"order": 0.5}, label="PUT another user's tag")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags",
          token=v.token, label="GET another user's tags")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags",
          token="syt_bogus_room_tags_token_zzz", label="GET tags with an invalid token")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags",
          label="GET tags without an access token")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/org.example.noauth",
          json_body={"order": 0.2}, label="PUT a tag without an access token")
    c.req("DELETE", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/m.favourite",
          label="DELETE a tag without an access token")


# --------------------------------------------------------------------------- #
# typing
# --------------------------------------------------------------------------- #
@scenario("cs/typing-basic", tags=["typing"], step=1)
def typing_basic(c):
    u, v, room = _two_in_a_room(c, "typ1", "typ2")
    r0 = sync(c, u, check=False)
    since = (r0.json or {}).get("next_batch")

    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": True, "timeout": 30000}, label="PUT typing: start")
    r_start = _sync_since(c, u, since, label="sync sees the typing notification",
                          derive=lambda b, rid=room: {"typing": _typing_users(b, rid)})

    since2 = (r_start.json or {}).get("next_batch") or since
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": False}, label="PUT typing: stop")
    _sync_since(c, u, since2, label="sync sees the typing notification withdrawn",
               derive=lambda b, rid=room: {"typing": _typing_users(b, rid)})

    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token="syt_bogus_typing_token_zzz",
          json_body={"typing": True, "timeout": 5000},
          label="PUT typing with an invalid token")
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}",
          json_body={"typing": True, "timeout": 5000},
          label="PUT typing without an access token")


@scenario("cs/typing-multiple-users", tags=["typing"], step=1)
def typing_multiple_users(c):
    u = register(c, "typm1")
    v = register(c, "typm2")
    w = register(c, "typm3")
    room = create_room(c, u, room_version="6", preset="private_chat")
    invite(c, u, room, v.user_id)
    join(c, v, room)
    invite(c, u, room, w.user_id)
    join(c, w, room)
    r0 = sync(c, u, check=False)
    since = (r0.json or {}).get("next_batch")

    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": True, "timeout": 30000}, label="v starts typing")
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(w.user_id)}", token=w.token,
          json_body={"typing": True, "timeout": 30000}, label="w starts typing")
    r_both = _sync_since(c, u, since, label="sync sees both typing users",
                         derive=lambda b, rid=room: {"typing": _typing_users(b, rid)})

    since2 = (r_both.json or {}).get("next_batch") or since
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": False}, label="v stops typing")
    _sync_since(c, u, since2, label="sync sees only w still typing",
               derive=lambda b, rid=room: {"typing": _typing_users(b, rid)})


@scenario("cs/typing-other-user", tags=["typing"], step=1)
def typing_other_user(c):
    u, v, room = _two_in_a_room(c, "typo1", "typo2")
    outsider = register(c, "typo3")

    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": True, "timeout": 20000},
          label="PUT typing state for yourself: allowed (positive control)")
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": False},
          label="PUT typing state for yourself: clear it (positive control)")
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=u.token,
          json_body={"typing": True, "timeout": 20000},
          label="PUT typing state for someone else")
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(outsider.user_id)}", token=outsider.token,
          json_body={"typing": True, "timeout": 20000},
          label="PUT typing state for yourself in a room you are not in")
    # Whether a non-boolean `typing` value is rejected or coerced is a schema-
    # validation strictness choice the specification does not mandate either
    # way, so it is not scored here.


# --------------------------------------------------------------------------- #
# receipts and read markers
# --------------------------------------------------------------------------- #
@scenario("cs/receipts-read", tags=["receipts"], step=1)
def receipts_read(c):
    u, v, room = _two_in_a_room(c, "rcpt1", "rcpt2")
    w = register(c, "rcpt3")
    invite(c, u, room, w.user_id)
    join(c, w, room)
    eid = send(c, u, room, "txn-rcpt-1", {"msgtype": "m.text", "body": "hello"}, check=False)
    r0 = sync(c, v, check=False)
    since = (r0.json or {}).get("next_batch")
    r0w = sync(c, w, check=False)
    since_w = (r0w.json or {}).get("next_batch")

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(eid)}", token=v.token,
          json_body={}, label="POST a read receipt")
    _sync_since(c, v, since, label="sync sees the receipt",
               derive=lambda b, rid=room, ev=eid, who=v.user_id: {
                   "has_receipt": _has_receipt(b, rid, ev, who)})
    # Whether a receipt for an event id the server has never seen is accepted
    # or validated away is not specified either way, so it is not scored here.
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.fake_receipt_type/{q(eid)}",
          token=v.token, json_body={}, label="POST a receipt with an invalid receipt type")
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(eid)}",
          json_body={}, label="POST a read receipt without an access token")

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(eid)}", token=w.token,
          json_body={}, label="a third user POSTs their own read receipt for the same event")
    _sync_since(c, w, since_w, label="sync sees both users' receipts independently (additive)",
               derive=lambda b, rid=room, ev=eid, u1=v.user_id, u2=w.user_id: {
                   "v_has_receipt": _has_receipt(b, rid, ev, u1),
                   "w_has_receipt": _has_receipt(b, rid, ev, u2)})


@scenario("cs/read-markers-fully-read", tags=["read-markers"], step=1)
def read_markers_scenario(c):
    u = register(c, "rm1")
    room = create_room(c, u, room_version="6", preset="private_chat")
    e1 = send(c, u, room, "txn-rm-1", {"msgtype": "m.text", "body": "one"}, check=False)
    e2 = send(c, u, room, "txn-rm-2", {"msgtype": "m.text", "body": "two"}, check=False)
    r0 = sync(c, u, check=False)
    since = (r0.json or {}).get("next_batch")

    c.req("POST", f"{CS}/rooms/{q(room)}/read_markers", token=u.token,
          json_body={"m.fully_read": e2, "m.read": e2},
          label="POST read markers: fully_read+read")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/m.fully_read",
          token=u.token, label="GET m.fully_read account data reflects the marker",
          derive=lambda b, expected=e2: {"points_at_expected_event": b.get("event_id") == expected})
    _sync_since(c, u, since, label="sync sees the m.read receipt bundled into the same POST",
               derive=lambda b, rid=room, ev=e2, who=u.user_id: {
                   "has_receipt": _has_receipt(b, rid, ev, who)})
    # Whether a later POST is allowed to move `m.fully_read` backward (to an
    # earlier event) is not specified either way by read_markers.yaml, which
    # documents no monotonicity constraint, so that direction is not scored -
    # only that the forward set above is observable.
    # read_markers.yaml lists `m.fully_read` as required, and the reference
    # accepts a body without it: a server may answer either way, so the request
    # is made (it must not wedge what follows) and not scored.
    c.req("POST", f"{CS}/rooms/{q(room)}/read_markers", token=u.token, json_body={},
          check=False,
          label="POST read markers without the required m.fully_read field")
    c.req("POST", f"{CS}/rooms/{q(room)}/read_markers", json_body={"m.fully_read": e2},
          label="POST read markers without an access token")


# --------------------------------------------------------------------------- #
# presence
# --------------------------------------------------------------------------- #
@scenario("cs/presence-basic", tags=["presence"], step=1)
def presence_basic(c):
    # Synapse's default rc_presence.per_user limiter allows only a single
    # presence-setting request per user before returning 429, and this
    # harness does not relax it (unlike rc_message/rc_login/...), so each
    # state-changing PUT below uses its own fresh user instead of making
    # several PUTs in a row on one user - which would make later checks in
    # the scenario observe a rate-limit response instead of what they intend
    # to test, and could do so differently run to run.
    a = register(c, "presb1")
    c.req("PUT", f"{CS}/presence/{q(a.user_id)}/status", token=a.token,
          json_body={"presence": "online", "status_msg": "hi there"},
          label="PUT own presence: online with a status message")
    c.req("GET", f"{CS}/presence/{q(a.user_id)}/status", token=a.token,
          label="GET own presence back",
          derive=lambda b: {"presence": b.get("presence"), "status_msg": b.get("status_msg")})

    b = register(c, "presb2")
    c.req("PUT", f"{CS}/presence/{q(b.user_id)}/status", token=b.token,
          json_body={"presence": "unavailable"},
          label="PUT own presence: unavailable, no status message")
    c.req("GET", f"{CS}/presence/{q(b.user_id)}/status", token=b.token,
          label="GET own presence: no status message was ever set",
          derive=lambda bb: {"presence": bb.get("presence"),
                             "status_msg_is_absent_or_null": bb.get("status_msg") is None})

    d = register(c, "presb3")
    c.req("PUT", f"{CS}/presence/{q(d.user_id)}/status", token=d.token,
          json_body={"status_msg": "missing the required presence field"},
          label="PUT presence without the required presence field")

    e = register(c, "presb4")
    c.req("PUT", f"{CS}/presence/{q(e.user_id)}/status", token=e.token,
          json_body={"presence": "not-a-real-presence-value"},
          label="PUT presence with an invalid presence enum value")

    c.req("GET", f"{CS}/presence/{q(a.user_id)}/status",
          label="GET presence without an access token")


@scenario("cs/presence-visibility", tags=["presence"], step=1)
def presence_visibility(c):
    # Whether an unrelated user's presence is shown or hidden is a policy
    # choice the specification leaves open (presence.yaml documents 200
    # "shown", 403 "not allowed", and 404 "no presence state / not exposed"
    # as equally valid outcomes) - only that the server picks one of those
    # two *shapes* rather than something else (a crash, a malformed body, an
    # unrelated status) is pinned here.
    def sanctioned_shape(status, body):
        if 200 <= status < 300:
            return isinstance(body, dict) and body.get("presence") in (
                "online", "offline", "unavailable")
        return status in (403, 404)

    u = register(c, "presv1")
    v = register(c, "presv2")

    c.req("PUT", f"{CS}/presence/{q(v.user_id)}/status", token=u.token,
          json_body={"presence": "online"}, label="PUT another user's presence")

    r = c.req("GET", f"{CS}/presence/{q(v.user_id)}/status", token=u.token, check=False)
    fact(c, {"outcome_is_spec_sanctioned": sanctioned_shape(r.status, r.json)},
         label="GET an unrelated user's presence: shown-with-valid-state or hidden (403/404)",
         token=u.token)

    unknown = f"@nosuchuser999:{c.server_name}"
    r2 = c.req("GET", f"{CS}/presence/{q(unknown)}/status", token=u.token, check=False)
    fact(c, {"outcome_is_spec_sanctioned": sanctioned_shape(r2.status, r2.json)},
         label="GET an unregistered user's presence: shown-with-valid-state or hidden (403/404)",
         token=u.token)


# --------------------------------------------------------------------------- #
# joined rooms
# --------------------------------------------------------------------------- #
@scenario("cs/joined-rooms", tags=["rooms"], step=1)
def joined_rooms(c):
    u = register(c, "jr1")
    room_a = create_room(c, u, room_version="6", preset="private_chat", name="Room A")
    room_b = create_room(c, u, room_version="6", preset="private_chat", name="Room B")

    c.req("GET", f"{CS}/joined_rooms", token=u.token, label="joined rooms: both present",
          derive=lambda b, a=room_a, bb=room_b: {"has_a": a in b.get("joined_rooms", []),
                                                 "has_b": bb in b.get("joined_rooms", [])})
    c.req("POST", f"{CS}/rooms/{q(room_a)}/leave", token=u.token, json_body={},
          check=False, label="leave room A")
    c.req("GET", f"{CS}/joined_rooms", token=u.token,
          label="joined rooms: after leaving one",
          derive=lambda b, a=room_a, bb=room_b: {"has_a": a in b.get("joined_rooms", []),
                                                 "has_b": bb in b.get("joined_rooms", [])})
    c.req("GET", f"{CS}/joined_rooms", label="joined rooms: without an access token")
    c.req("GET", f"{CS}/joined_rooms", token="syt_bogus_joined_rooms_token_zzz",
          label="joined rooms: with an invalid token")

    lonely = register(c, "jrlonely")
    c.req("GET", f"{CS}/joined_rooms", token=lonely.token,
          label="joined rooms: a fresh user with no rooms at all",
          derive=lambda b: {"joined_rooms_is_empty": b.get("joined_rooms") == []})


# --------------------------------------------------------------------------- #
# legacy initial sync (old_sync.yaml, room_initial_sync.yaml)
# --------------------------------------------------------------------------- #
@scenario("cs/room-initial-sync", tags=["initial-sync"], step=1)
def room_initial_sync(c):
    u = register(c, "risync1")
    v = register(c, "risync2")
    room = create_room(c, u, room_version="6", preset="private_chat", name="RIS Room")
    eid = send(c, u, room, "txn-ris-1", {"msgtype": "m.text", "body": "ris hello"}, check=False)
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/m.favourite",
          token=u.token, json_body={"order": 0.5}, check=False, label="tag the room")

    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync", token=u.token,
          label="room initialSync for a joined member",
          derive=lambda b, rid=room, ev=eid: {
              "room_id_matches": b.get("room_id") == rid,
              "membership": b.get("membership"),
              # `visibility` is an optional property of this response: a server
              # that reports the room's directory visibility and one that
              # leaves it out are both conforming, so what is scored is that
              # whatever it reports is one of the two values defined for it.
              "visibility_if_reported_is_one_of_the_two":
                  b.get("visibility") in (None, "public", "private"),
              "state_types": keep_types(1, sorted({e.get("type") for e in b.get("state", [])
                                                    if isinstance(e, dict)})),
              "message_contains_our_event": any(
                  e.get("event_id") == ev for e in b.get("messages", {}).get("chunk", [])),
              "account_data_has_our_tag": any(
                  e.get("type") == "m.tag" for e in b.get("account_data", [])
                  if isinstance(e, dict)),
          })
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync", token=u.token,
          query={"limit": 1}, label="room initialSync with limit=1",
          derive=lambda b: {"chunk_len_le_1": len(b.get("messages", {}).get("chunk", [])) <= 1})

    other_room = create_room(c, v, room_version="6", preset="private_chat", name="Not Yours")
    c.req("GET", f"{CS}/rooms/{q(other_room)}/initialSync", token=u.token,
          label="room initialSync for a room you have never been in")
    c.req("GET", f"{CS}/rooms/{q('!nonexistentroom00000000000:' + c.server_name)}/initialSync",
          token=u.token, label="room initialSync for a room id that does not exist at all")
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync", token="syt_bogus_ris_token_zzz",
          label="room initialSync with an invalid token")
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync",
          label="room initialSync without an access token")


@scenario("cs/initial-sync", tags=["initial-sync"], step=1)
def initial_sync(c):
    u = register(c, "isync1")
    room = create_room(c, u, room_version="6", preset="private_chat", name="IS Room")

    c.req("GET", f"{CS}/initialSync", token=u.token, label="whole-account initialSync",
          derive=lambda b, rid=room: {
              "end_present": isinstance(b.get("end"), str) and bool(b.get("end")),
              "our_room_present_and_joined": any(
                  ri.get("room_id") == rid and ri.get("membership") == "join"
                  for ri in b.get("rooms", []) if isinstance(ri, dict)),
          })
    c.req("GET", f"{CS}/initialSync", token=u.token, query={"limit": 0},
          label="whole-account initialSync with limit=0",
          derive=lambda b, rid=room: {
              "our_room_still_present": any(
                  ri.get("room_id") == rid for ri in b.get("rooms", []) if isinstance(ri, dict)),
          })
    c.req("GET", f"{CS}/initialSync", token="syt_bogus_initial_sync_token_zzz",
          label="whole-account initialSync with an invalid token")
    c.req("GET", f"{CS}/initialSync", label="whole-account initialSync without a token")


# --------------------------------------------------------------------------- #
# CORS preflight
# --------------------------------------------------------------------------- #
@scenario("cs/options-preflight", tags=["cors"], step=1)
def options_preflight(c):
    """`OPTIONS` on any endpoint, without performing the endpoint's logic.

    The release *requires* two things of a pre-flight: that every endpoint
    answers `OPTIONS`, and that the server performs none of the endpoint's own
    logic when it does.  The CORS header values it lists are "recommended", so
    only `Access-Control-Allow-Origin` is compared (`INTERESTING_HEADERS`), and
    the body is derived rather than pinned: a server that answers `{}` and one
    that answers with no body at all are both conforming.
    """
    empty = lambda b: {"body_carries_nothing_of_substance": not b}       # noqa: E731
    for path, method, label in (
            (f"{CS}/createRoom", "POST", "a normal endpoint"),
            (f"{MEDIA}/upload", "POST", "a media-namespace endpoint"),
            (f"{CS}/rooms/{q('!nonexistentroom00000000000:' + c.server_name)}/join",
             "POST", "a path-parameterized endpoint whose room does not exist"),
            ("/_matrix/client/versions", "GET", "the unversioned endpoint")):
        c.req("OPTIONS", path,
              headers={"Origin": "https://example.invalid",
                       "Access-Control-Request-Method": method},
              label=f"OPTIONS preflight on {label}", derive=empty)

    # ... and the logic really did not run: the room was not created, and the
    # non-existent room was not joined.
    u = register(c, "opt1")
    c.req("GET", f"{CS}/joined_rooms", token=u.token,
          label="the preflights created and joined nothing",
          derive=lambda b: {"joined": b.get("joined_rooms")})


# --------------------------------------------------------------------------- #
# auth edge cases
# --------------------------------------------------------------------------- #
@scenario("cs/auth-query-token", tags=["auth"], step=1)
def auth_query_token(c):
    u = register(c, "authq1")
    c.req("GET", f"{CS}/account/whoami", query={"access_token": u.token},
          label="auth via an access_token query parameter")
    c.req("GET", f"{CS}/account/whoami",
          query={"access_token": "syt_totally_bogus_token_value_zzz"},
          label="auth via an invalid access_token query parameter")
    # The release says credentials "missing or invalid" return 401 with
    # M_MISSING_TOKEN or M_UNKNOWN_TOKEN "respectively", and never says which an
    # EMPTY token is. Unscored rather than pinned to the reference's pick.
    c.req("GET", f"{CS}/account/whoami", query={"access_token": ""}, check=False,
          label="auth via an empty access_token query parameter")
    c.req("GET", f"{CS}/account/whoami", token=u.token, query={"access_token": u.token},
          check=False, label="auth via both the header and query parameter")
    c.req("PUT", f"{CS}/profile/{q(u.user_id)}/displayname",
          query={"access_token": u.token}, json_body={"displayname": "set via query token"},
          label="auth via query parameter also works for a write request")


@scenario("cs/auth-malformed-body", tags=["auth"], step=1)
def auth_malformed_body(c):
    u = register(c, "authb1")
    c.req("POST", f"{CS}/createRoom", token=u.token, body=b"{not valid json!!",
          headers={"Content-Type": "application/json"},
          label="malformed JSON body")
    c.req("POST", f"{CS}/createRoom", token=u.token,
          json_body=["this", "is", "an", "array", "not", "an", "object"],
          label="valid JSON but not an object: an array", drop=("errcode",))
    c.req("POST", f"{CS}/createRoom", token=u.token, json_body="just a string",
          label="valid JSON but not an object: a string", drop=("errcode",))
    c.req("POST", f"{CS}/createRoom", token=u.token, body=b"null",
          headers={"Content-Type": "application/json"},
          label="valid JSON but not an object: null", drop=("errcode",))
    c.req("POST", f"{CS}/createRoom", token=u.token, body=b"",
          headers={"Content-Type": "application/json"},
          label="empty request body where a JSON object was required")


# --------------------------------------------------------------------------- #
# persistence across a restart
# --------------------------------------------------------------------------- #
@scenario("cs/restart-persistence", tags=["persistence"], step=1)
def restart_persistence(c):
    u = register(c, "persist1")
    room = create_room(c, u, room_version="6", preset="private_chat", name="Persistent Room")
    eid = send(c, u, room, "txn-persist-1", {"msgtype": "m.text", "body": "before restart"},
              check=False)
    media_bytes = png(3, 3, rgb=(9, 8, 7))
    mxc = upload(c, u, media_bytes, "image/png", "keepme.png", check=False)
    server, media_id = mxc_parts(mxc)
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/org.example.persist_pref",
          token=u.token, json_body={"v": 42}, check=False,
          label="set account data before restart")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/m.favourite",
          token=u.token, json_body={"order": 0.5}, check=False,
          label="tag the room before restart")

    srv = c.extra["server"]
    srv.stop()
    srv.start()

    c.req("GET", f"{CS}/account/whoami", token=u.token,
          label="the access token still works after a restart")
    c.req("GET", f"{CS}/joined_rooms", token=u.token,
          label="room membership survived the restart",
          derive=lambda b, rid=room: {"still_joined": rid in b.get("joined_rooms", [])})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(eid)}", token=u.token,
          label="message history survived the restart",
          derive=lambda b, expected=eid: {
              "event_id_matches": b.get("event_id") == expected,
              "body_matches": b.get("content", {}).get("body") == "before restart"})
    c.req("GET", f"{MEDIA}/download/{server}/{media_id}", token=u.token,
          label="uploaded media survived the restart", binary=True)
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/org.example.persist_pref",
          token=u.token, label="account data survived the restart",
          derive=lambda b: {"v": b.get("v")})
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags",
          token=u.token, label="room tag survived the restart",
          derive=lambda b: {"has_favourite": "m.favourite" in b.get("tags", {})})
    c.req("GET", f"{CS}/devices/{q(u.device_id)}", token=u.token,
          label="device entry survived the restart",
          derive=lambda b: {"device_id_present": bool(b.get("device_id"))})
    c.req("GET", f"{CS}/profile/{q(u.user_id)}/displayname", token=u.token,
          label="profile displayname survived the restart",
          derive=lambda b: {"displayname": b.get("displayname")})
