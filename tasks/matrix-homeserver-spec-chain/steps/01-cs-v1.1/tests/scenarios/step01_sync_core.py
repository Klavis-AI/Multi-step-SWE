"""Step 1 — /sync: the initial snapshot, incremental deltas, long polling, filters."""

import json

from mxconf.mx import (CS, create_room, invite, join, ok, q, register, send,
                       set_state, sync)
from mxconf.registry import scenario
from mxconf.steps import keep_types

V = "6"


# The sections this step's instruction requires.  Anything else a server
# chooses to send (the end-to-end-encryption counters, a room summary) is its
# own business at this release and is not compared.
REQUIRED_TOP = ("next_batch", "rooms", "presence", "account_data", "to_device")
def shape(b):
    """The structural facts a /sync response must carry, without its volume."""
    rooms = b.get("rooms") or {}
    return {
        "join_rooms": len(rooms.get("join", {})),
        "invite_rooms": len(rooms.get("invite", {})),
        "leave_rooms": len(rooms.get("leave", {})),
    }


def room_shape(b, which="join", *, split=False):
    """What one room looks like in a sync response.

    Where the boundary between `state` and `timeline` falls is the server's
    own decision whenever the request does not fix it: the release sets no
    default timeline limit, and `state` is by definition "the state before the
    timeline", so a server with a shorter default puts events on the other
    side of the line and is still right.  What is compared is therefore the
    union of the two, and which sections are present is not compared at all --
    an empty `ephemeral` or `account_data` may be sent or omitted.

    `split=True` is for the scenarios that *did* fix the boundary, by asking
    for a timeline limit of their own.  There the release decides which events
    land where, so the split, `limited` and `prev_batch` are all compared.
    """
    out = {}
    for rid, room in sorted((b.get("rooms", {}) or {}).get(which, {}).items()):
        tl = room.get("timeline", {}) or {}
        st = room.get("state", {}) or {}
        tl_events = tl.get("events", []) or []
        st_events = st.get("events", []) or []
        entry = {
            "event_types": keep_types(1, sorted(
                e.get("type") for e in st_events + tl_events)),
            "bodies": [e.get("content", {}).get("body")
                       for e in st_events + tl_events
                       if e.get("type") == "m.room.message"],
        }
        if split:
            entry["state_types"] = keep_types(
                1, sorted(e.get("type") for e in st_events))
            entry["timeline_types"] = keep_types(
                1, [e.get("type") for e in tl_events])
            entry["limited"] = tl.get("limited")
            entry["has_prev_batch"] = "prev_batch" in tl
        out[rid] = entry
    return out


def settle(c, path, token, done, tries=240, pause=0.25):
    """Poll an endpoint, unscored, until the server has caught up.

    Some of what the release requires -- propagating a profile change into
    every room's membership, for one -- is allowed to happen after the request
    that caused it returns.  Waiting here keeps the measurement about *whether*
    the server does it rather than about how fast, which is why the budget is a
    minute rather than ten seconds: on a loaded machine a correct server can be
    slow, and a poll that decides a verdict must not call slow wrong.
    """
    import time
    for _ in range(tries):
        r = c.req("GET", path, token=token, check=False)
        try:
            if r.json is not None and done(r.json):
                return True
        except Exception:                                   # noqa: BLE001
            pass
        time.sleep(pause)
    return False


@scenario("cs/sync-initial-empty", tags=["sync"], step=1)
def sync_initial_empty(c):
    u = register(c, "sy1")
    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0},
          label="initial sync with no rooms", derive=shape)


@scenario("cs/sync-initial-room", tags=["sync"], step=1)
def sync_initial_room(c):
    u = register(c, "sy2")
    room = create_room(c, u, room_version=V, name="Synced")
    send(c, u, room, "m1", {"msgtype": "m.text", "body": "hello"})
    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0},
          label="initial sync with one room", derive=lambda b: room_shape(b))


