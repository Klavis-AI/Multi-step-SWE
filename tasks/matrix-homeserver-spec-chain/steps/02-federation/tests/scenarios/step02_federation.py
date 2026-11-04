"""Step 2 -- federation: two independent homeservers, observed only through the
Client-Server API of each.

Every scenario here is tagged `needs=("federation",)`, so the harness starts two
homeservers; `c.req(..., peer=True)` addresses the second one.  While recording
both are the pinned Synapse; while scoring the first is the candidate and the
second is still Synapse, so what passes between them is real Server-Server API
traffic.  Nothing in this module calls `/_matrix/federation/...` directly:
federation is the *mechanism* under test, and what is scored is what each
server's Client-Server API reports once the two have talked to each other.
That is what makes an expectation comparable -- the Server-Server API's own wire
format is full of server-chosen detail (signatures, hashes, PDU batching, which
server is asked for what), while "the join shows up in
`/rooms/{roomId}/members` on both servers" is a fact the specification fixes.

Determinism
-----------
Federation is asynchronous, so a scored request issued immediately after an
action on the other server would race the transaction that carries it.  Every
scored request in this module is therefore preceded by `settle()` -- the single
polling helper below -- which repeats an *unscored* (`check=False`) read of the
same data until the awaited effect has arrived *and* two further reads agree
with it.  The second half matters as much as the first: "the join is visible"
is not "nothing else is still in flight".  No scenario sleeps for a fixed time
and hopes.

Two further rules keep a transcript byte-identical across runs:

* Only one event at a time is put in flight.  Symbols are allocated in
  first-appearance order, so if two messages could arrive together they might be
  seen in one read in one run and in two reads in the next, which would number
  them differently.  After every send the far side is settled before the next.
* A freshly created room is read once, unscored, on its own server, so its
  creation events are numbered in the order that server made them rather than in
  the order federation happened to deliver them.

Room versions are always passed explicitly (`room_version=`): the default is
server-chosen.  Whole response bodies are compared only where the published
schema is tight enough that every field in it is specified; wherever an event
object would be returned the scenario uses `derive=` to record structural facts
-- event types in order, membership values, message bodies in order -- because a
whole event carries server-chosen detail (`unsigned` bookkeeping) and content
keys from proposals a later spec release adopted.
"""

import json
import time

from mxconf.ctx import ScenarioAbort
from mxconf.mx import CS, User, fact, ok, q
from mxconf.registry import scenario

# --------------------------------------------------------------------------- #
# The two adjustments every federating scenario needs -- emptying the reference
# homeserver's `ip_range_blacklist` so one loopback server will talk to another,
# and keeping listeners out of Linux's ephemeral port range and retrying a lost
# race -- now live in `mxconf/targets.py` and `mxconf/runner.py`, where the
# recording and the scoring paths share them.


# The room versions this module pins.  v6 is the last one before knocking, v7
# adds the `knock` join rule, v8/v9 add `restricted`, v10 adds
# `knock_restricted` and requires power levels to be integers.
V6, V7, V8, V9, V10 = "6", "7", "8", "9", "10"

# A `/messages` filter that keeps the message timeline and nothing else, so a
# derived list of bodies is not perturbed by the membership and state events
# that federation also delivers.
MSG_FILTER = json.dumps({"types": ["m.room.message"]})

# `m.room.guest_access` is left out of every derived state listing.  The v1.1
# `POST /createRoom` preset table says `public_chat` sets
# `guest_access: forbidden`, but the pinned reference creates no such event for
# that preset.  Specification and oracle disagree, so a server that follows
# either one must pass.
UNSCORED_STATE_TYPES = {"m.room.guest_access"}


# --------------------------------------------------------------------------- #
# The polling helper.  Used before every scored request in this module.
# --------------------------------------------------------------------------- #
_UNREAD = object()


def settle(c, read, ready=None, *, what, tries=350, delay=0.2):
    """Poll an unscored read until federation has converged on it.

    `read` performs a `check=False` request and returns a comparable summary of
    what it saw.  `ready` says whether the awaited effect has arrived (the
    default is "anything truthy").  The value is returned only once `ready`
    holds *and* two further reads produced exactly the same value, which is
    what makes the scored request issued straight afterwards see what the
    reference saw.
    """
    ready = ready or (lambda v: bool(v))
    prev = _UNREAD
    agreed = 0
    cur = None
    for _ in range(tries):
        cur = read()
        if ready(cur) and cur == prev:
            agreed += 1
            if agreed >= 2:
                return cur
        else:
            agreed = 0
        prev = cur
        time.sleep(delay)
    raise ScenarioAbort(f"federation did not converge: {what}; last read {cur!r}")


def settle_then_score(c, read, ready=None, *, what, **kw):
    """`settle` for a probe of exactly what the next scored read asks.

    If the awaited value never arrives, that scored read is issued anyway and
    fails on its own, instead of the abort also taking every later check in
    the scenario with it.
    """
    try:
        return settle(c, read, ready, what=what, **kw)
    except ScenarioAbort:
        return None


# --------------------------------------------------------------------------- #
# Small equivalents of the mx.py helpers that can address either server:
# mx.py always drives the first one, these take `peer=`.
# --------------------------------------------------------------------------- #
def reg(c, localpart, *, peer=False, display_name=None):
    """Register through the `m.login.dummy` flow on either server.

    The display name is always set explicitly: the specification does not fix
    the display name of a fresh account, and it ends up inside every
    `m.room.member` event this module compares.
    """
    password = f"pw-{localpart}-123"
    body = {"username": localpart, "password": password}
    r = c.req("POST", f"{CS}/register", json_body=body, check=False, peer=peer)
    if r.status == 401 and r.json and "session" in r.json:
        body["auth"] = {"type": "m.login.dummy", "session": r.json["session"]}
        r = c.req("POST", f"{CS}/register", json_body=body, check=False, peer=peer)
    ok(c, r, f"register {localpart}")
    j = r.json or {}
    u = User(localpart, j.get("user_id", ""), j.get("access_token", ""),
             j.get("device_id", ""), password)
    c.req("PUT", f"{CS}/profile/{q(u.user_id)}/displayname", token=u.token,
          json_body={"displayname": display_name or f"{localpart} D"},
          check=False, peer=peer)
    return u


def mk_room(c, u, *, peer=False, **kwargs):
    """Create a room, then read it once so its creation events are numbered in
    the order its own server made them."""
    r = c.req("POST", f"{CS}/createRoom", token=u.token, json_body=kwargs,
              check=False, peer=peer)
    ok(c, r, "createRoom")
    room = (r.json or {})["room_id"]
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=u.token, check=False,
          peer=peer)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=u.token, check=False,
          peer=peer)
    return room


def do_send(c, u, room, txn, body, *, etype="m.room.message", peer=False,
            check=False, label=None):
    r = c.req("PUT", f"{CS}/rooms/{q(room)}/send/{etype}/{txn}", token=u.token,
              json_body=body, check=check, label=label, peer=peer)
    ok(c, r, "send")
    return (r.json or {})["event_id"]


def do_state(c, u, room, etype, content, skey="", *, peer=False, check=False,
             label=None):
    r = c.req("PUT", f"{CS}/rooms/{q(room)}/state/{etype}/{q(skey)}",
              token=u.token, json_body=content, check=check, label=label,
              peer=peer)
    ok(c, r, f"set {etype}")
    return (r.json or {})["event_id"]


def do_join(c, u, room, *, peer=False, via=None, check=False, label=None):
    """Join, naming the server to route through when the room is remote: a bare
    room id carries no routing information of its own."""
    query = {"server_name": via} if via else None
    r = c.req("POST", f"{CS}/join/{q(room)}", token=u.token, json_body={},
              query=query, check=check, label=label, peer=peer)
    if not check:
        ok(c, r, "join")
    return r


def sync_token(c, u, *, peer=False):
    """A `since` token with nothing still arriving behind it.

    A full sync's `next_batch` can lag behind what the other server has already
    delivered -- observed with the pinned reference for both rooms (a token
    that did not move although a remote knock was already in the room list) and
    presence (a delta that kept re-serving one user's presence and never showed
    the next user's) -- so a delta taken from it would replay, or shadow, what
    the scenario believes is already in the past.  The token is therefore
    rolled forward until an incremental sync from it reports neither rooms nor
    presence, three times running; only then is the *next* delta exactly the
    effect of the action that follows.
    """
    box = {"tok": None}

    def probe():
        if box["tok"] is None:
            r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0},
                      check=False, peer=peer)
            box["tok"] = (r.json or {}).get("next_batch")
            if not box["tok"]:
                return None
        delta = sync_body(c, u, box["tok"], peer=peer)
        rooms = delta.get("rooms") or {}
        busy = (any(rooms.get(sec) for sec in ("join", "invite", "leave",
                                               "knock"))
                or (delta.get("presence") or {}).get("events"))
        if busy:
            box["tok"] = delta.get("next_batch") or box["tok"]
            return None
        return "quiet"

    settle(c, probe, lambda v: v == "quiet", what="a quiescent /sync token")
    return box["tok"]


def sync_body(c, u, since, *, peer=False):
    r = c.req("GET", f"{CS}/sync", token=u.token,
              query={"timeout": 0, "since": since}, check=False, peer=peer)
    return r.json or {}


def scored_sync(c, u, since, *, peer=False, label, derive):
    return c.req("GET", f"{CS}/sync", token=u.token,
                 query={"timeout": 0, "since": since}, peer=peer, label=label,
                 derive=derive)


# --------------------------------------------------------------------------- #
# Unscored reads used as `settle` probes.
# --------------------------------------------------------------------------- #
def read_members(c, u, room, *, peer=False):
    """{user id: membership} from `/rooms/{roomId}/members`."""
    r = c.req("GET", f"{CS}/rooms/{q(room)}/members", token=u.token,
              check=False, peer=peer)
    if r.status != 200:
        return {}
    return {e.get("state_key"): (e.get("content") or {}).get("membership")
            for e in (r.json or {}).get("chunk", [])}


def read_state_content(c, u, room, etype, skey="", *, peer=False):
    r = c.req("GET", f"{CS}/rooms/{q(room)}/state/{etype}/{q(skey)}",
              token=u.token, check=False, peer=peer)
    return r.json if r.status == 200 else None


def read_bodies(c, u, room, *, peer=False, limit=50):
    """Message bodies, newest first, or [] while the room is unreadable."""
    r = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
              query={"dir": "b", "limit": limit, "filter": MSG_FILTER},
              check=False, peer=peer)
    if r.status != 200:
        return []
    return [(e.get("content") or {}).get("body")
            for e in (r.json or {}).get("chunk", [])]


