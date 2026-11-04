"""Step 1 -- history: history_visibility, guest_access/join_rules, redaction,
/rooms/{roomId}/messages and /rooms/{roomId}/context/{eventId}, as Matrix
Client-Server API v1.1 defines them.
"""

import json

from mxconf.mx import CS, create_room, invite, join, ok, q, register, send, set_state
from mxconf.registry import scenario

V = "6"


def _f(d):
    return json.dumps(d, separators=(",", ":"), sort_keys=True)


def _find(events, event_id):
    for e in events or []:
        if e.get("event_id") == event_id:
            return e
    return None


def _anchor(c, user):
    """A `from` token positioned at 'now' for /messages and /context callers
    that need to bootstrap their own pagination chain, obtained via an
    unscored /sync (its shape is scored elsewhere; here it is only a token
    source)."""
    r = c.req("GET", f"{CS}/sync", token=user.token, query={"timeout": 0},
              check=False)
    return r.json["next_batch"]


# --------------------------------------------------------------------------- #
# History visibility
# --------------------------------------------------------------------------- #

@scenario("cs/history-shared-default-visible-to-later-joiner", tags=["history_visibility"], step=1)
def history_shared_default(c):
    a = register(c, "hvs-a")
    b = register(c, "hvs-b")
    room = create_room(c, a, room_version=V, preset="private_chat")
    mid = send(c, a, room, "m1", {"msgtype": "m.text", "body": "before b joins"})
    invite(c, a, room, b.user_id)
    join(c, b, room)

    def ev_view(bd):
        return {"event_id_matches": bd.get("event_id") == mid,
                "body": bd.get("content", {}).get("body")}

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=b.token,
          label="shared (default) visibility: later joiner reads a pre-join event",
          derive=ev_view)

    anchor = _anchor(c, b)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token,
          query={"dir": "b", "from": anchor, "limit": 10},
          label="shared (default) visibility: later joiner pages back through /messages",
          derive=lambda bd: {"found": _find(bd.get("chunk"), mid) is not None})

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(mid)}", token=b.token,
          label="shared (default) visibility: later joiner reads /context",
          derive=lambda bd: {"event_id_matches": (bd.get("event") or {}).get("event_id") == mid})


@scenario("cs/history-invited-visible-only-while-invited-or-after", tags=["history_visibility"], step=1)
def history_invited_setting(c):
    a = register(c, "hvi-a")
    b = register(c, "hvi-b")
    room = create_room(c, a, room_version=V, preset="private_chat")
    set_state(c, a, room, "m.room.history_visibility", {"history_visibility": "invited"})
    before_id = send(c, a, room, "m0", {"msgtype": "m.text", "body": "before invite"})
    invite(c, a, room, b.user_id)
    during_id = send(c, a, room, "m1", {"msgtype": "m.text", "body": "while invited"})
    join(c, b, room)
    after_id = send(c, a, room, "m2", {"msgtype": "m.text", "body": "after join"})

    def readable(event_id):
        r = c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(event_id)}", token=b.token,
                  check=False)
        return r.status

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(before_id)}", token=b.token,
          label="invited visibility: event sent before the invite is not visible")
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(during_id)}", token=b.token,
          label="invited visibility: event sent while invited is visible",
          derive=lambda bd: {"event_id_matches": bd.get("event_id") == during_id})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(after_id)}", token=b.token,
          label="invited visibility: event sent after joining is visible",
          derive=lambda bd: {"event_id_matches": bd.get("event_id") == after_id})