@scenario("cs/sync-incremental", tags=["sync"], step=1)
def sync_incremental(c):
    u = register(c, "sy3")
    room = create_room(c, u, room_version=V)
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0, "since": since},
          label="incremental sync with nothing new", derive=shape)
    send(c, u, room, "m1", {"msgtype": "m.text", "body": "one"})
    send(c, u, room, "m2", {"msgtype": "m.text", "body": "two"})
    r2 = c.req("GET", f"{CS}/sync", token=u.token,
               query={"timeout": 0, "since": since},
               label="incremental sync returns exactly the new events",
               derive=lambda b: room_shape(b))
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 0, "since": r2.json["next_batch"]},
          label="and nothing after that", derive=shape)


@scenario("cs/sync-message-content", tags=["sync"], step=1)
def sync_message_content(c):
    u = register(c, "sy4")
    room = create_room(c, u, room_version=V)
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    send(c, u, room, "m1", {"msgtype": "m.text", "body": "the body"})
    # `/sync`'s timeline carries ClientEventWithoutRoomID, so the properties
    # the schema declares are the ones compared; a server that carries more is
    # not saying anything different, and the projection drops extras everywhere
    # a derive is not used.  Listing them keeps this check as strict as the
    # schema and no stricter.
    DECLARED = ("content", "event_id", "origin_server_ts", "sender",
                "state_key", "type", "unsigned")

    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0, "since": since},
          label="the event as /sync serves it",
          derive=lambda b: [{k: e.get(k) for k in DECLARED if k in e}
                            for room in b["rooms"]["join"].values()
                            for e in room["timeline"]["events"]])


@scenario("cs/sync-transaction-id", tags=["sync"], step=1)
def sync_transaction_id(c):
    u = register(c, "sy5")
    from mxconf.mx import login
    other = login(c, u)
    room = create_room(c, u, room_version=V)
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    r2 = c.req("GET", f"{CS}/sync", token=other, query={"timeout": 0}, check=False)
    since2 = r2.json["next_batch"]
    send(c, u, room, "unique-txn-42", {"msgtype": "m.text", "body": "mine"})
    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0, "since": since},
          label="the sending device sees unsigned.transaction_id",
          derive=lambda b: [e.get("unsigned", {}).get("transaction_id")
                            for room in b["rooms"].get("join", {}).values()
                            for e in room["timeline"]["events"]])
    c.req("GET", f"{CS}/sync", token=other, query={"timeout": 0, "since": since2},
          label="another device does not",
          derive=lambda b: [e.get("unsigned", {}).get("transaction_id")
                            for room in b["rooms"].get("join", {}).values()
                            for e in room["timeline"]["events"]])


@scenario("cs/sync-full-state", tags=["sync"], step=1)
def sync_full_state(c):
    u = register(c, "sy6")
    room = create_room(c, u, room_version=V)
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    send(c, u, room, "m1", {"msgtype": "m.text", "body": "one"})
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 0, "since": since, "full_state": True},
          label="full_state re-sends the room state",
          derive=lambda b: room_shape(b))


@scenario("cs/sync-invite", tags=["sync", "membership"], step=1)
def sync_invite(c):
    u = register(c, "si1")
    v = register(c, "si2")
    room = create_room(c, u, room_version=V, name="Invitation")
    invite(c, u, room, v.user_id)
    c.req("GET", f"{CS}/sync", token=v.token, query={"timeout": 0},
          label="the invite section", derive=lambda b: {
              "invite_rooms": len(b["rooms"].get("invite", {})),
              "stripped_state": sorted(
                  {(e.get("type"), e.get("state_key"))
                   for room in b["rooms"].get("invite", {}).values()
                   for e in room["invite_state"]["events"]}),
              "sections": sorted(
                  k for room in b["rooms"].get("invite", {}).values()
                  for k in room),
          })


