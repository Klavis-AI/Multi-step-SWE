"""Step 7 - simplified sliding sync (MSC4186), and the v1.12 -> v1.13 changes.

Two subjects live here.

**Simplified sliding sync** is not in any specification release: it is
MSC4186, served by the pinned reference at
``POST /_matrix/client/unstable/org.matrix.simplified_msc3575/sync`` and
advertised as ``org.matrix.simplified_msc3575`` in ``/_matrix/client/versions``
``unstable_features``.  Because the proposal is the only definition, these
scenarios pin only what the proposal fixes: which rooms a list selects, the
list's ``count``, whether a room is ``initial``, which ``required_state``
entries were resolved, which event *types* the timeline carried, whether it was
``limited``, what a second request carrying a ``pos`` reports, what the filters
select, that two ``conn_id``s do not interfere, that an unknown ``pos`` is
refused with ``M_UNKNOWN_POS``, and that each extension is present and shaped.
Everything the proposal leaves open - ``bump_stamp``, ``num_live``, the
``ops``/``notification_count``/``highlight_count`` fields the reference still
carries from the abandoned MSC3575, and the opaque ``pos`` and ``prev_batch``
tokens themselves - is deliberately not compared: a derivation keeps the
*structure* and drops the volume, exactly as the step-1 ``/sync`` scenarios do.

**The v1.12 -> v1.13 changes** are ordinary Client-Server API and are pinned
against the published schema of release v1.13: the ``m.marked_unread`` room
account-data event, ``POST /rooms/{roomId}/report``, ``reason`` being optional
on ``POST /rooms/{roomId}/report/{eventId}``, the ``via`` query parameter on
``POST /join/{roomIdOrAlias}`` beside the deprecated ``server_name``, the
now-required ``Content-Disposition`` on media downloads, and the order
``POST /keys/claim`` hands out one-time keys.

Two divergences between the pinned reference and release v1.13 are therefore
left unscored rather than pinned.  The reference *requires* `reason` on
`POST /rooms/{roomId}/report` although the release's request schema marks it
optional, so every room report below sends one.  And the reference does not
refuse a room report from a caller who is not in the room - which is what the
release says ("The caller is not required to be joined to the room to report
it"), so what is pinned is the acceptance and the 404 for a room the server
does not know; the refusal that *is* specified belongs to the event report,
where the caller must be joined, and that is pinned there.

Account *suspension* has no scenario: release v1.13 says in as many words that
"APIs for initiating suspension or unsuspension are not included in this
version of the specification, and left as an implementation detail", and the
reference exposes it only through its own ``PUT /_synapse/admin/v1/suspend``,
which needs a server administrator and which no specification asks a
homeserver to implement.  Nothing a client can reach suspends a user here, so
nothing is pinned rather than inventing an endpoint.

Determinism.  A sliding-sync ``pos`` is opaque and is never compared; every
scored request that depends on something the scenario has just done is preceded
by unchecked polling (``settle``) until the server reports it, so the scored
request observes a caught-up server instead of racing one.
"""

from __future__ import annotations

import base64
import hashlib
import json
import time

from mxconf.mx import (CS, CS1, MEDIA, User, create_room, fact, invite,
                       join, mxc_parts, png, q, register, send, sync)
from mxconf.registry import scenario

# Every room is created at this version, explicitly, so the reference's default
# room version cannot move a transcript.
V = "10"

SSYNC = "/_matrix/client/unstable/org.matrix.simplified_msc3575/sync"
FEATURE = "org.matrix.simplified_msc3575"

# `m.room.guest_access` is left out of every derived event list here for the
# reason `mxconf/steps.py` gives for the earlier steps: the v1.13 `createRoom`
# preset table says `public_chat` sets `guest_access: forbidden`, but the
# pinned reference creates no such event for that preset.  Oracle and
# specification disagree, so the event is not scored either way.  Step 7 has no
# entry in `steps.py`, so the exclusion is applied here in the derivations
# rather than by editing a file other scenarios share.
NOT_SCORED_TYPES = {"m.room.guest_access"}


def events(seq):
    """A list of events with the types this step does not score removed."""
    return [e for e in (seq or []) if e.get("type") not in NOT_SCORED_TYPES]


# --------------------------------------------------------------------------- #
# Requests
# --------------------------------------------------------------------------- #
def slide(c, user, body, *, pos=None, timeout=None, token=None, check=False,
          label=None, derive=None, http_timeout=None):
    """One simplified-sliding-sync request.

    `pos` and `timeout` are query parameters on this endpoint, not body
    fields; the harness renders a `pos` in a path as a constant, so the
    token's identity never reaches a transcript.
    """
    query = {}
    if pos is not None:
        query["pos"] = pos
    if timeout is not None:
        query["timeout"] = timeout
    return c.req("POST", SSYNC, token=token if token is not None else user.token,
                 json_body=body, query=query or None, check=check, label=label,
                 derive=derive, timeout=http_timeout)


def window(*, name="all", ranges=((0, 9),), required_state=(), timeline_limit=3,
           filters=None, conn_id=None, extensions=None, subscriptions=None,
           extra_lists=None):
    """A request body with one list, which is what nearly every scenario wants."""
    lists = {name: {"ranges": [list(r) for r in ranges],
                    "required_state": [list(e) for e in required_state],
                    "timeline_limit": timeline_limit}}
    if filters is not None:
        lists[name]["filters"] = filters
    if extra_lists:
        lists.update(extra_lists)
    body = {"lists": lists}
    if conn_id is not None:
        body["conn_id"] = conn_id
    if extensions is not None:
        body["extensions"] = extensions
    if subscriptions is not None:
        body["room_subscriptions"] = subscriptions
    return body


def settle(c, user, body, want, *, pos=None, tries=100, delay=0.25):
    """Poll unchecked until the server reports what the scenario just did.

    The proposal has a client re-send the same `pos` after cancelling an
    in-flight request, so repeating one is allowed and is observed to be
    idempotent; nothing here is scored, it only removes the race between a
    write and the read that is scored next.
    """
    r = None
    for _ in range(tries):
        r = slide(c, user, body, pos=pos, check=False)
        if r.status == 200 and r.json is not None:
            try:
                if want(r.json):
                    return r
            except Exception:                                  # noqa: BLE001
                pass
        time.sleep(delay)
    return r


# The reference caches a `/sync` response for two minutes under a key that
# includes the device, the `since` token and the `timeout`, so repeating a
# request returns the first response again instead of observing the server a
# second time.  Every `/sync` here therefore carries its own `timeout`: the
# unchecked polls take values from `POLL_TIMEOUT` upwards, and each scored
# `/sync` is given a small distinct one of its own.
POLL_TIMEOUT = 900


def settle_sync(c, user, want, *, since=None, tries=100, delay=0.25):
    """The same, for `GET /sync`."""
    r = None
    for i in range(tries):
        r = sync(c, user, since=since, timeout=POLL_TIMEOUT + i, check=False)
        if r.status == 200 and r.json is not None:
            try:
                if want(r.json):
                    return r
            except Exception:                                  # noqa: BLE001
                pass
        time.sleep(delay)
    return r


def pos_of(r):
    return (r.json or {}).get("pos")


# --------------------------------------------------------------------------- #
# Derivations: structure, never volume, never an opaque token
# --------------------------------------------------------------------------- #
def counts(b):
    """Each list's `count`, which the proposal defines as ignoring the range."""
    return {"lists": sorted(b.get("lists") or {}),
            "counts": {k: v.get("count")
                       for k, v in (b.get("lists") or {}).items()}}


def names(b):
    """Which rooms came back, named by the room name the server computed."""
    return {"rooms": {rid: r.get("name")
                      for rid, r in (b.get("rooms") or {}).items()}}


def rooms_shape(b):
    """The structural facts of each returned room."""
    out = {}
    for rid, r in (b.get("rooms") or {}).items():
        out[rid] = {
            "initial": bool(r.get("initial")),
            # `limited` is optional, and absent says what `false` says, so the
            # two are recorded the same way.
            "limited": bool(r.get("limited")),
            "required_state": sorted([e.get("type"), e.get("state_key", "")]
                                     for e in events(r.get("required_state"))),
            "timeline": [e.get("type") for e in events(r.get("timeline"))],
            # `prev_batch` is "a token to supply to /messages to retrieve
            # earlier messages" and the release does not say a server must
            # offer one for every room, so whether it is there is the server's
            # choice and not compared.
        }
    return {"rooms": out}


def rooms_bodies(b):
    """What the timeline of each returned room actually said."""
    return {"bodies": {rid: [e.get("content", {}).get("body")
                             for e in events(r.get("timeline"))]
                       for rid, r in (b.get("rooms") or {}).items()}}


