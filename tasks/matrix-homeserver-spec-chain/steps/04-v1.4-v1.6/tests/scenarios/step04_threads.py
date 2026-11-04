"""Step 4 -- threads, event relationships, edits, spaces, the refined receipt
model and room versions 10 and 11 (Matrix specification releases v1.4 - v1.6).

Everything here is observed through the Client-Server API.  The spec release
this step targets is shipped at SPEC/v1.6: prose in
`client-server-api/modules/` (threading.md, event_replacements.md,
reference_relations.md, receipts.md, read_markers.md, spaces.md, push.md,
content_repo.md), machine-readable definitions in `api/client-server/`
(relations.yaml, threads_list.yaml, space_hierarchy.yaml, receipts.yaml,
read_markers.yaml, list_public_rooms.yaml, sync.yaml) and one page per room
version in `rooms/`.

Two conventions run through the whole file.

*   **Nothing volatile is pinned.**  Room ids, event ids, batch tokens and
    timestamps differ between two servers, so every scenario reports facts
    *about* them (a body, a count, a relationship, a label this file chose)
    rather than the values themselves.  Where an id must appear in a derived
    body it is first mapped through a scenario-local label table, so the
    transcript never depends on how a server happens to order a map keyed by
    room id.

*   **Arrays whose order the specification fixes are compared in order; arrays
    whose order it leaves open are labelled and sorted here.**  Note that
    `mxconf/normalize.py` sorts some whole-response arrays by operation
    (`hierarchy`'s `rooms`, `publicRooms`' `chunk`) and some by trailing path
    component (`room_types`, `results`, ...).  Derived bodies below therefore
    never reuse those key names, so that an ordering this module means to pin
    is not silently sorted away.

Where the pinned reference runs ahead of release v1.6
----------------------------------------------------
Step 4 has no entry in `mxconf/steps.py`, so no field, header or event type is
excluded here: whatever the pinned reference emits on a recorded request is
compared.  Four things it emits are not defined by release v1.6.  That file
belongs to the step's own configuration rather than to this module, so they are
recorded here and reported, not excluded:

*   `unsigned.membership` on every serialised event (MSC4115; the specification
    adds it in release v1.11).  No scenario below derives it, so nothing here
    is graded on it.
*   Top-level `age` and `user_id` on `GET /rooms/{roomId}/event/{eventId}`:
    `api/client-server/definitions/client_event_without_room_id.yaml` declares
    neither, and the projection in `mxconf/normalize.py` drops both from any
    non-derived recording, so nothing here is graded on them either.
*   `Content-Disposition` on a media download, which the content repository
    does not specify until release v1.12 -- which is why steps 1 and 2 exclude
    the header.  It is one of `mxconf/ctx.py`'s INTERESTING_HEADERS, so
    thr/media-cross-origin-resource-policy does record it alongside the
    `Cross-Origin-Resource-Policy` header that step 4 is actually about.
*   Room version 11 itself: SPEC/v1.6/rooms/ stops at v10.  The v11 scenarios
    below pin *observed reference behaviour*, and say so in their docstrings
    instead of citing a v1.6 rule that does not exist.

Note also that `unsigned.m.relations` -- the aggregation mechanism this step is
largely about -- is defined only in prose ("Aggregations" in
client-server-api/_index.md) and not in client_event.yaml, so a non-derived
recording would project it away.  Every bundle below is read through `derive=`.
"""

from __future__ import annotations

import json

from mxconf.mx import (CS, CS1, MEDIA, create_room, fact, join, mxc_parts,
                       png, q, register, send, set_state, upload)
from mxconf.ctx import ScenarioAbort
from mxconf.registry import scenario

# Every room is created at an explicit version so the reference's own default
# (which moves between releases -- it is "11" on the pinned Synapse) can never
# move a transcript.  "10" unless the scenario is about another version.
V = "10"

# The properties `api/client-server/definitions/client_event.yaml` declares for
# a serialised event at release v1.6.  Used to describe a bundled
# `latest_event` by *shape* rather than by its (volatile) contents.
CLIENT_EVENT_KEYS = ("content", "event_id", "origin_server_ts", "room_id",
                     "sender", "state_key", "type")

# Response keys the release lets a server carry or leave out without saying
# anything different: a pagination token that is only there when there is
# another page, and the bundles a server may have nothing to put in.  Comparing
# the *set* of keys a response carries is worth doing -- it catches a missing
# `chunk` -- but only over the keys whose presence the release fixes.
OPTIONAL_RESPONSE_KEYS = {"next_batch", "prev_batch", "next_token",
                          "prev_token", "recursion_depth", "original_event",
                          "chunk_next_batch"}


def stable_keys(b):
    """A response's keys, minus the ones the release lets a server omit."""
    return sorted(k for k in b if k not in OPTIONAL_RESPONSE_KEYS)

# `RoomEventFilter.unread_thread_notifications` (added in v1.4,
# api/client-server/definitions/room_event_filter.yaml): per-thread
# notification counts are only returned when the /sync filter asks for them.
THREAD_COUNT_FILTER = json.dumps(
    {"room": {"timeline": {"unread_thread_notifications": True, "limit": 20}}})
PLAIN_FILTER = json.dumps({"room": {"timeline": {"limit": 20}}})


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def label(table: dict, value):
    """A scenario-chosen label for an id this scenario created.

    Ids are minted by the server, so two recordings mint different ones and
    anything ordered *by* an id (a sort, a map iteration) would differ between
    them.  Replacing an id with the name this file gave the thing it points at
    makes both the value and any ordering derived from it stable, and makes the
    recorded transcript readable.
    """
    return table.get(value, value)


def bodies(events):
    return [(e.get("content") or {}).get("body") for e in events]


def types_and_bodies(events):
    return [[e.get("type"), (e.get("content") or {}).get("body")]
            for e in events]


def relates_to(event):
    return ((event.get("content") or {}).get("m.relates_to")) or {}


def thread_bundle(event, *, participants=None):
    """`unsigned.m.relations.m.thread`, described rather than quoted.

    threading.md "Server-side aggregation of m.thread relationships": the
    bundle carries `count`, `latest_event` (a serialised copy of the most
    recent event in the thread) and `current_user_participated`.
    """
    bundle = ((event.get("unsigned") or {}).get("m.relations") or {})
    thread = bundle.get("m.thread")
    if thread is None:
        return {"bundled": False, "rel_types_bundled": sorted(bundle)}
    latest = thread.get("latest_event") or {}
    return {
        "bundled": True,
        "rel_types_bundled": sorted(bundle),
        "count": thread.get("count"),
        "current_user_participated": thread.get("current_user_participated"),
        "latest_event_body": (latest.get("content") or {}).get("body"),
        "latest_event_sender": label(participants or {}, latest.get("sender")),
        "latest_event_rel_type": relates_to(latest).get("rel_type"),
        "latest_event_declared_keys": sorted(
            k for k in latest if k in CLIENT_EVENT_KEYS),
    }


def replace_bundle(event, *, participants=None):
    """`unsigned.m.relations.m.replace`.

    event_replacements.md "Server-side aggregation of m.replace relationships":
    the aggregation gives the `event_id`, `origin_server_ts` and `sender` of the
    most recent *valid* replacement.  The reference bundles a whole serialised
    event there; only the three properties the specification names are compared
    here, so a server that bundles exactly those conforms.
    """
    bundle = ((event.get("unsigned") or {}).get("m.relations") or {})
    repl = bundle.get("m.replace")
    if repl is None:
        return {"bundled": False, "rel_types_bundled": sorted(bundle)}
    return {
        "bundled": True,
        "rel_types_bundled": sorted(bundle),
        "has_event_id": isinstance(repl.get("event_id"), str),
        "sender": label(participants or {}, repl.get("sender")),
        "has_origin_server_ts": isinstance(repl.get("origin_server_ts"), int),
        # Which edit won, by the identity the release's aggregation carries.
        # The reference bundles a whole serialised event here and a server that
        # bundles only the three named properties conforms, so the winner is
        # named by its event id rather than by its content.
        "winning_event_id": repl.get("event_id"),
    }


def sync_room(body, room):
    return (((body.get("rooms") or {}).get("join") or {}).get(room)) or {}


def notif_counts(body, room, roots=None):
    """The two notification counters `/sync` carries for one room.

    sync.yaml: `unread_notifications` is the room's count (main timeline only
    once the filter asks for per-thread counts) and `unread_thread_notifications`
    is keyed by thread root id.  A thread with no notifications "can be omitted
    from this object", so a zeroed entry and an absent one are folded together
    here: both mean "nothing unread in that thread".
    """
    joined = ((body.get("rooms") or {}).get("join") or {})
    room_body = joined.get(room) or {}
    main = room_body.get("unread_notifications") or {}
    per_thread = {}
    for tid, counts in (room_body.get("unread_thread_notifications") or {}).items():
        entry = {"notification_count": counts.get("notification_count", 0),
                 "highlight_count": counts.get("highlight_count", 0)}
        if entry["notification_count"] or entry["highlight_count"]:
            per_thread[label(roots or {}, tid)] = entry
    return {
        "room_present_in_delta": room in joined,
        "main_notification_count": main.get("notification_count"),
        "main_highlight_count": main.get("highlight_count"),
        "per_thread": per_thread,
        # "If no threads have notification counts, this whole object can be
        # omitted": an empty object says the same thing as no object.
        "per_thread_counts_present": bool(
            room_body.get("unread_thread_notifications")),
    }


def receipts_in(body, room, *, users=None, events=None):
    """`m.receipt` ephemeral events for one room, with ids replaced by labels.

    receipts.md "Events": the content is keyed by event id, then by receipt
    type, then by user id, with `ts` and (from v1.4) `thread_id` inside.
    """
    out = []
    for ev in (sync_room(body, room).get("ephemeral") or {}).get("events", []):
        if ev.get("type") != "m.receipt":
            continue
        for eid, by_type in sorted((ev.get("content") or {}).items()):
            for rtype, by_user in sorted(by_type.items()):
                for uid, details in sorted(by_user.items()):
                    out.append({
                        "on": label(events or {}, eid),
                        "receipt_type": rtype,
                        "user": label(users or {}, uid),
                        "has_ts": isinstance(details.get("ts"), int),
                        "thread_id": label(events or {},
                                           details.get("thread_id")),
                    })
    out.sort(key=lambda r: (r["receipt_type"], r["user"], str(r["on"])))
    return out


def room_account_data(body, room):
    return sorted(
        [ev.get("type")
         for ev in (sync_room(body, room).get("account_data") or {}).get("events", [])]
    )


def sync_token(c, user, *, filt=None):
    r = c.req("GET", f"{CS}/sync", token=user.token,
              query={"timeout": 0, "filter": filt}, check=False)
    return r.json["next_batch"]


def _await_event(c, user, room, event_id, *, tries=150, delay=0.2):
    """Poll the UNSCORED single-event read until `event_id` is readable."""
    import time
    for _ in range(tries):
        r = c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(event_id)}",
                  token=user.token, check=False)
        if r.status == 200:
            return
        time.sleep(delay)


def _await_child(c, user, room, root, child, *, tries=150, delay=0.2):
    """Poll the UNSCORED relations view until `child` is listed under `root`."""
    import time
    for _ in range(tries):
        r = c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}",
                  token=user.token, query={"limit": 50}, check=False)
        body = r.json if r.json is not None else {}
        for e in (body.get("chunk") or []):
            if e.get("event_id") == child:
                return
        time.sleep(delay)


def _await_children(c, user, room, root, want, *, tries=150, delay=0.2):
    """Poll the UNSCORED relations view until `want` children are visible.

    `thread_reply()` is a fire-and-forget `send(..., check=False)`: it returns
    once the server accepted the event, not once the relation is queryable.
    Asserting a child count immediately after sending is therefore a race, and
    under load it loses -- one reply not yet indexed makes the unpaged answer
    short by one and shifts the whole `dir=b` walk. `check=False` skips
    recording entirely, so these polls add no transcript entries and the scored
    count is unchanged. This is the same discipline `step02_federation` states
    for itself: every scored request is preceded by a settle.
    """
    import time
    for _ in range(tries):
        r = c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}",
                  token=user.token, query={"limit": 50}, check=False)
        chunk = ((r.json or {}).get("chunk") or []) if r.json is not None else []
        if len(chunk) >= want:
            return
        time.sleep(delay)


def thread_reply(c, user, room, txn, body_text, root, *, extra=None,
                 etype="m.room.message"):
    content = {"msgtype": "m.text", "body": body_text,
               "m.relates_to": {"rel_type": "m.thread", "event_id": root}}
    if etype != "m.room.message":
        content = {"body": body_text,
                   "m.relates_to": {"rel_type": "m.thread", "event_id": root}}
    if extra:
        content["m.relates_to"].update(extra)
    eid = send(c, user, room, txn, content, etype, check=False)
    # Do not return until the reply is queryable as a child of `root`.
    # `send()` returns when the server ACCEPTED the event, not when it has been
    # indexed as a relation. Every scenario in this module sends replies and then
    # asserts on the child count or bundle, so the gap between those two moments
    # is a race that the whole module inherits, so it is closed here once rather
    # than in each scenario.
    # Unscored: `check=False` skips recording, so no transcript entry is added
    # and the scored count is unchanged.
    _await_child(c, user, room, root, eid)
    return eid