@scenario("cs/history-joined-strict-from-own-join-point", tags=["history_visibility"], step=1)
def history_joined_setting(c):
    a = register(c, "hvj-a")
    b = register(c, "hvj-b")
    room = create_room(c, a, room_version=V, preset="private_chat")
    set_state(c, a, room, "m.room.history_visibility", {"history_visibility": "joined"})
    before_id = send(c, a, room, "m0", {"msgtype": "m.text", "body": "before invite"})
    invite(c, a, room, b.user_id)
    during_id = send(c, a, room, "m1", {"msgtype": "m.text", "body": "while invited"})
    join(c, b, room)
    after_id = send(c, a, room, "m2", {"msgtype": "m.text", "body": "after join"})

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(before_id)}", token=b.token,
          label="joined visibility: event sent before the invite is not visible")
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(during_id)}", token=b.token,
          label="joined visibility: event sent while only invited is not visible either")
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(after_id)}", token=b.token,
          label="joined visibility: event sent after joining is visible",
          derive=lambda bd: {"event_id_matches": bd.get("event_id") == after_id})


@scenario("cs/history-world-readable-open-to-outsiders", tags=["history_visibility"], step=1)
def history_world_readable(c):
    a = register(c, "hvw-a")
    outsider = register(c, "hvw-out")
    room = create_room(c, a, room_version=V, preset="private_chat")
    set_state(c, a, room, "m.room.history_visibility", {"history_visibility": "world_readable"})
    mid = send(c, a, room, "m1", {"msgtype": "m.text", "body": "readable by anyone"})

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=outsider.token,
          label="world_readable: a user who never joined nor was invited can still read",
          derive=lambda bd: {"event_id_matches": bd.get("event_id") == mid})

    anchor = _anchor(c, a)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=outsider.token,
          query={"dir": "b", "from": anchor, "limit": 10},
          label="world_readable: an outsider can page through /messages",
          derive=lambda bd: {"found": _find(bd.get("chunk"), mid) is not None})

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(mid)}", token=outsider.token,
          label="world_readable: an outsider can read /context",
          derive=lambda bd: {"event_id_matches": (bd.get("event") or {}).get("event_id") == mid})


@scenario("cs/history-visibility-change-not-retroactive", tags=["history_visibility"], step=1)
def history_change_not_retroactive(c):
    a = register(c, "hvr-a")
    b = register(c, "hvr-b")
    room = create_room(c, a, room_version=V, preset="private_chat")
    early_id = send(c, a, room, "m0", {"msgtype": "m.text", "body": "shared era"})
    set_state(c, a, room, "m.room.history_visibility", {"history_visibility": "joined"})
    invite(c, a, room, b.user_id)
    join(c, b, room)

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.history_visibility", token=a.token,
          label="history_visibility now reads back as joined",
          derive=lambda bd: {"history_visibility": bd.get("history_visibility")})

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(early_id)}", token=b.token,
          label="visibility tightened to joined after the fact still lets a later "
                "joiner see the event that predates the tightening (per-event state, "
                "not retroactive)",
          derive=lambda bd: {"event_id_matches": bd.get("event_id") == early_id})

    anchor = _anchor(c, b)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token,
          query={"dir": "b", "from": anchor, "limit": 50},
          label="the pre-tightening event also appears via /messages for the later joiner",
          derive=lambda bd: {"found": _find(bd.get("chunk"), early_id) is not None})

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(early_id)}", token=b.token,
          label="the pre-tightening event is also reachable via /context for the later joiner",
          derive=lambda bd: {"event_id_matches": (bd.get("event") or {}).get("event_id") == early_id})


@scenario("cs/history-left-member-retains-access-up-to-departure", tags=["history_visibility"], step=1)
def history_left_member_retains(c):
    a = register(c, "hvl-a")
    b = register(c, "hvl-b")
    room = create_room(c, a, room_version=V, preset="private_chat")
    invite(c, a, room, b.user_id)
    join(c, b, room)
    seen_id = send(c, a, room, "m1", {"msgtype": "m.text", "body": "seen before leaving"})
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=b.token, json_body={}, check=False)
    after_leave_id = send(c, a, room, "m2", {"msgtype": "m.text", "body": "sent after b left"})

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(seen_id)}", token=b.token,
          label="a departed member still reads an event seen before leaving",
          derive=lambda bd: {"event_id_matches": bd.get("event_id") == seen_id})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(after_leave_id)}", token=b.token,
          label="a departed member cannot read an event sent after they left")

    anchor = _anchor(c, b)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=b.token,
          query={"dir": "b", "from": anchor, "limit": 50},
          label="via /messages, a departed member sees events up to departure but not after",
          derive=lambda bd: {
              "seen_before_leaving_found": _find(bd.get("chunk"), seen_id) is not None,
              "sent_after_leaving_found": _find(bd.get("chunk"), after_leave_id) is not None})

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(seen_id)}", token=b.token,
          label="a departed member can still use /context on an event seen before leaving",
          derive=lambda bd: {"event_id_matches": (bd.get("event") or {}).get("event_id") == seen_id})
    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(after_leave_id)}", token=b.token,
          label="a departed member cannot use /context on an event sent after they left")


