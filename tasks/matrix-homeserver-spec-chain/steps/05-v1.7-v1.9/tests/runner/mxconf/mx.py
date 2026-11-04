"""Helpers for writing scenarios against the Matrix Client-Server API."""

from __future__ import annotations

import json
import struct
import zlib
from dataclasses import dataclass, field
from typing import Any

from .ctx import Ctx, ScenarioAbort

CS = "/_matrix/client/v3"
CS1 = "/_matrix/client/v1"
MEDIA = "/_matrix/media/v3"


@dataclass
class User:
    localpart: str
    user_id: str
    token: str
    device_id: str
    password: str


def ok(c: Ctx, r, what: str):
    if not (200 <= r.status < 300):
        raise ScenarioAbort(f"{what}: HTTP {r.status} {r.body[:200]!r}")
    return r


def register(c: Ctx, localpart: str, *, password: str | None = None,
             device_id: str | None = None, display_name: str | None = None,
             profile_name: str | None = "auto", check: bool = False) -> User:
    """Register through the v1.1 `m.login.dummy` user-interactive auth flow."""
    password = password or f"pw-{localpart}-123"
    body: dict[str, Any] = {"username": localpart, "password": password}
    if device_id:
        body["device_id"] = device_id
    if display_name:
        body["initial_device_display_name"] = display_name
    r = c.req("POST", f"{CS}/register", json_body=body, check=check,
              label=f"register {localpart} (uia challenge)")
    if r.status == 401 and r.json and "session" in r.json:
        body["auth"] = {"type": "m.login.dummy", "session": r.json["session"]}
        r = c.req("POST", f"{CS}/register", json_body=body, check=check,
                  label=f"register {localpart}")
    ok(c, r, f"register {localpart}")
    j = r.json or {}
    u = User(localpart, j.get("user_id", ""), j.get("access_token", ""),
             j.get("device_id", ""), password)
    if profile_name and u.token:
        # The specification does not fix the display name of a fresh account, so
        # every scenario sets one explicitly before it observes membership or
        # profile content.
        name = f"{localpart} D" if profile_name == "auto" else profile_name
        c.req("PUT", f"{CS}/profile/{q(u.user_id)}/displayname", token=u.token,
              json_body={"displayname": name}, check=False)
    return u


def login(c: Ctx, user: User, *, device_id: str | None = None,
          check: bool = False) -> str:
    body: dict[str, Any] = {
        "type": "m.login.password",
        "identifier": {"type": "m.id.user", "user": user.localpart},
        "password": user.password,
    }
    if device_id:
        body["device_id"] = device_id
    r = c.req("POST", f"{CS}/login", json_body=body, check=check,
              label=f"login {user.localpart}")
    ok(c, r, f"login {user.localpart}")
    return (r.json or {}).get("access_token", "")


def create_room(c: Ctx, user: User, *, check: bool = False, **kwargs) -> str:
    r = c.req("POST", f"{CS}/createRoom", token=user.token, json_body=kwargs,
              check=check, label="createRoom")
    ok(c, r, "createRoom")
    return (r.json or {})["room_id"]


def join(c: Ctx, user: User, room: str, *, check: bool = False, **body) -> None:
    r = c.req("POST", f"{CS}/join/{q(room)}", token=user.token, json_body=body or {},
              check=check, label="join")
    ok(c, r, "join")


def invite(c: Ctx, user: User, room: str, who: str, *, check: bool = False,
           **extra) -> None:
    body = {"user_id": who}
    body.update(extra)
    r = c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=user.token,
              json_body=body, check=check, label="invite")
    ok(c, r, "invite")


def send(c: Ctx, user: User, room: str, txn: str, content: dict,
         etype: str = "m.room.message", *, check: bool = False) -> str:
    r = c.req("PUT", f"{CS}/rooms/{q(room)}/send/{etype}/{txn}", token=user.token,
              json_body=content, check=check, label=f"send {etype}")
    ok(c, r, "send")
    return (r.json or {})["event_id"]


def set_state(c: Ctx, user: User, room: str, etype: str, content: dict,
              state_key: str = "", *, check: bool = False) -> str:
    r = c.req("PUT", f"{CS}/rooms/{q(room)}/state/{etype}/{q(state_key)}",
              token=user.token, json_body=content, check=check,
              label=f"state {etype}")
    ok(c, r, "set_state")
    return (r.json or {})["event_id"]


def sync(c: Ctx, user: User, *, since: str | None = None, timeout: int = 0,
         filt: str | None = None, full_state: bool | None = None,
         set_presence: str | None = None, check: bool = True,
         label: str | None = None, http_timeout: float | None = None):
    query: dict[str, Any] = {"timeout": timeout}
    if since:
        query["since"] = since
    if filt:
        query["filter"] = filt
    if full_state is not None:
        query["full_state"] = full_state
    if set_presence:
        query["set_presence"] = set_presence
    return c.req("GET", f"{CS}/sync", token=user.token, query=query, check=check,
                 label=label or "sync", timeout=http_timeout)


def q(segment: str) -> str:
    from urllib.parse import quote
    return quote(segment, safe="")


# --------------------------------------------------------------------------- #
def png(width: int, height: int, rgb: tuple[int, int, int] = (200, 30, 30)) -> bytes:
    """A deterministic PNG, built here so media tests do not need a fixture file."""
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = b"".join(
        b"\x00" + bytes(rgb) * width for _ in range(height)
    )
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def upload(c: Ctx, user: User, data: bytes, content_type: str, filename: str,
           *, check: bool = False) -> str:
    r = c.req("POST", f"{MEDIA}/upload", token=user.token, body=data,
              headers={"Content-Type": content_type},
              query={"filename": filename}, check=check, label="media upload")
    ok(c, r, "upload")
    return (r.json or {})["content_uri"]


def mxc_parts(uri: str) -> tuple[str, str]:
    rest = uri[len("mxc://"):]
    server, _, media_id = rest.partition("/")
    return server, media_id


def fact(c, computed, *, label, token, peer=False):
    """Record a precomputed, deterministic fact instead of a real response.

    Used exactly where a real observation cannot go through `derive` safely:
    either the response is binary/non-JSON (an image - `ctx._record` nulls the
    body before `derive` ever sees it), or its status is one the
    specification documents as one of *several* implementation-chosen
    alternatives for the same condition (so `status_exact` would stay pinned
    to the reference's particular pick).  A cheap, always-successful request
    (`whoami` with a valid token) carries the fact as its body via `derive`;
    the request's own status/headers are the part that is genuinely
    invariant.
    """
    c.req("GET", f"{CS}/account/whoami", token=token, label=label, peer=peer,
          derive=lambda body, _c=computed: _c)