@scenario("cs/sync-leave", tags=["sync", "membership"], step=1)
def sync_leave(c):
    u = register(c, "sl1")
    v = register(c, "sl2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    r = c.req("GET", f"{CS}/sync", token=v.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=v.token, json_body={},
          check=False)
    c.req("GET", f"{CS}/sync", token=v.token, query={"timeout": 0, "since": since},
          label="the leave section", derive=lambda b: {
              "leave_rooms": len(b["rooms"].get("leave", {})),
              "join_rooms": len(b["rooms"].get("join", {})),
              "timeline_types": [e.get("type")
                                 for room in b["rooms"].get("leave", {}).values()
                                 for e in room.get("timeline", {}).get("events", [])],
          })


@scenario("cs/sync-kicked", tags=["sync", "membership"], step=1)
def sync_kicked(c):
    u = register(c, "sk1")
    v = register(c, "sk2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    r = c.req("GET", f"{CS}/sync", token=v.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    c.req("POST", f"{CS}/rooms/{q(room)}/kick", token=u.token,
          json_body={"user_id": v.user_id, "reason": "out"}, check=False)
    c.req("GET", f"{CS}/sync", token=v.token, query={"timeout": 0, "since": since},
          label="a kicked user gets the room in leave", derive=lambda b: {
              "leave_rooms": len(b["rooms"].get("leave", {})),
              "membership": [e.get("content", {}).get("membership")
                             for room in b["rooms"].get("leave", {}).values()
                             for e in room.get("timeline", {}).get("events", [])
                             if e.get("type") == "m.room.member"],
          })


@scenario("cs/sync-longpoll-wakes", tags=["sync", "longpoll"], step=1)
def sync_longpoll(c):
    u = register(c, "lp1")
    v = register(c, "lp2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]

    def poll():
        return c.req("GET", f"{CS}/sync", token=u.token,
                     query={"timeout": 20000, "since": since},
                     label="a long poll woken by another user's message",
                     timeout=60,
                     derive=lambda b: {
                         "types": [e.get("type")
                                   for room in b["rooms"].get("join", {}).values()
                                   for e in room["timeline"]["events"]],
                         "bodies": [e.get("content", {}).get("body")
                                    for room in b["rooms"].get("join", {}).values()
                                    for e in room["timeline"]["events"]],
                     })

    t, box = c.background(poll)
    import time
    time.sleep(0.7)
    send(c, v, room, "wake", {"msgtype": "m.text", "body": "wake up"})
    t.join(60)
    if "error" in box:
        raise box["error"]


@scenario("cs/sync-longpoll-times-out", tags=["sync", "longpoll"], step=1)
def sync_longpoll_timeout(c):
    u = register(c, "lp3")
    create_room(c, u, room_version=V)
    r = c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0}, check=False)
    since = r.json["next_batch"]
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 1200, "since": since}, timeout=30,
          label="a long poll that times out returns an empty delta", derive=shape)


@scenario("cs/filter-create-and-read", tags=["sync", "filters"], step=1)
def filter_create_read(c):
    u = register(c, "fl1")
    spec = {"room": {"timeline": {"limit": 3, "types": ["m.room.message"]}},
            "presence": {"types": []}}
    r = c.req("POST", f"{CS}/user/{q(u.user_id)}/filter", token=u.token,
              json_body=spec, label="create a filter")
    fid = r.json["filter_id"]
    c.req("GET", f"{CS}/user/{q(u.user_id)}/filter/{q(fid)}", token=u.token,
          label="read the filter back")
    c.req("GET", f"{CS}/user/{q(u.user_id)}/filter/nosuchfilter", token=u.token,
          label="read an unknown filter")
    v = register(c, "fl2")
    c.req("POST", f"{CS}/user/{q(u.user_id)}/filter", token=v.token,
          json_body=spec, label="create a filter for another user")


@scenario("cs/filter-timeline-limit", tags=["sync", "filters"], step=1)
def filter_timeline_limit(c):
    u = register(c, "fl3")
    room = create_room(c, u, room_version=V)
    for i in range(8):
        send(c, u, room, f"m{i}", {"msgtype": "m.text", "body": f"msg{i}"})
    filt = json.dumps({"room": {"timeline": {"limit": 3}}})
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 0, "filter": filt},
          label="a timeline limit truncates and sets limited",
          derive=lambda b: room_shape(b, split=True))
    filt2 = json.dumps({"room": {"timeline": {"limit": 50}}})
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 0, "filter": filt2},
          label="a generous limit is not limited",
          derive=lambda b: room_shape(b, split=True))