# --------------------------------------------------------------------------- #
# join_rules / guest_access
# --------------------------------------------------------------------------- #

@scenario("cs/join-rules-public-anyone-can-join", tags=["join_rules"], step=1)
def join_rules_public(c):
    a = register(c, "jrp-a")
    b = register(c, "jrp-b")
    room = create_room(c, a, room_version=V, preset="public_chat")

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.join_rules", token=a.token,
          label="join_rules state is public",
          derive=lambda bd: {"join_rule": bd.get("join_rule")})

    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          label="a user with no invite joins a public room directly")

    def member_view(bd):
        return {"membership": bd.get("membership")}

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(b.user_id)}",
          token=a.token, label="the joiner's membership is now join", derive=member_view)


@scenario("cs/join-rules-invite-required-forbidden-then-succeeds", tags=["join_rules"], step=1)
def join_rules_invite(c):
    a = register(c, "jri-a")
    b = register(c, "jri-b")
    room = create_room(c, a, room_version=V, preset="private_chat")

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.join_rules", token=a.token,
          label="join_rules state is invite",
          derive=lambda bd: {"join_rule": bd.get("join_rule")})

    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          label="joining an invite-only room without an invite is forbidden")

    invite(c, a, room, b.user_id)
    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          label="the same user joins once invited")


@scenario("cs/guest-access-state-mechanics", tags=["guest_access"], step=1)
def guest_access_state_mechanics(c):
    # Guest registration is switched off server-wide for this harness (a
    # deployment choice), so guest_access is exercised here as room state:
    # its value round-trips, and changing it is gated like any other state
    # event by the state_default power level.
    a = register(c, "gam-a")
    b = register(c, "gam-b")
    room = create_room(c, a, room_version=V, preset="private_chat")
    invite(c, a, room, b.user_id)
    join(c, b, room)

    set_state(c, a, room, "m.room.guest_access", {"guest_access": "forbidden"})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.guest_access", token=a.token,
          label="guest_access explicitly set to forbidden",
          derive=lambda bd: {"guest_access": bd.get("guest_access")})

    set_state(c, a, room, "m.room.guest_access", {"guest_access": "can_join"})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.guest_access", token=a.token,
          label="guest_access changed to can_join",
          derive=lambda bd: {"guest_access": bd.get("guest_access")})

    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.guest_access",
          token=b.token, json_body={"guest_access": "forbidden"},
          label="a member without state_default power cannot change guest_access")


@scenario("cs/guest-access-does-not-relax-join-rules", tags=["guest_access", "join_rules"], step=1)
def guest_access_does_not_relax_join_rules(c):
    a = register(c, "gaj-a")
    b = register(c, "gaj-b")
    room = create_room(c, a, room_version=V, preset="private_chat")
    set_state(c, a, room, "m.room.guest_access", {"guest_access": "can_join"})

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.join_rules", token=a.token,
          label="join_rules remains invite even though guest_access allows guests",
          derive=lambda bd: {"join_rule": bd.get("join_rule")})

    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          label="a non-invited (non-guest) user is still forbidden: "
                "guest_access does not relax join_rules")

    invite(c, a, room, b.user_id)
    c.req("POST", f"{CS}/join/{q(room)}", token=b.token, json_body={},
          label="the same user joins once actually invited")


