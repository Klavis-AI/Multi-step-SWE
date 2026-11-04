"""Step 1 — the content repository, client config, receipts, typing, presence."""

import hashlib
import json
import struct

from mxconf.mx import (CS, MEDIA, create_room, fact, invite, join, mxc_parts, ok, png, q,
                       register, send, upload)
from mxconf.registry import scenario

V = "6"


def image_size(data: bytes):
    """The geometry of a returned image, so a thumbnail is compared on what it
    depicts rather than on the bytes one encoder happens to produce.  The
    specification does not fix the codec, so only "it is one of the image
    formats the endpoint may return" is pinned, never which one."""
    if len(data) >= 24 and data[:8] == b"\x89PNG\r\n\x1a\n":
        w, h = struct.unpack(">II", data[16:24])
        return {"is_image": True, "width": w, "height": h}
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
                return {"is_image": True, "width": w, "height": h}
            i += 2 + struct.unpack(">H", data[i + 2:i + 4])[0]
    return {"is_image": False, "len": len(data)}


@scenario("cs/media-upload-download", tags=["media"], step=1)
def media_upload_download(c):
    u = register(c, "md1")
    data = b"hello media content\n"
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=data,
              headers={"Content-Type": "text/plain"},
              query={"filename": "hello.txt"}, label="upload a text file")
    server, media_id = mxc_parts(r.json["content_uri"])
    c.req("GET", f"{MEDIA}/download/{q(server)}/{q(media_id)}", binary=True,
          label="download it back")
    c.req("GET", f"{MEDIA}/download/{q(server)}/{q(media_id)}/{q('other.txt')}",
          binary=True, label="download with a file name")
    c.req("GET", f"{MEDIA}/download/{q(server)}/nosuchmediaid", binary=True,
          label="download an unknown media id")


@scenario("cs/media-upload-png", tags=["media"], step=1)
def media_upload_png(c):
    u = register(c, "md2")
    data = png(64, 48)
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=data,
              headers={"Content-Type": "image/png"},
              query={"filename": "pic.png"}, label="upload a PNG")
    server, media_id = mxc_parts(r.json["content_uri"])
    c.req("GET", f"{MEDIA}/download/{q(server)}/{q(media_id)}", binary=True,
          label="the bytes come back unchanged")


@scenario("cs/media-thumbnails", tags=["media"], step=1)
def media_thumbnails(c):
    """"The dimensions are a guide": a thumbnail must be at least what was
    asked for, never larger than the source, and the shape the method promises.

    The exact pixels a server returns are its own choice among the sizes it
    keeps, and which of the allowed content types it encodes in is its choice
    too, so neither is compared.
    """
    u = register(c, "md3")
    src_w, src_h = 128, 96
    mxc = upload(c, u, png(src_w, src_h), "image/png", "big.png")
    server, media_id = mxc_parts(mxc)

    def fits(asked_w, asked_h, method):
        # `scale` keeps the source's aspect, so for a box of another aspect
        # inside the source the server picks which side meets the box.  The
        # release says it must not return less than was asked; the reference
        # rounds the other way.  Neither reading is pinned there: that fact is
        # compared only where it is not in dispute.
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
            }
            if not disputed:
                out["at_least_asked_or_the_whole_source"] = (
                    gw >= min(_w, src_w) and gh >= min(_h, src_h))
            return out
        return derive

    for w, h, method in [(32, 32, "crop"), (96, 96, "crop"),
                         (64, 64, "scale"), (640, 480, "scale")]:
        c.req("GET", f"{MEDIA}/thumbnail/{q(server)}/{q(media_id)}",
              query={"width": w, "height": h, "method": method},
              binary=True, derive=fits(w, h, method), record_headers=False,
              label=f"thumbnail {w}x{h} {method}")


@scenario("cs/media-config-and-limits", tags=["media"], step=1)
def media_config(c):
    u = register(c, "md4")
    c.req("GET", f"{MEDIA}/config", token=u.token, label="media config")
    c.req("POST", f"{MEDIA}/upload", body=b"no token",
          headers={"Content-Type": "text/plain"}, label="upload without a token")
    c.req("POST", f"{MEDIA}/upload", token=u.token, body=b"x" * (11 * 1024 * 1024),
          headers={"Content-Type": "application/octet-stream"},
          label="upload above max_file_size_bytes")