def edit(c, user, room, txn, target, fallback, new_content):
    return send(c, user, room, txn,
                {"msgtype": "m.text", "body": fallback,
                 "m.new_content": new_content,
                 "m.relates_to": {"rel_type": "m.replace", "event_id": target}},
                check=False)


# --------------------------------------------------------------------------- #
# Threads -- SPEC/v1.6/client-server-api/modules/threading.md
# --------------------------------------------------------------------------- #
@scenario("thr/thread-bundle-basics", tags=["threads", "relations"], step=4)
def thread_bundle_basics(c):
    """threading.md: a thread is a root plus children carrying
    `m.relates_to: {rel_type: m.thread, event_id: <root>}`.  The server
    aggregates them onto the root as `unsigned.m.relations.m.thread` with
    `count`, `latest_event` and `current_user_participated`, the last of which
    is per-caller: true for the root's sender and for anyone who replied,
    false for a member who has only read the thread.
    """
    author = register(c, "tba1")
    replier = register(c, "tba2")
    lurker = register(c, "tba3")
    who = {author.user_id: "author", replier.user_id: "replier",
           lurker.user_id: "lurker"}
    room = create_room(c, author, room_version=V, preset="public_chat")
    join(c, replier, room)
    join(c, lurker, room)

    root = send(c, author, room, "root", {"msgtype": "m.text", "body": "ROOT"},
                check=False)
    for i in range(3):
        thread_reply(c, replier, room, f"r{i}", f"R{i}", root)

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root)}", token=author.token,
          label="the root's thread bundle, seen by the root's own sender",
          derive=lambda b: thread_bundle(b, participants=who))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root)}", token=replier.token,
          label="...seen by a member who replied in the thread",
          derive=lambda b: thread_bundle(b, participants=who))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root)}", token=lurker.token,
          label="...seen by a member who only read it: not a participant",
          derive=lambda b: thread_bundle(b, participants=who))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root)}", token=author.token,
          label="the root's own content is untouched by the aggregation",
          derive=lambda b: {"content": b.get("content"),
                            "has_m_relates_to": "m.relates_to" in (b.get("content") or {})})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}/m.thread",
          token=author.token,
          label="the three children, most-recent first",
          derive=lambda b: {"children": bodies(b["chunk"]),
                            "rel_types": [relates_to(e).get("rel_type")
                                          for e in b["chunk"]],
                            "all_point_at_one_parent":
                                len({relates_to(e).get("event_id")
                                     for e in b["chunk"]}) == 1})


@scenario("thr/thread-bundle-grows", tags=["threads", "relations"], step=4)
def thread_bundle_grows(c):
    """threading.md: `count` "is simply the number of events using m.thread as
    a rel_type pointing at the target event" and `latest_event` is "the most
    recent event (topologically to the server) in the thread", so both move as
    replies arrive.  A second, independent thread in the same room gets its own
    bundle on its own root and does not disturb the first.
    """
    author = register(c, "tbg1")
    other = register(c, "tbg2")
    who = {author.user_id: "author", other.user_id: "other"}
    room = create_room(c, author, room_version=V, preset="public_chat")
    join(c, other, room)

    root_a = send(c, author, room, "ra", {"msgtype": "m.text", "body": "ROOT-A"},
                  check=False)
    root_b = send(c, other, room, "rb", {"msgtype": "m.text", "body": "ROOT-B"},
                  check=False)

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root_a)}", token=author.token,
          label="before any reply the root carries no thread bundle",
          derive=lambda b: thread_bundle(b, participants=who))

    thread_reply(c, other, room, "a1", "A1", root_a)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root_a)}", token=author.token,
          label="one reply: count 1, latest_event is that reply",
          derive=lambda b: thread_bundle(b, participants=who))

    thread_reply(c, author, room, "a2", "A2", root_a)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root_a)}", token=other.token,
          label="a second reply moves count and latest_event",
          derive=lambda b: thread_bundle(b, participants=who))

    thread_reply(c, author, room, "b1", "B1", root_b)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root_b)}", token=other.token,
          label="a second thread in the same room has its own bundle",
          derive=lambda b: thread_bundle(b, participants=who))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root_a)}", token=other.token,
          label="...and the first thread's bundle is unchanged by it",
          derive=lambda b: thread_bundle(b, participants=who))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root_b)}/m.thread",
          token=other.token,
          label="the second thread's children are only its own",
          derive=lambda b: bodies(b["chunk"]))


@scenario("thr/thread-in-main-timeline", tags=["threads", "messages"], step=4)
def thread_in_main_timeline(c):
    """What a thread does, and does not, do to the room's own timeline.

    Release v1.6 gives `/messages` no relation filter (its `RoomEventFilter`,
    api/client-server/definitions/room_event_filter.yaml, has no
    relation-related property), and threading.md asks clients -- not servers --
    to "render threads differently ... keeping the full conversation history
    behind a disclosure".  So the pinned reference returns the thread root
    *and* its replies in `/messages`: the timeline is not partitioned by the
    transport.  What *is* partitioned is notification accounting, which
    receipts.md defines in terms of a "main timeline" that excludes events in a
    thread -- pinned here as the room's main `unread_notifications` counting
    only the two non-threaded messages.  (See thr/unread-thread-notifications
    for the per-thread side of the same split.)
    """
    author = register(c, "tmt1")
    reader = register(c, "tmt2")
    room = create_room(c, author, room_version=V, preset="public_chat")
    join(c, reader, room)
    since = sync_token(c, reader, filt=THREAD_COUNT_FILTER)

    root = send(c, author, room, "root", {"msgtype": "m.text", "body": "ROOT"},
                check=False)
    thread_reply(c, author, room, "r0", "R0", root)
    thread_reply(c, author, room, "r1", "R1", root)
    plain = send(c, author, room, "plain", {"msgtype": "m.text", "body": "PLAIN"},
                 check=False)
    # `thread_reply` waits for its own indexing, but a PLAIN send has no
    # such wait and the scored /sync below counts notifications. If the last
    # threaded reply is not yet attributed to its thread when /sync runs, it is
    # counted into the MAIN timeline instead -- main 3 rather than 2, per-thread
    # 1 rather than 2, exactly one event on the wrong side of the split.
    _await_event(c, author, room, plain)

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=reader.token,
          query={"dir": "b", "limit": 20},
          label="/messages carries the root and its thread replies alike",
          derive=lambda b: [e.get("content", {}).get("body")
                            for e in b["chunk"]
                            if e.get("type") == "m.room.message"])
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=reader.token,
          query={"dir": "b", "limit": 20},
          label="...with the thread relationship intact on each reply",
          derive=lambda b: [[e.get("content", {}).get("body"),
                             relates_to(e).get("rel_type")]
                            for e in b["chunk"]
                            if e.get("type") == "m.room.message"])
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=reader.token,
          query={"dir": "b", "limit": 20},
          label="...and the root, not the replies, carrying the bundle",
          derive=lambda b: [[e.get("content", {}).get("body"),
                             thread_bundle(e)["bundled"]]
                            for e in b["chunk"]
                            if e.get("type") == "m.room.message"])
    c.req("GET", f"{CS}/sync", token=reader.token,
          query={"timeout": 0, "since": since, "filter": THREAD_COUNT_FILTER},
          label="the main-timeline count excludes the two threaded replies",
          derive=lambda b, _r=room, _t={root: "ROOT"}: notif_counts(b, _r, _t))


@scenario("thr/thread-reply-falling-back", tags=["threads"], step=4)
def thread_reply_falling_back(c):
    """threading.md "Fallback for unthreaded clients": a thread reply may merge
    the rich-reply structure into `m.relates_to` -- an `m.in_reply_to` naming
    the most recent message known in the thread, plus `is_falling_back: true`.
    The event is still an ordinary thread child: it aggregates onto the root
    like any other, and the merged relationship round-trips untouched.  The
    same shape with `is_falling_back` absent (the flag "defaults to false when
    not provided") is a genuine reply *within* the thread.
    """
    author = register(c, "trf1")
    other = register(c, "trf2")
    who = {author.user_id: "author", other.user_id: "other"}
    room = create_room(c, author, room_version=V, preset="public_chat")
    join(c, other, room)
    root = send(c, author, room, "root", {"msgtype": "m.text", "body": "ROOT"},
                check=False)
    first = thread_reply(c, other, room, "r0", "R0", root)

    fallback = thread_reply(c, other, room, "fb", "FB", root,
                            extra={"is_falling_back": True,
                                   "m.in_reply_to": {"event_id": first}})
    inner = thread_reply(c, author, room, "in", "IN", root,
                         extra={"m.in_reply_to": {"event_id": fallback}})

    names = {root: "ROOT", first: "R0", fallback: "FB", inner: "IN"}
    _await_children(c, author, room, root, 3)

    def merged(b, _n=names):
        rel = relates_to(b)
        return {
            "rel_type": rel.get("rel_type"),
            "points_at": label(_n, rel.get("event_id")),
            # "defaults to false when not provided", so writing the default
            # and leaving it out say the same thing.
            "is_falling_back": bool(rel.get("is_falling_back")),
            "in_reply_to": label(_n, (rel.get("m.in_reply_to") or {}).get("event_id")),
            "relates_to_keys": sorted(rel),
        }

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(fallback)}", token=author.token,
          label="the falling-back reply's merged m.relates_to round-trips",
          derive=merged)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(inner)}", token=author.token,
          label="a reply within the thread omits is_falling_back",
          derive=merged)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root)}", token=author.token,
          label="both still count as thread children on the root",
          derive=lambda b: thread_bundle(b, participants=who))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}/m.thread",
          token=author.token,
          label="...and both are listed as m.thread children",
          derive=lambda b: bodies(b["chunk"]))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(first)}", token=author.token,
          label="m.in_reply_to is not a rel_type, so it creates no child list",
          derive=lambda b: {"n": len(b["chunk"]), "keys": stable_keys(b)})


@scenario("thr/thread-cannot-nest", tags=["threads"], step=4)
def thread_cannot_nest(c):
    """threading.md "Validation of m.thread relationships": "Servers SHOULD
    reject client requests which attempt to start a thread off an event with a
    rel_type ... it should receive a HTTP 400 error response", and a specific
    error code is not available so `M_UNKNOWN` is used -- which this harness
    never compares, so only the status class is pinned.  Threading off a
    thread child, off an `m.replace` child, and off an event the server does
    Threading off a thread child and off an `m.replace` child are both that
    case and are scored; threading off an event the server has never seen is
    not that case and is issued unscored.  The root's bundle must be unmoved by
    the refusals.
    """
    author = register(c, "tcn1")
    who = {author.user_id: "author"}
    room = create_room(c, author, room_version=V, preset="public_chat")
    root = send(c, author, room, "root", {"msgtype": "m.text", "body": "ROOT"},
                check=False)
    child = thread_reply(c, author, room, "r0", "R0", root)
    replacement = edit(c, author, room, "e0", root, "* EDIT",
                       {"msgtype": "m.text", "body": "EDIT"})

    for target, name, txn in ((child, "an m.thread child", "n1"),
                              (replacement, "an m.replace child", "n2")):
        c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/{txn}",
              token=author.token,
              json_body={"msgtype": "m.text", "body": "NESTED",
                         "m.relates_to": {"rel_type": "m.thread",
                                          "event_id": target}},
              label=f"a thread cannot be started off {name}")
    # An event the server does not know is *not* what the release's validation
    # rule is about -- it is about starting a thread off an event that already
    # has a `rel_type` -- so the request is made (it must not wedge what
    # follows) and what happens to it is not scored.
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/n3",
          token=author.token,
          json_body={"msgtype": "m.text", "body": "NESTED",
                     "m.relates_to": {"rel_type": "m.thread",
                                      "event_id": "$nosuchevent"}},
          check=False, learn=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(root)}", token=author.token,
          label="the root's bundle is unmoved by the refusals",
          derive=lambda b: thread_bundle(b, participants=who))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(child)}/m.thread",
          token=author.token,
          label="...and the child has acquired no children of its own",
          derive=lambda b: {"n": len(b["chunk"]), "keys": stable_keys(b)})
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/n4",
          token=author.token,
          json_body={"msgtype": "m.text", "body": "SIBLING",
                     "m.relates_to": {"rel_type": "m.thread",
                                      "event_id": root}},
          label="a further child of the root itself is still accepted",
          derive=lambda b: {"event_id_returned":
                            isinstance(b.get("event_id"), str)})