# --------------------------------------------------------------------------- #
# Redaction
# --------------------------------------------------------------------------- #

@scenario("cs/redact-own-message-readback", tags=["redact"], step=1)
def redact_own_message_readback(c):
    a = register(c, "rdo-a")
    room = create_room(c, a, room_version=V, preset="private_chat")
    mid = send(c, a, room, "m1", {"msgtype": "m.text", "body": "will be redacted"})
    anchor = _anchor(c, a)

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-own-redact",
          token=a.token, json_body={"reason": "cleanup"},
          label="redact own message")

    def ev_view(bd):
        u = bd.get("unsigned", {}) or {}
        rb = u.get("redacted_because", {}) or {}
        return {
            "content_stripped": bd.get("content") == {},
            "redacted_because_present": bool(rb),
            "redacted_because_type": rb.get("type"),
            "redacted_because_reason": rb.get("content", {}).get("reason"),
        }

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=a.token,
          label="event readback after redaction", derive=ev_view)

    def messages_view(bd):
        ev = _find(bd.get("chunk"), mid)
        return {
            "found": ev is not None,
            "content_stripped": (ev or {}).get("content") == {},
            "redacted_because_present": bool((ev or {}).get("unsigned", {})
                                             .get("redacted_because")),
        }

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": anchor, "limit": 10},
          label="messages readback after redaction", derive=messages_view)

    def context_view(bd):
        ev = bd.get("event") or {}
        return {
            "event_id_matches": ev.get("event_id") == mid,
            "content_stripped": ev.get("content") == {},
            "redacted_because_present": bool(ev.get("unsigned", {}).get("redacted_because")),
        }

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(mid)}", token=a.token,
          label="context readback after redaction", derive=context_view)


@scenario("cs/redact-other-with-power", tags=["redact"], step=1)
def redact_other_with_power(c):
    a = register(c, "rdp-a")
    b = register(c, "rdp-b")
    room = create_room(c, a, room_version=V, preset="private_chat")
    invite(c, a, room, b.user_id)
    join(c, b, room)
    mid = send(c, b, room, "m1", {"msgtype": "m.text", "body": "b's message"})

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-power-redact",
          token=a.token, json_body={"reason": "moderation"},
          label="creator (power >= redact level) redacts another member's message")

    def ev_view(bd):
        return {"content_stripped": bd.get("content") == {},
                "redacted_because_present": bool(bd.get("unsigned", {})
                                                 .get("redacted_because"))}

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=a.token,
          label="event readback (as redactor)", derive=ev_view)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=b.token,
          label="event readback (as original author)", derive=ev_view)

    def context_view(bd):
        ev = bd.get("event") or {}
        return {"content_stripped": ev.get("content") == {},
                "redacted_because_present": bool(ev.get("unsigned", {})
                                                 .get("redacted_because"))}

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(mid)}", token=b.token,
          label="context readback (as original author)", derive=context_view)


@scenario("cs/redact-other-without-power-forbidden", tags=["redact"], step=1)
def redact_other_without_power(c):
    a = register(c, "rdw-a")
    b = register(c, "rdw-b")
    cc = register(c, "rdw-c")
    room = create_room(c, a, room_version=V, preset="private_chat")
    invite(c, a, room, b.user_id)
    join(c, b, room)
    invite(c, a, room, cc.user_id)
    join(c, cc, room)
    mid = send(c, b, room, "m1", {"msgtype": "m.text", "body": "b's message"})

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-noauth-redact",
          token=cc.token, json_body={},
          label="member without the redact power level attempts to redact another's message")

    def ev_view(bd):
        return {"content_intact": bd.get("content", {}).get("body") == "b's message",
                "redacted_because_absent": "redacted_because" not in (bd.get("unsigned") or {})}

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=a.token,
          label="event is unaffected by the forbidden attempt", derive=ev_view)

    mid2 = send(c, cc, room, "m2", {"msgtype": "m.text", "body": "c's own message"})
    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid2)}/tx-self-redact",
          token=cc.token, json_body={},
          label="the same member redacts their own message instead")

    def ev_view2(bd):
        return {"content_stripped": bd.get("content") == {},
                "redacted_because_present": bool(bd.get("unsigned", {})
                                                 .get("redacted_because"))}

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid2)}", token=cc.token,
          label="own-message redaction (no power level needed) took effect",
          derive=ev_view2)