def stripped_shape(b):
    """An invited room: the proposal gives it stripped state and no timeline.

    The reference still spells the field `invite_state` (the name MSC3575
    used); the proposal renamed it to `stripped_state`.  Whichever name is
    used, the same facts are pinned.
    """
    out = {}
    for rid, r in (b.get("rooms") or {}).items():
        stripped = r.get("stripped_state")
        if stripped is None:
            stripped = r.get("invite_state")
        out[rid] = {
            "initial": bool(r.get("initial")),
            "stripped": sorted([e.get("type"), e.get("state_key", "")]
                               for e in events(stripped)),
            # An invited room has no timeline to send.  A server that says so
            # by leaving the key out and one that says so with an empty list
            # have said the same thing.
            "timeline_is_empty": not (r.get("timeline") or []),
            "has_required_state": "required_state" in r,
        }
    return {"rooms": out}


def extensions_present(b):
    return {"extensions": sorted(b.get("extensions") or {})}


def ext(b, name):
    return ((b.get("extensions") or {}).get(name)) or {}


def to_device_shape(b):
    e = ext(b, "to_device")
    return {
        "keys": sorted(e),
        "has_next_batch": isinstance(e.get("next_batch"), str),
        "events": [[x.get("type"), x.get("sender")] for x in e.get("events") or []],
    }


def e2ee_shape(b):
    """The e2ee extension, compared on what the release fixes.

    "If an algorithm is not listed, the count for that algorithm is zero", so
    the count for the algorithm these scenarios use is read with absent as
    zero; and which *other* keys the extension carries (`device_lists` when
    there is movement to report, and not when there is none) is not a
    difference between servers.
    """
    e = ext(b, "e2ee")
    counts = e.get("device_one_time_keys_count") or {}
    return {
        "signed_curve25519_count": int(counts.get("signed_curve25519") or 0),
        "unused_fallback_key_types": sorted(
            e.get("device_unused_fallback_key_types") or []),
    }


def account_data_shape(b):
    e = ext(b, "account_data")
    return {
        "keys": sorted(e),
        "rooms": {rid: sorted(x.get("type") for x in evs)
                  for rid, evs in (e.get("rooms") or {}).items() if evs},
    }


def account_data_content(b, etype):
    e = ext(b, "account_data")
    return {"content": {rid: [x.get("content") for x in evs or []
                              if x.get("type") == etype]
                        for rid, evs in (e.get("rooms") or {}).items()}}


def receipts_shape(b):
    e = ext(b, "receipts")
    out = {}
    for rid, edu in (e.get("rooms") or {}).items():
        seen = set()
        for _event, by_type in ((edu or {}).get("content") or {}).items():
            for rtype, users in (by_type or {}).items():
                for who in users or {}:
                    seen.add((rtype, who))
        out[rid] = {"type": (edu or {}).get("type"),
                    "receipts": sorted(list(x) for x in seen)}
    return {"keys": sorted(e), "rooms": out}


def typing_shape(b):
    e = ext(b, "typing")
    return {"keys": sorted(e),
            "rooms": {rid: {"type": (edu or {}).get("type"),
                            "user_ids": sorted(((edu or {}).get("content") or {})
                                               .get("user_ids") or [])}
                      for rid, edu in (e.get("rooms") or {}).items()}}


def typing_users(b):
    """Who is typing, without pinning whether a quiet room is listed at all."""
    return {"keys": sorted(ext(b, "typing")),
            "typing": sorted(
                [rid, who]
                for rid, edu in (ext(b, "typing").get("rooms") or {}).items()
                for who in ((edu or {}).get("content") or {}).get("user_ids") or [])}


def errcode(b):
    """An error body, compared on its code alone: the text is never specified."""
    return {"errcode": b.get("errcode")}


def refused(b):
    """That the request was refused with *some* code, without pinning which."""
    return {"refused": isinstance(b.get("errcode"), str)}


def v2_rooms(b):
    """`GET /sync`, reduced the same way, so the two APIs can be compared."""
    out = {}
    for rid, r in ((b.get("rooms") or {}).get("join") or {}).items():
        tl = r.get("timeline") or {}
        out[rid] = {
            "timeline": [e.get("type") for e in events(tl.get("events"))],
            "bodies": [e.get("content", {}).get("body")
                       for e in events(tl.get("events"))],
            "limited": tl.get("limited"),
        }
    return {"join": out}


def v2_room_account_data(b, types_only=True):
    out = {}
    for rid, r in ((b.get("rooms") or {}).get("join") or {}).items():
        evs = (r.get("account_data") or {}).get("events") or []
        out[rid] = (sorted(e.get("type") for e in evs) if types_only else
                    sorted(([e.get("type"), e.get("content")] for e in evs),
                           key=lambda x: x[0]))
    return {"room_account_data": out}


# --------------------------------------------------------------------------- #
# Key material for the /keys/claim ordering scenario
# --------------------------------------------------------------------------- #
def _b64(raw: bytes) -> str:
    return base64.b64encode(raw).decode().rstrip("=")


def material(label: str, size: int = 32) -> str:
    """A fixed, made-up key / signature, so two recordings carry the same bytes."""
    out = b""
    i = 0
    while len(out) < size:
        out += hashlib.sha512(f"mxconf/step07/{label}/{i}".encode()).digest()
        i += 1
    return _b64(out[:size])


def otk(user: User, device_id: str, name: str) -> dict:
    return {
        "key": material(f"otk/{device_id}/{name}"),
        "signatures": {user.user_id: {
            f"ed25519:{device_id}": material(f"otksig/{device_id}/{name}", 64)}},
    }


def claimed(b):
    """Which one-time keys came back, and the key material itself."""
    return {
        "ids": {uid: {dev: sorted(keys) for dev, keys in sorted(devs.items())}
                for uid, devs in sorted((b.get("one_time_keys") or {}).items())},
        "keys": {kid: obj.get("key")
                 for devs in (b.get("one_time_keys") or {}).values()
                 for keys in devs.values()
                 for kid, obj in keys.items()},
        "failures": b.get("failures") or {},
    }


# --------------------------------------------------------------------------- #
# Shared setup
# --------------------------------------------------------------------------- #
def rooms_with_messages(c, u, specs):
    """Create rooms in order and give each one message, newest last.

    A list's range selects rooms by how recently each was active, so every
    scenario that uses a narrow range fixes that order here explicitly.
    """
    made = [create_room(c, u, room_version=V, **spec) for spec in specs]
    for i, room in enumerate(made):
        for j in range(3):
            send(c, u, room, f"seed{i}-{j}",
                 {"msgtype": "m.text", "body": f"seed {i}.{j}"})
    return made


# =========================================================================== #
# Simplified sliding sync
# =========================================================================== #
@scenario("sl/slide-endpoint-advertised", tags=["sliding-sync", "msc4186"], step=7)
def slide_endpoint_advertised(c):
    """The endpoint exists, is POST-only, and needs an access token."""
    u = register(c, "sla1")
    # `unstable_features` is where a server advertises what it supports ahead
    # of a release, and which flags it puts there is its own business -- the
    # rest of the harness drops the object for that reason.  What is scored is
    # the endpoint, below, not the advertisement.
    c.req("GET", "/_matrix/client/versions",
          label="the versions endpoint answers",
          derive=lambda b: {"versions_is_a_list":
                            isinstance(b.get("versions"), list)
                            and bool(b.get("versions"))})
    slide(c, u, {}, check=True,
          label="a request with no lists still returns a position",
          derive=lambda b: {"has_pos": isinstance(b.get("pos"), str)
                            and bool(b.get("pos")),
                            "rooms": b.get("rooms") or {},
                            "lists": b.get("lists") or {},
                            "extensions": b.get("extensions") or {}})
    # Which of 404/405 and which errcode a server uses for "wrong method" is
    # not settled by the release; that it refuses is.
    c.req("GET", SSYNC, token=u.token, label="the endpoint is POST-only",
          derive=lambda b: {"refused": True})
    c.req("POST", SSYNC, json_body=window(), label="without an access token",
          derive=errcode)
    c.req("POST", SSYNC, token="syt_not_a_real_token", json_body=window(),
          label="with an unknown access token", derive=errcode)