@scenario("thr/threads-list", tags=["threads"], step=4)
def threads_list(c):
    """api/client-server/threads_list.yaml (added in v1.4):
    `GET /_matrix/client/v1/rooms/{roomId}/threads` "paginates over the thread
    roots in a room, ordered by the latest_event of each thread root in its
    bundle", with `include=all|participated`, `limit` and `from`.  Every
    returned root carries its bundled aggregation.
    """
    author = register(c, "tls1")
    other = register(c, "tls2")
    outsider = register(c, "tls3")
    room = create_room(c, author, room_version=V, preset="public_chat")
    join(c, other, room)

    first = send(c, author, room, "t1", {"msgtype": "m.text", "body": "T1"},
                 check=False)
    second = send(c, other, room, "t2", {"msgtype": "m.text", "body": "T2"},
                  check=False)
    thread_reply(c, author, room, "t1r", "T1-reply", first)
    thread_reply(c, other, room, "t2r", "T2-reply", second)
    send(c, author, room, "plain", {"msgtype": "m.text", "body": "PLAIN"},
         check=False)

    c.req("GET", f"{CS1}/rooms/{q(room)}/threads", token=author.token,
          label="every thread root in the room, newest thread first",
          derive=lambda b: {"roots": bodies(b["chunk"]), "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/threads", token=author.token,
          query={"include": "all"},
          label="include=all is the default",
          derive=lambda b: bodies(b["chunk"]))
    c.req("GET", f"{CS1}/rooms/{q(room)}/threads", token=author.token,
          query={"include": "participated"},
          label="include=participated keeps only the caller's own threads",
          derive=lambda b: bodies(b["chunk"]))
    c.req("GET", f"{CS1}/rooms/{q(room)}/threads", token=other.token,
          query={"include": "participated"},
          label="...which is a different set for the other member",
          derive=lambda b: bodies(b["chunk"]))
    c.req("GET", f"{CS1}/rooms/{q(room)}/threads", token=author.token,
          label="each returned root carries its bundled aggregation",
          derive=lambda b: [[e.get("content", {}).get("body"),
                             thread_bundle(e)["count"]] for e in b["chunk"]])
    page = c.req("GET", f"{CS1}/rooms/{q(room)}/threads", token=author.token,
                 query={"limit": 1},
                 label="limit=1 returns one root and a next_batch",
                 derive=lambda b: {"roots": bodies(b["chunk"]),
                                   "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/threads", token=author.token,
          query={"limit": 1, "from": page.json.get("next_batch")},
          label="...and the next page is the other root, with no further token",
          derive=lambda b: {"roots": bodies(b["chunk"]), "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/threads", token=outsider.token,
          label="a caller who is not in the room cannot list its threads")


# --------------------------------------------------------------------------- #
# Relations endpoints -- api/client-server/relations.yaml,
# client-server-api/_index.md "Forming relationships between events"
# --------------------------------------------------------------------------- #
def _seed_mixed_relations(c, user, room):
    """A parent with children of three rel_types and two event types."""
    parent = send(c, user, room, "parent",
                  {"msgtype": "m.text", "body": "PARENT"}, check=False)
    children = {
        "THREAD-1": thread_reply(c, user, room, "c1", "THREAD-1", parent),
        "NOTE": thread_reply(c, user, room, "c2", "NOTE", parent,
                             etype="com.example.note"),
        "THREAD-2": thread_reply(c, user, room, "c3", "THREAD-2", parent),
        "REFERENCE": send(c, user, room, "c4",
                          {"msgtype": "m.text", "body": "REFERENCE",
                           "m.relates_to": {"rel_type": "m.reference",
                                            "event_id": parent}}, check=False),
        "EDIT": edit(c, user, room, "c5", parent, "* EDIT",
                     {"msgtype": "m.text", "body": "EDIT"}),
    }
    return parent, children


@scenario("thr/relations-all-children", tags=["relations"], step=4)
def relations_all_children(c):
    """relations.yaml `GET /rooms/{roomId}/relations/{eventId}`: "Retrieve all
    of the child events for a given parent event", "ordered topologically
    most-recent first".  Every rel_type and every event type is included, and
    `chunk` is the only required property of the response.
    """
    user = register(c, "rac1")
    room = create_room(c, user, room_version=V, preset="public_chat")
    parent, _children = _seed_mixed_relations(c, user, room)

    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}", token=user.token,
          label="all children of the parent, most-recent first",
          derive=lambda b: {"children": bodies(b["chunk"]), "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}", token=user.token,
          label="...with every rel_type and event type represented",
          derive=lambda b: [[e.get("type"), relates_to(e).get("rel_type"),
                             e.get("content", {}).get("body")]
                            for e in b["chunk"]])
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}", token=user.token,
          label="...and every child naming the same parent",
          derive=lambda b, _p=parent: {
              "all_name_the_parent": all(
                  relates_to(e).get("event_id") == _p for e in b["chunk"]),
              "n": len(b["chunk"])})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}", token=user.token,
          query={"dir": "b"},
          label="dir=b is the documented default",
          derive=lambda b: bodies(b["chunk"]))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}", token=user.token,
          query={"dir": "f"},
          label="dir=f returns the same children in chronological order",
          derive=lambda b: bodies(b["chunk"]))


@scenario("thr/relations-paging", tags=["relations", "paging"], step=4)
def relations_paging(c):
    """relations.yaml: `limit` bounds one `chunk`; `next_batch` "means there
    are no more results to fetch" by its absence, and `prev_batch` marks a page
    that is not the first.  The pages of a `dir=b` walk must reassemble the
    single unpaged answer.
    """
    user = register(c, "rpg1")
    room = create_room(c, user, room_version=V, preset="public_chat")
    root = send(c, user, room, "root", {"msgtype": "m.text", "body": "ROOT"},
                check=False)
    for i in range(5):
        thread_reply(c, user, room, f"r{i}", f"R{i}", root)
    _await_children(c, user, room, root, 5)

    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}",
          token=user.token, query={"limit": 50},
          label="the unpaged answer",
          derive=lambda b: {"children": bodies(b["chunk"]), "keys": stable_keys(b)})
    page1 = c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}",
                  token=user.token, query={"dir": "b", "limit": 2},
                  label="first page of two",
                  derive=lambda b: {"children": bodies(b["chunk"]),
                                    "keys": stable_keys(b)})
    page2 = c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}",
                  token=user.token,
                  query={"dir": "b", "limit": 2,
                         "from": page1.json.get("next_batch")},
                  label="second page, which also carries a prev_batch",
                  derive=lambda b: {"children": bodies(b["chunk"]),
                                    "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}", token=user.token,
          query={"dir": "b", "limit": 2,
                 "from": page2.json.get("next_batch")},
          label="last page: no next_batch, so pagination stops",
          derive=lambda b: {"children": bodies(b["chunk"]),
                            "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}", token=user.token,
          query={"dir": "f", "limit": 3},
          label="a forward page of three",
          derive=lambda b: {"children": bodies(b["chunk"]),
                            "keys": stable_keys(b)})
    # `to`: "The pagination token to stop returning results at ... If none is
    # specified, results continue up to limit."  A walk bounded at the token the
    # first page ended on must not run past it.
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}", token=user.token,
          query={"dir": "b", "limit": 50,
                 "to": page1.json.get("next_batch")},
          label="a walk bounded by `to` stops where it was told to",
          derive=lambda b: {"children": bodies(b["chunk"]),
                            "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}", token=user.token,
          query={"dir": "b", "limit": 50, "from": page1.json.get("next_batch"),
                 "to": page2.json.get("next_batch")},
          label="...and one bounded at both ends returns what lies between",
          derive=lambda b: {"children": bodies(b["chunk"]),
                            "keys": stable_keys(b)})
    walked = bodies(page1.json["chunk"]) + bodies(page2.json["chunk"])
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}", token=user.token,
          query={"limit": 50},
          label="the paged walk is a prefix of the unpaged answer",
          derive=lambda b, _w=walked: bodies(b["chunk"])[:len(_w)] == _w)
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}", token=user.token,
          query={"limit": 50},
          label="...and every child appears exactly once across the walk",
          derive=lambda b: sorted(bodies(b["chunk"])))


@scenario("thr/relations-by-reltype", tags=["relations"], step=4)
def relations_by_reltype(c):
    """relations.yaml `GET .../relations/{eventId}/{relType}`: "which relate to
    the parent using the given relType".  `m.thread` (threading.md),
    `m.replace` (event_replacements.md) and `m.reference`
    (reference_relations.md) each select their own children, and a rel_type
    nothing used selects none.
    """
    user = register(c, "rbr1")
    room = create_room(c, user, room_version=V, preset="public_chat")
    parent, _children = _seed_mixed_relations(c, user, room)

    for rel_type in ("m.thread", "m.replace", "m.reference",
                     "com.example.unused"):
        c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}/{rel_type}",
              token=user.token,
              label=f"children related by {rel_type}",
              derive=lambda b: {"children": bodies(b["chunk"]),
                                "rel_types": sorted({relates_to(e).get("rel_type")
                                                     for e in b["chunk"]}),
                                "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}/m.thread",
          token=user.token, query={"dir": "f"},
          label="the same filter honours dir=f",
          derive=lambda b: bodies(b["chunk"]))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}/m.thread",
          token=user.token, query={"limit": 1},
          label="...and limit, with a next_batch",
          derive=lambda b: {"children": bodies(b["chunk"]),
                            "keys": stable_keys(b)})


@scenario("thr/relations-by-reltype-and-eventtype", tags=["relations"], step=4)
def relations_by_reltype_and_eventtype(c):
    """relations.yaml `GET .../relations/{eventId}/{relType}/{eventType}`:
    "which relate to the parent using the given relType and have the given
    eventType".  The two path filters compose, and an event type nothing in the
    thread used selects nothing even though the rel_type matches.
    """
    user = register(c, "rre1")
    room = create_room(c, user, room_version=V, preset="public_chat")
    parent, _children = _seed_mixed_relations(c, user, room)

    for rel_type, event_type in (("m.thread", "m.room.message"),
                                 ("m.thread", "com.example.note"),
                                 ("m.thread", "com.example.absent"),
                                 ("m.replace", "m.room.message"),
                                 ("m.reference", "com.example.note")):
        c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}"
                     f"/{rel_type}/{event_type}", token=user.token,
              label=f"{rel_type} children of type {event_type}",
              derive=lambda b: {"children": types_and_bodies(b["chunk"]),
                                "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(parent)}"
                 f"/m.thread/m.room.message", token=user.token,
          query={"dir": "f", "limit": 1},
          label="dir and limit apply to the two-filter form too",
          derive=lambda b: {"children": bodies(b["chunk"]), "keys": stable_keys(b)})


@scenario("thr/relations-empty-and-unknown", tags=["relations"], step=4)
def relations_empty_and_unknown(c):
    """relations.yaml: "If no events are pointing to the parent or the
    pagination yields no results, an empty chunk is returned" -- so an event
    with no children is a 200 with `chunk: []`, not an error.  A parent the
    server does not know is the documented 404.
    """
    user = register(c, "reu1")
    room = create_room(c, user, room_version=V, preset="public_chat")
    lonely = send(c, user, room, "lonely",
                  {"msgtype": "m.text", "body": "LONELY"}, check=False)

    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(lonely)}", token=user.token,
          label="an event with no relations: empty chunk, no tokens",
          derive=lambda b: {"n": len(b["chunk"]), "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(lonely)}/m.thread",
          token=user.token,
          label="...the same with a rel_type filter",
          derive=lambda b: {"n": len(b["chunk"]), "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(lonely)}"
                 f"/m.thread/m.room.message", token=user.token,
          label="...and with both filters",
          derive=lambda b: {"n": len(b["chunk"]), "keys": stable_keys(b)})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(lonely)}", token=user.token,
          label="...and no m.relations is bundled onto it",
          derive=lambda b: {"m_relations_present":
                            "m.relations" in (b.get("unsigned") or {})})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q('$nosuchevent')}",
          token=user.token,
          label="an unknown parent event")
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q('$nosuchevent')}/m.thread",
          token=user.token,
          label="...with a rel_type filter, likewise")


@scenario("thr/relations-outside-room", tags=["relations"], step=4)
def relations_outside_room(c):
    """relations.yaml documents one refusal for these endpoints: 404, for "the
    parent event was not found *or the user does not have permission to read
    this event*".  The pinned reference answers a non-member with 403
    M_FORBIDDEN instead, which is a different point in the same 4xx class: the
    published operation does not document 403 at all, so this harness compares
    only the class, and `errcode` is dropped here rather than pinning the
    reference's choice between two defensible answers.  A member of the room
    reading the same parent is the control.
    """
    owner = register(c, "ror1")
    member = register(c, "ror2")
    outsider = register(c, "ror3")
    room = create_room(c, owner, room_version=V, preset="public_chat")
    join(c, member, room)
    root = send(c, owner, room, "root", {"msgtype": "m.text", "body": "ROOT"},
                check=False)
    thread_reply(c, owner, room, "r0", "R0", root)

    private = create_room(c, owner, room_version=V, preset="private_chat")
    secret = send(c, owner, private, "s", {"msgtype": "m.text", "body": "SECRET"},
                  check=False)
    thread_reply(c, owner, private, "s0", "SECRET-REPLY", secret)

    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}", token=member.token,
          label="a member of the room can read the children",
          derive=lambda b: bodies(b["chunk"]))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(root)}",
          token=outsider.token, drop=("errcode",),
          label="a caller who is not in the (public) room cannot")
    c.req("GET", f"{CS1}/rooms/{q(private)}/relations/{q(secret)}",
          token=outsider.token, drop=("errcode",),
          label="...nor in an invite-only room")
    c.req("GET", f"{CS1}/rooms/{q(private)}/relations/{q(secret)}/m.thread",
          token=outsider.token, drop=("errcode",),
          label="...with a rel_type filter, likewise")
    c.req("GET", f"{CS1}/rooms/{q(private)}/relations/{q(secret)}",
          token=owner.token,
          label="the room's own member still reads them",
          derive=lambda b: bodies(b["chunk"]))