@scenario("cs/redact-state-event", tags=["redact"], step=1)
def redact_state_event(c):
    a = register(c, "rds-a")
    room = create_room(c, a, room_version=V, preset="private_chat")
    tid = set_state(c, a, room, "m.room.topic", {"topic": "hello world"})

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(tid)}/tx-state-redact",
          token=a.token, json_body={"reason": "oops"},
          label="redact a state event")

    def ev_view(bd):
        return {"content_stripped": bd.get("content") == {},
                "redacted_because_present": bool(bd.get("unsigned", {})
                                                 .get("redacted_because")),
                "type_preserved": bd.get("type") == "m.room.topic",
                "state_key_preserved": bd.get("state_key") == ""}

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(tid)}", token=a.token,
          label="event readback of the redacted state event", derive=ev_view)

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic", token=a.token,
          label="current-state readback of the redacted event",
          derive=lambda bd: {"content_stripped": bd == {}})

    anchor = _anchor(c, a)

    def messages_view(bd):
        ev = _find(bd.get("chunk"), tid)
        return {"found": ev is not None, "content_stripped": (ev or {}).get("content") == {}}

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": anchor, "limit": 10},
          label="messages readback of the redacted state event", derive=messages_view)


@scenario("cs/redact-already-redacted-event", tags=["redact"], step=1)
def redact_already_redacted(c):
    a = register(c, "rda-a")
    room = create_room(c, a, room_version=V, preset="private_chat")
    mid = send(c, a, room, "m1", {"msgtype": "m.text", "body": "double redact target"})

    r1 = c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-first-redact",
               token=a.token, json_body={"reason": "first pass"},
               label="redact an event for the first time")
    rid1 = r1.json["event_id"]

    r2 = c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-second-redact",
               token=a.token, json_body={"reason": "second pass"},
               label="redact the same, already-redacted event again")
    rid2 = r2.json["event_id"]

    def ev_view(bd):
        rb = bd.get("unsigned", {}).get("redacted_because", {}) or {}
        return {
            "content_stripped": bd.get("content") == {},
            "redacted_because_present": bool(rb),
            "redacted_because_is_one_of_the_two_redactions": rb.get("event_id") in (rid1, rid2),
        }

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=a.token,
          label="event stays stripped, pinned to one of the two redactions",
          derive=ev_view)

    anchor = _anchor(c, a)

    def messages_view(bd):
        ev = _find(bd.get("chunk"), mid)
        return {"found": ev is not None, "content_stripped": (ev or {}).get("content") == {}}

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": anchor, "limit": 10},
          label="messages readback stays stripped", derive=messages_view)


@scenario("cs/redact-unknown-and-forbidden-targets", tags=["redact"], step=1)
def redact_unknown_and_forbidden(c):
    a = register(c, "rdu-a")
    outsider = register(c, "rdu-out")
    room = create_room(c, a, room_version=V, preset="private_chat")
    mid = send(c, a, room, "m1", {"msgtype": "m.text", "body": "hi"})

    other_room = create_room(c, outsider, room_version=V, preset="private_chat")
    other_mid = send(c, outsider, other_room, "m2", {"msgtype": "m.text", "body": "elsewhere"})

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/$doesnotexist1234567890abcdef/tx-nx",
          token=a.token, json_body={}, label="redact a nonexistent event id")

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(other_mid)}/tx-wrongroom",
          token=a.token, json_body={},
          label="redact a real event that belongs to a different room")

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-notmember",
          token=outsider.token, json_body={},
          label="redact attempted by a user who is not in the room")

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/not-a-valid-event-id/tx-malformed",
          token=a.token, json_body={},
          label="redact a syntactically malformed event id")