@scenario("sl/slide-first-page", tags=["sliding-sync", "msc4186"], step=7)
def slide_first_page(c):
    """A first request with no `pos`: one list, a window, a required state."""
    u = register(c, "slb1")
    rooms_with_messages(c, u, [
        {"name": "Alpha", "preset": "public_chat"},
        {"name": "Beta", "preset": "public_chat"},
        {"name": "Gamma", "preset": "public_chat"},
        {"name": "Delta", "preset": "public_chat"},
    ])
    base = dict(required_state=[["m.room.create", ""]], timeline_limit=3)
    settle(c, u, window(conn_id="warm", **base),
           lambda b: len(b.get("rooms") or {}) == 4)
    slide(c, u, window(conn_id="c1", **base), check=True,
          label="the list count over four rooms", derive=counts)
    slide(c, u, window(conn_id="c2", **base), check=True,
          label="which rooms the window returned", derive=names)
    slide(c, u, window(conn_id="c3", **base), check=True,
          label="each room is initial, with its required state and timeline",
          derive=rooms_shape)
    slide(c, u, window(conn_id="c4", required_state=[], timeline_limit=3),
          check=True, label="an empty required_state resolves nothing",
          derive=rooms_shape)
    slide(c, u, window(conn_id="c5",
                       required_state=[["m.room.create", ""],
                                       ["m.room.name", ""],
                                       ["m.room.topic", ""]],
                       timeline_limit=1),
          check=True, label="a required_state entry with no such event is absent",
          derive=rooms_shape)
    slide(c, u, window(conn_id="c6", ranges=[[0, 1]], **base), check=True,
          label="a narrower range returns fewer rooms but the same count",
          derive=lambda b: dict(counts(b), **names(b)))
    slide(c, u, window(conn_id="c7",
                       extra_lists={"second": {"ranges": [[0, 0]],
                                               "required_state": [],
                                               "timeline_limit": 1}},
                       **base),
          check=True, label="two lists in one request each get a count",
          derive=counts)
    slide(c, u, window(conn_id="c8", **base), check=True,
          label="the first page carries a position",
          derive=lambda b: {"has_pos": isinstance(b.get("pos"), str)
                            and bool(b.get("pos")),
                            # `pos` and `rooms` are what the response must
                            # carry; `lists`, `extensions` and the rest are
                            # there when there is something to put in them.
                            "required_top_level": sorted(
                                k for k in ("pos", "rooms") if k in b)})


@scenario("sl/slide-second-request-with-pos", tags=["sliding-sync", "msc4186"],
          step=7)
def slide_second_request(c):
    """A second request carrying the returned `pos` reports only what changed."""
    u = register(c, "slc1")
    alpha, beta, gamma = rooms_with_messages(c, u, [
        {"name": "Alpha", "preset": "public_chat"},
        {"name": "Beta", "preset": "public_chat"},
        {"name": "Gamma", "preset": "public_chat"},
    ])
    body = window(conn_id="live", required_state=[["m.room.create", ""]],
                  timeline_limit=3)
    settle(c, u, body, lambda b: len(b.get("rooms") or {}) == 3)
    r = slide(c, u, body, check=True, label="the initial page",
              derive=lambda b: dict(counts(b), **names(b)))
    p = pos_of(r)

    r = slide(c, u, body, pos=p, check=True,
              label="a second request with nothing new returns no rooms",
              derive=rooms_shape)
    p = pos_of(r)

    send(c, u, beta, "new1", {"msgtype": "m.text", "body": "a new message"})
    settle(c, u, body, lambda b: beta in (b.get("rooms") or {}), pos=p)
    r = slide(c, u, body, pos=p, check=True,
              label="only the room that changed comes back, not initial",
              derive=lambda b: dict(rooms_shape(b), **rooms_bodies(b)))
    p = pos_of(r)

    r = slide(c, u, body, pos=p, check=True,
              label="and nothing after that", derive=rooms_shape)
    p = pos_of(r)

    send(c, u, gamma, "new2", {"msgtype": "m.text", "body": "gamma moved"})
    send(c, u, alpha, "new3", {"msgtype": "m.text", "body": "alpha moved"})
    settle(c, u, body,
           lambda b: {alpha, gamma} <= set(b.get("rooms") or {}), pos=p)
    slide(c, u, body, pos=p, check=True,
          label="two rooms that changed come back together",
          derive=lambda b: dict(rooms_shape(b), **rooms_bodies(b)))

    r = slide(c, u, body, check=True,
              label="a request with no pos on the same conn_id starts over",
              derive=rooms_shape)
    slide(c, u, body, pos=pos_of(r), check=True,
          label="and the restarted connection then reports nothing new",
          derive=rooms_shape)


@scenario("sl/slide-required-state-wildcards", tags=["sliding-sync", "msc4186"],
          step=7)
def slide_required_state_wildcards(c):
    """`["*","*"]`, `["m.room.member","$LAZY"]` and `["m.room.member","$ME"]`."""
    u = register(c, "sld1")
    v = register(c, "sld2")
    w = register(c, "sld3")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Tri")
    join(c, v, room)
    join(c, w, room)
    send(c, u, room, "a", {"msgtype": "m.text", "body": "from u"})
    send(c, v, room, "b", {"msgtype": "m.text", "body": "from v"})
    send(c, w, room, "c", {"msgtype": "m.text", "body": "from w"})
    settle(c, u, window(conn_id="warm", timeline_limit=2),
           lambda b: len(((b.get("rooms") or {}).get(room) or {})
                         .get("timeline") or []) == 2)

    def ask(tag, rs, label):
        slide(c, u, window(conn_id=tag, required_state=rs, timeline_limit=2),
              check=True, label=label, derive=rooms_shape)

    ask("w1", [["m.room.create", ""]], "a single named state event")
    ask("w2", [["*", "*"]], "the ['*','*'] wildcard resolves every state event")
    ask("w3", [["m.room.member", "$LAZY"]],
        "$LAZY resolves the senders of the timeline events")
    ask("w4", [["m.room.member", "$ME"]],
        "$ME resolves the calling user's own membership")
    ask("w5", [["m.room.member", "*"]],
        "a state-key wildcard resolves every membership")
    ask("w6", [["m.room.member", "$LAZY"], ["m.room.create", ""]],
        "$LAZY beside a named state event")
    ask("w7", [["m.room.member", v.user_id]],
        "one membership named by its state key")
    ask("w8", [], "an empty required_state")


@scenario("sl/slide-room-subscription", tags=["sliding-sync", "msc4186"], step=7)
def slide_room_subscription(c):
    """`room_subscriptions` reach a room the list's ranges leave out."""
    u = register(c, "sle1")
    v = register(c, "sle2")
    one, _two, three = rooms_with_messages(c, u, [
        {"name": "One", "preset": "public_chat"},
        {"name": "Two", "preset": "public_chat"},
        {"name": "Three", "preset": "public_chat"},
    ])
    elsewhere = create_room(c, v, room_version=V, preset="private_chat",
                            name="Elsewhere")
    top = window(conn_id="warm", ranges=[[0, 0]],
                 required_state=[["m.room.create", ""]], timeline_limit=1)
    settle(c, u, top, lambda b: set(b.get("rooms") or {}) == {three})

    sub = {one: {"required_state": [["m.room.name", ""]], "timeline_limit": 1}}
    slide(c, u, window(conn_id="s1", ranges=[[0, 0]],
                       required_state=[["m.room.create", ""]], timeline_limit=1),
          check=True, label="the window alone returns one room", derive=names)
    slide(c, u, window(conn_id="s2", ranges=[[0, 0]],
                       required_state=[["m.room.create", ""]], timeline_limit=1,
                       subscriptions=sub),
          check=True, label="a subscription adds a room outside the ranges",
          derive=names)
    slide(c, u, window(conn_id="s3", ranges=[[0, 0]],
                       required_state=[["m.room.create", ""]], timeline_limit=1,
                       subscriptions=sub),
          check=True,
          label="the subscribed room carries its own required_state",
          derive=rooms_shape)
    slide(c, u, {"conn_id": "s4", "room_subscriptions": sub}, check=True,
          label="a subscription with no list at all",
          derive=lambda b: dict(counts(b), **rooms_shape(b)))
    slide(c, u, window(conn_id="s5", ranges=[[0, 0]],
                       required_state=[["m.room.create", ""]], timeline_limit=1,
                       subscriptions={three: {"required_state":
                                              [["m.room.name", ""]],
                                              "timeline_limit": 1}}),
          check=True,
          label="subscribing to a room already in the window",
          derive=rooms_shape)
    slide(c, u, window(conn_id="s6", ranges=[[0, 0]], required_state=[],
                       timeline_limit=1,
                       subscriptions={elsewhere: {"required_state": [],
                                                  "timeline_limit": 1}}),
          check=True,
          label="subscribing to a room the caller is not in",
          derive=names)


@scenario("sl/slide-filter-encryption",
          tags=["sliding-sync", "msc4186", "filters"], step=7)