def read_types(c, u, room, *, peer=False, limit=50):
    """Every event type in the room's backward timeline, newest first."""
    r = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
              query={"dir": "b", "limit": limit}, check=False, peer=peer)
    if r.status != 200:
        return []
    return [e.get("type") for e in (r.json or {}).get("chunk", [])]


def read_status(c, path, token, *, peer=False):
    return c.req("GET", path, token=token, check=False, peer=peer).status


def read_profile(c, user_id, token, *, peer=False):
    """A profile reduced to the two fields the specification defines, so the
    probe settles (a raw body would be stable here, but the projection keeps
    every probe in this module the same shape)."""
    b = c.req("GET", f"{CS}/profile/{q(user_id)}", token=token, check=False,
              peer=peer).json or {}
    return {k: b.get(k) for k in ("displayname", "avatar_url") if k in b}


def read_presence(c, user_id, token, *, peer=False):
    """Presence without `last_active_ago`, which moves on every read."""
    b = c.req("GET", f"{CS}/presence/{q(user_id)}/status", token=token,
              check=False, peer=peer).json or {}
    return _presence_body(b)


# --------------------------------------------------------------------------- #
# `derive=` helpers: structural facts rather than whole bodies.
# --------------------------------------------------------------------------- #
def _member_rows(body):
    """Every member event as "user|membership|displayname", sorted.

    The whole member event is not compared: it carries `unsigned` bookkeeping
    and an event id, none of which the specification fixes here.
    """
    return sorted(
        "{}|{}|{}".format(e.get("state_key"),
                          (e.get("content") or {}).get("membership"),
                          (e.get("content") or {}).get("displayname"))
        for e in (body.get("chunk") or []))


def _state_pairs(body):
    """(type, state_key) of every current-state event, sorted."""
    return sorted("{}|{}".format(e.get("type"), e.get("state_key"))
                  for e in body
                  if e.get("type") not in UNSCORED_STATE_TYPES)


def _member_content(body):
    """The specified keys of an `m.room.member` content, and nothing else.

    A key that is present but null is dropped: the specification marks all of
    these except `membership` optional, and an optional field explicitly set to
    null is equivalent to omitting it.
    """
    return {k: body.get(k) for k in
            ("membership", "displayname", "avatar_url", "reason",
             "join_authorised_via_users_server")
            if body.get(k) is not None}


def _presence_body(b):
    return {k: b.get(k) for k in ("presence", "status_msg") if k in b}


def _sections(b):
    rooms = b.get("rooms") or {}
    return {sec: sorted(rooms.get(sec) or {})
            for sec in ("join", "invite", "leave", "knock")}


def _timeline(b, section="join"):
    """Per room in one `/sync` section: timeline event types, senders and
    message bodies, in the order the server served them."""
    rooms = (b.get("rooms") or {}).get(section) or {}
    out = {}
    for rid, room in sorted(rooms.items()):
        evs = [e for e in ((room.get("timeline") or {}).get("events") or [])
               if e.get("type") not in UNSCORED_STATE_TYPES]
        out[rid] = {
            "types": [e.get("type") for e in evs],
            "senders": [e.get("sender") for e in evs],
            "bodies": [(e.get("content") or {}).get("body") for e in evs
                       if e.get("type") == "m.room.message"],
        }
    return out


def _membership_delta(b):
    """Membership transitions a `/sync` delta carries, per section, in order."""
    rooms = b.get("rooms") or {}
    out = {}
    for sec in ("join", "invite", "leave", "knock"):
        for rid, room in sorted((rooms.get(sec) or {}).items()):
            rows = []
            for e in ((room.get("timeline") or {}).get("events") or []):
                if e.get("type") == "m.room.member":
                    rows.append("{}|{}".format(
                        e.get("state_key"),
                        (e.get("content") or {}).get("membership")))
            out["{}:{}".format(sec, rid)] = rows
    return out


# Stripped state is defined as a room's state cut down to these four keys...
STRIPPED_KEYS = ("content", "sender", "state_key", "type")
# ...and the specification says the state for these types SHOULD be included.
# Which events a server strips beyond that is its own choice -- the release
# tells clients they "must not rely on any particular state being present
# here" -- so neither the exact set nor which of these four it chose is
# pinned; see `_stripped`, which only asks that it drew on this list.  The
# pinned reference also puts `event_id`, `origin_server_ts` and `unsigned` on
# each stripped event, which spec release v1.2 does not define for stripped
# state; those are not pinned either.  See this step's report.
# The release's own list of state a stripped-state set "should" include:
# create, name, avatar, topic, join_rules, canonical_alias, encryption.  Which
# of them a server picks is its own choice -- clients "must not assume any
# particular event is present" -- so what is asked is that the stripped state
# is drawn from this list at all.
STRIPPED_RECOMMENDED = ("m.room.create", "m.room.name", "m.room.avatar",
                        "m.room.topic", "m.room.join_rules",
                        "m.room.canonical_alias", "m.room.encryption")


def _stripped(b, section="invite", key="invite_state"):
    """What a `/sync` invite or knock carries instead of the room."""
    rooms = (b.get("rooms") or {}).get(section) or {}
    out = {}
    for rid, room in sorted(rooms.items()):
        evs = [e for e in ((room.get(key) or {}).get("events") or [])
               if e.get("type") not in UNSCORED_STATE_TYPES]
        types = {e.get("type") for e in evs}
        out[rid] = {
            # Which sections a `/sync` room object carries is not fixed by the
            # release, so it is not pinned.  Nor is *which* of the recommended
            # types a server chose: they are recommended, not required, so a
            # server that sends three of the four conforms.  What is pinned is
            # that the stripped state is drawn from that set at all.
            "has_recommended_state": bool(types & set(STRIPPED_RECOMMENDED)),
            "specified_keys_on_every_event": sorted(
                k for k in STRIPPED_KEYS if evs and all(k in e for e in evs)),
            "join_rule": next(((e.get("content") or {}).get("join_rule")
                               for e in evs
                               if e.get("type") == "m.room.join_rules"), None),
            "membership": sorted(
                "{}|{}".format(e.get("state_key"),
                               (e.get("content") or {}).get("membership"))
                for e in evs if e.get("type") == "m.room.member"),
        }
    return out


def _directory(b, origin):
    """A directory lookup: the room id, and that `servers` points at the room.

    The release calls `servers` "an array of server names that are likely to
    hold the given room", so which servers a resolver lists, and in what order,
    is its own judgement.  What it has to get right is the room id, and that
    the server the alias belongs to is among the candidates.
    """
    return {"room_id": b.get("room_id"),
            "servers_contains_origin": origin in (b.get("servers") or [])}


def _typing(b):
    """`m.typing` per room: the `user_ids` list the specification defines."""
    rooms = (b.get("rooms") or {}).get("join") or {}
    return {rid: [sorted((e.get("content") or {}).get("user_ids") or [])
                  for e in ((room.get("ephemeral") or {}).get("events") or [])
                  if e.get("type") == "m.typing"]
            for rid, room in sorted(rooms.items())}


def _receipts(b):
    """The events an `m.receipt` marks, per room.

    Only the event ids are taken.  The `ts` is a wall-clock value, and which
    receipt types and which users a server groups under one `m.receipt` is its
    own choice at this release - what the specification fixes is which event
    the receipt is *for*, so that is what is compared.
    """
    rooms = (b.get("rooms") or {}).get("join") or {}
    out = {}
    for rid, room in sorted(rooms.items()):
        ids = set()
        for e in ((room.get("ephemeral") or {}).get("events") or []):
            if e.get("type") != "m.receipt":
                continue
            ids.update((e.get("content") or {}).keys())
        out[rid] = sorted(ids)
    return out


def _presence_of(b, user_id):
    """The `m.presence` a `/sync` carries for one user."""
    for e in ((b.get("presence") or {}).get("events") or []):
        if e.get("type") == "m.presence" and e.get("sender") == user_id:
            content = e.get("content") or {}
            return {"type": e.get("type"), "sender": e.get("sender"),
                    "presence": content.get("presence"),
                    "status_msg": content.get("status_msg")}
    return {}


def _bodies(b):
    return {"bodies": [(e.get("content") or {}).get("body")
                       for e in (b.get("chunk") or [])],
            "types": [e.get("type") for e in (b.get("chunk") or [])]}


def _event_types(b):
    return {"types": [e.get("type") for e in (b.get("chunk") or [])
                      if e.get("type") not in UNSCORED_STATE_TYPES]}


def _create_facts(b):
    return {"room_version": b.get("room_version"),
            "has_creator": "creator" in b}


def _topic(b):
    # Only `topic` is compared: `m.room.topic` content may also carry `m.topic`
    # (MSC3765 rich topics), which the spec adds in release v1.15, and this
    # step must not score it either way.
    return {"topic": b.get("topic")}


def _join_rules(b):
    return {"join_rule": b.get("join_rule"), "allow": b.get("allow")}


def _power_facts(b):
    """Power levels without the `users` map, which is keyed by user id."""
    return {k: b.get(k) for k in
            ("ban", "events_default", "invite", "kick", "redact",
             "state_default", "users_default") if k in b}


def _authorised_by(body, user_id):
    """A restricted join names a user on the resident server that authorised
    it; which user is the server's choice, so only "a local user did" is
    pinned."""
    return {"membership": body.get("membership"),
            "authorised_by_a_resident_user":
                body.get("join_authorised_via_users_server") == user_id}


def _count(b):
    return {"n": len(b) if isinstance(b, list) else None}


# --------------------------------------------------------------------------- #
# Composite waits, all built on `settle`.
#
# Everything `settle` compares has to be *stable* once federation is done, so
# these wait on derived projections rather than on whole responses: a raw
# `/sync` body carries `last_active_ago`, which changes on every read and would
# never settle.
# --------------------------------------------------------------------------- #
def wait_members(c, u, room, want, *, peer=False):
    """Wait until `/rooms/{roomId}/members` shows at least `want`, and stops
    changing.  Only usable for a user who is joined on the server being asked;
    for an invited, knocking or departed user the waits below read `/sync`."""
    where = "hs2" if peer else "hs1"
    return settle(c, lambda: read_members(c, u, room, peer=peer),
                  lambda v: all(v.get(k) == m for k, m in want.items()),
                  what=f"{where} membership {want}")


def wait_state(c, u, room, etype, check, *, skey="", peer=False):
    where = "hs2" if peer else "hs1"
    return settle(c, lambda: read_state_content(c, u, room, etype, skey,
                                                peer=peer),
                  lambda v: v is not None and check(v),
                  what=f"{where} state {etype}/{skey}")