@scenario("cs/redact-transaction-replay-idempotent", tags=["redact", "transactions"], step=1)
def redact_txn_replay(c):
    a = register(c, "rdt-a")
    room = create_room(c, a, room_version=V, preset="private_chat")
    mid = send(c, a, room, "m1", {"msgtype": "m.text", "body": "idempotent redact target"})

    r1 = c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-replay-1",
               token=a.token, json_body={"reason": "only take"},
               label="redact: first use of this transaction id")
    rid = r1.json["event_id"]

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-replay-1",
          token=a.token, json_body={"reason": "different reason, same txn"},
          label="redact: replay the same transaction id")

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-replay-1",
          token=a.token, json_body={},
          label="redact: replay the same transaction id a third time")

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=a.token,
          label="exactly one redaction event was ever created",
          derive=lambda bd: {"redacted_because_event_id_is_stable":
                             bd.get("unsigned", {}).get("redacted_because", {})
                             .get("event_id") == rid})


# --------------------------------------------------------------------------- #
# /rooms/{roomId}/messages
# --------------------------------------------------------------------------- #

@scenario("cs/messages-dir-back-and-forward", tags=["messages"], step=1)
def messages_dir_back_and_forward(c):
    a = register(c, "msg-a")
    room = create_room(c, a, room_version=V, preset="private_chat")
    send(c, a, room, "m1", {"msgtype": "m.text", "body": "one"})
    send(c, a, room, "m2", {"msgtype": "m.text", "body": "two"})
    send(c, a, room, "m3", {"msgtype": "m.text", "body": "three"})
    send(c, a, room, "m4", {"msgtype": "m.text", "body": "four"})
    now = _anchor(c, a)

    def ids(bd):
        return [e.get("event_id") for e in bd.get("chunk", [])]

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": now, "limit": 10},
          label="dir=b from now returns everything in reverse order",
          derive=lambda bd: {"order": ids(bd)})

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "f", "from": now, "limit": 10},
          label="dir=f from now returns nothing yet",
          derive=lambda bd: {"chunk_empty": bd.get("chunk") == []})

    r2 = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
               query={"dir": "b", "from": now, "limit": 2},
               label="dir=b with a small limit truncates",
               derive=lambda bd: {"order": ids(bd), "count": len(bd.get("chunk", []))})
    end2 = r2.json["end"]

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": end2, "limit": 10},
          label="continuing backward from the truncated page's end token",
          derive=lambda bd: {"order": ids(bd)})


@scenario("cs/messages-pagination-end-and-runs-off-history", tags=["messages"], step=1)
def messages_pagination_runs_off(c):
    a = register(c, "msgp-a")
    room = create_room(c, a, room_version=V, preset="private_chat")
    send(c, a, room, "m1", {"msgtype": "m.text", "body": "one"})
    send(c, a, room, "m2", {"msgtype": "m.text", "body": "two"})
    send(c, a, room, "m3", {"msgtype": "m.text", "body": "three"})
    anchor = _anchor(c, a)

    def ids(bd):
        return [e.get("event_id") for e in bd.get("chunk", [])]

    r1 = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
               query={"dir": "b", "from": anchor, "limit": 1},
               label="page 1 (limit=1)", derive=lambda bd: {"order": ids(bd)})
    end1 = r1.json["end"]

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": end1, "limit": 1},
          label="page 2, following the previous page's end token",
          derive=lambda bd: {"order": ids(bd)})

    # A limit large enough to exhaust the whole room's history in one page.
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": anchor, "limit": 1000},
          label="a limit larger than history returns the whole room in one page",
          derive=lambda bd: {"order": ids(bd)})

    # The release says `end` is omitted "if no further events are available".
    # A server is free to discover that either inside the request that drains
    # the room or only on the next one, so the chain here continues from page
    # 1's token -- which both readings agree exists -- and the omission is
    # measured on pages that are unambiguously empty.
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": end1, "limit": 1000},
          label="the rest of history, from page 1's end token, in one page",
          derive=lambda bd: {"order": ids(bd)})

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "f", "from": anchor, "limit": 1000},
          label="dir=f from now, over-large limit: nothing forward",
          derive=lambda bd: {
              "chunk_empty": bd.get("chunk") == [],
              # The release says `end` is omitted "if no further events are
              # available" and, in the same schema, that "an empty `chunk` does
              # not *necessarily* imply that no more events are available" --
              # so an empty page that still carries an `end` is explicitly
              # allowed, and when a server stops sending one is its own
              # business.  What is scored is the direction that does hold: a
              # page without an `end` is a page with nothing on it.
              "no_end_means_no_events": "end" in bd or bd.get("chunk") == []})