def slide_filter_encryption(c):
    """The `is_encrypted` list filter."""
    u = register(c, "slf1")
    rooms_with_messages(c, u, [
        {"name": "Plain One", "preset": "public_chat"},
        {"name": "Plain Two", "preset": "public_chat"},
        {"name": "Secret", "preset": "public_chat",
         "initial_state": [{"type": "m.room.encryption", "state_key": "",
                            "content": {"algorithm": "m.megolm.v1.aes-sha2"}}]},
    ])
    settle(c, u, window(conn_id="warm", timeline_limit=1),
           lambda b: len(b.get("rooms") or {}) == 3)
    slide(c, u, window(conn_id="e0", required_state=[], timeline_limit=1),
          check=True, label="no filter sees every room", derive=counts)
    slide(c, u, window(conn_id="e1", required_state=[], timeline_limit=1,
                       filters={"is_encrypted": True}),
          check=True, label="is_encrypted true selects the encrypted room",
          derive=lambda b: dict(counts(b), **names(b)))
    slide(c, u, window(conn_id="e2", required_state=[["m.room.encryption", ""]],
                       timeline_limit=1, filters={"is_encrypted": True}),
          check=True, label="and that room does carry m.room.encryption",
          derive=rooms_shape)
    slide(c, u, window(conn_id="e3", required_state=[], timeline_limit=1,
                       filters={"is_encrypted": False}),
          check=True, label="is_encrypted false selects the others",
          derive=lambda b: dict(counts(b), **names(b)))
    slide(c, u, window(conn_id="e4", required_state=[], timeline_limit=1,
                       filters={"is_encrypted": True},
                       extra_lists={"plain": {"ranges": [[0, 9]],
                                              "required_state": [],
                                              "timeline_limit": 1,
                                              "filters": {"is_encrypted": False}}}),
          check=True, label="two lists filtered in opposite directions",
          derive=lambda b: dict(counts(b), **names(b)))


@scenario("sl/slide-filter-invite", tags=["sliding-sync", "msc4186", "filters"],
          step=7)
def slide_filter_invite(c):
    """The `is_invite` list filter, and the shape of an invited room."""
    u = register(c, "slg1")
    v = register(c, "slg2")
    shared = create_room(c, u, room_version=V, preset="public_chat", name="Shared")
    join(c, v, shared)
    send(c, u, shared, "s1", {"msgtype": "m.text", "body": "hello"})
    pending = create_room(c, u, room_version=V, preset="private_chat",
                          name="Pending")
    invite(c, u, pending, v.user_id)
    settle(c, v, window(conn_id="warm", timeline_limit=1),
           lambda b: len(b.get("rooms") or {}) == 2)
    slide(c, v, window(conn_id="i0", required_state=[], timeline_limit=1),
          check=True, label="the invited user sees both rooms", derive=counts)
    slide(c, v, window(conn_id="i1", required_state=[], timeline_limit=1,
                       filters={"is_invite": True}),
          check=True, label="is_invite true selects the invitation",
          derive=lambda b: dict(counts(b), **names(b)))
    slide(c, v, window(conn_id="i2", required_state=[["m.room.create", ""]],
                       timeline_limit=5, filters={"is_invite": True}),
          check=True, label="an invited room carries stripped state, no timeline",
          derive=stripped_shape)
    slide(c, v, window(conn_id="i3", required_state=[], timeline_limit=1,
                       filters={"is_invite": False}),
          check=True, label="is_invite false selects the joined room",
          derive=lambda b: dict(counts(b), **names(b)))
    slide(c, u, window(conn_id="i4", required_state=[], timeline_limit=1,
                       filters={"is_invite": True}),
          check=True, label="the inviting user has no invitations of their own",
          derive=lambda b: dict(counts(b), **names(b)))
    join(c, v, pending)
    settle(c, v, window(conn_id="warm2", timeline_limit=1,
                        filters={"is_invite": True}),
           lambda b: (b.get("lists") or {}).get("all", {}).get("count") == 0)
    slide(c, v, window(conn_id="i5", required_state=[], timeline_limit=1,
                       filters={"is_invite": True}),
          check=True, label="after accepting, nothing is an invitation",
          derive=lambda b: dict(counts(b), **names(b)))


@scenario("sl/slide-filter-room-types",
          tags=["sliding-sync", "msc4186", "filters"], step=7)
def slide_filter_room_types(c):
    """`room_types` and `not_room_types`, including the untyped `null`."""
    u = register(c, "slh1")
    rooms_with_messages(c, u, [
        {"name": "Plain", "preset": "public_chat"},
        {"name": "Space", "preset": "public_chat",
         "creation_content": {"type": "m.space"}},
        {"name": "Custom", "preset": "public_chat",
         "creation_content": {"type": "com.example.thing"}},
    ])
    settle(c, u, window(conn_id="warm", timeline_limit=1),
           lambda b: len(b.get("rooms") or {}) == 3)

    def ask(tag, filters, label):
        slide(c, u, window(conn_id=tag, required_state=[], timeline_limit=1,
                           filters=filters),
              check=True, label=label,
              derive=lambda b: dict(counts(b), **names(b)))

    ask("t0", None, "no room-type filter")
    ask("t1", {"room_types": ["m.space"]}, "room_types selects the space")
    ask("t2", {"not_room_types": ["m.space"]}, "not_room_types excludes it")
    ask("t3", {"room_types": [None]}, "room_types [null] selects untyped rooms")
    ask("t4", {"room_types": ["com.example.thing"]},
        "room_types selects a room type of the caller's own choosing")
    ask("t5", {"room_types": [None, "com.example.thing"]},
        "room_types accepts several values at once")
    ask("t6", {"room_types": ["m.space"], "not_room_types": ["m.space"]},
        "not_room_types takes priority over room_types")


@scenario("sl/slide-two-conn-ids", tags=["sliding-sync", "msc4186"], step=7)
def slide_two_conn_ids(c):
    """Two connections advance independently and do not interfere."""
    u = register(c, "sli1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Shared")
    send(c, u, room, "s0", {"msgtype": "m.text", "body": "zero"})
    one = window(conn_id="one", required_state=[["m.room.create", ""]],
                 timeline_limit=5)
    two = window(conn_id="two", required_state=[["m.room.create", ""]],
                 timeline_limit=5)
    settle(c, u, window(conn_id="warm", timeline_limit=1),
           lambda b: room in (b.get("rooms") or {}))
    r1 = slide(c, u, one, check=True, label="connection one opens",
               derive=lambda b: dict(counts(b), **names(b)))
    r2 = slide(c, u, two, check=True, label="connection two opens",
               derive=lambda b: dict(counts(b), **names(b)))
    p1, p2 = pos_of(r1), pos_of(r2)

    send(c, u, room, "s1", {"msgtype": "m.text", "body": "one"})
    settle(c, u, one, lambda b: room in (b.get("rooms") or {}), pos=p1)
    r = slide(c, u, one, pos=p1, check=True,
              label="connection one takes the first message",
              derive=rooms_bodies)
    p1 = pos_of(r)

    send(c, u, room, "s2", {"msgtype": "m.text", "body": "two"})
    settle(c, u, one, lambda b: room in (b.get("rooms") or {}), pos=p1)
    r = slide(c, u, one, pos=p1, check=True,
              label="and then the second, one delta at a time",
              derive=rooms_bodies)
    p1 = pos_of(r)

    settle(c, u, two,
           lambda b: len(((b.get("rooms") or {}).get(room) or {})
                         .get("timeline") or []) == 2, pos=p2)
    slide(c, u, two, pos=p2, check=True,
          label="connection two, untouched, takes both in one delta",
          derive=lambda b: dict(rooms_shape(b), **rooms_bodies(b)))
    slide(c, u, one, pos=p1, check=True,
          label="connection one has nothing left", derive=rooms_shape)


@scenario("sl/slide-unknown-pos", tags=["sliding-sync", "msc4186"], step=7)
def slide_unknown_pos(c):
    """A `pos` the server did not issue for this connection: `M_UNKNOWN_POS`."""
    u = register(c, "slj1")
    v = register(c, "slj2")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    join(c, v, room)
    send(c, u, room, "s0", {"msgtype": "m.text", "body": "zero"})
    one = window(conn_id="alpha", required_state=[], timeline_limit=1)
    two = window(conn_id="beta", required_state=[], timeline_limit=1)
    settle(c, u, window(conn_id="warm", timeline_limit=1),
           lambda b: room in (b.get("rooms") or {}))
    ra = slide(c, u, one, check=True, label="connection alpha opens",
               derive=counts)
    rb = slide(c, u, two, check=True, label="connection beta opens",
               derive=counts)
    pa, pb = pos_of(ra), pos_of(rb)

    slide(c, u, one, pos=pb, check=True,
          label="beta's position offered to alpha", derive=errcode)
    slide(c, u, two, pos=pa, check=True,
          label="alpha's position offered to beta", derive=errcode)
    settle(c, v, window(conn_id="warm", timeline_limit=1),
           lambda b: room in (b.get("rooms") or {}))
    slide(c, v, window(conn_id="alpha", required_state=[], timeline_limit=1),
          pos=pa, check=True,
          label="a position issued to another user", derive=errcode)
    slide(c, u, one, pos="not-a-position", check=True,
          label="a position the server never issued at all", derive=refused)
    slide(c, u, one, pos=pa, check=True,
          label="alpha's own position still works", derive=rooms_shape)


@scenario("sl/slide-ext-to-device",
          tags=["sliding-sync", "msc4186", "extensions"], step=7)