def wait_bodies(c, u, room, want, *, peer=False, limit=50):
    where = "hs2" if peer else "hs1"
    want = list(want)
    return settle(c, lambda: read_bodies(c, u, room, peer=peer, limit=limit),
                  lambda v: all(b in v for b in want),
                  what=f"{where} messages {want}")


def wait_section(c, u, since, room, section, *, peer=False):
    """Wait until a `/sync` delta files `room` under one of its sections.

    This is how an invited, knocking or departed user's server is observed:
    such a user cannot read `/rooms/{roomId}/members` at all.
    """
    where = "hs2" if peer else "hs1"
    return settle(c, lambda: _sections(sync_body(c, u, since, peer=peer)),
                  lambda v: room in v[section],
                  what=f"{where} sync files the room under {section}")


def wait_member_delta(c, u, since, uid, membership, *, peer=False):
    """Wait until a `/sync` delta carries one membership transition."""
    where = "hs2" if peer else "hs1"
    return settle(
        c, lambda: _membership_delta(sync_body(c, u, since, peer=peer)),
        lambda v: any(f"{uid}|{membership}" in rows for rows in v.values()),
        what=f"{where} sync carries {uid} -> {membership}")


def settled_sync(c, u, since, *, peer=False, label, derive, ready):
    """Settle on exactly the projection that is about to be scored.

    Waiting on the derived form and then scoring the same derivation is what
    makes a `/sync` check reproducible: the poll stops only once the awaited
    effect is there and two further reads produce the identical projection.
    """
    settle(c, lambda: derive(sync_body(c, u, since, peer=peer)), ready,
           what=label)
    return scored_sync(c, u, since, peer=peer, label=label, derive=derive)


def _has_member(delta, uid, membership):
    return any(f"{uid}|{membership}" in rows for rows in delta.values())


def _has_body(rooms, body):
    return any(body in room["bodies"] for room in rooms.values())


def send_and_settle(c, sender, room, txn, body, watchers, *, peer=False,
                    seen=None):
    """Send one message and confirm it on every watching server before doing
    anything else, so only one event is ever in flight."""
    do_send(c, sender, room, txn, {"msgtype": "m.text", "body": body},
            peer=peer)
    seen = list(seen or []) + [body]
    for watcher, wpeer in watchers:
        wait_bodies(c, watcher, room, seen, peer=wpeer)
    return seen


# --------------------------------------------------------------------------- #
# 1. A remote user joins a public room
# --------------------------------------------------------------------------- #
@scenario("fed/join-public-by-id", tags=["federation", "membership", "join"],
          step=2, needs=("federation",))
def join_public_by_id(c):
    """A user on hs2 joins a public room on hs1 by room id; the join has to be
    visible through membership, room state and `/sync` on *both* servers."""
    a = reg(c, "fja")
    b = reg(c, "fjb", peer=True)
    room = mk_room(c, a, room_version=V6, preset="public_chat", name="Public")
    since1 = sync_token(c, a)
    since2 = sync_token(c, b, peer=True)

    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a user on hs2 joins a public room on hs1 by room id")
    wait_members(c, a, room, {a.user_id: "join", b.user_id: "join"})
    wait_members(c, b, room, {a.user_id: "join", b.user_id: "join"}, peer=True)

    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="hs1 lists the remote member", derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=b.token, peer=True,
          label="hs2 lists the same two members", derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=a.token,
          label="hs1 room state after the remote join",
          derive=lambda bd: {"state": _state_pairs(bd)})
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=b.token, peer=True,
          label="hs2 resolved the same room state",
          derive=lambda bd: {"state": _state_pairs(bd)})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="hs1 member event for the remote user",
          derive=_member_content)

    settled_sync(c, a, since1, label="hs1 /sync carries the remote join",
                 derive=_membership_delta,
                 ready=lambda d: _has_member(d, b.user_id, "join"))
    settled_sync(c, b, since2, peer=True,
                 label="hs2 /sync puts the remote room in join",
                 derive=lambda d: {"sections": _sections(d),
                                   "members": _membership_delta(d)},
                 ready=lambda d: _has_member(d["members"], b.user_id, "join"))
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True,
          label="hs2 joined_rooms contains the remote room")


@scenario("fed/join-public-by-alias", tags=["federation", "join", "directory"],
          step=2, needs=("federation",))
def join_public_by_alias(c):
    """The same join addressed as `#alias:<HS1>`: hs2 has to resolve the alias
    on hs1 before it can join at all."""
    a = reg(c, "faa")
    b = reg(c, "fab", peer=True)
    room = mk_room(c, a, room_version=V6, preset="public_chat",
                   room_alias_name="aliased", name="By alias")
    alias = f"#aliased:{c.server_name}"
    since2 = sync_token(c, b, peer=True)

    settle(c, lambda: read_status(c, f"{CS}/directory/room/{q(alias)}",
                                  b.token, peer=True),
           lambda v: v == 200, what="hs2 can resolve the hs1 alias")
    c.req("GET", f"{CS}/directory/room/{q(alias)}", token=b.token, peer=True,
          label="hs2 resolves an alias that lives on hs1",
          derive=lambda bd: _directory(bd, c.server_name))
    c.req("POST", f"{CS}/join/{q(alias)}", token=b.token, json_body={},
          peer=True, label="hs2 joins by alias")
    wait_members(c, a, room, {b.user_id: "join"})
    wait_members(c, b, room, {b.user_id: "join"}, peer=True)

    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="hs1 sees the alias join", derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/",
          token=b.token, peer=True,
          label="hs2 replicated the canonical alias",
          derive=lambda bd: {"alias": bd.get("alias")})
    settled_sync(c, b, since2, peer=True,
                 label="hs2 /sync after the alias join",
                 derive=lambda d: {"sections": _sections(d),
                                   "members": _membership_delta(d)},
                 ready=lambda d: _has_member(d["members"], b.user_id, "join"))
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True,
          label="hs2 joined the aliased room")
    c.req("POST", f"{CS}/join/{q('#nothing-here:' + c.server_name)}",
          token=b.token, json_body={}, peer=True,
          label="hs2 joins an alias that does not exist on hs1")


# --------------------------------------------------------------------------- #
# 2. Messages crossing, in both directions
# --------------------------------------------------------------------------- #
def _shared_room(c, a, b, *, version=V6, name="Shared"):
    """A public room created on hs1 that the hs2 user has joined."""
    room = mk_room(c, a, room_version=version, preset="public_chat", name=name)
    do_join(c, b, room, peer=True, via=c.server_name)
    wait_members(c, a, room, {a.user_id: "join", b.user_id: "join"})
    wait_members(c, b, room, {a.user_id: "join", b.user_id: "join"}, peer=True)
    return room


@scenario("fed/message-hs1-to-hs2", tags=["federation", "messages"], step=2,
          needs=("federation",))
def message_hs1_to_hs2(c):
    """A message sent on hs1 reaches the remote member through hs2's `/sync`
    and `/rooms/{roomId}/messages`."""
    a = reg(c, "m1a")
    b = reg(c, "m1b", peer=True)
    room = _shared_room(c, a, b)
    since2 = sync_token(c, b, peer=True)

    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/tx-1",
          token=a.token, json_body={"msgtype": "m.text", "body": "from hs1"},
          label="hs1 sends into the shared room")
    wait_bodies(c, b, room, ["from hs1"], peer=True)
    settled_sync(c, b, since2, peer=True,
                 label="hs2 /sync carries the remote message",
                 derive=_timeline,
                 ready=lambda d: _has_body(d, "from hs1"))
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="hs2 /messages carries the remote message", derive=_bodies)

    seen = send_and_settle(c, a, room, "tx-2", "second", [(b, True)],
                           seen=["from hs1"])
    send_and_settle(c, a, room, "tx-3", "third", [(b, True)], seen=seen)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="hs2 has all three, newest first", derive=_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="hs1 has the same three", derive=_bodies)


@scenario("fed/message-hs2-to-hs1", tags=["federation", "messages"], step=2,
          needs=("federation",))
def message_hs2_to_hs1(c):
    """The reverse direction: the remote member sends, and hs1 -- the room's
    resident server -- has to accept the event and serve it on."""
    a = reg(c, "m2a")
    b = reg(c, "m2b", peer=True)
    room = _shared_room(c, a, b)
    since1 = sync_token(c, a)

    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/tx-1",
          token=b.token, json_body={"msgtype": "m.text", "body": "from hs2"},
          peer=True, label="hs2 sends into a room that lives on hs1")
    wait_bodies(c, a, room, ["from hs2"])
    settled_sync(c, a, since1, label="hs1 /sync carries the remote message",
                 derive=_timeline, ready=lambda d: _has_body(d, "from hs2"))
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="hs1 /messages carries the remote message", derive=_bodies)

    send_and_settle(c, b, room, "tx-2", "and again", [(a, False)], peer=True,
                    seen=["from hs2"])
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="hs1 has both remote messages", derive=_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="hs2 sees its own two messages", derive=_bodies)


@scenario("fed/two-servers-interleaved", tags=["federation", "messages"],
          step=2, needs=("federation",))
def two_servers_interleaved(c):
    """Two servers in one room.  Each send is confirmed on the far side before
    the next is issued, so the causal order is forced and both servers must
    report the same six messages in the same order."""
    a = reg(c, "twa")
    b = reg(c, "twb", peer=True)
    room = _shared_room(c, a, b, name="Both")
    seen = []
    for i in range(3):
        seen = send_and_settle(c, a, room, f"a{i}", f"a{i}",
                               [(b, True), (a, False)], seen=seen)
        seen = send_and_settle(c, b, room, f"b{i}", f"b{i}",
                               [(a, False), (b, True)], peer=True, seen=seen)

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 20, "filter": MSG_FILTER},
          label="hs1 sees all six in order", derive=_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 20, "filter": MSG_FILTER},
          label="hs2 sees all six in the same order", derive=_bodies)

    # The release says `end` is omitted "if no further events are available",
    # and a server may discover that inside the request that drains the room or
    # only on the next one -- both readings conform, so the read above cannot
    # be chained from.  The forward walk continues from a page that stopped
    # short of the start instead, whose `end` both readings produce.
    r = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
              query={"dir": "b", "limit": 5, "filter": MSG_FILTER},
              label="hs1 reads a page that stops short of the start",
              derive=_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "f", "limit": 20, "filter": MSG_FILTER,
                 "from": r.json["end"]},
          label="hs1 forwards again from that page's end token", derive=_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=b.token, peer=True,
          label="both users are joined on hs2", derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/joined_members", token=a.token,
          label="hs1 joined_members lists both")