@scenario("cs/account-data", tags=["account-data"], step=1)
def account_data(c):
    u = register(c, "ad1")
    v = register(c, "ad2")
    room = create_room(c, u, room_version=V)
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/com.example.setting",
          token=u.token, label="unset account data")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/com.example.setting",
          token=u.token, json_body={"enabled": True, "level": 3},
          label="set account data")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/com.example.setting",
          token=u.token, label="read it back")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/com.example.room",
          token=u.token, json_body={"scoped": "to the room"},
          label="set per-room account data")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/com.example.room",
          token=u.token, label="read per-room account data")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/com.example.room",
          token=u.token, label="per-room data is not global data")
    c.req("PUT", f"{CS}/user/{q(v.user_id)}/account_data/com.example.setting",
          token=u.token, json_body={"hijack": True},
          label="set another user's account data")


@scenario("cs/account-data-in-sync", tags=["account-data", "sync"], step=1)
def account_data_sync(c):
    u = register(c, "ad3")
    room = create_room(c, u, room_version=V)
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/com.example.g",
          token=u.token, json_body={"a": 1}, check=False)
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/com.example.r",
          token=u.token, json_body={"b": 2}, check=False)
    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0, "since": since},
          label="account data reaches /sync",
          derive=lambda b: {
              "global": sorted(e.get("type")
                               for e in b.get("account_data", {}).get("events", [])),
              "per_room": sorted(
                  e.get("type") for room in (b.get("rooms") or {}).get("join", {}).values()
                  for e in room.get("account_data", {}).get("events", [])),
          })


@scenario("cs/tags", tags=["tags"], step=1)
def tags(c):
    u = register(c, "tg1")
    v = register(c, "tg2")
    room = create_room(c, u, room_version=V)
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags", token=u.token,
          label="no tags yet")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/m.favourite",
          token=u.token, json_body={"order": 0.25}, label="set a tag with an order")
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/u.custom",
          token=u.token, json_body={}, label="set a tag without an order")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags", token=u.token,
          label="both tags are listed")
    c.req("DELETE", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags/m.favourite",
          token=u.token, label="delete a tag")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags", token=u.token,
          label="the tag is gone")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/tags", token=v.token,
          label="another user cannot read them")


@scenario("cs/receipts", tags=["receipts"], step=1)
def receipts(c):
    u = register(c, "rc1")
    v = register(c, "rc2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    eid = send(c, u, room, "m1", {"msgtype": "m.text", "body": "read me"})
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(eid)}", token=v.token,
          json_body={}, label="post a read receipt")
    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0, "since": since},
          label="the receipt reaches the other member",
          derive=lambda b: {
              "ephemeral_types": sorted(
                  e.get("type") for room in (b.get("rooms") or {}).get("join", {}).values()
                  for e in room.get("ephemeral", {}).get("events", [])),
              "receipt_users": sorted(
                  user for room in (b.get("rooms") or {}).get("join", {}).values()
                  for e in room.get("ephemeral", {}).get("events", [])
                  if e.get("type") == "m.receipt"
                  for ev in e.get("content", {}).values()
                  for user in ev.get("m.read", {})),
          })
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.unknown/{q(eid)}", token=v.token,
          json_body={}, label="an unknown receipt type")
    w = register(c, "rc3")
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(eid)}", token=w.token,
          json_body={}, label="a receipt from someone not in the room")


@scenario("cs/read-markers", tags=["receipts"], step=1)
def read_markers(c):
    u = register(c, "rm1")
    room = create_room(c, u, room_version=V)
    eid = send(c, u, room, "m1", {"msgtype": "m.text", "body": "marker"})
    c.req("POST", f"{CS}/rooms/{q(room)}/read_markers", token=u.token,
          json_body={"m.fully_read": eid, "m.read": eid}, label="set read markers")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/m.fully_read",
          token=u.token, label="m.fully_read lands in room account data")


@scenario("cs/typing", tags=["typing"], step=1)
def typing(c):
    u = register(c, "ty1")
    v = register(c, "ty2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": True, "timeout": 30000}, label="start typing")
    r2 = c.req("GET", f"{CS}/sync", token=u.token,
               query={"timeout": 0, "since": since},
               label="the other member sees it",
               derive=lambda b: {
                   "typing": sorted(
                       uid for room in (b.get("rooms") or {}).get("join", {}).values()
                       for e in room.get("ephemeral", {}).get("events", [])
                       if e.get("type") == "m.typing"
                       for uid in e.get("content", {}).get("user_ids", [])),
               })
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": False}, label="stop typing")
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 0, "since": r2.json["next_batch"]},
          label="and the list empties",
          derive=lambda b: {
              "typing": sorted(
                  uid for room in (b.get("rooms") or {}).get("join", {}).values()
                  for e in room.get("ephemeral", {}).get("events", [])
                  if e.get("type") == "m.typing"
                  for uid in e.get("content", {}).get("user_ids", [])),
          })
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(u.user_id)}", token=v.token,
          json_body={"typing": True, "timeout": 1000},
          label="typing on behalf of another user")