# --------------------------------------------------------------------------- #
# Event replacements -- client-server-api/modules/event_replacements.md
# --------------------------------------------------------------------------- #
@scenario("thr/edit-replace-aggregation", tags=["relations", "edits"], step=4)
def edit_replace_aggregation(c):
    """event_replacements.md: a replacement carries
    `m.relates_to: {rel_type: m.replace, ...}` plus `m.new_content`, and the
    server aggregates it under `unsigned.m.relations.m.replace`.  "Server
    implementations must not actually overwrite the original event's content",
    and the `GET /_matrix/client/v3/rooms/{roomId}/event/{eventId}` exception
    says that endpoint "should return the unmodified event (though the
    relationship should still be bundled)" -- which is what is pinned here.
    """
    author = register(c, "era1")
    who = {author.user_id: "author"}
    room = create_room(c, author, room_version=V, preset="public_chat")
    original = send(c, author, room, "orig",
                    {"msgtype": "m.text", "body": "I really like cake",
                     "format": "org.matrix.custom.html",
                     "formatted_body": "I really like cake"}, check=False)

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=author.token,
          label="before the edit: content as sent, no bundle",
          derive=lambda b: {"content": b.get("content"),
                            "replace": replace_bundle(b, participants=who)})

    replacement = edit(c, author, room, "ed1", original,
                       "* I really like chocolate cake",
                       {"msgtype": "m.text",
                        "body": "I really like chocolate cake",
                        "com.example.extension_property": "chocolate"})

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=author.token,
          label="the original's content is unchanged in place",
          derive=lambda b: b.get("content"))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=author.token,
          label="...and the m.replace aggregation is bundled onto it",
          derive=lambda b: replace_bundle(b, participants=who))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(replacement)}",
          token=author.token,
          label="the replacement event itself carries the fallback and m.new_content",
          derive=lambda b: {"content": b.get("content"),
                            "rel_type": relates_to(b).get("rel_type"),
                            "points_at_original":
                                relates_to(b).get("event_id") == original})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(original)}/m.replace",
          token=author.token,
          label="the replacement is listed as an m.replace child",
          derive=lambda b: {"children": bodies(b["chunk"]),
                            "new_contents": [e.get("content", {}).get("m.new_content")
                                             for e in b["chunk"]]})
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(replacement)}",
          token=author.token,
          label="...and has itself acquired no children",
          derive=lambda b: {"n": len(b["chunk"]), "keys": stable_keys(b)})


@scenario("thr/edit-latest-wins", tags=["relations", "edits"], step=4)
def edit_latest_wins(c):
    """event_replacements.md: "there can be multiple events with an m.replace
    relationship to a given event ... The aggregation format of m.replace
    relationships gives the event_id, origin_server_ts, and sender of the most
    recent replacement event."  Every edit stays listed by
    `/relations/{eventId}/m.replace`; only the newest is bundled.
    """
    author = register(c, "elw1")
    who = {author.user_id: "author"}
    room = create_room(c, author, room_version=V, preset="public_chat")
    original = send(c, author, room, "orig",
                    {"msgtype": "m.text", "body": "ORIGINAL"}, check=False)

    edit(c, author, room, "ed1", original, "* FIRST",
         {"msgtype": "m.text", "body": "FIRST"})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=author.token,
          label="after one edit the bundle names it",
          derive=lambda b: replace_bundle(b, participants=who))

    edit(c, author, room, "ed2", original, "* SECOND",
         {"msgtype": "m.emote", "body": "SECOND"})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=author.token,
          label="after a second edit the newest one wins",
          derive=lambda b: replace_bundle(b, participants=who))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=author.token,
          label="...and the original's own content is still untouched",
          derive=lambda b: b.get("content"))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(original)}/m.replace",
          token=author.token,
          label="both edits remain listed, most-recent first",
          derive=lambda b: bodies(b["chunk"]))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(original)}/m.replace",
          token=author.token, query={"dir": "f"},
          label="...and in send order with dir=f",
          derive=lambda b: bodies(b["chunk"]))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(original)}"
                 f"/m.replace/m.room.message", token=author.token,
          label="a replacement may change msgtype, so both are m.room.message",
          derive=lambda b: [[e.get("type"),
                             e.get("content", {}).get("m.new_content", {}).get("msgtype")]
                            for e in b["chunk"]])


@scenario("thr/edit-invalid-not-aggregated", tags=["relations", "edits"], step=4)
def edit_invalid_not_aggregated(c):
    """event_replacements.md "Validity of replacement events": the original and
    the replacement "must have the same sender", and "the original event must
    not, itself, have a rel_type of m.replace (i.e. you cannot edit an edit)".
    "If any of these criteria are not satisfied, implementations should ignore
    the replacement event (the content of the original should not be replaced,
    and the edit should not be included in the server-side aggregation)."  The
    invalid replacements are still ordinary events, so they remain listed by
    the relations endpoint -- only the aggregation ignores them.
    """
    author = register(c, "ein1")
    other = register(c, "ein2")
    who = {author.user_id: "author", other.user_id: "other"}
    room = create_room(c, author, room_version=V, preset="public_chat")
    join(c, other, room)
    original = send(c, author, room, "orig",
                    {"msgtype": "m.text", "body": "ORIGINAL"}, check=False)
    first = edit(c, author, room, "ed1", original, "* VALID",
                 {"msgtype": "m.text", "body": "VALID"})

    edit(c, author, room, "ed2", first, "* EDIT-OF-EDIT",
         {"msgtype": "m.text", "body": "EDIT-OF-EDIT"})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(first)}", token=author.token,
          label="an edit of an edit is not aggregated onto the edit",
          derive=lambda b: replace_bundle(b, participants=who))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(first)}", token=author.token,
          label="...and the edit's own content is unchanged",
          derive=lambda b: b.get("content"))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=author.token,
          label="...nor does it displace the original's bundle",
          derive=lambda b: replace_bundle(b, participants=who))

    edit(c, other, room, "ed3", original, "* BY-SOMEONE-ELSE",
         {"msgtype": "m.text", "body": "BY-SOMEONE-ELSE"})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=author.token,
          label="an edit by a different sender is not aggregated",
          derive=lambda b: replace_bundle(b, participants=who))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=other.token,
          label="...and the other sender sees the same bundle",
          derive=lambda b: replace_bundle(b, participants=who))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(original)}", token=author.token,
          label="...with the original's content still in place",
          derive=lambda b: b.get("content"))
    c.req("GET", f"{CS1}/rooms/{q(room)}/relations/{q(original)}/m.replace",
          token=author.token,
          label="the ignored replacement is still an ordinary child event",
          derive=lambda b: [[e.get("content", {}).get("body"),
                             label(who, e.get("sender"))] for e in b["chunk"]])


# --------------------------------------------------------------------------- #
# Receipts -- client-server-api/modules/receipts.md, api/.../receipts.yaml
# --------------------------------------------------------------------------- #
@scenario("thr/receipt-private-invisible", tags=["receipts"], step=4)
def receipt_private_invisible(c):
    """receipts.md "Private read receipts" (added in v1.4): "Servers MUST NOT
    send the m.read.private receipt to any other user than the one which
    originally sent it", while it still clears the sender's own notification
    count.  A public `m.read` for the same position is, by contrast, delivered
    to the other member.
    """
    reader = register(c, "rpi1")
    talker = register(c, "rpi2")
    who = {reader.user_id: "reader", talker.user_id: "talker"}
    room = create_room(c, reader, room_version=V, preset="public_chat")
    join(c, talker, room)
    msgs = [send(c, talker, room, f"m{i}",
                 {"msgtype": "m.text", "body": f"M{i}"}, check=False)
            for i in range(4)]
    names = {eid: f"M{i}" for i, eid in enumerate(msgs)}
    reader_since = sync_token(c, reader)
    talker_since = sync_token(c, talker)

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read.private/{q(msgs[1])}",
          token=reader.token, json_body={},
          label="the reader posts a private read receipt")
    r = c.req("GET", f"{CS}/sync", token=reader.token,
              query={"timeout": 0, "since": reader_since},
              label="the reader sees their own private receipt",
              derive=lambda b, _r=room: receipts_in(b, _r, users=who,
                                                    events=names))
    reader_since = r.json["next_batch"]
    r = c.req("GET", f"{CS}/sync", token=talker.token,
              query={"timeout": 0, "since": talker_since},
              label="...and the other member sees nothing of it",
              derive=lambda b, _r=room: receipts_in(b, _r, users=who,
                                                    events=names))
    talker_since = r.json["next_batch"]
    c.req("GET", f"{CS}/sync", token=reader.token,
          query={"timeout": 0, "since": reader_since,
                 "filter": THREAD_COUNT_FILTER},
          label="the private receipt cleared the reader's own count",
          derive=lambda b, _r=room: notif_counts(b, _r))

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(msgs[3])}",
          token=reader.token, json_body={},
          label="the reader now posts a public read receipt")
    c.req("GET", f"{CS}/sync", token=talker.token,
          query={"timeout": 0, "since": talker_since},
          label="...which the other member does see",
          derive=lambda b, _r=room: receipts_in(b, _r, users=who, events=names))


@scenario("thr/receipt-private-ahead-of-public", tags=["receipts"], step=4)
def receipt_private_ahead_of_public(c):
    """receipts.md: "Between m.read and m.read.private, the receipt which is
    more 'ahead' or 'recent' is used when determining the highest read-up-to
    mark", and "If a client sends an m.read receipt which is 'behind' the
    m.read.private receipt ... the sending user will not have their
    notification counts rewound to that point".
    """
    reader = register(c, "rpa1")
    talker = register(c, "rpa2")
    room = create_room(c, reader, room_version=V, preset="public_chat")
    join(c, talker, room)
    msgs = [send(c, talker, room, f"m{i}",
                 {"msgtype": "m.text", "body": f"M{i}"}, check=False)
            for i in range(4)]
    since = sync_token(c, reader, filt=THREAD_COUNT_FILTER)

    def counts(label_text, since_token):
        return c.req("GET", f"{CS}/sync", token=reader.token,
                     query={"timeout": 0, "since": since_token,
                            "filter": THREAD_COUNT_FILTER},
                     label=label_text,
                     derive=lambda b, _r=room: notif_counts(b, _r))

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read.private/{q(msgs[2])}",
          token=reader.token, json_body={},
          label="a private receipt three messages in")
    r = counts("...leaves one message unread", since)
    since = r.json["next_batch"]
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(msgs[0])}",
          token=reader.token, json_body={},
          label="a public receipt behind the private one")
    r = counts("...does not rewind the count", since)
    since = r.json["next_batch"]
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(msgs[3])}",
          token=reader.token, json_body={},
          label="a public receipt ahead of the private one")
    counts("...clears the room", since)


@scenario("thr/receipt-threaded", tags=["receipts", "threads"], step=4)
def receipt_threaded(c):
    """receipts.md "Threaded read receipts" (added in v1.4): "A threaded read
    receipt is simply one which has a thread_id on it, targeting either a
    thread root's event ID or `main` for the main timeline", and each
    (user, receipt_type, thread_id) tuple is its own read-up-to marker, so a
    threaded receipt never replaces an unthreaded one and vice versa.  The
    `thread_id` is carried back in the `m.receipt` ephemeral event.
    """
    reader = register(c, "rth1")
    talker = register(c, "rth2")
    who = {reader.user_id: "reader", talker.user_id: "talker"}
    room = create_room(c, reader, room_version=V, preset="public_chat")
    join(c, talker, room)
    root = send(c, talker, room, "root", {"msgtype": "m.text", "body": "ROOT"},
                check=False)
    reply = thread_reply(c, talker, room, "r0", "R0", root)
    plain = send(c, talker, room, "plain",
                 {"msgtype": "m.text", "body": "PLAIN"}, check=False)
    names = {root: "ROOT", reply: "R0", plain: "PLAIN", "main": "main"}
    since = sync_token(c, talker)

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(reply)}",
          token=reader.token, json_body={"thread_id": root},
          label="a threaded receipt inside the thread")
    r = c.req("GET", f"{CS}/sync", token=talker.token,
              query={"timeout": 0, "since": since},
              label="...comes back with its thread_id",
              derive=lambda b, _r=room: receipts_in(b, _r, users=who,
                                                    events=names))
    since = r.json["next_batch"]

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(plain)}",
          token=reader.token, json_body={"thread_id": "main"},
          label="a threaded receipt on the main timeline")
    r = c.req("GET", f"{CS}/sync", token=talker.token,
              query={"timeout": 0, "since": since},
              label="...uses the reserved thread id `main`",
              derive=lambda b, _r=room: receipts_in(b, _r, users=who,
                                                    events=names))
    since = r.json["next_batch"]

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(root)}",
          token=reader.token, json_body={},
          label="an unthreaded receipt on the root")
    c.req("GET", f"{CS}/sync", token=talker.token,
          query={"timeout": 0, "since": since},
          label="...is a separate marker, carrying no thread_id",
          derive=lambda b, _r=room: receipts_in(b, _r, users=who, events=names))
    # A fresh *initial* sync, under a filter no earlier request in this
    # scenario used: the reference caches a /sync response by its request
    # parameters, so repeating an identical one would return the earlier answer
    # rather than the room's current ephemeral state.
    c.req("GET", f"{CS}/sync", token=talker.token,
          query={"timeout": 0, "filter": PLAIN_FILTER},
          label="all three markers coexist in the room's ephemeral state",
          derive=lambda b, _r=room: receipts_in(b, _r, users=who, events=names))