# --------------------------------------------------------------------------- #
# 3. State crossing
# --------------------------------------------------------------------------- #
@scenario("fed/state-topic-crosses", tags=["federation", "state"], step=2,
          needs=("federation",))
def state_topic_crosses(c):
    """A topic set on hs1 has to be served by hs2's
    `/rooms/{roomId}/state/m.room.topic/`, and a later change has to replace it
    rather than accumulate."""
    a = reg(c, "sta")
    b = reg(c, "stb", peer=True)
    room = _shared_room(c, a, b, name="Topical")
    since2 = sync_token(c, b, peer=True)

    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=a.token,
          json_body={"topic": "first topic"}, label="hs1 sets the topic")
    wait_state(c, b, room, "m.room.topic",
               lambda v: v.get("topic") == "first topic", peer=True)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=b.token,
          peer=True, label="hs2 serves the remote topic", derive=_topic)
    settled_sync(c, b, since2, peer=True,
                 label="hs2 /sync carries the topic event", derive=_timeline,
                 ready=lambda d: any("m.room.topic" in r["types"]
                                     for r in d.values()))

    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=a.token,
          json_body={"topic": "second topic"}, label="hs1 replaces the topic")
    wait_state(c, b, room, "m.room.topic",
               lambda v: v.get("topic") == "second topic", peer=True)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=b.token,
          peer=True, label="hs2 serves the replacement", derive=_topic)
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=b.token, peer=True,
          label="the topic is current state on hs2, once",
          derive=lambda bd: {"state": _state_pairs(bd)})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=a.token,
          label="hs1 agrees on the topic", derive=_topic)


@scenario("fed/state-name-and-avatar-cross", tags=["federation", "state"],
          step=2, needs=("federation",))
def state_name_avatar_cross(c):
    """Several state types cross, and a state type hs2 has never heard of is
    still replicated verbatim."""
    a = reg(c, "sna")
    b = reg(c, "snb", peer=True)
    room = _shared_room(c, a, b, name="Before")
    avatar = f"mxc://{c.server_name}/roomavatar1"

    do_state(c, a, room, "m.room.name", {"name": "After"})
    wait_state(c, b, room, "m.room.name", lambda v: v.get("name") == "After",
               peer=True)
    do_state(c, a, room, "m.room.avatar", {"url": avatar})
    wait_state(c, b, room, "m.room.avatar", lambda v: "url" in v, peer=True)
    do_state(c, a, room, "com.example.federated", {"n": 7, "s": "seven"},
             skey="k")
    wait_state(c, b, room, "com.example.federated", lambda v: "n" in v,
               skey="k", peer=True)

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=b.token,
          peer=True, label="hs2 serves the remote room name")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.avatar/", token=b.token,
          peer=True, label="hs2 serves the remote room avatar")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/com.example.federated/k",
          token=b.token, peer=True,
          label="an unknown state type crosses verbatim")
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=b.token, peer=True,
          label="hs2 current state carries all of them",
          derive=lambda bd: {"state": _state_pairs(bd)})
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=a.token,
          label="hs1 agrees on the state set",
          derive=lambda bd: {"state": _state_pairs(bd)})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=a.token,
          label="hs1 serves the same name")


@scenario("fed/displayname-change-crosses",
          tags=["federation", "state", "membership"], step=2,
          needs=("federation",))
def displayname_change_crosses(c):
    """A profile change on hs2 rewrites that user's `m.room.member` event in a
    room that lives on hs1."""
    a = reg(c, "dna")
    b = reg(c, "dnb", peer=True)
    room = _shared_room(c, a, b, name="Profiles")
    since1 = sync_token(c, a)

    c.req("PUT", f"{CS}/profile/{q(b.user_id)}/displayname", token=b.token,
          json_body={"displayname": "renamed remotely"}, peer=True,
          label="the remote user renames themselves")
    wait_state(c, a, room, "m.room.member",
               lambda v: v.get("displayname") == "renamed remotely",
               skey=b.user_id)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="hs1 member event carries the new name",
          derive=_member_content)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="hs1 members listing carries the new name",
          derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/joined_members", token=a.token,
          label="hs1 joined_members carries the new name")
    settled_sync(c, a, since1, label="hs1 /sync carries the member rewrite",
                 derive=_membership_delta,
                 ready=lambda d: _has_member(d, b.user_id, "join"))
    settle(c, lambda: read_profile(c, b.user_id, a.token),
           lambda v: v.get("displayname") == "renamed remotely",
           what="hs1 profile query reflects the rename")
    c.req("GET", f"{CS}/profile/{q(b.user_id)}", token=a.token,
          label="hs1 profile query for the renamed remote user")


@scenario("fed/power-levels-enforced-on-remote",
          tags=["federation", "power-levels", "auth"], step=2,
          needs=("federation",))
def power_levels_enforced_on_remote(c):
    """A power-level change made on hs1 has to be enforced by hs2 against its
    own user: the same send succeeds before it and is refused after it."""
    a = reg(c, "pwa")
    b = reg(c, "pwb", peer=True)
    room = _shared_room(c, a, b, name="Power")

    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/before",
          token=b.token, json_body={"msgtype": "m.text", "body": "allowed"},
          peer=True, label="the remote user may send at events_default 0")
    wait_bodies(c, a, room, ["allowed"])

    levels = {"users": {a.user_id: 100}, "users_default": 0,
              "events_default": 50, "state_default": 50, "ban": 50,
              "kick": 50, "redact": 50, "invite": 0}
    do_state(c, a, room, "m.room.power_levels", levels)
    wait_state(c, b, room, "m.room.power_levels",
               lambda v: v.get("events_default") == 50, peer=True)

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=b.token, peer=True, label="hs2 has the new power levels",
          derive=_power_facts)
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/after",
          token=b.token, json_body={"msgtype": "m.text", "body": "refused"},
          peer=True, label="hs2 refuses the same send above events_default")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=b.token,
          json_body={"name": "not mine"}, peer=True,
          label="hs2 refuses a state event above state_default")

    do_state(c, a, room, "m.room.power_levels",
             dict(levels, users={a.user_id: 100, b.user_id: 50}))
    wait_state(c, b, room, "m.room.power_levels",
               lambda v: (v.get("users") or {}).get(b.user_id) == 50, peer=True)
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/promoted",
          token=b.token, json_body={"msgtype": "m.text", "body": "now allowed"},
          peer=True, label="the promoted remote user may send again")
    wait_bodies(c, a, room, ["allowed", "now allowed"])
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="hs1 has exactly the two allowed messages", derive=_bodies)


# --------------------------------------------------------------------------- #
# 4. Backfill
# --------------------------------------------------------------------------- #
def _room_with_history(c, a, n=15, version=V6):
    """`n` messages accumulated on hs1 *before* the hs2 user joins, so hs2 has
    to fetch them rather than receive them live."""
    room = mk_room(c, a, room_version=version, preset="public_chat",
                   name="Backlog")
    for i in range(n):
        do_send(c, a, room, f"m{i:02d}",
                {"msgtype": "m.text", "body": f"msg{i:02d}"})
    return room


@scenario("fed/backfill-before-join",
          tags=["federation", "backfill", "messages"], step=2,
          needs=("federation",))
def backfill_before_join(c):
    """hs2 joins a room that already has 15 messages in it and pages backwards,
    so every one of them has to come from hs1's backfill rather than from a
    live transaction."""
    a = reg(c, "bfa")
    b = reg(c, "bfb", peer=True)
    room = _room_with_history(c, a)
    want = [f"msg{i:02d}" for i in range(15)]

    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="hs2 joins a room that already has 15 messages")
    wait_members(c, b, room, {a.user_id: "join", b.user_id: "join"}, peer=True)
    wait_bodies(c, b, room, want, peer=True)

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 30, "filter": MSG_FILTER},
          label="hs2 pages back over history it never received live",
          derive=_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 30, "filter": MSG_FILTER},
          label="hs1 serves the same history", derive=_bodies)
    settle(c, lambda: read_types(c, b, room, peer=True, limit=40),
           lambda v: v.count("m.room.message") == 15,
           what="hs2 backfilled the room's state events too")
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 40},
          label="the backfilled timeline includes the room's state events",
          derive=_event_types)
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=b.token, peer=True,
          label="hs2 resolved the room's state",
          derive=lambda bd: {"state": _state_pairs(bd)})
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=b.token, peer=True,
          label="hs2 has both members", derive=_member_rows)


@scenario("fed/backfill-paginated",
          tags=["federation", "backfill", "messages"], step=2,
          needs=("federation",))
def backfill_paginated(c):
    """The same history walked in pages of five with `from=`: the page
    boundaries and the order within each page are the behaviour under test."""
    a = reg(c, "bpa")
    b = reg(c, "bpb", peer=True)
    room = _room_with_history(c, a)
    want = [f"msg{i:02d}" for i in range(15)]

    do_join(c, b, room, peer=True, via=c.server_name)
    wait_members(c, b, room, {b.user_id: "join"}, peer=True)
    wait_bodies(c, b, room, want, peer=True)

    frm = None
    for page in range(3):
        query = {"dir": "b", "limit": 5, "filter": MSG_FILTER}
        if frm:
            query["from"] = frm
        r = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token,
                  peer=True, query=query,
                  label=f"hs2 backward page {page + 1} of backfilled history",
                  derive=_bodies)
        frm = (r.json or {}).get("end")
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 5, "filter": MSG_FILTER, "from": frm},
          label="hs2 past the start of the room", derive=_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "f", "limit": 20, "filter": MSG_FILTER, "from": frm},
          label="hs2 forwards again from the oldest token", derive=_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 5, "filter": MSG_FILTER},
          label="hs1's own first backward page", derive=_bodies)


# --------------------------------------------------------------------------- #
# 5. Invites over federation
# --------------------------------------------------------------------------- #
@scenario("fed/invite-accept-then-leave",
          tags=["federation", "membership", "invite"], step=2,
          needs=("federation",))