@scenario("cs/filter-types", tags=["sync", "filters"], step=1)
def filter_types(c):
    u = register(c, "fl4")
    room = create_room(c, u, room_version=V)
    send(c, u, room, "m1", {"msgtype": "m.text", "body": "a message"})
    send(c, u, room, "n1", {"body": "a note"}, etype="com.example.note")
    for spec, label in [
        ({"room": {"timeline": {"types": ["m.room.message"], "limit": 20}}},
         "timeline types"),
        ({"room": {"timeline": {"not_types": ["m.room.message"], "limit": 20}}},
         "timeline not_types"),
        ({"room": {"timeline": {"types": ["com.example.*"], "limit": 20}}},
         "timeline types with a wildcard"),
    ]:
        c.req("GET", f"{CS}/sync", token=u.token,
              query={"timeout": 0, "filter": json.dumps(spec)},
              label=f"filter: {label}", derive=lambda b: room_shape(b))


@scenario("cs/filter-senders-and-rooms", tags=["sync", "filters"], step=1)
def filter_senders(c):
    u = register(c, "fl5")
    v = register(c, "fl6")
    room = create_room(c, u, room_version=V, preset="public_chat")
    other = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    send(c, u, room, "a", {"msgtype": "m.text", "body": "from u"})
    send(c, v, room, "b", {"msgtype": "m.text", "body": "from v"})
    send(c, u, other, "c", {"msgtype": "m.text", "body": "elsewhere"})
    # Only the timeline is compared here: which state a server replays
    # alongside a filtered timeline is not fixed by the specification.
    def timelines(b):
        return {rid: {"types": keep_types(1, [e.get("type")
                                              for e in r["timeline"]["events"]]),
                      "bodies": [e.get("content", {}).get("body")
                                 for e in r["timeline"]["events"]
                                 if e.get("type") != "m.room.guest_access"]}
                for rid, r in sorted(
                    (b.get("rooms") or {}).get("join", {}).items())}

    for spec, label in [
        ({"room": {"timeline": {"senders": [v.user_id], "limit": 20}}}, "senders"),
        ({"room": {"timeline": {"not_senders": [v.user_id], "limit": 20}}},
         "not_senders"),
        ({"room": {"rooms": [room], "timeline": {"limit": 20}}}, "rooms"),
        ({"room": {"not_rooms": [room], "timeline": {"limit": 20}}}, "not_rooms"),
    ]:
        c.req("GET", f"{CS}/sync", token=u.token,
              query={"timeout": 0, "filter": json.dumps(spec)},
              label=f"filter: {label}", derive=timelines)


@scenario("cs/filter-include-leave", tags=["sync", "filters"], step=1)
def filter_include_leave(c):
    u = register(c, "fla")
    v = register(c, "flb")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=v.token, json_body={},
          check=False)
    c.req("GET", f"{CS}/sync", token=v.token,
          query={"timeout": 0,
                 "filter": json.dumps({"room": {"include_leave": True,
                                                "timeline": {"limit": 5}}})},
          label="include_leave brings the room back", derive=shape)
    c.req("GET", f"{CS}/sync", token=v.token,
          query={"timeout": 0,
                 "filter": json.dumps({"room": {"include_leave": False,
                                                "timeline": {"limit": 5}}})},
          label="without include_leave it is gone", derive=shape)


@scenario("cs/filter-invalid", tags=["sync", "filters"], step=1)
def filter_invalid(c):
    u = register(c, "flc")
    create_room(c, u, room_version=V)
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 0, "filter": "{not json"},
          label="a filter that is not JSON")
    c.req("POST", f"{CS}/user/{q(u.user_id)}/filter", token=u.token,
          body=b"[1,2,3]", headers={"Content-Type": "application/json"},
          label="a filter that is not an object")