def slide_ext_to_device(c):
    """The `to_device` extension is present and correctly shaped."""
    u = register(c, "slk1")
    v = register(c, "slk2")
    create_room(c, u, room_version=V, preset="public_chat", name="Room")
    body = window(conn_id="td", required_state=[], timeline_limit=1,
                  extensions={"to_device": {"enabled": True}})
    settle(c, u, body, lambda b: "to_device" in (b.get("extensions") or {}))
    slide(c, u, body, check=True,
          label="the to_device section with nothing waiting",
          derive=to_device_shape)
    c.req("PUT", f"{CS}/sendToDevice/com.example.hello/txn1", token=v.token,
          json_body={"messages": {u.user_id: {"*": {"greeting": "hi"}}}},
          check=False)
    settle(c, u, body, lambda b: ext(b, "to_device").get("events"))
    slide(c, u, body, check=True,
          label="a message sent to the device appears there",
          derive=to_device_shape)
    slide(c, u, window(conn_id="td2", required_state=[], timeline_limit=1,
                       extensions={"to_device": {"enabled": False}}),
          check=True, label="the extension disabled",
          derive=lambda b: dict(extensions_present(b), **to_device_shape(b)))
    slide(c, u, window(conn_id="td3", required_state=[], timeline_limit=1),
          check=True, label="no extensions requested at all",
          derive=extensions_present)


@scenario("sl/slide-ext-e2ee", tags=["sliding-sync", "msc4186", "extensions"],
          step=7)
def slide_ext_e2ee(c):
    """The `e2ee` extension is present and correctly shaped."""
    u = register(c, "sll1")
    create_room(c, u, room_version=V, preset="public_chat", name="Room")
    body = window(conn_id="ee", required_state=[], timeline_limit=1,
                  extensions={"e2ee": {"enabled": True}})
    settle(c, u, body, lambda b: "e2ee" in (b.get("extensions") or {}))
    slide(c, u, body, check=True,
          label="the e2ee section before any key is uploaded",
          derive=e2ee_shape)
    c.req("POST", f"{CS}/keys/upload", token=u.token, check=False,
          json_body={"one_time_keys": {
              "signed_curve25519:AAAAAA": otk(u, u.device_id, "a"),
              "signed_curve25519:AAAAAB": otk(u, u.device_id, "b")}})
    settle(c, u, body,
           lambda b: (e2ee_shape(b)["one_time_key_counts"] or {})
           .get("signed_curve25519") == 2)
    slide(c, u, body, check=True, label="the one-time key count is reported",
          derive=e2ee_shape)
    c.req("POST", f"{CS}/keys/upload", token=u.token, check=False,
          json_body={"fallback_keys": {
              "signed_curve25519:FBAAAA": dict(otk(u, u.device_id, "f"),
                                               fallback=True)}})
    settle(c, u, body,
           lambda b: e2ee_shape(b)["unused_fallback_key_types"])
    slide(c, u, body, check=True,
          label="the unused fallback key types are reported",
          derive=e2ee_shape)
    slide(c, u, window(conn_id="ee2", required_state=[], timeline_limit=1,
                       extensions={"e2ee": {"enabled": False}}),
          check=True, label="the extension disabled",
          derive=lambda b: dict(extensions_present(b), **e2ee_shape(b)))


@scenario("sl/slide-ext-account-data",
          tags=["sliding-sync", "msc4186", "extensions"], step=7)
def slide_ext_account_data(c):
    """The `account_data` extension is present and correctly shaped."""
    u = register(c, "slm1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    send(c, u, room, "s0", {"msgtype": "m.text", "body": "zero"})
    body = window(conn_id="ad", required_state=[], timeline_limit=1,
                  extensions={"account_data": {"enabled": True}})
    settle(c, u, body, lambda b: "account_data" in (b.get("extensions") or {}))
    slide(c, u, body, check=True,
          label="the account_data section with only the defaults",
          derive=account_data_shape)
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/"
          "com.example.note", token=u.token, json_body={"n": 1}, check=False)
    settle(c, u, body,
           lambda b: any("com.example.note" in types
                         for types in account_data_shape(b)["rooms"].values()))
    slide(c, u, body, check=True, label="room account data appears there",
          derive=account_data_shape)
    c.req("PUT", f"{CS}/user/{q(u.user_id)}/account_data/com.example.global",
          token=u.token, json_body={"g": 2}, check=False)
    settle(c, u, body,
           lambda b: any(x.get("type") == "com.example.global"
                         for x in ext(b, "account_data").get("global") or []))
    slide(c, u, body, check=True, label="global account data appears there",
          # The full global set includes whatever defaults the reference
          # happens to seed; MSC4186 defines no account-data extension at all.
          # What this scenario established is that the type it just wrote shows
          # up, and that is what is scored.
          derive=lambda b: {"global_has_written": any(
              x.get("type") == "com.example.global"
              for x in ext(b, "account_data").get("global") or [])})
    slide(c, u, window(conn_id="ad2", required_state=[], timeline_limit=1,
                       extensions={"account_data": {"enabled": False}}),
          check=True, label="the extension disabled",
          derive=lambda b: dict(extensions_present(b), **account_data_shape(b)))


@scenario("sl/slide-ext-receipts",
          tags=["sliding-sync", "msc4186", "extensions"], step=7)
def slide_ext_receipts(c):
    """The `receipts` extension is present and correctly shaped."""
    u = register(c, "sln1")
    v = register(c, "sln2")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    join(c, v, room)
    body = window(conn_id="rc", required_state=[], timeline_limit=1,
                  extensions={"receipts": {"enabled": True}})
    settle(c, u, body, lambda b: "receipts" in (b.get("extensions") or {}))
    slide(c, u, body, check=True,
          label="the receipts section before anybody has read anything",
          derive=receipts_shape)
    eid = send(c, u, room, "s1", {"msgtype": "m.text", "body": "read me"})
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(eid)}", token=v.token,
          json_body={}, check=False)
    settle(c, u, body, lambda b: receipts_shape(b)["rooms"])
    slide(c, u, body, check=True, label="another user's read receipt appears",
          derive=receipts_shape)
    slide(c, u, window(conn_id="rc2", required_state=[], timeline_limit=1,
                       extensions={"receipts": {"enabled": False}}),
          check=True, label="the extension disabled",
          derive=lambda b: dict(extensions_present(b), **receipts_shape(b)))
    slide(c, u, window(conn_id="rc3", required_state=[], timeline_limit=1),
          check=True, label="no extension requested",
          derive=extensions_present)


@scenario("sl/slide-ext-typing", tags=["sliding-sync", "msc4186", "extensions"],
          step=7)
def slide_ext_typing(c):
    """The `typing` extension is present and correctly shaped."""
    u = register(c, "slo1")
    v = register(c, "slo2")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    join(c, v, room)
    send(c, u, room, "s0", {"msgtype": "m.text", "body": "zero"})
    body = window(conn_id="ty", required_state=[], timeline_limit=1,
                  extensions={"typing": {"enabled": True}})
    settle(c, u, body, lambda b: "typing" in (b.get("extensions") or {}))
    slide(c, u, body, check=True,
          label="the typing section with nobody typing", derive=typing_users)
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": True, "timeout": 30000}, check=False)
    settle(c, u, body, lambda b: any(r["user_ids"] for r in
                                     typing_shape(b)["rooms"].values()))
    slide(c, u, body, check=True, label="a user who is typing is listed",
          derive=typing_shape)
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(v.user_id)}", token=v.token,
          json_body={"typing": False}, check=False)
    settle(c, u, body,
           lambda b: typing_shape(b)["rooms"] and
           all(not r["user_ids"] for r in typing_shape(b)["rooms"].values()))
    slide(c, u, body, check=True, label="and is no longer listed once stopped",
          derive=typing_users)
    slide(c, u, window(conn_id="ty2", required_state=[], timeline_limit=1,
                       extensions={"typing": {"enabled": False}}),
          check=True, label="the extension disabled",
          derive=lambda b: dict(extensions_present(b), **typing_users(b)))