@scenario("thr/receipt-thread-id-invalid", tags=["receipts"], step=4)
def receipt_thread_id_invalid(c):
    """receipts.yaml documents 400 for a `thread_id` that "is not a string",
    "is empty", "is provided for an incompatible receipt type" or where "the
    event_id is not related to the thread_id", with `M_INVALID_PARAM` in the
    published example.  A valid threaded receipt on the same room is the
    control.
    """
    reader = register(c, "rti1")
    talker = register(c, "rti2")
    room = create_room(c, reader, room_version=V, preset="public_chat")
    join(c, talker, room)
    root = send(c, talker, room, "root", {"msgtype": "m.text", "body": "ROOT"},
                check=False)
    reply = thread_reply(c, talker, room, "r0", "R0", root)
    other_root = send(c, talker, room, "other",
                      {"msgtype": "m.text", "body": "OTHER"}, check=False)
    thread_reply(c, talker, room, "o0", "O0", other_root)

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(reply)}",
          token=reader.token, json_body={"thread_id": ""},
          label="an empty thread_id")
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(reply)}",
          token=reader.token, json_body={"thread_id": 5},
          label="a thread_id that is not a string")
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.fully_read/{q(reply)}",
          token=reader.token, json_body={"thread_id": "main"},
          label="a thread_id on an incompatible receipt type")
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(reply)}",
          token=reader.token, json_body={"thread_id": other_root},
          label="an event that is not related to the named thread")
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(reply)}",
          token=reader.token, json_body={"thread_id": root},
          label="the same event under its own thread is accepted")
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read.private/{q(reply)}",
          token=reader.token, json_body={"thread_id": root},
          label="...and so is the private form of it")


@scenario("thr/receipt-fully-read", tags=["receipts", "read-markers"], step=4)
def receipt_fully_read(c):
    """receipts.yaml: the receipt type "can also be m.fully_read as an
    alternative to /read_markers ... Note that m.fully_read does not appear
    under m.receipt: this endpoint effectively calls /read_markers internally".
    read_markers.yaml carries the same marker plus optional `m.read` and (from
    v1.4) `m.read.private` positions.  The marker lands in the room's account
    data, not in its ephemeral receipts.
    """
    reader = register(c, "rfr1")
    talker = register(c, "rfr2")
    who = {reader.user_id: "reader", talker.user_id: "talker"}
    room = create_room(c, reader, room_version=V, preset="public_chat")
    join(c, talker, room)
    msgs = [send(c, talker, room, f"m{i}",
                 {"msgtype": "m.text", "body": f"M{i}"}, check=False)
            for i in range(3)]
    names = {eid: f"M{i}" for i, eid in enumerate(msgs)}
    since = sync_token(c, reader)

    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.fully_read/{q(msgs[0])}",
          token=reader.token, json_body={},
          label="m.fully_read set through /receipt")
    r = c.req("GET", f"{CS}/sync", token=reader.token,
              query={"timeout": 0, "since": since},
              label="...lands in the room's account data",
              derive=lambda b, _r=room: room_account_data(b, _r))
    since = r.json["next_batch"]
    c.req("GET", f"{CS}/user/{q(reader.user_id)}/rooms/{q(room)}"
                 f"/account_data/m.fully_read", token=reader.token,
          label="...and names the event it was set to",
          derive=lambda b, _n=names: label(_n, b.get("event_id")))
    c.req("GET", f"{CS}/sync", token=talker.token, query={"timeout": 0},
          label="...while no m.receipt is produced for the other member",
          derive=lambda b, _r=room: receipts_in(b, _r, users=who, events=names))

    c.req("POST", f"{CS}/rooms/{q(room)}/read_markers", token=reader.token,
          json_body={"m.fully_read": msgs[1], "m.read": msgs[1],
                     "m.read.private": msgs[2]},
          label="/read_markers moves the marker and both receipts at once")
    c.req("GET", f"{CS}/user/{q(reader.user_id)}/rooms/{q(room)}"
                 f"/account_data/m.fully_read", token=reader.token,
          label="...the marker moved",
          derive=lambda b, _n=names: label(_n, b.get("event_id")))
    c.req("GET", f"{CS}/sync", token=reader.token,
          query={"timeout": 0, "since": since},
          label="...and both receipts were recorded for the sender",
          derive=lambda b, _r=room: receipts_in(b, _r, users=who, events=names))
    c.req("POST", f"{CS}/rooms/{q(room)}/read_markers", token=reader.token,
          json_body={"m.read": msgs[2]},
          label="m.fully_read is no longer required in a /read_markers body")


# --------------------------------------------------------------------------- #
# Per-thread notification counts -- sync.yaml `unread_thread_notifications`
# (added in v1.4), client-server-api/modules/push.md "Receiving notifications"
# --------------------------------------------------------------------------- #
@scenario("thr/unread-thread-notifications", tags=["threads", "sync",
                                                   "notifications"], step=4)
def unread_thread_notifications(c):
    """sync.yaml: with `unread_thread_notifications: true` on the
    `RoomEventFilter`, `unread_notifications` counts "only ... the main
    timeline rather than all events in the room" and
    `unread_thread_notifications` carries "the notification counts for each
    thread in this room ... keyed by thread root ID".  push.md: "the server is
    to partition the notification count to each thread (with the main timeline
    being its own thread)".
    """
    reader = register(c, "utn1")
    talker = register(c, "utn2")
    room = create_room(c, reader, room_version=V, preset="public_chat")
    join(c, talker, room)
    since = sync_token(c, reader, filt=THREAD_COUNT_FILTER)

    root_a = send(c, talker, room, "ta", {"msgtype": "m.text", "body": "ROOT-A"},
                  check=False)
    root_b = send(c, talker, room, "tb", {"msgtype": "m.text", "body": "ROOT-B"},
                  check=False)
    roots = {root_a: "thread-A", root_b: "thread-B"}

    def counts(text, since_token):
        return c.req("GET", f"{CS}/sync", token=reader.token,
                     query={"timeout": 0, "since": since_token,
                            "filter": THREAD_COUNT_FILTER},
                     label=text,
                     derive=lambda b, _r=room, _t=roots: notif_counts(b, _r, _t))

    r = counts("two plain messages: main counts two, no thread counts", since)
    since = r.json["next_batch"]
    thread_reply(c, talker, room, "a0", "A0", root_a)
    r = counts("a reply in thread A: main unmoved, thread A at one", since)
    since = r.json["next_batch"]
    thread_reply(c, talker, room, "a1", "A1", root_a)
    thread_reply(c, talker, room, "b0", "B0", root_b)
    r = counts("more replies: each thread counted separately", since)
    since = r.json["next_batch"]
    send(c, talker, room, "plain", {"msgtype": "m.text", "body": "PLAIN"},
         check=False)
    r = counts("a plain message moves only the main count", since)
    since = r.json["next_batch"]
    c.req("GET", f"{CS}/sync", token=talker.token,
          query={"timeout": 0, "filter": THREAD_COUNT_FILTER},
          label="the sender of every event has nothing unread anywhere",
          derive=lambda b, _r=room, _t=roots: notif_counts(b, _r, _t))


@scenario("thr/unread-thread-notifications-receipts",
          tags=["threads", "sync", "notifications", "receipts"], step=4)
def unread_thread_notifications_receipts(c):
    """receipts.md: "A threaded read receipt on I would mark A, B, and I as
    read ... Note that marking A as read with a threaded read receipt would not
    mean that C, E, G, or H get marked as read."  So a receipt carrying a
    thread root's id clears that thread's count alone, a `main` receipt clears
    the main timeline alone, and an unthreaded receipt clears the room.
    """
    reader = register(c, "utr1")
    talker = register(c, "utr2")
    room = create_room(c, reader, room_version=V, preset="public_chat")
    join(c, talker, room)
    since = sync_token(c, reader, filt=THREAD_COUNT_FILTER)

    root_a = send(c, talker, room, "ta", {"msgtype": "m.text", "body": "ROOT-A"},
                  check=False)
    root_b = send(c, talker, room, "tb", {"msgtype": "m.text", "body": "ROOT-B"},
                  check=False)
    roots = {root_a: "thread-A", root_b: "thread-B"}
    last_a = None
    for i in range(2):
        last_a = thread_reply(c, talker, room, f"a{i}", f"A{i}", root_a)
    last_b = thread_reply(c, talker, room, "b0", "B0", root_b)
    plain = send(c, talker, room, "plain",
                 {"msgtype": "m.text", "body": "PLAIN"}, check=False)

    def counts(text, since_token):
        return c.req("GET", f"{CS}/sync", token=reader.token,
                     query={"timeout": 0, "since": since_token,
                            "filter": THREAD_COUNT_FILTER},
                     label=text,
                     derive=lambda b, _r=room, _t=roots: notif_counts(b, _r, _t))

    r = counts("three unread streams: main, thread A, thread B", since)
    since = r.json["next_batch"]
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(last_a)}",
          token=reader.token, json_body={"thread_id": root_a},
          label="a threaded receipt at the end of thread A")
    r = counts("...clears thread A and nothing else", since)
    since = r.json["next_batch"]
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read.private/{q(plain)}",
          token=reader.token, json_body={"thread_id": "main"},
          label="a private threaded receipt on the main timeline")
    r = counts("...clears the main count, leaving thread B", since)
    since = r.json["next_batch"]
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(last_b)}",
          token=reader.token, json_body={"thread_id": root_b},
          label="a threaded receipt at the end of thread B")
    counts("...and now nothing is unread anywhere", since)


@scenario("thr/unread-thread-notifications-filter",
          tags=["threads", "sync", "notifications", "filters"], step=4)
def unread_thread_notifications_filter(c):
    """room_event_filter.yaml: `unread_thread_notifications` "Only applies to
    the /sync endpoint. Defaults to false."  Without it the room's
    `unread_notifications` covers every unread event in the room, threaded or
    not, and no per-thread object is returned; with it the same room's counts
    are split.  Both syncs cover exactly the same events.
    """
    reader = register(c, "utf1")
    talker = register(c, "utf2")
    room = create_room(c, reader, room_version=V, preset="public_chat")
    join(c, talker, room)
    plain_since = sync_token(c, reader, filt=PLAIN_FILTER)
    thread_since = sync_token(c, reader, filt=THREAD_COUNT_FILTER)

    root = send(c, talker, room, "root", {"msgtype": "m.text", "body": "ROOT"},
                check=False)
    roots = {root: "thread"}
    for i in range(3):
        thread_reply(c, talker, room, f"r{i}", f"R{i}", root)
    send(c, talker, room, "plain", {"msgtype": "m.text", "body": "PLAIN"},
         check=False)

    c.req("GET", f"{CS}/sync", token=reader.token,
          query={"timeout": 0, "since": plain_since, "filter": PLAIN_FILTER},
          label="without the flag the room count covers everything unread",
          derive=lambda b, _r=room, _t=roots: notif_counts(b, _r, _t))
    c.req("GET", f"{CS}/sync", token=reader.token,
          query={"timeout": 0, "since": thread_since,
                 "filter": THREAD_COUNT_FILTER},
          label="with the flag the same events are split main/thread",
          derive=lambda b, _r=room, _t=roots: notif_counts(b, _r, _t))
    c.req("GET", f"{CS}/sync", token=reader.token,
          query={"timeout": 0, "since": plain_since, "filter": PLAIN_FILTER},
          label="the unflagged sync still delivers the threaded events",
          derive=lambda b, _r=room: sorted(
              (e.get("content") or {}).get("body")
              for e in (sync_room(b, _r).get("timeline") or {}).get("events", [])
              if e.get("type") == "m.room.message"))
    c.req("GET", f"{CS}/sync", token=reader.token,
          query={"timeout": 0, "since": thread_since,
                 "filter": THREAD_COUNT_FILTER},
          label="...and so does the flagged one",
          derive=lambda b, _r=room: sorted(
              (e.get("content") or {}).get("body")
              for e in (sync_room(b, _r).get("timeline") or {}).get("events", [])
              if e.get("type") == "m.room.message"))