@scenario("cs/messages-pagination", tags=["messages"], step=1)
def messages_pagination(c):
    u = register(c, "mg1")
    room = create_room(c, u, room_version=V)
    for i in range(6):
        send(c, u, room, f"m{i}", {"msgtype": "m.text", "body": f"msg{i}"})
    r = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
              query={"dir": "b", "limit": 3}, label="first backward page",
              derive=lambda b: {"bodies": [e.get("content", {}).get("body")
                                           for e in b["chunk"]],
                                "types": [e.get("type") for e in b["chunk"]],
                                "has_end": "end" in b})
    end = r.json.get("end")
    r2 = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
               query={"dir": "b", "limit": 3, "from": end},
               label="second backward page",
               derive=lambda b: {"bodies": [e.get("content", {}).get("body")
                                            for e in b["chunk"]],
                                 "types": [e.get("type") for e in b["chunk"]]})
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
          query={"dir": "f", "limit": 10, "from": r2.json.get("end")},
          label="forward from the oldest token",
          derive=lambda b: {"n": len(b["chunk"])})


@scenario("cs/messages-from-sync-token", tags=["messages", "sync"], step=1)
def messages_from_sync(c):
    u = register(c, "mg2")
    room = create_room(c, u, room_version=V)
    for i in range(4):
        send(c, u, room, f"m{i}", {"msgtype": "m.text", "body": f"msg{i}"})
    r = c.req("GET", f"{CS}/sync", token=u.token,
              query={"timeout": 0,
                     "filter": json.dumps({"room": {"timeline": {"limit": 2}}})},
              check=False)
    prev = next(iter(r.json["rooms"].get("join", {}).values()))["timeline"]["prev_batch"]
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
          query={"dir": "b", "limit": 5, "from": prev},
          label="paginate backwards from a /sync prev_batch",
          derive=lambda b: {"bodies": [e.get("content", {}).get("body")
                                       for e in b["chunk"]],
                            "types": [e.get("type") for e in b["chunk"]]})


@scenario("cs/messages-filtered", tags=["messages", "filters"], step=1)
def messages_filtered(c):
    u = register(c, "mg3")
    room = create_room(c, u, room_version=V)
    send(c, u, room, "a", {"msgtype": "m.text", "body": "message"})
    send(c, u, room, "b", {"body": "note"}, etype="com.example.note")
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
          query={"dir": "b", "limit": 20,
                 "filter": json.dumps({"types": ["m.room.message"]})},
          label="messages with a type filter",
          derive=lambda b: {"types": [e.get("type") for e in b["chunk"]]})
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
          query={"dir": "b", "limit": 20,
                 "filter": json.dumps({"not_types": ["m.room.message"]})},
          label="messages with a not_types filter",
          derive=lambda b: {"types": [e.get("type") for e in b["chunk"]]})


@scenario("cs/context", tags=["messages"], step=1)
def context(c):
    u = register(c, "ctx1")
    room = create_room(c, u, room_version=V)
    ids = [send(c, u, room, f"m{i}", {"msgtype": "m.text", "body": f"msg{i}"})
           for i in range(5)]
    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(ids[2])}", token=u.token,
          query={"limit": 2}, label="context around an event",
          derive=lambda b: {
              "before": [e.get("content", {}).get("body")
                         for e in b.get("events_before", [])],
              "after": [e.get("content", {}).get("body")
                        for e in b.get("events_after", [])],
              "event_body": b.get("event", {}).get("content", {}).get("body"),
              # The release's required set, not the whole key list: a server
              # that also sends a documented optional field is conforming.
              "required_keys_present": sorted(
                  k for k in ("event", "events_before", "events_after", "state",
                              "start", "end")
                  if k in b),
              "state_types": sorted({e.get("type") for e in b.get("state", [])}),
          })
    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q('$nosuchevent')}", token=u.token,
          query={"limit": 2}, label="context for an unknown event")