def invite_accept_then_leave(c):
    """An hs1 user invites an hs2 user to an invite-only room; the invitee
    accepts and then leaves, and every step is visible on both servers."""
    a = reg(c, "iaa")
    b = reg(c, "iab", peer=True)
    room = mk_room(c, a, room_version=V6, preset="private_chat", name="Invited")
    since2 = sync_token(c, b, peer=True)

    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=a.token,
          json_body={"user_id": b.user_id},
          label="hs1 invites a user who lives on hs2")
    wait_members(c, a, room, {b.user_id: "invite"})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="hs1 records the invite", derive=_member_content)

    settled_sync(c, b, since2, peer=True,
                 label="the invite arrives in hs2's /sync with stripped state",
                 derive=lambda d: {"sections": _sections(d),
                                   "invite": _stripped(d)},
                 ready=lambda d: bool(d["sections"]["invite"]))

    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="the invited remote user accepts")
    wait_members(c, a, room, {b.user_id: "join"})
    wait_members(c, b, room, {b.user_id: "join"}, peer=True)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="hs1 membership after the remote accept", derive=_member_rows)

    since1 = sync_token(c, a)
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=b.token,
          json_body={"reason": "done"}, peer=True,
          label="the remote user leaves")
    wait_members(c, a, room, {b.user_id: "leave"})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="hs1 records the remote leave",
          derive=_member_content)
    settled_sync(c, a, since1, label="hs1 /sync carries the remote leave",
                 derive=_membership_delta,
                 ready=lambda d: _has_member(d, b.user_id, "leave"))
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True,
          label="hs2 no longer lists the room as joined")


@scenario("fed/invite-rejected", tags=["federation", "membership", "invite"],
          step=2, needs=("federation",))
def invite_rejected(c):
    """The invitee on hs2 rejects: it has to leave without ever having joined,
    and hs1 has to record the rejection.

    What is scored is the rejection itself and hs1's view of it.  The two
    reads of the *rejecting* server's own `/sync` and `/joined_rooms` at the
    end are issued but not compared: the reference persists an out-of-band
    rejection as an outlier, and its sync stream token can sit behind that
    event's stream ordering, so the room stays in `invite` for that client
    indefinitely.  That is the reference's own bookkeeping rather than
    anything the release fixes, so there is nothing comparable to pin.  See
    this step's report.
    """
    a = reg(c, "ira")
    b = reg(c, "irb", peer=True)
    room = mk_room(c, a, room_version=V6, preset="private_chat", name="Refused")
    since2 = sync_token(c, b, peer=True)

    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=a.token,
          json_body={"user_id": b.user_id, "reason": "please come"},
          label="hs1 invites a remote user")
    settled_sync(c, b, since2, peer=True, label="hs2 has the pending invite",
                 derive=lambda d: {"sections": _sections(d),
                                   "invite": _stripped(d)},
                 ready=lambda d: bool(d["sections"]["invite"]))

    since1 = sync_token(c, a)
    since3 = sync_token(c, b, peer=True)
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=b.token,
          json_body={"reason": "no thanks"}, peer=True,
          label="the remote invitee rejects the invite")
    wait_members(c, a, room, {b.user_id: "leave"})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="hs1 records the rejection",
          derive=_member_content)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          query={"membership": "join"},
          label="the rejecting user never joined", derive=_member_rows)
    settled_sync(c, a, since1, label="hs1 /sync carries the rejection",
                 derive=_membership_delta,
                 ready=lambda d: _has_member(d, b.user_id, "leave"))
    # Not scored, and deliberately so.  The rejection itself is scored above,
    # on hs1: the leave reaches the room and the invitee never joined.  What
    # the *rejecting* server then shows its own client is a different matter --
    # the reference persists an out-of-band rejection as an outlier and its
    # sync stream token can sit behind that event's stream ordering, so the
    # room stays in `invite` for that client indefinitely.  That is the
    # reference's own bookkeeping, not something the release fixes or another
    # server could reproduce, so these two reads are issued and not compared.
    c.req("GET", f"{CS}/sync", token=b.token, peer=True,
          query={"timeout": 0, "since": since3}, check=False)
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True, check=False)


@scenario("fed/invite-stripped-state", tags=["federation", "invite", "sync"],
          step=2, needs=("federation",))
def invite_stripped_state(c):
    """What the stripped state of a federated invite has to carry: the state
    types the specification says it should identify the room by, the four keys
    it defines on a stripped event, the invitee's own membership -- and no
    access to anything else in the room."""
    a = reg(c, "isa")
    b = reg(c, "isb", peer=True)
    room = mk_room(c, a, room_version=V6, preset="private_chat",
                   name="Named room", topic="A topic")
    do_state(c, a, room, "m.room.avatar",
             {"url": f"mxc://{c.server_name}/stripped1"})
    since2 = sync_token(c, b, peer=True)

    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=a.token,
          json_body={"user_id": b.user_id}, label="invite a remote user")
    settled_sync(c, b, since2, peer=True,
                 label="stripped state of a federated invite",
                 derive=_stripped, ready=lambda d: bool(d))
    # A *full* `/sync` is deliberately not scored here: the pinned reference
    # sometimes leaves a freshly arrived remote invite out of one indefinitely
    # while serving it correctly in every incremental delta, so there is no
    # comparable answer.  See this step's report.
    # /members and /state return room STATE, which history visibility does not
    # govern, and both documented 403s read "aren't a member and weren't
    # previously" -- false of an invitee. Nothing in the release denies them, so
    # the refusal is the reference's choice and is not scored.
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=b.token, peer=True, check=False,
          label="an invitee cannot list the room's members on hs2")
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=b.token, peer=True, check=False,
          label="an invitee cannot read full room state on hs2")
    # Here the release DOES mandate the substance: the history-visibility
    # algorithm allows world_readable, membership=join, shared-plus-later-join,
    # or invite-plus-`invited` visibility, and otherwise denies. Under the
    # default `shared` an invitee matches nothing and must see no events. But
    # denying every event is equally well expressed as 200 with an empty chunk,
    # so the status is not pinned; the projection is what is scored.
    _inv_msgs = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token,
                      peer=True, query={"dir": "b", "limit": 10}, check=False)
    fact(c, _event_types(_inv_msgs.json or {}),
         label="an invitee cannot page the room's history on hs2",
         token=b.token, peer=True)
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True,
          label="an invite is not a join")
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/early",
          token=b.token, json_body={"msgtype": "m.text", "body": "too soon"},
          peer=True, label="an invitee cannot send into the room")


# --------------------------------------------------------------------------- #
# 6. Kick and ban across servers
# --------------------------------------------------------------------------- #
@scenario("fed/kick-remote-user", tags=["federation", "membership", "kick"],
          step=2, needs=("federation",))
def kick_remote_user(c):
    """hs1 kicks a user who lives on hs2; hs2 has to act on the membership it
    is sent rather than on its own opinion."""
    a = reg(c, "kka")
    b = reg(c, "kkb", peer=True)
    room = _shared_room(c, a, b, name="Kicked")
    since2 = sync_token(c, b, peer=True)

    c.req("POST", f"{CS}/rooms/{q(room)}/kick", token=a.token,
          json_body={"user_id": b.user_id, "reason": "out you go"},
          label="hs1 kicks a remote user")
    wait_members(c, a, room, {b.user_id: "leave"})

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="hs1 membership after the kick",
          derive=_member_content)
    settled_sync(c, b, since2, peer=True,
                 label="hs2 /sync puts the kicked user's room in leave",
                 derive=lambda d: {"sections": _sections(d),
                                   "members": _membership_delta(d)},
                 ready=lambda d: _has_member(d["members"], b.user_id, "leave"))
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True,
          label="hs2 no longer lists the room")
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/after-kick",
          token=b.token, json_body={"msgtype": "m.text", "body": "still here?"},
          peer=True, label="a kicked remote user cannot send")
    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a kicked remote user may rejoin a public room")
    wait_members(c, a, room, {b.user_id: "join"})
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="hs1 sees the rejoin", derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=b.token, peer=True,
          label="hs2 agrees after the rejoin", derive=_member_rows)


@scenario("fed/ban-remote-user", tags=["federation", "membership", "ban"],
          step=2, needs=("federation",))
def ban_remote_user(c):
    """A ban issued on hs1 against an hs2 user, and the unban that lifts it.

    A second hs2 user stays in the room throughout.  Without one, hs2 has no
    member left once the ban lands and stops being sent the room's events at
    all, so it would never learn about the unban -- which is correct, but makes
    the unban unobservable from hs2.
    """
    a = reg(c, "bna")
    b = reg(c, "bnb", peer=True)
    keeper = reg(c, "bnc", peer=True)
    room = _shared_room(c, a, b, name="Banned")
    do_join(c, keeper, room, peer=True, via=c.server_name)
    wait_members(c, a, room, {keeper.user_id: "join"})
    wait_members(c, keeper, room, {keeper.user_id: "join"}, peer=True)
    since2 = sync_token(c, b, peer=True)

    c.req("POST", f"{CS}/rooms/{q(room)}/ban", token=a.token,
          json_body={"user_id": b.user_id, "reason": "spam"},
          label="hs1 bans a remote user")
    wait_members(c, a, room, {b.user_id: "ban"})

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="hs1 membership after the ban",
          derive=_member_content)
    settled_sync(c, b, since2, peer=True,
                 label="hs2 /sync puts the banned user's room in leave",
                 derive=lambda d: {"sections": _sections(d),
                                   "members": _membership_delta(d)},
                 ready=lambda d: _has_member(d["members"], b.user_id, "ban"))
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="the ban is in hs1's members listing", derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=keeper.token, peer=True,
          label="hs2 replicated the ban", derive=_member_rows)
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True,
          label="hs2 drops the room for the banned user")

    c.req("POST", f"{CS}/rooms/{q(room)}/unban", token=a.token,
          json_body={"user_id": b.user_id}, label="hs1 unbans the remote user")
    wait_members(c, a, room, {b.user_id: "leave"})
    wait_members(c, keeper, room, {b.user_id: "leave"}, peer=True)
    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="the unbanned remote user may join again")
    wait_members(c, a, room, {b.user_id: "join"})
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="hs1 sees the join after the unban", derive=_member_rows)


@scenario("fed/banned-remote-cannot-rejoin", tags=["federation", "ban", "auth"],
          step=2, needs=("federation",))
def banned_remote_cannot_rejoin(c):
    """A banned remote user is refused whether they ask their own server to
    join by id or by alias, and cannot send into the room either."""
    a = reg(c, "bra")
    b = reg(c, "brb", peer=True)
    room = mk_room(c, a, room_version=V6, preset="public_chat",
                   room_alias_name="banroom", name="Ban")
    alias = f"#banroom:{c.server_name}"
    do_join(c, b, room, peer=True, via=c.server_name)
    wait_members(c, a, room, {b.user_id: "join"})
    since2 = sync_token(c, b, peer=True)

    c.req("POST", f"{CS}/rooms/{q(room)}/ban", token=a.token,
          json_body={"user_id": b.user_id, "reason": "no"},
          label="ban the remote user")
    wait_members(c, a, room, {b.user_id: "ban"})
    wait_member_delta(c, b, since2, b.user_id, "ban", peer=True)

    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a banned remote user cannot rejoin by room id")
    c.req("POST", f"{CS}/join/{q(alias)}", token=b.token, json_body={},
          peer=True, label="nor by alias")
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/nope",
          token=b.token, json_body={"msgtype": "m.text", "body": "hello?"},
          peer=True, label="nor send into the room")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="hs1 still shows the ban",
          derive=_member_content)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          query={"membership": "ban"},
          label="the members listing filters to it", derive=_member_rows)
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True,
          label="hs2 lists no joined rooms for the banned user")