# --------------------------------------------------------------------------- #
# Spaces -- client-server-api/modules/spaces.md,
# api/client-server/space_hierarchy.yaml
# --------------------------------------------------------------------------- #
def space_child(c, user, space, child, server_name, **extra):
    content = {"via": [server_name]}
    content.update(extra)
    return set_state(c, user, space, "m.space.child", content,
                     state_key=child, check=False)


def hierarchy_entries(body, names):
    """The hierarchy response reduced to labels and declared properties.

    Note the key names used here: `mxconf/normalize.py` sorts this operation's
    `rooms` array and any array whose trailing path component is `room_types`,
    so a derived body must not reuse those names or an ordering this module
    means to pin would be silently sorted away.
    """
    out = []
    for entry in body["rooms"]:
        out.append({
            "room": label(names, entry.get("room_id")),
            "type": entry.get("room_type"),
            "name": entry.get("name"),
            "num_joined_members": entry.get("num_joined_members"),
            "join_rule": entry.get("join_rule"),
            "world_readable": entry.get("world_readable"),
            "guest_can_join": entry.get("guest_can_join"),
            "children": sorted(
                [label(names, child.get("state_key"))
                 for child in entry.get("children_state", [])]),
            "child_event_types": sorted(
                {child.get("type") for child in entry.get("children_state", [])}),
            "children_have_ts": all(
                isinstance(child.get("origin_server_ts"), int)
                for child in entry.get("children_state", [])),
        })
    # `next_batch` is there when there is another page and absent when there is
    # not, so the *required* key is what is compared.
    return {"entries": out, "keys": stable_keys(body)}


@scenario("thr/space-create-and-state", tags=["spaces"], step=4)
def space_create_and_state(c):
    """spaces.md: "A space is defined by the m.space room type" -- a room
    created with `creation_content: {"type": "m.space"}` -- and the
    parent/child relationship is carried by `m.space.child` state in the space
    (state_key: the child's room id) and `m.space.parent` state in the child
    (state_key: the parent's room id), both with a `via` list.  Spaces are
    rooms, so ordinary state and membership work unchanged.
    """
    owner = register(c, "spc1")
    space = create_room(c, owner, room_version=V, preset="public_chat",
                        name="A Space", creation_content={"type": "m.space"})
    plain = create_room(c, owner, room_version=V, preset="public_chat",
                        name="A Room")
    names = {space: "space", plain: "child"}

    c.req("GET", f"{CS}/rooms/{q(space)}/state/m.room.create/", token=owner.token,
          label="the space's m.room.create carries type m.space",
          derive=lambda b: {"type": b.get("type"),
                            "room_version": b.get("room_version")})
    c.req("GET", f"{CS}/rooms/{q(plain)}/state/m.room.create/", token=owner.token,
          label="an ordinary room's create event has no type",
          derive=lambda b: {"type": b.get("type"),
                            "room_version": b.get("room_version")})

    space_child(c, owner, space, plain, c.server_name, suggested=True,
                order="aaa")
    set_state(c, owner, plain, "m.space.parent",
              {"via": [c.server_name], "canonical": True}, state_key=space,
              check=False)

    c.req("GET", f"{CS}/rooms/{q(space)}/state/m.space.child/{q(plain)}",
          token=owner.token,
          label="the m.space.child content in the space",
          derive=lambda b: {"via_is_a_list": isinstance(b.get("via"), list),
                            "via_len": len(b.get("via") or []),
                            "suggested": b.get("suggested"),
                            "order": b.get("order")})
    c.req("GET", f"{CS}/rooms/{q(plain)}/state/m.space.parent/{q(space)}",
          token=owner.token,
          label="the m.space.parent content in the child",
          derive=lambda b: {"via_is_a_list": isinstance(b.get("via"), list),
                            "canonical": b.get("canonical")})
    c.req("GET", f"{CS}/rooms/{q(space)}/state", token=owner.token,
          label="the space's state, with the child keyed by the child's room id",
          derive=lambda b, _n=names: sorted(
              [[e.get("type"), label(_n, e.get("state_key"))] for e in b
               if e.get("type") in ("m.space.child", "m.room.create")]))
    c.req("PUT", f"{CS}/rooms/{q(space)}/send/m.room.message/in-space",
          token=owner.token,
          json_body={"msgtype": "m.text", "body": "spaces are rooms"},
          label="a space is a room, so ordinary events can still be sent into it",
          derive=lambda b: {"event_id_returned":
                            isinstance(b.get("event_id"), str)})
    c.req("GET", f"{CS}/rooms/{q(space)}/state/m.space.child/{q(space)}",
          token=owner.token,
          label="a child entry that was never set is absent")


@scenario("thr/space-hierarchy", tags=["spaces", "hierarchy"], step=4)
def space_hierarchy(c):
    """space_hierarchy.yaml: `GET /_matrix/client/v1/rooms/{roomId}/hierarchy`
    "paginates over the space tree in a depth-first manner to locate child
    rooms of a given space", returning a `PublicRoomsChunk` per room plus
    `room_type` and `children_state` ("the m.space.child events of the
    space-room, represented as Stripped State Events with an added
    origin_server_ts key.  If the room is not a space-room, this should be
    empty").  spaces.md: "when it encounters another space as a child it
    recurses into that space before returning non-space children".
    """
    owner = register(c, "shy1")
    space = create_room(c, owner, room_version=V, preset="public_chat",
                        name="Root Space", creation_content={"type": "m.space"})
    subspace = create_room(c, owner, room_version=V, preset="public_chat",
                           name="Subspace",
                           creation_content={"type": "m.space"})
    first = create_room(c, owner, room_version=V, preset="public_chat",
                        name="First Child")
    second = create_room(c, owner, room_version=V, preset="public_chat",
                         name="Second Child")
    grandchild = create_room(c, owner, room_version=V, preset="public_chat",
                             name="Grandchild")
    names = {space: "space", subspace: "subspace", first: "first",
             second: "second", grandchild: "grandchild"}

    space_child(c, owner, space, first, c.server_name, order="aaa")
    space_child(c, owner, space, subspace, c.server_name, order="bbb")
    space_child(c, owner, space, second, c.server_name, order="ccc")
    space_child(c, owner, subspace, grandchild, c.server_name)

    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=owner.token,
          label="the whole tree, depth-first from the space",
          derive=lambda b, _n=names: {"tree": [label(_n, e.get("room_id"))
                                               for e in b["rooms"]],
                                      "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=owner.token,
          label="every entry, with its room_type and children_state",
          derive=lambda b, _n=names: hierarchy_entries(b, _n))
    c.req("GET", f"{CS1}/rooms/{q(subspace)}/hierarchy", token=owner.token,
          label="the subspace's own hierarchy is the subtree below it",
          derive=lambda b, _n=names: {"tree": [label(_n, e.get("room_id"))
                                               for e in b["rooms"]],
                                      "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(first)}/hierarchy", token=owner.token,
          label="an ordinary room's hierarchy is just itself, with no children",
          derive=lambda b, _n=names: hierarchy_entries(b, _n))
    space_child(c, owner, space, second, c.server_name)
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=owner.token,
          label="clearing a child's `order` leaves it in the tree",
          derive=lambda b, _n=names: sorted(label(_n, e.get("room_id"))
                                            for e in b["rooms"]))
    set_state(c, owner, space, "m.space.child", {}, state_key=second,
              check=False)
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=owner.token,
          label="clearing its `via` removes it: 'Child rooms can be removed "
                "from a space by omitting the via key'",
          derive=lambda b, _n=names: sorted(label(_n, e.get("room_id"))
                                            for e in b["rooms"]))


@scenario("thr/space-hierarchy-filters", tags=["spaces", "hierarchy", "paging"],
          step=4)
def space_hierarchy_filters(c):
    """space_hierarchy.yaml: `suggested_only` ("only consider suggested rooms.
    Suggested rooms are annotated in their m.space.child event contents"),
    `max_depth` ("how far to go into the space ... When reached, no further
    child rooms will be returned"), `limit` ("the maximum number of rooms to
    include per response") and `from` ("a pagination token from a previous
    result").
    """
    owner = register(c, "shf1")
    space = create_room(c, owner, room_version=V, preset="public_chat",
                        name="Filtered Space",
                        creation_content={"type": "m.space"})
    subspace = create_room(c, owner, room_version=V, preset="public_chat",
                           name="Filtered Subspace",
                           creation_content={"type": "m.space"})
    suggested = create_room(c, owner, room_version=V, preset="public_chat",
                            name="Suggested Child")
    ordinary = create_room(c, owner, room_version=V, preset="public_chat",
                           name="Ordinary Child")
    deep = create_room(c, owner, room_version=V, preset="public_chat",
                       name="Deep Child")
    names = {space: "space", subspace: "subspace", suggested: "suggested",
             ordinary: "ordinary", deep: "deep"}

    space_child(c, owner, space, suggested, c.server_name, suggested=True,
                order="aaa")
    space_child(c, owner, space, ordinary, c.server_name, order="bbb")
    space_child(c, owner, space, subspace, c.server_name, order="ccc")
    space_child(c, owner, subspace, deep, c.server_name)

    def tree(text, **query):
        return c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy",
                     token=owner.token, query=query, label=text,
                     derive=lambda b, _n=names: {
                         "tree": [label(_n, e.get("room_id"))
                                  for e in b["rooms"]],
                         "keys": stable_keys(b)})

    tree("no filters: the whole tree")
    tree("suggested_only keeps only the annotated child", suggested_only=True)
    tree("max_depth=0 stops at the space itself", max_depth=0)
    tree("max_depth=1 stops before the subspace's own child", max_depth=1)
    page1 = tree("limit=2 returns a page and a next_batch", limit=2)
    page2 = c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=owner.token,
                  query={"limit": 2, "from": page1.json.get("next_batch")},
                  label="the second page continues the same depth-first walk",
                  derive=lambda b, _n=names: {
                      "tree": [label(_n, e.get("room_id")) for e in b["rooms"]],
                      "keys": stable_keys(b)})
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=owner.token,
          query={"limit": 2, "from": page2.json.get("next_batch")},
          label="...and the last page has no next_batch",
          derive=lambda b, _n=names: {
              "tree": [label(_n, e.get("room_id")) for e in b["rooms"]],
              "keys": stable_keys(b)})


@scenario("thr/space-hierarchy-forbidden", tags=["spaces", "hierarchy"], step=4)
def space_hierarchy_forbidden(c):
    """space_hierarchy.yaml documents 403 for "the user cannot view or peek on
    the room", listing "the room is not set up for peeking" and "the room does
    not exist" among the reasons, with `M_FORBIDDEN` in the published example.
    A public space is peekable by a non-member; an invite-only one is not, and
    neither is a room id nobody ever created.
    """
    owner = register(c, "shb1")
    outsider = register(c, "shb2")
    public_space = create_room(c, owner, room_version=V, preset="public_chat",
                               name="Public Space",
                               creation_content={"type": "m.space"})
    public_child = create_room(c, owner, room_version=V, preset="public_chat",
                               name="Public Child")
    private_space = create_room(c, owner, room_version=V, preset="private_chat",
                                name="Private Space",
                                creation_content={"type": "m.space"})
    private_child = create_room(c, owner, room_version=V, preset="private_chat",
                                name="Private Child")
    names = {public_space: "public-space", public_child: "public-child",
             private_space: "private-space", private_child: "private-child"}
    space_child(c, owner, public_space, public_child, c.server_name)
    space_child(c, owner, private_space, private_child, c.server_name)

    # `/hierarchy` documents a 403 when "the user cannot view or peek" a room,
    # and a `public_chat` space is joinable without being world-readable -- so
    # whether a non-member may walk it is the server's reading.  What is scored
    # is that *if* it answers, it answers with the space and its child.
    r_out = c.req("GET", f"{CS1}/rooms/{q(public_space)}/hierarchy",
                  token=outsider.token, check=False)
    fact(c, {"answered_or_refused": r_out.status in (200, 403),
             "if_answered_it_holds_the_space_and_its_child":
                 r_out.status != 200 or sorted(
                     label(names, e.get("room_id"))
                     for e in (r_out.json or {}).get("rooms") or [])
                 == ["public-child", "public-space"]},
         label="a non-member asking for a public space", token=owner.token)
    c.req("GET", f"{CS1}/rooms/{q(private_space)}/hierarchy",
          token=outsider.token,
          label="...but not an invite-only one")
    c.req("GET", f"{CS1}/rooms/{q(private_space)}/hierarchy", token=owner.token,
          label="its own member may",
          derive=lambda b, _n=names: [label(_n, e.get("room_id"))
                                      for e in b["rooms"]])
    c.req("GET", f"{CS1}/rooms/{q('!nosuchroom:' + c.server_name)}/hierarchy",
          token=owner.token,
          label="a room that does not exist")
    c.req("GET", f"{CS1}/rooms/{q(public_space)}/hierarchy",
          label="and the endpoint needs an access token at all")
    c.req("GET", f"{CS1}/rooms/{q(public_space)}/hierarchy", token=outsider.token,
          query={"from": "not-a-real-pagination-token"},
          label="an unknown `from` token is refused")