@scenario("cs/messages-with-filter", tags=["messages", "filter"], step=1)
def messages_with_filter(c):
    a = register(c, "msgf-a")
    b = register(c, "msgf-b")
    room = create_room(c, a, room_version=V, preset="private_chat")
    invite(c, a, room, b.user_id)
    join(c, b, room)
    send(c, a, room, "m1", {"msgtype": "m.text", "body": "one"})
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/org.example.custom/tx-custom",
          token=a.token, json_body={"foo": "bar"}, check=False)
    send(c, a, room, "m2", {"msgtype": "m.text", "body": "two"})
    send(c, b, room, "m3", {"msgtype": "m.text", "body": "three, from b"})
    anchor = _anchor(c, a)

    def types_of(bd):
        return sorted({e.get("type") for e in bd.get("chunk", [])})

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": anchor, "limit": 50,
                 "filter": _f({"types": ["org.example.custom"]})},
          label="filter: only the custom type",
          derive=lambda bd: {"types": types_of(bd)})

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": anchor, "limit": 50,
                 "filter": _f({"not_types": ["org.example.custom"]})},
          label="filter: excludes the custom type",
          derive=lambda bd: {"types": types_of(bd)})

    def senders_of(bd):
        return sorted({e.get("sender") for e in bd.get("chunk", [])})

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": anchor, "limit": 50,
                 "filter": _f({"senders": [a.user_id]})},
          label="filter: only sender a",
          derive=lambda bd: {"senders": senders_of(bd)})

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=a.token,
          query={"dir": "b", "from": anchor, "limit": 50,
                 "filter": _f({"not_senders": [a.user_id]})},
          label="filter: excludes sender a",
          derive=lambda bd: {"senders": senders_of(bd)})


# --------------------------------------------------------------------------- #
# /rooms/{roomId}/context/{eventId}
# --------------------------------------------------------------------------- #

@scenario("cs/context-limit-window", tags=["context"], step=1)
def context_limit_window(c):
    a = register(c, "ctxw-a")
    room = create_room(c, a, room_version=V, preset="private_chat")
    send(c, a, room, "m1", {"msgtype": "m.text", "body": "one"})
    send(c, a, room, "m2", {"msgtype": "m.text", "body": "two"})
    m3 = send(c, a, room, "m3", {"msgtype": "m.text", "body": "three"})
    send(c, a, room, "m4", {"msgtype": "m.text", "body": "four"})
    m5 = send(c, a, room, "m5", {"msgtype": "m.text", "body": "five"})

    def cview(bd):
        return {
            "event_id_matches": (bd.get("event") or {}).get("event_id") == m3,
            "before_count": len(bd.get("events_before", [])),
            "after_count": len(bd.get("events_after", [])),
            "state_has_create": any(e.get("type") == "m.room.create"
                                    for e in bd.get("state", [])),
        }

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(m3)}", token=a.token,
          query={"limit": 2}, label="context with a small limit window", derive=cview)

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(m3)}", token=a.token,
          query={"limit": 0},
          label="context with a zero limit window",
          derive=lambda bd: {"before_empty": bd.get("events_before") == [],
                             "after_empty": bd.get("events_after") == []})

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(m5)}", token=a.token,
          query={"limit": 10},
          label="context of the last event has nothing after it",
          derive=lambda bd: {"after_empty": bd.get("events_after") == [],
                             "event_id_matches": (bd.get("event") or {}).get("event_id") == m5})

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(m3)}", token=a.token,
          label="context with the default limit",
          derive=lambda bd: {"event_id_matches": (bd.get("event") or {}).get("event_id") == m3,
                             "has_before_and_after": bool(bd.get("events_before"))
                                 and bool(bd.get("events_after"))})