@scenario("sl/slide-timeout", tags=["sliding-sync", "msc4186"], step=7)
def slide_timeout(c):
    """`timeout` returns promptly when there is something to report."""
    u = register(c, "slp1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    send(c, u, room, "s0", {"msgtype": "m.text", "body": "zero"})
    body = window(conn_id="to", required_state=[], timeline_limit=3)
    settle(c, u, body, lambda b: room in (b.get("rooms") or {}))

    t0 = time.time()
    r = slide(c, u, body, timeout=10000, check=True,
              label="a timeout on a request with no pos is not waited out",
              http_timeout=60,
              derive=lambda b: dict(names(b), prompt=(time.time() - t0) < 5.0))
    p = pos_of(r)

    t1 = time.time()
    r = slide(c, u, body, pos=p, timeout=1500, check=True,
              label="with nothing to report the server waits", http_timeout=60,
              derive=lambda b: dict(rooms_shape(b),
                                    waited=(time.time() - t1) >= 0.4))
    p = pos_of(r)

    send(c, u, room, "s1", {"msgtype": "m.text", "body": "something"})
    settle(c, u, body, lambda b: room in (b.get("rooms") or {}), pos=p)
    t2 = time.time()
    r = slide(c, u, body, pos=p, timeout=10000, check=True,
              label="with something to report it returns promptly",
              http_timeout=60,
              derive=lambda b: dict(rooms_bodies(b),
                                    prompt=(time.time() - t2) < 5.0))
    p = pos_of(r)

    t3 = time.time()
    slide(c, u, body, pos=p, timeout=0, check=True,
          label="a zero timeout returns at once", http_timeout=60,
          derive=lambda b: dict(rooms_shape(b),
                                prompt=(time.time() - t3) < 3.0))
    slide(c, u, body, pos=p, check=True,
          label="and so does no timeout parameter at all", derive=rooms_shape)


@scenario("sl/slide-beside-v2-sync", tags=["sliding-sync", "msc4186", "sync"],
          step=7)
def slide_beside_v2_sync(c):
    """`GET /sync` keeps working unchanged beside the sliding-sync endpoint."""
    u = register(c, "slq1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    for i in range(3):
        send(c, u, room, f"s{i}", {"msgtype": "m.text", "body": f"zero {i}"})
    body = window(conn_id="mix", required_state=[["m.room.create", ""]],
                  timeline_limit=3)
    settle(c, u, body, lambda b: room in (b.get("rooms") or {}))
    settle_sync(c, u, lambda b: room in ((b.get("rooms") or {}).get("join") or {}))
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 1},
              label="a v2 sync beside sliding sync", derive=v2_rooms)
    since = r.json["next_batch"]
    rs = slide(c, u, body, check=True, label="and the sliding-sync first page",
               derive=lambda b: dict(names(b), **rooms_bodies(b)))
    p = pos_of(rs)

    send(c, u, room, "later", {"msgtype": "m.text", "body": "shared"})
    settle_sync(c, u, lambda b: any(
        "shared" in v2_rooms(b)["join"].get(rid, {}).get("bodies", [])
        for rid in v2_rooms(b)["join"]), since=since)
    settle(c, u, body,
           lambda b: "shared" in rooms_bodies(b)["bodies"].get(room, []), pos=p)
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 2, "since": since},
          label="the v2 delta carries the new message", derive=v2_rooms)
    slide(c, u, body, pos=p, check=True,
          label="the sliding-sync delta carries it too", derive=rooms_bodies)
    # An explicit, generous timeline limit: the default one truncates, and how
    # many events a room holds at this point depends on whether the server
    # writes an `m.room.guest_access` event for `public_chat` -- which the
    # release's preset table says to do and the recording reference does not.
    # That disagreement is not scored anywhere (see `steps.EXCLUDED_STATE_TYPES`),
    # so it must not decide this check either.
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 3,
                 "filter": json.dumps({"room": {"timeline": {"limit": 50}}})},
          label="a fresh v2 sync also carries it", derive=v2_rooms)
    slide(c, u, window(conn_id="mix2", required_state=[["m.room.create", ""]],
                       timeline_limit=3),
          check=True, label="and so is a fresh sliding-sync page",
          derive=rooms_shape)


@scenario("sl/slide-timeline-limit", tags=["sliding-sync", "msc4186"], step=7)
def slide_timeline_limit(c):
    """`timeline_limit` truncates the timeline and sets `limited`."""
    u = register(c, "slr1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    for i in range(10):
        send(c, u, room, f"m{i}", {"msgtype": "m.text", "body": f"body {i}"})
    settle(c, u, window(conn_id="warm", timeline_limit=10),
           lambda b: len(((b.get("rooms") or {}).get(room) or {})
                         .get("timeline") or []) == 10)

    def _shape(b):
        return dict(rooms_shape(b), **rooms_bodies(b))

    def _shape_no_limited(b):
        """MSC4186 defines `limited` as "more events since the previous sync
        than were included", and says nothing about timeline_limit=0. A server
        holding unreturned events may report true there and still conform, so
        that one field is not scored for this ask."""
        d = _shape(b)
        for r in (d.get("rooms") or {}).values():
            r.pop("limited", None)
        return d

    def ask(tag, limit, label, derive=None):
        slide(c, u, window(conn_id=tag, required_state=[], timeline_limit=limit),
              check=True, label=label, derive=derive or _shape)

    ask("l0", 0, "a timeline_limit of zero returns no timeline",
        derive=_shape_no_limited)
    ask("l1", 1, "a timeline_limit of one returns the newest event")
    ask("l2", 3, "a small limit truncates and sets limited")
    ask("l3", 5, "a larger limit returns more of the same tail")
    ask("l4", 100, "a limit past the whole room is not limited")


@scenario("sl/slide-list-ranges", tags=["sliding-sync", "msc4186"], step=7)
def slide_list_ranges(c):
    """A list's ranges select a window; the count is of the whole list."""
    u = register(c, "sls1")
    rooms_with_messages(c, u, [
        {"name": "One", "preset": "public_chat"},
        {"name": "Two", "preset": "public_chat"},
        {"name": "Three", "preset": "public_chat"},
        {"name": "Four", "preset": "public_chat"},
        {"name": "Five", "preset": "public_chat"},
    ])
    settle(c, u, window(conn_id="warm", timeline_limit=1),
           lambda b: len(b.get("rooms") or {}) == 5)

    def ask(tag, ranges, label):
        slide(c, u, window(conn_id=tag, ranges=ranges, required_state=[],
                           timeline_limit=1),
              check=True, label=label,
              derive=lambda b: dict(counts(b), **names(b)))

    ask("g0", [[0, 9]], "a range covering everything")
    ask("g1", [[0, 0]], "a range of one")
    ask("g2", [[0, 1]], "a range of two")
    ask("g3", [[1, 2]], "a range that does not start at zero")
    ask("g4", [[0, 0], [3, 4]], "two disjoint ranges in one list")
    ask("g5", [[9, 9]], "a range past the end of the list")


@scenario("sl/slide-membership-changes",
          tags=["sliding-sync", "msc4186", "membership"], step=7)
def slide_membership_changes(c):
    """An invitation becomes a join, and the list follows it."""
    u = register(c, "slt1")
    v = register(c, "slt2")
    room = create_room(c, u, room_version=V, preset="private_chat", name="Private")
    send(c, u, room, "s0", {"msgtype": "m.text", "body": "before"})
    invite(c, u, room, v.user_id)
    body = window(conn_id="mb", required_state=[["m.room.create", ""]],
                  timeline_limit=100)
    settle(c, v, body, lambda b: room in (b.get("rooms") or {}))
    r = slide(c, v, body, check=True, label="the invited user's first page",
              derive=lambda b: dict(counts(b), **stripped_shape(b)))
    p = pos_of(r)
    join(c, v, room)
    settle(c, v, body,
           lambda b: "timeline" in ((b.get("rooms") or {}).get(room) or {}),
           pos=p)
    slide(c, v, body, pos=p, check=True,
          label="accepting the invitation turns it into a joined room",
          derive=rooms_shape)
    slide(c, v, body, check=True, label="a fresh page shows a joined room",
          derive=lambda b: dict(counts(b), **rooms_shape(b)))
    send(c, u, room, "s1", {"msgtype": "m.text", "body": "after"})
    later = window(conn_id="mb2", required_state=[], timeline_limit=100)
    settle(c, v, later,
           lambda b: "after" in [e.get("content", {}).get("body")
                                 for e in ((b.get("rooms") or {}).get(room) or {})
                                 .get("timeline") or []])
    slide(c, v, later, check=True,
          label="and the new member can see the room's messages",
          derive=rooms_bodies)
    slide(c, u, window(conn_id="mb3",
                       required_state=[["m.room.member", "$LAZY"]],
                       timeline_limit=2),
          check=True, label="the inviter sees the membership in the timeline",
          derive=rooms_shape)


@scenario("sl/slide-multiple-lists", tags=["sliding-sync", "msc4186"], step=7)
def slide_multiple_lists(c):
    """Several lists in one request, each with its own window and filter."""
    u = register(c, "slu1")
    rooms_with_messages(c, u, [
        {"name": "Plain", "preset": "public_chat"},
        {"name": "Space", "preset": "public_chat",
         "creation_content": {"type": "m.space"}},
        {"name": "Secret", "preset": "public_chat",
         "initial_state": [{"type": "m.room.encryption", "state_key": "",
                            "content": {"algorithm": "m.megolm.v1.aes-sha2"}}]},
    ])
    settle(c, u, window(conn_id="warm", timeline_limit=1),
           lambda b: len(b.get("rooms") or {}) == 3)
    three = {"spaces": {"ranges": [[0, 9]], "required_state": [],
                        "timeline_limit": 1,
                        "filters": {"room_types": ["m.space"]}},
             "encrypted": {"ranges": [[0, 9]], "required_state": [],
                           "timeline_limit": 1,
                           "filters": {"is_encrypted": True}}}
    slide(c, u, window(conn_id="m1", name="everything", required_state=[],
                       timeline_limit=1, extra_lists=three),
          check=True, label="three lists each report their own count",
          derive=counts)
    slide(c, u, window(conn_id="m2", name="everything", required_state=[],
                       timeline_limit=1, extra_lists=three),
          check=True, label="the rooms of all three lists are merged once",
          derive=names)
    slide(c, u, window(conn_id="m3", name="everything",
                       required_state=[["m.room.create", ""]],
                       timeline_limit=1,
                       extra_lists={"spaces": dict(
                           three["spaces"],
                           required_state=[["m.room.name", ""]])}),
          check=True,
          label="a room in two lists gets the union of their required_state",
          derive=rooms_shape)
    slide(c, u, window(conn_id="m4", name="only", ranges=[[0, 0]],
                       required_state=[], timeline_limit=1,
                       extra_lists={"wide": {"ranges": [[0, 9]],
                                             "required_state": [],
                                             "timeline_limit": 1}}),
          check=True, label="a narrow list beside a wide one",
          derive=lambda b: dict(counts(b), **names(b)))
    slide(c, u, {"conn_id": "m5", "lists": {}}, check=True,
          label="an empty lists map returns no rooms",
          derive=lambda b: dict(counts(b), **rooms_shape(b)))
    slide(c, u, window(conn_id="m6", name="none", required_state=[],
                       timeline_limit=1,
                       filters={"room_types": ["com.example.absent"]}),
          check=True, label="a list that matches nothing has a count of zero",
          derive=lambda b: dict(counts(b), **rooms_shape(b)))


# =========================================================================== #
# The v1.12 -> v1.13 changes
# =========================================================================== #
@scenario("sl/marked-unread-round-trip", tags=["account-data", "v1.13"], step=7)
def marked_unread_round_trip(c):
    """`m.marked_unread` is set and read back as room account data."""
    u = register(c, "slv1")
    v = register(c, "slv2")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    path = (f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/"
            "m.marked_unread")
    c.req("GET", path, token=u.token, label="before it is ever set")
    c.req("PUT", path, token=u.token, json_body={"unread": True},
          label="mark the room unread")
    c.req("GET", path, token=u.token, label="read the marker back")
    c.req("PUT", path, token=u.token, json_body={"unread": False},
          label="clear the marker")
    c.req("GET", path, token=u.token, label="read the cleared marker back")
    c.req("GET", path, token=v.token, label="another user may not read it")
    c.req("PUT", path, token=v.token, json_body={"unread": True},
          label="another user may not write it")


@scenario("sl/marked-unread-in-sync", tags=["account-data", "sync", "v1.13"],
          step=7)
def marked_unread_in_sync(c):
    """The marker is pushed down `/sync` and the sliding-sync extension."""
    u = register(c, "slw1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    send(c, u, room, "s0", {"msgtype": "m.text", "body": "zero"})
    path = (f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/"
            "m.marked_unread")

    def has_marker(b):
        return any("m.marked_unread" in types for types in
                   v2_room_account_data(b)["room_account_data"].values())

    settle_sync(c, u, lambda b: room in ((b.get("rooms") or {}).get("join") or {}))
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 1},
              label="the room account data before the marker is set",
              derive=v2_room_account_data)
    since = r.json["next_batch"]
    c.req("PUT", path, token=u.token, json_body={"unread": True}, check=False)
    settle_sync(c, u, has_marker, since=since)
    r = c.req("GET", f"{CS}/sync", token=u.token,
              query={"timeout": 2, "since": since},
              label="the marker arrives in the sync delta",
              derive=lambda b: v2_room_account_data(b, types_only=False))
    since = r.json["next_batch"]
    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 3},
          label="and in a fresh initial sync", derive=v2_room_account_data)
    body = window(conn_id="mu", required_state=[], timeline_limit=1,
                  extensions={"account_data": {"enabled": True}})
    settle(c, u, body,
           lambda b: any("m.marked_unread" in types
                         for types in account_data_shape(b)["rooms"].values()))
    slide(c, u, body, check=True,
          label="the sliding-sync account_data extension carries it",
          derive=lambda b: dict(account_data_shape(b),
                                **account_data_content(b, "m.marked_unread")))
    c.req("PUT", path, token=u.token, json_body={"unread": False}, check=False)
    settle_sync(c, u, has_marker, since=since)
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 4, "since": since},
          label="clearing it is pushed down the stream too",
          derive=lambda b: v2_room_account_data(b, types_only=False))
    c.req("GET", path, token=u.token, label="and the stored event agrees")