# --------------------------------------------------------------------------- #
# Room directory -- api/client-server/list_public_rooms.yaml,
# definitions/public_rooms_chunk.yaml (`room_type`, added in v1.4)
# --------------------------------------------------------------------------- #
@scenario("thr/roomver-type-across-upgrade", tags=["spaces", "roomver"], step=4)
def roomver_type_across_upgrade(c):
    """`m.room.create`'s `type` survives `/upgrade`.

    create_room.yaml (v1.4) gives `creation_content.type`, and
    upgrade_room.yaml says the new room "replaces" the old one -- a space that
    is upgraded has to still be a space afterwards, or every client that found
    it through the directory or through a parent's hierarchy stops seeing it as
    one.
    """
    owner = register(c, "rtu1")
    space = create_room(c, owner, room_version="9", preset="public_chat",
                        visibility="public", name="Upgradable Space",
                        creation_content={"type": "m.space"})
    c.req("GET", f"{CS}/rooms/{q(space)}/state/m.room.create/", token=owner.token,
          label="the room starts out as a space",
          derive=lambda b: {"type": b.get("type"),
                            "room_version": b.get("room_version")})
    r = c.req("POST", f"{CS}/rooms/{q(space)}/upgrade", token=owner.token,
              json_body={"new_version": "10"},
              label="the upgrade is accepted",
              derive=lambda b: {"replacement_is_a_room_id":
                                isinstance(b.get("replacement_room"), str)
                                and b["replacement_room"].startswith("!")})
    new_room = (r.json or {}).get("replacement_room")
    if not new_room:
        raise ScenarioAbort("the upgrade returned no replacement room")
    c.req("GET", f"{CS}/rooms/{q(new_room)}/state/m.room.create/",
          token=owner.token,
          label="and the replacement is still a space, at the new version",
          derive=lambda b: {"type": b.get("type"),
                            "room_version": b.get("room_version"),
                            "predecessor_is_the_old_room":
                                ((b.get("predecessor") or {}).get("room_id")
                                 is not None)})
    # Whether the *old* room stays published after an upgrade is the server's
    # own housekeeping; what the release fixes is that a space is still a space
    # wherever it is listed.
    c.req("GET", f"{CS}/publicRooms", token=owner.token,
          label="the directory reports the type it had",
          derive=lambda b: {"every_listed_room_is_a_space": all(
              e.get("room_type") == "m.space" for e in b["chunk"]),
              "at_least_one_is_listed": bool(b["chunk"])})


@scenario("thr/directory-room-type", tags=["spaces", "directory"], step=4)
def directory_room_type(c):
    """public_rooms_chunk.yaml `room_type` (v1.4): "The type of room (from
    m.room.create), if any", and list_public_rooms.yaml's `filter.room_types`
    (v1.4): "An optional list of room types to search for.  To include rooms
    without a room type, specify null within this list.  When not specified,
    all applicable rooms (regardless of type) are returned."
    """
    owner = register(c, "drt1")
    space = create_room(c, owner, room_version=V, preset="public_chat",
                        visibility="public", name="Directory Space",
                        creation_content={"type": "m.space"})
    plain = create_room(c, owner, room_version=V, preset="public_chat",
                        visibility="public", name="Directory Room")
    custom = create_room(c, owner, room_version=V, preset="public_chat",
                         visibility="public", name="Directory Custom",
                         creation_content={"type": "com.example.custom"})
    names = {space: "space", plain: "plain", custom: "custom"}

    def listed(body, _n=names):
        return {
            "listed": sorted([[label(_n, e.get("room_id")), e.get("room_type")]
                              for e in body["chunk"]]),
            "keys": stable_keys(body),
        }

    c.req("GET", f"{CS}/publicRooms", token=owner.token,
          label="room_type is carried in the published directory",
          derive=listed)
    c.req("POST", f"{CS}/publicRooms", token=owner.token,
          json_body={"limit": 20},
          label="...and by the POST form with no filter",
          derive=listed)
    c.req("POST", f"{CS}/publicRooms", token=owner.token,
          json_body={"limit": 20, "filter": {"room_types": ["m.space"]}},
          label="room_types selects spaces only", derive=listed)
    c.req("POST", f"{CS}/publicRooms", token=owner.token,
          json_body={"limit": 20, "filter": {"room_types": [None]}},
          label="a null in the list means rooms without a type", derive=listed)
    c.req("POST", f"{CS}/publicRooms", token=owner.token,
          json_body={"limit": 20,
                     "filter": {"room_types": [None, "m.space"]}},
          label="...and the two can be combined", derive=listed)
    c.req("POST", f"{CS}/publicRooms", token=owner.token,
          json_body={"limit": 20,
                     "filter": {"room_types": ["com.example.custom"]}},
          label="an arbitrary room type is selectable too", derive=listed)
    c.req("POST", f"{CS}/publicRooms", token=owner.token,
          json_body={"limit": 20,
                     "filter": {"room_types": ["com.example.never-used"]}},
          label="a room type nothing uses selects nothing", derive=listed)
    c.req("POST", f"{CS}/publicRooms", token=owner.token,
          json_body={"limit": 20,
                     "filter": {"generic_search_term": "Directory Space",
                                "room_types": ["m.space"]}},
          label="the type filter composes with a search term", derive=listed)


# --------------------------------------------------------------------------- #
# Room versions 10 and 11
#
# SPEC/v1.6/rooms/ carries one page per room version up to v10 (added in spec
# release v1.5).  Room version 11 is NOT described by release v1.6 -- it
# arrives in a later release -- but the pinned reference implements it and
# advertises it as stable, so the v11 checks below pin *observed reference
# behaviour* and say so, rather than citing a v1.6 rule that does not exist.
# The v10 side of each comparison is cited from rooms/v10.md.
# --------------------------------------------------------------------------- #
def _creator_record(state, owner_id):
    """How a room's create event records who made it.

    rooms/v10.md keeps `content.creator` (its auth rule 1.4 rejects a create
    event without one); the pinned reference's room version 11 drops the
    property, leaving the event's own `sender` as the only record.
    """
    create = next(e for e in state if e.get("type") == "m.room.create")
    content = create.get("content") or {}
    return {
        "content_has_creator": "creator" in content,
        "content_creator_is_the_owner": content.get("creator") == owner_id,
        "sender_is_the_owner": create.get("sender") == owner_id,
        "state_key": create.get("state_key"),
    }


def content_keys(body):
    """Content keys of an event served by GET /rooms/{roomId}/event/{eventId}."""
    return sorted((body.get("content") or {}).keys())


def redaction_shape(body):
    """Where a redaction event's target id lives, and what survives redaction."""
    content = body.get("content") or {}
    return {
        "top_level_redacts_present": "redacts" in body,
        "content_redacts_present": "redacts" in content,
        "content_keys": sorted(content),
        "type": body.get("type"),
        "redacted_because_present":
            "redacted_because" in (body.get("unsigned") or {}),
    }


@scenario("thr/roomver-create-10", tags=["roomver", "create"], step=4)
def roomver_create_10(c):
    """rooms/v10.md builds on v9; its `m.room.create` still carries `creator`
    (auth rule 1.4: "If content has no creator property, reject").  The version
    is advertised through `GET /capabilities` and accepted as an explicit
    `room_version` on `POST /createRoom`.
    """
    owner = register(c, "rcv1")

    c.req("GET", f"{CS}/capabilities", token=owner.token,
          label="the room version is advertised as available",
          derive=lambda b: {
              v: b["capabilities"]["m.room_versions"]["available"].get(v)
              for v in ("9", "10")})

    for version in ("10",):
        room = create_room(c, owner, room_version=version, preset="public_chat",
                           name=f"Room v{version}")
        c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/",
              token=owner.token,
              label=f"v{version}: the create event's content",
              # `m.federate` "defaults to true if the key does not exist", so
              # writing it and leaving it out are the same statement; what the
              # version fixes is `room_version` and whether `creator` is there.
              derive=lambda b: {"room_version": b.get("room_version"),
                                "has_creator": "creator" in b,
                                "federate": b.get("m.federate", True)})
        c.req("GET", f"{CS}/rooms/{q(room)}/state", token=owner.token,
              label=f"v{version}: how the create event records who made the room",
              derive=lambda b, _o=owner.user_id: _creator_record(b, _o))
        message = send(c, owner, room, "m1",
                       {"msgtype": "m.text", "body": "hello"}, check=False)
        c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(message)}",
              token=owner.token,
              label=f"v{version}: an ordinary message round-trips",
              derive=lambda b: {"content": b.get("content"),
                                "type": b.get("type")})

    c.req("POST", f"{CS}/createRoom", token=owner.token,
          json_body={"preset": "public_chat",
                     "room_version": "not-a-real-version"},
          label="an unsupported room version is still refused")


def _power_level_integer_checks(c, owner, version):
    """The three payloads, at one room version."""
    room = create_room(c, owner, room_version=version, preset="public_chat")
    levels = c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
                   token=owner.token, check=False).json
    nobody = "@nobody:" + c.server_name

    for name, value in (("a numeral string ('50')", "50"),
                        ("a string that is not a numeral", "abc"),
                        ("a genuine JSON integer", 50)):
        body = dict(levels)
        body["users"] = dict(levels.get("users", {}))
        body["users"][nobody] = value
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
              token=owner.token, json_body=body,
              label=f"v{version}: users{{}} value is {name}",
              derive=lambda b: {"event_id_returned":
                                isinstance(b.get("event_id"), str)})

    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/"
                 f"{q('alternate')}", token=owner.token,
          json_body={"users_default": "0"},
          label=f"v{version}: a numeral string under an alternate state key",
          derive=lambda b: {"event_id_returned":
                            isinstance(b.get("event_id"), str)})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=owner.token,
          label=f"v{version}: the room's real power levels after all that",
          derive=lambda b, _n=nobody: {
              "nobody_level": (b.get("users") or {}).get(_n),
              "users_default": b.get("users_default"),
              "events_default": b.get("events_default"),
              "state_default": b.get("state_default")})


@scenario("thr/roomver-power-level-integers",
          tags=["roomver", "power-levels"], step=4)
def roomver_power_level_integers(c):
    """rooms/v10.md: "This room version additionally requires that values in
    the power levels event be integers and not string representations", where
    rooms/fragments/v1-stringy-power-levels.md (in force up to v9) says "each
    of the integer-valued properties within m.room.power_levels events can be
    encoded as strings instead of integers.  This includes the nested values
    within the events, notifications and users properties."

    Only room version 10 is scored here.  The *difference* between v9 and v10
    is not reachable through the Client-Server API on the pinned reference --
    its request-body JSON schema for
    `PUT /rooms/{roomId}/state/m.room.power_levels/{stateKey}` requires a true
    JSON integer and rejects a numeral string at v9 as well, before the
    room-version authorization rules are consulted -- and a v9 room that
    *accepts* `"50"` is doing what the release in force up to v9 says.  Scoring
    the v9 refusal would therefore fail a conforming server for conforming, so
    v9 is left alone and what is scored is v10's own rule: the three payloads,
    an alternate state key as well (to rule out the empty state key being
    special), and the room's real power levels read back to show the refusals
    changed nothing.  (`M_BAD_JSON` is not documented for this endpoint and is
    not a commonly-specified code, so the harness compares only the status
    class here, never the error code or its text.)
    """
    owner = register(c, "rpl1")
    _power_level_integer_checks(c, owner, "10")


@scenario("thr/roomver-redaction-target-location",
          tags=["roomver", "redaction"], step=4)
def roomver_redaction_target_location(c):
    """Room version 10's redaction target, which is a top-level property."""
    _redaction_target_checks(c, ("10",), "rtl1")