@scenario("cs/presence", tags=["presence"], step=1)
def presence(c):
    u = register(c, "pz1")
    v = register(c, "pz2")
    # The specification lets a server refuse presence for a user you share no
    # room with, so the readable case is set up explicitly.
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    c.req("PUT", f"{CS}/presence/{q(u.user_id)}/status", token=u.token,
          json_body={"presence": "online", "status_msg": "here"},
          label="set presence")
    c.req("GET", f"{CS}/presence/{q(u.user_id)}/status", token=u.token,
          label="read own presence", drop=("last_active_ago", "currently_active"))
    c.req("GET", f"{CS}/presence/{q(u.user_id)}/status", token=v.token,
          label="read another user's presence",
          drop=("last_active_ago", "currently_active"))
    # `presence.yaml` documents 200, 403 and 404 as equally valid answers for a
    # user whose presence this server will not show, so only the shape is
    # pinned, not which of them the server picks.
    r = c.req("GET", f"{CS}/presence/{q('@nobody:' + c.server_name)}/status",
              token=u.token, check=False)
    fact(c, {"answered_a_sanctioned_way":
             (200 <= r.status < 300
              and isinstance(r.json, dict)
              and r.json.get("presence") in ("online", "offline", "unavailable"))
             or r.status in (403, 404)},
         label="presence of an unknown user: shown with a valid state, or hidden",
         token=u.token)


@scenario("cs/cors-and-content-type", tags=["http"], step=1)
def cors(c):
    c.req("OPTIONS", f"{CS}/sync", label="a CORS preflight",
          derive=lambda b: {})
    c.req("GET", "/_matrix/client/versions", label="CORS on a normal response",
          derive=lambda b: {"ok": True})


@scenario("cs/malformed-bodies", tags=["errors"], step=1)
def malformed_bodies(c):
    u = register(c, "mb9")
    c.req("POST", f"{CS}/createRoom", token=u.token, body=b"{not json",
          headers={"Content-Type": "application/json"},
          label="a body that is not JSON")
    c.req("POST", f"{CS}/createRoom", token=u.token, body=b"[1,2,3]",
          headers={"Content-Type": "application/json"},
          label="a body that is JSON but not an object")


@scenario("cs/query-string-access-token", tags=["auth"], step=1)
def query_token(c):
    u = register(c, "qt1")
    c.req("GET", f"{CS}/account/whoami", query={"access_token": u.token},
          label="the deprecated access_token query parameter")
    c.req("GET", f"{CS}/account/whoami", query={"access_token": "not-a-token"},
          label="a bad token in the query string")


@scenario("cs/persistence-across-restart", tags=["persistence"], step=1, needs=["restart"])
def persistence(c):
    u = register(c, "pr9")
    room = create_room(c, u, room_version=V, name="Durable")
    eid = send(c, u, room, "m1", {"msgtype": "m.text", "body": "survives"})
    mxc = upload(c, u, b"durable bytes", "text/plain", "keep.txt")
    server, media_id = mxc_parts(mxc)
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/com.example.keep",
          token=u.token, json_body={"keep": True}, check=False)

    assert c.extra["server"].restart(), "the server did not come back up"

    c.req("GET", f"{CS}/account/whoami", token=u.token,
          label="the access token survives")
    c.req("GET", f"{CS}/joined_rooms", token=u.token, label="the room survives")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=u.token,
          label="the room state survives")
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(eid)}", token=u.token,
          label="the message survives")
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
          query={"dir": "b", "limit": 10}, label="history survives",
          derive=lambda b: {"bodies": [e.get("content", {}).get("body")
                                       for e in b["chunk"]]})
    c.req("GET", f"{MEDIA}/download/{q(server)}/{q(media_id)}", binary=True,
          label="the media survives")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/account_data/com.example.keep",
          token=u.token, label="account data survives")