@scenario("sl/marked-unread-with-read-markers", tags=["account-data", "v1.13"],
          step=7)
def marked_unread_with_read_markers(c):
    """The unread marker sits beside `m.fully_read` and does not disturb it."""
    u = register(c, "slx1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    eid = send(c, u, room, "s0", {"msgtype": "m.text", "body": "zero"})
    path = (f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/"
            "m.marked_unread")
    fully = (f"{CS}/user/{q(u.user_id)}/rooms/{q(room)}/account_data/"
             "m.fully_read")
    c.req("POST", f"{CS}/rooms/{q(room)}/read_markers", token=u.token,
          json_body={"m.fully_read": eid, "m.read": eid},
          label="set a fully-read marker")
    c.req("PUT", path, token=u.token, json_body={"unread": True},
          label="then mark the room unread")
    c.req("GET", fully, token=u.token, label="the fully-read marker is untouched")
    c.req("GET", path, token=u.token, label="and the unread marker is set")
    settle_sync(c, u, lambda b: {"m.fully_read", "m.marked_unread"} <= set(
        sum(v2_room_account_data(b)["room_account_data"].values(), [])))
    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 1},
          label="both appear in the room's account data",
          derive=v2_room_account_data)
    # Writing `m.fully_read` through the account-data endpoint is refused --
    # `/read_markers` is its door -- and which code a server picks for that
    # refusal the release does not fix, so only the class is compared.
    c.req("PUT", fully, token=u.token, json_body={"event_id": eid},
          drop=("errcode",),
          label="m.fully_read may not be written directly")


@scenario("sl/report-room", tags=["reporting", "v1.13"], step=7)
def report_room(c):
    """`POST /rooms/{roomId}/report`, added in v1.13."""
    u = register(c, "sly1")
    v = register(c, "sly2")
    public = create_room(c, u, room_version=V, preset="public_chat", name="Public")
    private = create_room(c, u, room_version=V, preset="private_chat",
                          name="Private")
    member = create_room(c, u, room_version=V, preset="public_chat", name="Joined")
    join(c, v, member)
    c.req("POST", f"{CS}/rooms/{q(member)}/report", token=v.token,
          json_body={"reason": "this makes me sad"},
          label="a member reports the room they are in")
    c.req("POST", f"{CS}/rooms/{q(public)}/report", token=v.token,
          json_body={"reason": "this makes me sad"},
          label="a caller who is not in the room may still report it")
    c.req("POST", f"{CS}/rooms/{q(private)}/report", token=v.token,
          json_body={"reason": "this makes me sad"},
          label="and may report an invite-only room they are not in")
    c.req("POST", f"{CS}/rooms/{q('!nosuchroom:' + c.server_name)}/report",
          token=u.token, json_body={"reason": "nothing there"},
          label="a room the server does not know")
    c.req("POST", f"{CS}/rooms/{q(member)}/report",
          json_body={"reason": "no token"},
          label="reporting without an access token")
    c.req("POST", f"{CS}/rooms/{q(member)}/report", token=u.token,
          json_body={"reason": "x" * 400},
          label="a long reason is still accepted")
    c.req("POST", f"{CS}/rooms/{q(member)}/report", token=u.token,
          json_body={"reason": "twice"},
          label="the same room may be reported again")