def _redaction_target_checks(c, versions, localpart):
    """Where a redaction names its target.

    Up to and including room version 10 (rooms/v10.md, whose event format is
    rooms/fragments/v4-event-format.md, unchanged since v3) the redaction's
    target is a top-level `redacts` property, which a client cannot set through
    `PUT /rooms/{roomId}/send/{eventType}/{txnId}` -- only
    `PUT /rooms/{roomId}/redact/{eventId}/{txnId}` can produce one.  In the
    pinned reference's room version 11 the target lives in `content.redacts`,
    so a redaction sent as an ordinary event *does* take effect.

    The v10 send is deliberately unscored setup: the pinned reference answers
    it with a 500, which is a defect in the oracle rather than a behaviour the
    specification defines, so only its *effect* (the target survives intact) is
    scored here.
    """
    owner = register(c, localpart)
    for version in versions:
        room = create_room(c, owner, room_version=version, preset="public_chat")
        target = send(c, owner, room, "t1",
                      {"msgtype": "m.text", "body": "TARGET"}, check=False)
        # Up to v10 a server may refuse this send or accept it as an event
        # that redacts nothing, so an accepting server's event id binds no
        # symbol (Ctx.req `learn`); from v11 it is a real redaction on every
        # conforming server and binds as usual.
        pre_v11 = version in ("1", "2", "3", "4", "5", "6", "7", "8", "9", "10")
        c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.redaction/raw",
              token=owner.token,
              json_body={"redacts": target, "reason": "sent as a plain event"},
              check=False, learn=not pre_v11)
        c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(target)}",
              token=owner.token,
              label=f"v{version}: after a redaction sent as an ordinary event",
              derive=lambda b: {"content": b.get("content"),
                                "redacted": not (b.get("content") or {}),
                                "redacted_because_present":
                                    "redacted_because" in (b.get("unsigned") or {})})

        second = send(c, owner, room, "t2",
                      {"msgtype": "m.text", "body": "SECOND"}, check=False)
        redaction = c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(second)}/tx1",
                          token=owner.token, json_body={"reason": "why"},
                          label=f"v{version}: the dedicated redaction endpoint",
                          derive=lambda b: {"event_id_returned":
                                            isinstance(b.get("event_id"), str)})
        redaction_id = redaction.json["event_id"]
        c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(second)}",
              token=owner.token,
              label=f"v{version}: ...strips the target's content",
              derive=lambda b: {"content": b.get("content"),
                                "redacted_because_present":
                                    "redacted_because" in (b.get("unsigned") or {})})
        # Where the target has to appear is the room version's business, and
        # only the place that version names is scored.  The pinned reference
        # writes `redacts` in *both* places in a v10 room -- a compatibility
        # habit of that implementation, not something rooms/v10.md asks for --
        # and it keeps the top-level copy in a v11 room for clients that
        # predate the move.  Scoring the copy would make the reference's habit
        # the answer, so each version is asked only for its own location.
        moved = version not in ("1", "2", "3", "4", "5", "6", "7", "8", "9", "10")
        c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(redaction_id)}",
              token=owner.token,
              label=f"v{version}: ...and the redaction names its target",
              derive=lambda b, moved=moved: {
                  "redacts_names_target_where_this_version_puts_it":
                      ((b.get("content") or {}).get("redacts") if moved
                       else b.get("redacts")) == second,
                  "the_reason_is_kept":
                      (b.get("content") or {}).get("reason") == "why"})


@scenario("thr/roomver-redaction-allow-list",
          tags=["roomver", "redaction"], step=4)
def roomver_redaction_allow_list(c):
    """Room version 10's redaction allow-list."""
    _redaction_allow_list_checks(c, ("10",), "ral1")


def _redaction_allow_list_checks(c, versions, localpart):
    """The redaction algorithm's content allow-list, v10 against v11.

    rooms/v10.md inherits rooms/fragments/v9-redactions.md, which allows
    `membership` and `join_authorised_via_users_server` on `m.room.member`;
    `join_rule` and `allow` on `m.room.join_rules`; `ban`, `events`,
    `events_default`, `kick`, `redact`, `state_default`, `users` and
    `users_default` on `m.room.power_levels`; and nothing at all on
    `m.room.redaction`, which is not a special-cased type there.

    The pinned reference's room version 11 widens that list: `m.room.member`
    additionally keeps `third_party_invite.signed`, `m.room.power_levels`
    additionally keeps `invite`, and `m.room.redaction` becomes a special-cased
    type keeping `redacts` (which is also where v11 puts the target -- see
    thr/roomver-redaction-target-location).  `m.room.join_rules` is unchanged
    between the two, and is included as the control.
    """
    owner = register(c, localpart)
    member = register(c, localpart + "b")
    for version in versions:
        room = create_room(c, owner, room_version=version, preset="public_chat")
        join(c, member, room)

        membership = set_state(
            c, owner, room, "m.room.member",
            {"membership": "join", "displayname": "junk",
             "avatar_url": "mxc://example.org/Junk",
             "third_party_invite": {"display_name": "d",
                                    "signed": {"mxid": owner.user_id,
                                               "token": "tok",
                                               "signatures": {}}},
             "com.example.junk": "x"},
            state_key=owner.user_id, check=False)
        rules = set_state(c, owner, room, "m.room.join_rules",
                          {"join_rule": "public", "allow": [],
                           "com.example.junk": "x"}, check=False)
        levels = c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
                       token=owner.token, check=False).json
        levels = dict(levels)
        levels["invite"] = 0
        levels["notifications"] = {"room": 50}
        levels["com.example.junk"] = "x"
        power = set_state(c, owner, room, "m.room.power_levels", levels,
                          check=False)
        victim = send(c, owner, room, "v1",
                      {"msgtype": "m.text", "body": "VICTIM"}, check=False)
        redaction = c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(victim)}/tx1",
                          token=owner.token, json_body={"reason": "why"},
                          check=False).json["event_id"]

        for eid, etype in ((membership, "m.room.member"),
                           (rules, "m.room.join_rules"),
                           (power, "m.room.power_levels")):
            c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(eid)}/tx-{etype}",
                  token=owner.token, json_body={}, check=False)
            c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(eid)}",
                  token=owner.token,
                  label=f"v{version}: {etype} content surviving redaction",
                  derive=content_keys)

        c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(redaction)}/tx-redaction",
              token=owner.token, json_body={}, check=False)
        c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(redaction)}",
              token=owner.token,
              label=f"v{version}: m.room.redaction after being redacted itself",
              derive=redaction_shape)
        c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(victim)}",
              token=owner.token,
              label=f"v{version}: the original victim stays redacted",
              derive=lambda b: {"content": b.get("content"),
                                "redacted_because_present":
                                    "redacted_because" in (b.get("unsigned") or {})})


# --------------------------------------------------------------------------- #
# Content repository -- client-server-api/modules/content_repo.md
# --------------------------------------------------------------------------- #
@scenario("thr/media-cross-origin-resource-policy", tags=["media"], step=4)
def media_cross_origin_resource_policy(c):
    """content_repo.md, added in v1.4: "The server SHOULD additionally provide
    `Cross-Origin-Resource-Policy: cross-origin` when serving content to allow
    (web) clients to access restricted APIs such as SharedArrayBuffer when
    interacting with the media repository."  The header is excluded from
    comparison at steps 1 and 2 (mxconf/steps.py) because the release those
    steps target predates it; step 4 compares it on a download and on a
    thumbnail -- the two places the release calls serving content.
    """
    owner = register(c, "mcp1")
    mxc = upload(c, owner, png(64, 48), "image/png", "pic.png")
    server, media_id = mxc_parts(mxc)

    c.req("GET", f"{MEDIA}/download/{q(server)}/{q(media_id)}", binary=True,
          label="a download carries Cross-Origin-Resource-Policy")
    c.req("GET", f"{MEDIA}/download/{q(server)}/{q(media_id)}/{q('other.png')}",
          binary=True,
          label="...and so does the named-file form of it")
    # "The exact file types which can be thumbnailed are not currently
    # specified", so which of the allowed formats a server encodes in is its
    # own choice: what is asked is that something decodable came back with the
    # header on it.
    def an_image(data):
        return {"looks_like_an_image": data[:8] == b"\x89PNG\r\n\x1a\n"
                or data[:2] == b"\xff\xd8"
                or data[:6] in (b"GIF87a", b"GIF89a")
                or (data[:4] == b"RIFF" and data[8:12] == b"WEBP")}

    # ...so only the two headers this scenario is about are recorded on a
    # thumbnail; `content-type` on a *download* is the type the content was
    # uploaded with and stays compared, but on a thumbnail it is the codec.
    thumb_headers = ("cross-origin-resource-policy", "access-control-allow-origin")
    c.req("GET", f"{MEDIA}/thumbnail/{q(server)}/{q(media_id)}",
          query={"width": 32, "height": 32, "method": "crop"}, binary=True,
          record_headers=thumb_headers,
          label="a thumbnail carries it too", derive=an_image)
    c.req("GET", f"{MEDIA}/thumbnail/{q(server)}/{q(media_id)}",
          query={"width": 96, "height": 96, "method": "scale"}, binary=True,
          record_headers=thumb_headers,
          label="...at another size and method", derive=an_image)
    # "when serving content": a download that finds nothing is not serving any,
    # and the release does not ask for the header there, so the answer is
    # scored without its headers.
    c.req("GET", f"{MEDIA}/download/{q(server)}/nosuchmediaid",
          record_headers=False, label="a download that finds nothing")


# --------------------------------------------------------------------------- #
# The federation form of the hierarchy, which a client cannot reach
# --------------------------------------------------------------------------- #
@scenario("thr/fed-space-hierarchy", tags=["spaces", "federation"], step=4,
          needs=("tls",))
def fed_space_hierarchy(c):
    """`GET /_matrix/federation/v1/hierarchy/{roomId}`.

    The client form of the hierarchy walks the whole tree; the federation form
    answers for *one* room -- "the requested room" plus "the stripped state of
    its children" -- and is how a server that holds none of a space's children
    builds the client's answer. No client request reaches it, so it is asked
    for directly, by a server.
    """
    from mxconf.fakefed import (FakeFederation, federation_url, pick_port,
                                port_hint)

    server = c.extra["server"]
    fake = FakeFederation(port=pick_port(port_hint(server) + 6))
    c.extra["fake"] = fake
    fake.start()
    c.syms.aliases[fake.server_name] = "<FAKE>"
    url, name = federation_url(server), server.server_name

    owner = register(c, "fsh1")
    space = create_room(c, owner, room_version=V, preset="public_chat",
                        name="Federated Space",
                        creation_content={"type": "m.space"})
    child = create_room(c, owner, room_version=V, preset="public_chat",
                        name="Federated Child")
    set_state(c, owner, space, "m.space.child", {"via": [name]}, child)
    remote = fake.user_id("remote")
    fake.send_join(url, name, space, remote, displayname="remote D")

    status, body = fake.request(url, name, "GET",
                                f"/_matrix/federation/v1/hierarchy/{q(space)}",
                                None)
    room = (body or {}).get("room") or {}
    children = (body or {}).get("children") or []
    inaccessible = (body or {}).get("inaccessible_children")
    fact(c, {
        "status": status,
        "the_room_is_the_one_asked_for": room.get("room_id") is not None,
        "it_says_it_is_a_space": room.get("room_type") == "m.space",
        "it_carries_its_children_state": bool(room.get("children_state")),
        "the_child_is_described": any(
            ch.get("name") == "Federated Child" for ch in children
            if isinstance(ch, dict)),
        "inaccessible_children_is_absent_or_a_list":
            inaccessible is None or isinstance(inaccessible, list),
    }, label="the federation hierarchy answers for one room",
       token=owner.token)

    status2, body2 = fake.request(
        url, name, "GET",
        f"/_matrix/federation/v1/hierarchy/{q('!nosuchroom:' + name)}", None)
    # The published schema's own example for this 404 is `M_NOT_FOUND` and the
    # reference answers `M_UNKNOWN`, so which code a server chooses here is not
    # something the release settles: the refusal itself is.
    fact(c, {"status_class": status2 // 100,
             "errcode_is_a_string": isinstance((body2 or {}).get("errcode"), str)},
         label="and refuses a room it does not have", token=owner.token)

# --------------------------------------------------------------------------- #
# The pre-flight, on the routes this milestone adds
# --------------------------------------------------------------------------- #
# Every step's ticket restates the release's CORS requirement -- every endpoint
# answers an `OPTIONS` request, and answering it performs none of the endpoint's
# own logic -- and step 1 scores it on the routes that existed then.  A route
# added later is a route that can be added without it, so each milestone asks
# the same two questions of its own.  Nothing here needs a room, a user or an
# upload to exist: that is the point of the second question.


@scenario("thr/options-preflight", tags=["cors"], step=4)
def thr_options_preflight(c):
    """`OPTIONS` on the endpoints this release adds."""
    empty = lambda b: {"body_carries_nothing_of_substance": not b}   # noqa: E731
    for path, method, label in (
            (f"{CS1}/rooms/{q('!nonexistentroom00000000000:' + c.server_name)}/threads", "GET",
          "the threads endpoint of a room that does not exist"),
            (f"{CS1}/rooms/{q('!nonexistentroom00000000000:' + c.server_name)}/relations/{q('$nonexistenteventaaaaaaaaaaaaaaaaaaaaaaaaaaa')}", "GET",
          "a relations path"),
            (f"{CS1}/rooms/{q('!nonexistentroom00000000000:' + c.server_name)}/hierarchy", "GET", "the hierarchy endpoint")
    ):
        c.req("OPTIONS", path,
              headers={"Origin": "https://example.invalid",
                       "Access-Control-Request-Method": method},
              label=f"OPTIONS preflight on {label}", derive=empty)