# --------------------------------------------------------------------------- #
# 7. Profile and directory queries over federation
# --------------------------------------------------------------------------- #
@scenario("fed/profile-over-federation", tags=["federation", "profile"], step=2,
          needs=("federation",))
def profile_over_federation(c):
    """hs2 answers `GET /profile/{userId}` for a user who lives on hs1, which
    it can only do by asking hs1."""
    a = reg(c, "pfa", display_name="Alice Remote")
    b = reg(c, "pfb", peer=True, display_name="Bob Local")
    avatar = f"mxc://{c.server_name}/profileavatar1"
    c.req("PUT", f"{CS}/profile/{q(a.user_id)}/avatar_url", token=a.token,
          json_body={"avatar_url": avatar}, check=False)

    settle(c, lambda: read_profile(c, a.user_id, b.token, peer=True),
           lambda v: v.get("avatar_url") == avatar,
           what="hs2 can read the hs1 profile")
    c.req("GET", f"{CS}/profile/{q(a.user_id)}", token=b.token, peer=True,
          label="hs2 answers a profile query for an hs1 user")
    c.req("GET", f"{CS}/profile/{q(a.user_id)}/displayname", token=b.token,
          peer=True, label="just the displayname, over federation")
    c.req("GET", f"{CS}/profile/{q(a.user_id)}/avatar_url", token=b.token,
          peer=True, label="just the avatar_url, over federation")
    c.req("GET", f"{CS}/profile/{q('@nosuchuser:' + c.server_name)}",
          token=b.token, peer=True,
          label="a profile query for a user hs1 does not have")
    settle(c, lambda: read_profile(c, b.user_id, a.token),
           lambda v: v.get("displayname") == "Bob Local",
           what="hs1 can read the hs2 profile")
    c.req("GET", f"{CS}/profile/{q(b.user_id)}", token=a.token,
          label="and the same query in the other direction")
    c.req("GET", f"{CS}/profile/{q(b.user_id)}/displayname", token=a.token,
          label="just the displayname, the other way")


@scenario("fed/directory-over-federation", tags=["federation", "directory"],
          step=2, needs=("federation",))
def directory_over_federation(c):
    """hs2 resolves `#alias:<HS1>`, including a second alias added after the
    room was created, and can then join through it."""
    a = reg(c, "dra")
    b = reg(c, "drb", peer=True)
    room = mk_room(c, a, room_version=V6, preset="public_chat",
                   room_alias_name="remote-dir", name="Directory")
    alias = f"#remote-dir:{c.server_name}"
    extra = f"#second-alias:{c.server_name}"
    c.req("PUT", f"{CS}/directory/room/{q(extra)}", token=a.token,
          json_body={"room_id": room}, check=False)

    settle(c, lambda: read_status(c, f"{CS}/directory/room/{q(alias)}", b.token,
                                  peer=True),
           lambda v: v == 200, what="hs2 resolves the hs1 alias")
    c.req("GET", f"{CS}/directory/room/{q(alias)}", token=b.token, peer=True,
          label="hs2 resolves an alias that lives on hs1",
          derive=lambda bd: _directory(bd, c.server_name))
    settle(c, lambda: read_status(c, f"{CS}/directory/room/{q(extra)}", b.token,
                                  peer=True),
           lambda v: v == 200, what="hs2 resolves the second alias")
    c.req("GET", f"{CS}/directory/room/{q(extra)}", token=b.token, peer=True,
          label="a second alias for the same remote room",
          derive=lambda bd: _directory(bd, c.server_name))
    c.req("GET", f"{CS}/directory/room/{q('#missing:' + c.server_name)}",
          token=b.token, peer=True, label="an alias hs1 does not have")
    c.req("POST", f"{CS}/join/{q(alias)}", token=b.token, json_body={},
          peer=True, label="the resolved alias can be joined")
    wait_members(c, a, room, {b.user_id: "join"})
    wait_members(c, b, room, {b.user_id: "join"}, peer=True)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="hs1 sees the join the directory made possible",
          derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/aliases", token=a.token,
          label="hs1 owns both aliases",
          derive=lambda bd: {"n": len(bd.get("aliases") or [])})
    c.req("GET", f"{CS}/rooms/{q(room)}/aliases", token=b.token, peer=True,
          label="hs2 has no aliases of its own for the remote room",
          derive=lambda bd: {"n": len(bd.get("aliases") or [])})


# --------------------------------------------------------------------------- #
# 8. EDUs: typing, receipts, presence
# --------------------------------------------------------------------------- #
@scenario("fed/typing-edu", tags=["federation", "edu", "typing"], step=2,
          needs=("federation",))
def typing_edu(c):
    """A typing notification is an EDU: it has to reach the other server's
    `/sync` `ephemeral` section and be withdrawn again."""
    a = reg(c, "tya")
    b = reg(c, "tyb", peer=True)
    room = _shared_room(c, a, b, name="Typing")
    # A message each way first.  An EDU rides the same server-to-server
    # transactions an event does, so confirming one in each direction leaves
    # neither direction still being retried when the EDU is sent.
    seen = send_and_settle(c, a, room, "warm1", "warm from hs1", [(b, True)])
    send_and_settle(c, b, room, "warm2", "warm from hs2", [(a, False)],
                    peer=True, seen=seen)
    since2 = sync_token(c, b, peer=True)

    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(a.user_id)}", token=a.token,
          json_body={"typing": True, "timeout": 30000},
          label="hs1 user starts typing")
    settled_sync(c, b, since2, peer=True,
                 label="hs2 /sync ephemeral carries the remote typing",
                 derive=_typing,
                 ready=lambda d: any(a.user_id in lst
                                     for lists in d.values() for lst in lists))

    since3 = sync_token(c, b, peer=True)
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(a.user_id)}", token=a.token,
          json_body={"typing": False}, label="hs1 user stops typing")
    settled_sync(c, b, since3, peer=True,
                 label="hs2 /sync ephemeral carries the withdrawal",
                 derive=_typing,
                 ready=lambda d: any(lst == []
                                     for lists in d.values() for lst in lists))

    since4 = sync_token(c, a)
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(b.user_id)}", token=b.token,
          json_body={"typing": True, "timeout": 30000}, peer=True,
          label="the remote user types")
    settled_sync(c, a, since4,
                 label="hs1 /sync ephemeral carries the remote typing",
                 derive=_typing,
                 ready=lambda d: any(b.user_id in lst
                                     for lists in d.values() for lst in lists))
    c.req("PUT", f"{CS}/rooms/{q(room)}/typing/{q(a.user_id)}", token=b.token,
          json_body={"typing": True, "timeout": 30000}, peer=True,
          label="a user cannot type as somebody else")


@scenario("fed/receipt-edu", tags=["federation", "edu", "receipts"], step=2,
          needs=("federation",))
def receipt_edu(c):
    """A read receipt sent on one server has to reach the other server's
    `/sync` `ephemeral` section, naming the event it acknowledges."""
    a = reg(c, "rca")
    b = reg(c, "rcb", peer=True)
    room = _shared_room(c, a, b, name="Receipts")
    # One message each way first.  An EDU rides the same server-to-server
    # transactions as an event does, and confirming an event in each direction
    # means neither direction is still being retried when the receipt is sent.
    eid = do_send(c, a, room, "tx-1", {"msgtype": "m.text", "body": "read me"})
    wait_bodies(c, b, room, ["read me"], peer=True)
    eid2 = do_send(c, b, room, "tx-2",
                   {"msgtype": "m.text", "body": "and back"}, peer=True)
    wait_bodies(c, a, room, ["read me", "and back"])

    since1 = sync_token(c, a)
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(eid)}",
          token=b.token, json_body={}, peer=True,
          label="the remote user posts a read receipt")
    settled_sync(c, a, since1,
                 label="hs1 /sync ephemeral carries the remote read receipt",
                 derive=_receipts,
                 ready=lambda d: any(eid in rows for rows in d.values()))

    since2 = sync_token(c, b, peer=True)
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(eid2)}",
          token=a.token, json_body={},
          label="hs1's user acknowledges the remote message")
    settled_sync(c, b, since2, peer=True,
                 label="hs2 /sync ephemeral carries the receipt",
                 derive=_receipts,
                 ready=lambda d: any(eid2 in rows for rows in d.values()))
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="both messages are on hs2", derive=_bodies)
    # A receipt for an event the server has never heard of: the release names
    # no status for it, and the reference's choice is its own.  Issued so that
    # it cannot wedge what follows, but not scored -- the same treatment step 1
    # gives it on the client-server side.
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q('$nosuchevent')}",
          token=b.token, json_body={}, peer=True, check=False)


@scenario("fed/presence-edu", tags=["federation", "edu", "presence"], step=2,
          needs=("federation",))