@scenario("sl/report-event-reason-optional", tags=["reporting", "v1.13"], step=7)
def report_event_reason_optional(c):
    """`reason` is optional on `POST /rooms/{roomId}/report/{eventId}`."""
    u = register(c, "slz1")
    v = register(c, "slz2")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Room")
    other = create_room(c, u, room_version=V, preset="private_chat", name="Other")
    eid = send(c, u, room, "e1", {"msgtype": "m.text", "body": "reportable"})
    oid = send(c, u, other, "e2", {"msgtype": "m.text", "body": "elsewhere"})
    c.req("POST", f"{CS}/rooms/{q(room)}/report/{q(eid)}", token=u.token,
          json_body={"reason": "this makes me sad", "score": -100},
          label="a report with a reason and a score")
    c.req("POST", f"{CS}/rooms/{q(room)}/report/{q(eid)}", token=u.token,
          json_body={}, label="a report with the reason omitted")
    c.req("POST", f"{CS}/rooms/{q(room)}/report/{q(eid)}", token=u.token,
          json_body={"score": 0}, label="a report with only a score")
    c.req("POST", f"{CS}/rooms/{q(room)}/report/{q(eid)}", token=v.token,
          json_body={"reason": "not mine"},
          label="a caller who is not joined to the room")
    c.req("POST", f"{CS}/rooms/{q(other)}/report/{q(oid)}", token=v.token,
          json_body={}, label="and cannot report an event they cannot see")
    c.req("POST", f"{CS}/rooms/{q(room)}/report/{q('$nosuchevent')}",
          token=u.token, json_body={}, label="an event that does not exist")
    c.req("POST", f"{CS}/rooms/{q(room)}/report/{q(eid)}", json_body={},
          label="reporting without an access token")


@scenario("sl/join-via-parameter", tags=["joining", "v1.12"], step=7)
def join_via_parameter(c):
    """`via` on `POST /join/{roomIdOrAlias}`, beside deprecated `server_name`."""
    u = register(c, "sm11")
    v = register(c, "sm12")
    here = c.server_name
    rooms = [create_room(c, u, room_version=V, preset="public_chat",
                         name=f"Join {i}", room_alias_name=f"joinvia{i}")
             for i in range(4)]
    c.req("POST", f"{CS}/join/{q(rooms[0])}", token=v.token, json_body={},
          query={"via": here}, label="join with a via parameter")
    c.req("POST", f"{CS}/join/{q(rooms[1])}", token=v.token, json_body={},
          query={"server_name": here},
          label="server_name is still accepted at this release")
    c.req("POST", f"{CS}/join/{q(rooms[2])}", token=v.token, json_body={},
          query={"via": here, "server_name": "nosuch.invalid"},
          label="via is used and server_name ignored when both are given")
    c.req("POST", f"{CS}/join/{q(rooms[3])}", token=v.token, json_body={},
          query={"via": [here, "nosuch.invalid"]},
          label="via may be given more than once")
    c.req("POST", f"{CS}/join/{q('#joinvia0:' + here)}", token=v.token,
          json_body={}, query={"via": here}, label="via on a room alias")
    c.req("POST", f"{CS}/join/{q(rooms[0])}", token=v.token, json_body={},
          query={"via": here},
          label="joining a room already joined is idempotent")
    c.req("GET", f"{CS}/joined_rooms", token=v.token,
          label="all four joins took effect",
          derive=lambda b: {"n": len(b.get("joined_rooms") or [])})


@scenario("sl/media-disposition-inline", tags=["media", "v1.12"], step=7)
def media_disposition_inline(c):
    """`Content-Disposition: inline` for a type on the allow-list."""
    u = register(c, "sm21")
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=png(8, 8),
              headers={"Content-Type": "image/png"},
              query={"filename": "pic.png"}, label="upload a PNG")
    server, media_id = mxc_parts(r.json["content_uri"])
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}",
          token=u.token, binary=True,
          label="an allow-listed type is served inline, with its file name")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}/"
          f"{q('other.png')}", token=u.token, binary=True,
          label="the file name in the path is the one carried in the header")
    # A thumbnail has no uploaded file name, so whether a server puts a
    # `filename` on it is its own choice: the disposition *type* is what the
    # release fixes, and it is read out of the response rather than compared as
    # a whole header.
    tr = c.req("GET", f"{CS1}/media/thumbnail/{q(server)}/{q(media_id)}",
               token=u.token, query={"width": 32, "height": 32,
                                     "method": "crop"},
               binary=True, record_headers=False, check=False)
    disposition = ""
    for k, v in (tr.headers or {}).items():
        if k.lower() == "content-disposition":
            disposition = v.split(";")[0].strip().lower()
    fact(c, {"status": tr.status, "disposition": disposition},
         label="a thumbnail is served inline", token=u.token)
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=b"plain words\n",
              headers={"Content-Type": "text/plain"},
              query={"filename": "notes.txt"}, label="upload a text file")
    server2, media2 = mxc_parts(r.json["content_uri"])
    c.req("GET", f"{CS1}/media/download/{q(server2)}/{q(media2)}",
          token=u.token, binary=True, label="text/plain is served inline")
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=b'{"a":1}',
              headers={"Content-Type": "application/json"},
              query={"filename": "data.json"}, label="upload a JSON file")
    server3, media3 = mxc_parts(r.json["content_uri"])
    c.req("GET", f"{CS1}/media/download/{q(server3)}/{q(media3)}",
          token=u.token, binary=True, label="application/json is served inline")


@scenario("sl/media-disposition-attachment", tags=["media", "v1.12"], step=7)
def media_disposition_attachment(c):
    """`Content-Disposition: attachment` for a type that is not allow-listed."""
    u = register(c, "sm31")
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=b"<h1>hello</h1>",
              headers={"Content-Type": "text/html"},
              query={"filename": "page.html"}, label="upload an HTML file")
    server, media_id = mxc_parts(r.json["content_uri"])
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}",
          token=u.token, binary=True,
          label="a type that is not allow-listed is served as an attachment")
    c.req("GET", f"{CS1}/media/download/{q(server)}/{q(media_id)}/"
          f"{q('given.html')}", token=u.token, binary=True,
          label="with the file name from the path")
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=b"%PDF-1.4\n",
              headers={"Content-Type": "application/pdf"},
              query={"filename": "doc.pdf"}, label="upload a PDF")
    server2, media2 = mxc_parts(r.json["content_uri"])
    c.req("GET", f"{CS1}/media/download/{q(server2)}/{q(media2)}",
          token=u.token, binary=True, label="application/pdf is an attachment")
    r = c.req("POST", f"{MEDIA}/upload", token=u.token, body=b"opaque bytes",
              headers={"Content-Type": "application/octet-stream"},
              label="upload with no file name at all")
    server3, media3 = mxc_parts(r.json["content_uri"])
    c.req("GET", f"{CS1}/media/download/{q(server3)}/{q(media3)}",
          token=u.token, binary=True,
          label="with no file name the header carries none")
    c.req("GET", f"{CS1}/media/download/{q(server3)}/{q(media3)}/"
          f"{q('named.bin')}", token=u.token, binary=True,
          label="but the path may still name it")


@scenario("sl/keys-claim-order", tags=["e2ee", "v1.13"], step=7)
def keys_claim_order(c):
    """`/keys/claim` hands out one-time keys in the order they were uploaded."""
    u = register(c, "sm41")
    # The device id is chosen here rather than left to the server: the made-up
    # key material below is derived from it, so a server-minted device id would
    # make the uploaded bytes differ between two recordings.
    v = register(c, "sm42", device_id="CLAIMDEVICE")
    dev = v.device_id
    for name, key_id in (("a", "AAAAAA"), ("b", "AAAAAB"), ("c", "AAAAAC")):
        c.req("POST", f"{CS}/keys/upload", token=v.token,
              json_body={"one_time_keys": {f"signed_curve25519:{key_id}":
                                           otk(v, dev, name)}},
              label=f"upload one-time key {name} on its own")
    want = {v.user_id: {dev: "signed_curve25519"}}
    for i in range(3):
        c.req("POST", f"{CS}/keys/claim", token=u.token,
              json_body={"one_time_keys": want},
              label=f"claim number {i + 1} takes the next key uploaded",
              derive=claimed)
    c.req("POST", f"{CS}/keys/claim", token=u.token,
          json_body={"one_time_keys": want},
          label="a fourth claim has nothing left to give", derive=claimed)
    c.req("POST", f"{CS}/keys/upload", token=v.token, json_body={},
          label="the device's remaining count is zero")

# --------------------------------------------------------------------------- #
# The pre-flight, on the routes this milestone adds
# --------------------------------------------------------------------------- #
# Every step's ticket restates the release's CORS requirement -- every endpoint
# answers an `OPTIONS` request, and answering it performs none of the endpoint's
# own logic -- and step 1 scores it on the routes that existed then.  A route
# added later is a route that can be added without it, so each milestone asks
# the same two questions of its own.  Nothing here needs a room, a user or an
# upload to exist: that is the point of the second question.


@scenario("sl/options-preflight", tags=["cors"], step=7)
def sl_options_preflight(c):
    """`OPTIONS` on the endpoints this release adds."""
    empty = lambda b: {"body_carries_nothing_of_substance": not b}   # noqa: E731
    for path, method, label in (
            (f"{CS}/rooms/{q('!nonexistentroom00000000000:' + c.server_name)}/report",
             "POST", "the room-report endpoint"),
    ):
        c.req("OPTIONS", path,
              headers={"Origin": "https://example.invalid",
                       "Access-Control-Request-Method": method},
              label=f"OPTIONS preflight on {label}", derive=empty)