@scenario("cs/context-filter-scope", tags=["context", "filter"], step=1)
def context_filter_scope(c):
    a = register(c, "ctxf-a")
    room = create_room(c, a, room_version=V, preset="private_chat")
    send(c, a, room, "m1", {"msgtype": "m.text", "body": "before one"})
    send(c, a, room, "m2", {"msgtype": "m.text", "body": "before two"})
    r = c.req("PUT", f"{CS}/rooms/{q(room)}/send/org.example.custom/tx-custom",
              token=a.token, json_body={"foo": "bar"}, check=False)
    tid = r.json["event_id"]
    send(c, a, room, "m3", {"msgtype": "m.text", "body": "after one"})
    send(c, a, room, "m4", {"msgtype": "m.text", "body": "after two"})

    # Larger than the room's total event count on either side of tid, so that
    # whichever order a server applies filter/limit in (the spec explicitly
    # leaves this unspecified for /context), nothing is truncated and the two
    # orders necessarily agree.
    BIG = 50

    def types_ba(bd):
        return {
            "before_types": sorted({e.get("type") for e in bd.get("events_before", [])}),
            "after_types": sorted({e.get("type") for e in bd.get("events_after", [])}),
        }

    def target_view(bd):
        ev = bd.get("event") or {}
        out = {
            "event_id_matches": ev.get("event_id") == tid,
            "event_content_present": ev.get("content") == {"foo": "bar"},
        }
        out.update(types_ba(bd))
        return out

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(tid)}", token=a.token,
          query={"limit": BIG, "filter": _f({"not_types": ["m.room.message"]})},
          label="filter (not_types) applies to the neighbors but not to the "
                "event itself, whose own type is unaffected",
          derive=target_view)

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(tid)}", token=a.token,
          query={"limit": BIG},
          label="without a filter, message-type neighbors are present",
          derive=types_ba)

    def target_view2(bd):
        ev = bd.get("event") or {}
        out = {"event_id_matches": ev.get("event_id") == tid}
        out.update(types_ba(bd))
        return out

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(tid)}", token=a.token,
          query={"limit": BIG, "filter": _f({"types": ["m.room.message"]})},
          label="a positive types filter still leaves the event itself untouched",
          derive=target_view2)

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(tid)}", token=a.token,
          query={"limit": 10, "filter": "{not valid json"},
          label="a malformed inline filter is rejected")


@scenario("cs/context-unknown-event", tags=["context"], step=1)
def context_unknown_event(c):
    a = register(c, "ctxu-a")
    outsider = register(c, "ctxu-out")
    room = create_room(c, a, room_version=V, preset="private_chat")
    m1 = send(c, a, room, "m1", {"msgtype": "m.text", "body": "hi"})

    other_room = create_room(c, outsider, room_version=V, preset="private_chat")
    other_mid = send(c, outsider, other_room, "m2", {"msgtype": "m.text", "body": "elsewhere"})

    c.req("GET", f"{CS}/rooms/{q(room)}/context/$doesnotexist1234567890abcdef",
          token=a.token, label="context for a nonexistent event id")

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(other_mid)}", token=a.token,
          label="context for a real event that belongs to a different room")

    c.req("GET", f"{CS}/rooms/{q(room)}/context/{q(m1)}", token=outsider.token,
          label="context requested by a user who is not in the room")

    c.req("GET", f"{CS}/rooms/{q(room)}/context/not-a-valid-event-id",
          token=a.token, label="context for a syntactically malformed event id")