@scenario("cs/sync-set-presence", tags=["sync", "presence"], step=1)
def sync_set_presence(c):
    """`set_presence` on `/sync`.

    The release fixes the parameter's *grammar* -- "one of: [offline, online,
    unavailable]" -- and leaves what a server does with it to the presence
    module, which may not be running at all.  What the reference can settle,
    and what is scored, is that each documented value is accepted and syncs
    normally, and that a value outside the enumeration is refused.
    """
    u = register(c, "sp1")
    room = create_room(c, u, room_version=V, preset="private_chat")
    send(c, u, room, "m1", {"msgtype": "m.text", "body": "hello"})

    def one_room(b):
        join = (b.get("rooms", {}) or {}).get("join", {})
        return {"rooms": len(join),
                "bodies": [e.get("content", {}).get("body")
                           for r in join.values()
                           for e in (r.get("timeline", {}) or {}).get("events", [])
                           if e.get("type") == "m.room.message"]}

    for value in ("offline", "online", "unavailable"):
        c.req("GET", f"{CS}/sync", token=u.token,
              query={"timeout": 0, "set_presence": value},
              label=f"sync with set_presence={value} is an ordinary sync",
              derive=one_room)
    # Which error code answers a parameter outside an enumeration is not
    # something the release settles -- the harness does not compare
    # `M_INVALID_PARAM` or `M_BAD_JSON` anywhere else for that reason -- so
    # only the refusal is scored.
    c.req("GET", f"{CS}/sync", token=u.token,
          query={"timeout": 0, "set_presence": "not-a-presence-value"},
          label="sync with a set_presence outside the enumeration",
          derive=lambda b: {"refused": True})


@scenario("cs/profile-propagates-into-joined-rooms", tags=["profile", "rooms"], step=1)
def profile_propagates(c):
    """A display-name change reaches the `m.room.member` of rooms already joined.

    The release: "If the user's displayname or avatar is changed, the server
    must propagate these changes to all rooms the user is currently in" -- an
    `m.room.member` event per room, not a rewrite of the old one.
    """
    u = register(c, "pp1")
    other = register(c, "pp2")
    room_a = create_room(c, u, room_version=V, preset="public_chat", name="A")
    room_b = create_room(c, u, room_version=V, preset="public_chat", name="B")
    join(c, other, room_a)

    c.req("PUT", f"{CS}/profile/{q(u.user_id)}/displayname", token=u.token,
          json_body={"displayname": "Renamed Person"},
          label="change the display name while joined to two rooms")
    # The reference rewrites each room's membership in the background, so the
    # scenario waits for the work to land before it measures anything.  The
    # polling is unscored; only the settled answer is compared.
    settle(c, f"{CS}/rooms/{q(room_b)}/state/m.room.member/{q(u.user_id)}",
           u.token, lambda b: b.get("displayname") == "Renamed Person")
    for room, name in ((room_a, "A"), (room_b, "B")):
        c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(u.user_id)}",
              token=u.token, label=f"room {name}: the membership carries the new name",
              derive=lambda b: {"displayname": b.get("displayname"),
                                "membership": b.get("membership")})
    c.req("GET", f"{CS}/rooms/{q(room_a)}/joined_members", token=other.token,
          label="another member of room A sees the new name",
          derive=lambda b, uid=u.user_id: {
              "displayname": ((b.get("joined") or {}).get(uid) or {}).get("display_name")})

    c.req("PUT", f"{CS}/profile/{q(u.user_id)}/avatar_url", token=u.token,
          json_body={"avatar_url": "mxc://example.invalid/propagated"},
          label="change the avatar too")
    settle(c, f"{CS}/rooms/{q(room_b)}/state/m.room.member/{q(u.user_id)}",
           u.token, lambda b: b.get("avatar_url") is not None)
    c.req("GET", f"{CS}/rooms/{q(room_b)}/state/m.room.member/{q(u.user_id)}",
          token=u.token, label="room B: the membership carries the new avatar",
          derive=lambda b: {"avatar_url": b.get("avatar_url"),
                            "displayname": b.get("displayname")})