def presence_edu(c):
    """Presence is an EDU too: what a user publishes on one server has to show
    up in the other server's `/sync` `presence` section and in
    `GET /presence/{userId}/status`, in both directions.

    Each user publishes exactly once.  The pinned reference rate-limits
    `PUT /presence/{userId}/status` to one call per user per ten seconds
    (`rc_presence`, which the harness's Synapse configuration does not
    neutralise the way it does every other rate limiter), so a scenario that
    updated one user twice would score the reference's rate limiting rather
    than federation -- see this step's report.
    """
    a = reg(c, "pra")
    b = reg(c, "prb", peer=True)
    b2 = reg(c, "prc", peer=True)
    room = mk_room(c, a, room_version=V6, preset="public_chat", name="Presence")
    do_join(c, b, room, peer=True, via=c.server_name)
    do_join(c, b2, room, peer=True, via=c.server_name)
    wait_members(c, a, room, {a.user_id: "join", b.user_id: "join",
                              b2.user_id: "join"})
    wait_members(c, b, room, {a.user_id: "join", b.user_id: "join",
                              b2.user_id: "join"}, peer=True)
    # A message each way first: an EDU rides the same server-to-server
    # transactions an event does, so this leaves neither direction mid-retry.
    seen = send_and_settle(c, a, room, "warm1", "warm from hs1", [(b, True)])
    send_and_settle(c, b, room, "warm2", "warm from hs2", [(a, False)],
                    peer=True, seen=seen)

    since1 = sync_token(c, a)
    c.req("PUT", f"{CS}/presence/{q(b.user_id)}/status", token=b.token,
          json_body={"presence": "online", "status_msg": "here on hs2"},
          peer=True, label="a remote user publishes a presence status")
    settled_sync(c, a, since1,
                 label="hs1 /sync presence carries the remote status",
                 derive=lambda d: _presence_of(d, b.user_id),
                 ready=lambda d: d.get("status_msg") == "here on hs2")
    settle_then_score(c, lambda: read_presence(c, b.user_id, a.token),
           lambda v: v.get("status_msg") == "here on hs2",
           what="hs1 serves the remote presence")
    c.req("GET", f"{CS}/presence/{q(b.user_id)}/status", token=a.token,
          label="hs1 serves the remote user's presence", derive=_presence_body)

    since2 = sync_token(c, a)
    c.req("PUT", f"{CS}/presence/{q(b2.user_id)}/status", token=b2.token,
          json_body={"presence": "online", "status_msg": "also on hs2"},
          peer=True, label="a second remote user publishes one")
    settled_sync(c, a, since2,
                 label="hs1 /sync presence carries the second remote status",
                 derive=lambda d: _presence_of(d, b2.user_id),
                 ready=lambda d: d.get("status_msg") == "also on hs2")
    settle_then_score(c, lambda: read_presence(c, b2.user_id, a.token),
           lambda v: v.get("status_msg") == "also on hs2",
           what="hs1 serves the second remote presence")
    c.req("GET", f"{CS}/presence/{q(b2.user_id)}/status", token=a.token,
          label="hs1 serves the second remote user's presence",
          derive=_presence_body)

    since3 = sync_token(c, b, peer=True)
    c.req("PUT", f"{CS}/presence/{q(a.user_id)}/status", token=a.token,
          json_body={"presence": "online", "status_msg": "here on hs1"},
          label="and the same in the other direction")
    settled_sync(c, b, since3, peer=True,
                 label="hs2 /sync presence carries the hs1 user's status",
                 derive=lambda d: _presence_of(d, a.user_id),
                 ready=lambda d: d.get("status_msg") == "here on hs1")
    settle_then_score(c, lambda: read_presence(c, a.user_id, b.token, peer=True),
           lambda v: v.get("status_msg") == "here on hs1",
           what="hs2 serves the hs1 presence")
    c.req("GET", f"{CS}/presence/{q(a.user_id)}/status", token=b.token,
          peer=True, label="hs2 serves the hs1 user's presence",
          derive=_presence_body)
    c.req("PUT", f"{CS}/presence/{q(a.user_id)}/status", token=b2.token,
          json_body={"presence": "online"}, peer=True,
          label="a user cannot set somebody else's presence")


# --------------------------------------------------------------------------- #
# 9. Room versions 7-10 over federation
# --------------------------------------------------------------------------- #
def _roomver_join(c, version, prefix):
    """Create at `version` on hs1, join from hs2, and exchange one message each
    way."""
    a = reg(c, f"{prefix}a")
    b = reg(c, f"{prefix}b", peer=True)
    room = mk_room(c, a, room_version=version, preset="public_chat",
                   name=f"Room v{version}")
    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label=f"v{version}: a remote user joins")
    wait_members(c, a, room, {a.user_id: "join", b.user_id: "join"})
    wait_members(c, b, room, {a.user_id: "join", b.user_id: "join"}, peer=True)

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=b.token,
          peer=True, label=f"v{version}: hs2 has the create event",
          derive=_create_facts)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label=f"v{version}: hs1 membership", derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=b.token, peer=True,
          label=f"v{version}: hs2 membership", derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=b.token, peer=True,
          label=f"v{version}: hs2 resolved the state",
          derive=lambda bd: {"state": _state_pairs(bd)})

    seen = send_and_settle(c, a, room, "from-hs1", "v-hs1", [(b, True)])
    send_and_settle(c, b, room, "from-hs2", "v-hs2", [(a, False)], peer=True,
                    seen=seen)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label=f"v{version}: hs1 has both messages", derive=_bodies)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label=f"v{version}: hs2 has both messages", derive=_bodies)
    return a, b, room


@scenario("fed/roomver-7-join", tags=["federation", "room-versions"], step=2,
          needs=("federation",))
def roomver_7_join(c):
    """Room version 7 across servers."""
    _roomver_join(c, V7, "r7")


@scenario("fed/roomver-8-join", tags=["federation", "room-versions"], step=2,
          needs=("federation",))
def roomver_8_join(c):
    """Room version 8 across servers."""
    _roomver_join(c, V8, "r8")


@scenario("fed/roomver-9-join", tags=["federation", "room-versions"], step=2,
          needs=("federation",))
def roomver_9_join(c):
    """Room version 9 across servers."""
    _roomver_join(c, V9, "r9")


# Room version 10 is release v1.6's, not v1.2's: these three are scored at
# the milestone whose spec tree ships `rooms/v10.md`.
@scenario("fed/roomver-10-join", tags=["federation", "room-versions"], step=4,
          needs=("federation",))
def roomver_10_join(c):
    """Room version 10 across servers."""
    _roomver_join(c, V10, "rx")


@scenario("fed/knock-v7", tags=["federation", "knock", "room-versions"], step=2,
          needs=("federation",))
def knock_v7(c):
    """Knocking is what room version 7 adds: a remote user asks to be let in,
    the knock is visible on both servers, and an invite turns it into a join."""
    a = reg(c, "kna")
    b = reg(c, "knb", peer=True)
    room = mk_room(c, a, room_version=V7, preset="private_chat", name="Knock")
    do_state(c, a, room, "m.room.join_rules", {"join_rule": "knock"})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.join_rules/", token=a.token,
          label="the room's join rule is knock", derive=_join_rules)
    since2 = sync_token(c, b, peer=True)

    c.req("POST", f"{CS}/knock/{q(room)}", token=b.token,
          json_body={"reason": "let me in"},
          query={"server_name": c.server_name}, peer=True,
          label="a remote user knocks on a v7 room")
    wait_members(c, a, room, {b.user_id: "knock"})

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="hs1 records the knock", derive=_member_content)
    settled_sync(c, b, since2, peer=True,
                 label="hs2 /sync carries the knock with stripped state",
                 derive=lambda d: {"sections": _sections(d),
                                   "knock": _stripped(d, "knock",
                                                      "knock_state")},
                 ready=lambda d: bool(d["sections"]["knock"]))
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          query={"membership": "knock"},
          label="hs1 members filtered to knock", derive=_member_rows)

    since3 = sync_token(c, b, peer=True)
    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=a.token,
          json_body={"user_id": b.user_id},
          label="hs1 answers the knock with an invite")
    wait_members(c, a, room, {b.user_id: "invite"})
    wait_section(c, b, since3, room, "invite", peer=True)
    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="the knocker joins once invited")
    wait_members(c, a, room, {b.user_id: "join"})
    wait_members(c, b, room, {b.user_id: "join"}, peer=True)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="hs1 membership after the knock became a join",
          derive=_member_rows)


@scenario("fed/knock-refused-v6", tags=["federation", "knock", "room-versions"],
          step=2, needs=("federation",))
def knock_refused_v6(c):
    """The four ways a remote knock has to be refused: the room version has no
    knocking, the join rule is `invite`, the join rule is `public`, and the
    knocker is banned.  All four are the 403 the specification documents for
    `POST /knock/{roomIdOrAlias}`.

    A knock on a room neither server has is deliberately not scored: the
    reference answers it with a 502 rather than the 404 the specification
    documents, so the situation has no comparable answer.  See this step's
    report.
    """
    a = reg(c, "k6a")
    b = reg(c, "k6b", peer=True)

    old = mk_room(c, a, room_version=V6, preset="private_chat", name="Old")
    c.req("GET", f"{CS}/rooms/{q(old)}/state/m.room.create/", token=a.token,
          label="the room really is at version 6", derive=_create_facts)
    c.req("POST", f"{CS}/knock/{q(old)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a remote knock on a version 6 room is refused")
    c.req("GET", f"{CS}/rooms/{q(old)}/members", token=a.token,
          label="no knock membership was created", derive=_member_rows)

    closed = mk_room(c, a, room_version=V7, preset="private_chat",
                     name="Invite")
    c.req("GET", f"{CS}/rooms/{q(closed)}/state/m.room.join_rules/",
          token=a.token, label="the v7 room is invite-only", derive=_join_rules)
    c.req("POST", f"{CS}/knock/{q(closed)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a knock on an invite-only v7 room is refused")

    public = mk_room(c, a, room_version=V7, preset="public_chat", name="Open")
    c.req("POST", f"{CS}/knock/{q(public)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a knock on a public v7 room is refused too")

    knockable = mk_room(c, a, room_version=V7, preset="private_chat",
                        name="Knockable")
    do_state(c, a, knockable, "m.room.join_rules", {"join_rule": "knock"})
    c.req("POST", f"{CS}/rooms/{q(knockable)}/ban", token=a.token,
          json_body={"user_id": b.user_id, "reason": "not you"},
          label="hs1 bans the would-be knocker in advance")
    wait_members(c, a, knockable, {b.user_id: "ban"})
    c.req("POST", f"{CS}/knock/{q(knockable)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a banned user cannot knock")
    c.req("GET", f"{CS}/rooms/{q(knockable)}/members", token=a.token,
          label="the ban still stands", derive=_member_rows)
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True,
          label="the knocker joined nothing")


@scenario("fed/restricted-v8",
          tags=["federation", "room-versions", "restricted"], step=2,
          needs=("federation",))
def restricted_v8(c):
    """Room version 8's `restricted` join rule: membership of another room is
    the condition, and the condition is evaluated across servers."""
    a = reg(c, "rsa")
    b = reg(c, "rsb", peer=True)
    outsider = reg(c, "rsc", peer=True)
    gate = mk_room(c, a, room_version=V8, preset="public_chat", name="Gate")
    do_join(c, b, gate, peer=True, via=c.server_name)
    wait_members(c, a, gate, {b.user_id: "join"})

    room = mk_room(c, a, room_version=V8, preset="private_chat",
                   name="Restricted")
    do_state(c, a, room, "m.room.join_rules",
             {"join_rule": "restricted",
              "allow": [{"type": "m.room_membership", "room_id": gate}]})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.join_rules/", token=a.token,
          label="the restricted join rule", derive=_join_rules)

    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a remote member of the allowed room may join")
    wait_members(c, a, room, {b.user_id: "join"})
    wait_members(c, b, room, {b.user_id: "join"}, peer=True)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token,
          label="the join was authorised by a user on the resident server",
          derive=lambda bd: _authorised_by(bd, a.user_id))
    c.req("POST", f"{CS}/join/{q(room)}", token=outsider.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a remote user outside the allowed room may not")
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="only the authorised user got in", derive=_member_rows)

    do_join(c, outsider, gate, peer=True, via=c.server_name)
    wait_members(c, a, gate, {outsider.user_id: "join"})
    c.req("POST", f"{CS}/join/{q(room)}", token=outsider.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="once in the allowed room, the same user may join")
    wait_members(c, a, room, {outsider.user_id: "join"})
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="both remote users are now in", derive=_member_rows)


@scenario("fed/knock-restricted-v10",
          tags=["federation", "room-versions", "restricted", "knock"], step=4,
          needs=("federation",))
def knock_restricted_v10(c):
    """Room version 10's `knock_restricted` is both at once: a member of the
    allowed room joins outright, anybody else is refused a join, and anybody
    else may still knock.

    The refusal and the knock are done by two different remote users on
    purpose.  A user whose join was refused first and who then knocks has, in
    the pinned reference, sometimes ended up with the knock missing from its
    own server's `/sync` for good, while the resident server had it -- so the
    knocker here has no earlier attempt behind it.
    """
    a = reg(c, "kra")
    b = reg(c, "krb", peer=True)
    knocker = reg(c, "krc", peer=True)
    blocked = reg(c, "krd", peer=True)
    gate = mk_room(c, a, room_version=V10, preset="public_chat", name="Gate")
    do_join(c, b, gate, peer=True, via=c.server_name)
    wait_members(c, a, gate, {b.user_id: "join"})

    room = mk_room(c, a, room_version=V10, preset="private_chat",
                   name="KnockRestricted")
    do_state(c, a, room, "m.room.join_rules",
             {"join_rule": "knock_restricted",
              "allow": [{"type": "m.room_membership", "room_id": gate}]})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.join_rules/", token=a.token,
          label="the knock_restricted join rule", derive=_join_rules)

    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a remote member of the allowed room joins outright")
    wait_members(c, a, room, {b.user_id: "join"})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="that join carries an authorising user",
          derive=lambda bd: _authorised_by(bd, a.user_id))
    c.req("POST", f"{CS}/join/{q(room)}", token=blocked.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="a remote user outside the allowed room cannot join")

    since = sync_token(c, knocker, peer=True)
    c.req("POST", f"{CS}/knock/{q(room)}", token=knocker.token,
          json_body={"reason": "may I"}, query={"server_name": c.server_name},
          peer=True, label="but such a user may knock on it")
    wait_members(c, a, room, {knocker.user_id: "knock"})
    settled_sync(c, knocker, since, peer=True,
                 label="hs2 /sync carries the knock on a v10 room",
                 derive=lambda d: {"sections": _sections(d),
                                   "knock": _stripped(d, "knock",
                                                      "knock_state")},
                 ready=lambda d: bool(d["sections"]["knock"]))
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="one join and one knock", derive=_member_rows)


@scenario("fed/roomver-10-power-levels",
          tags=["federation", "room-versions", "power-levels"], step=4,
          needs=("federation",))
def roomver_10_power_levels(c):
    """Room version 10 requires power levels to be integers.  A string value
    has to be refused, an integer one accepted, and the accepted one enforced
    on the other server."""
    a = reg(c, "vpa")
    b = reg(c, "vpb", peer=True)
    room = _shared_room(c, a, b, version=V10, name="Integers")
    base = {"users": {a.user_id: 100}, "users_default": 0,
            "events_default": 0, "state_default": 50, "ban": 50, "kick": 50,
            "redact": 50, "invite": 0}
    # A baseline this scenario sets itself, so "unchanged" below is measured
    # against a known value rather than against the reference's own choice of
    # default power levels for a preset.
    do_state(c, a, room, "m.room.power_levels", base)
    wait_state(c, b, room, "m.room.power_levels",
               lambda v: v.get("events_default") == 0, peer=True)

    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=a.token, json_body=dict(base, events_default="50"),
          label="v10 refuses a string events_default")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=a.token,
          json_body=dict(base, users={a.user_id: 100, b.user_id: "50"}),
          label="v10 refuses a string in the users map")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=a.token, label="the power levels are unchanged",
          derive=_power_facts)

    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=a.token, json_body=dict(base, events_default=50),
          label="v10 accepts the integer form")
    wait_state(c, b, room, "m.room.power_levels",
               lambda v: v.get("events_default") == 50, peer=True)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=b.token, peer=True,
          label="hs2 has the integer power levels", derive=_power_facts)
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/blocked",
          token=b.token, json_body={"msgtype": "m.text", "body": "nope"},
          peer=True, label="and enforces them against its own user")


# --------------------------------------------------------------------------- #
# 10. The first server as the joining server
# --------------------------------------------------------------------------- #
@scenario("fed/local-user-joins-remote-room", tags=["federation", "join"],
          step=2, needs=("federation",))
def local_user_joins_remote_room(c):
    """The other direction of a join: the room lives on hs2, so hs1 has to
    fetch its state and its history before it can show it to anybody."""
    a = reg(c, "lra")
    b = reg(c, "lrb", peer=True)
    room = mk_room(c, b, peer=True, room_version=V6, preset="public_chat",
                   name="Theirs")
    do_send(c, b, room, "pre", {"msgtype": "m.text", "body": "before"},
            peer=True)
    since1 = sync_token(c, a)

    c.req("POST", f"{CS}/join/{q(room)}", token=a.token, json_body={},
          query={"server_name": c.peer_name},
          label="an hs1 user joins a room that lives on hs2")
    wait_members(c, a, room, {a.user_id: "join", b.user_id: "join"})
    wait_members(c, b, room, {a.user_id: "join", b.user_id: "join"}, peer=True)

    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="hs1 has both members of the remote room", derive=_member_rows)
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=a.token,
          label="hs1 resolved the remote room's state",
          derive=lambda bd: {"state": _state_pairs(bd)})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=a.token,
          label="hs1 has the remote create event", derive=_create_facts)
    c.req("GET", f"{CS}/joined_rooms", token=a.token,
          label="hs1 lists the remote room as joined")
    settled_sync(c, a, since1, label="hs1 /sync after joining a remote room",
                 derive=lambda d: {"sections": _sections(d),
                                   "members": _membership_delta(d)},
                 ready=lambda d: _has_member(d["members"], a.user_id, "join"))
    wait_bodies(c, a, room, ["before"])
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="hs1 backfilled the message sent before it joined",
          derive=_bodies)
    send_and_settle(c, a, room, "post", "after", [(b, True)], seen=["before"])
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token, peer=True,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="hs2 accepted the message from the newcomer", derive=_bodies)


@scenario("fed/leave-and-rejoin-remote-room",
          tags=["federation", "join", "membership"], step=2,
          needs=("federation",))
def leave_and_rejoin_remote_room(c):
    """A room on hs2 that an hs1 user joins, leaves, forgets and rejoins: the
    second join has to work from a cold local cache, and has to pick up what
    was said while the user was away."""
    a = reg(c, "lja")
    b = reg(c, "ljb", peer=True)
    room = mk_room(c, b, peer=True, room_version=V6, preset="public_chat",
                   name="Revolving")
    do_join(c, a, room, via=c.peer_name)
    wait_members(c, b, room, {a.user_id: "join"}, peer=True)
    send_and_settle(c, b, room, "one", "while in", [(a, False)], peer=True)

    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=a.token,
          json_body={"reason": "back later"}, label="the hs1 user leaves")
    wait_members(c, b, room, {a.user_id: "leave"}, peer=True)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(a.user_id)}",
          token=b.token, peer=True, label="hs2 recorded the leave",
          derive=_member_content)
    c.req("GET", f"{CS}/joined_rooms", token=a.token,
          label="hs1 no longer lists it")
    c.req("POST", f"{CS}/rooms/{q(room)}/forget", token=a.token, json_body={},
          label="the hs1 user forgets the room")

    send_and_settle(c, b, room, "two", "while out", [(b, True)], peer=True,
                    seen=["while in"])

    c.req("POST", f"{CS}/join/{q(room)}", token=a.token, json_body={},
          query={"server_name": c.peer_name}, label="and rejoins")
    wait_members(c, a, room, {a.user_id: "join"})
    wait_members(c, b, room, {a.user_id: "join"}, peer=True)
    c.req("GET", f"{CS}/joined_rooms", token=a.token,
          label="hs1 lists it again")
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=b.token, peer=True,
          label="hs2 membership after the rejoin", derive=_member_rows)
    wait_bodies(c, a, room, ["while in", "while out"])
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "limit": 10, "filter": MSG_FILTER},
          label="the rejoiner sees what was said while it was away",
          derive=_bodies)


@scenario("fed/join-refused-remote-room",
          tags=["federation", "join", "errors"], step=2, needs=("federation",))
def join_refused_remote_room(c):
    """Joins that have to be refused, in both directions, and what a
    non-member may not do with a remote room.

    Joining a room id neither server has is deliberately not scored: the
    reference answers `POST /join` for an unknown remote room with a 502, and
    the specification documents no status for that situation at all, so there
    is nothing comparable to pin.  See this step's report.
    """
    a = reg(c, "jua")
    b = reg(c, "jub", peer=True)
    private = mk_room(c, a, room_version=V6, preset="private_chat",
                      name="Private")
    theirs = mk_room(c, b, peer=True, room_version=V6, preset="private_chat",
                     name="Also private")

    c.req("POST", f"{CS}/join/{q(private)}", token=b.token, json_body={},
          query={"server_name": c.server_name}, peer=True,
          label="join an invite-only remote room without an invite")
    c.req("GET", f"{CS}/rooms/{q(private)}/members", token=a.token,
          label="no membership was created", derive=_member_rows)
    c.req("GET", f"{CS}/joined_rooms", token=b.token, peer=True,
          label="hs2 joined nothing")
    c.req("GET", f"{CS}/rooms/{q(private)}/state", token=b.token, peer=True,
          label="a non-member cannot read the remote room's state",
          derive=_count)
    c.req("PUT", f"{CS}/rooms/{q(private)}/send/m.room.message/nope",
          token=b.token, json_body={"msgtype": "m.text", "body": "hello?"},
          peer=True, label="nor send into it")
    c.req("POST", f"{CS}/join/{q(theirs)}", token=a.token, json_body={},
          query={"server_name": c.peer_name},
          label="and the same in the other direction")
    c.req("GET", f"{CS}/rooms/{q(theirs)}/members", token=b.token, peer=True,
          label="hs2 created no membership either", derive=_member_rows)
    c.req("GET", f"{CS}/joined_rooms", token=a.token,
          label="hs1 joined nothing")
