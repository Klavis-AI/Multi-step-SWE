"""Step 5 -- room version 11, which release v1.8 introduced.

Room version 10 belongs to the previous milestone: release v1.6 ships its page
and its two rules (integer-only power levels, the `knock_restricted` join
rule).  Room version 11 is first described by `/spec/v1.9/rooms/v11.md`, which
is this milestone's release tree, so it is scored here -- against the page that
defines it rather than against a page that does not exist yet.

The scenarios themselves are the room-version half of `step04_threads.py`,
re-pointed at the version this step introduces: what a create event carries,
where a redaction names its target, and which content keys survive a redaction.
"""

from __future__ import annotations

from mxconf.mx import CS, q
from mxconf.registry import scenario

from .step04_threads import (_creator_record, _power_level_integer_checks,
                             _redaction_allow_list_checks,
                             _redaction_target_checks, create_room, register,
                             send)  # noqa: F401


@scenario("rv/roomver-create-11", tags=["roomver", "create"], step=5)
def roomver_create_11(c):
    """`rooms/v11.md`: the create event drops `creator` -- the event's `sender`
    is the creator -- which is the first thing a client sees of the version."""
    owner = register(c, "rcv11")

    c.req("GET", f"{CS}/capabilities", token=owner.token,
          label="room version 11 is advertised as available",
          derive=lambda b: {
              v: b["capabilities"]["m.room_versions"]["available"].get(v)
              for v in ("10", "11")})

    room = create_room(c, owner, room_version="11", preset="public_chat",
                       name="Room v11")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=owner.token,
          label="v11: the create event's content",
          derive=lambda b: {"room_version": b.get("room_version"),
                            "has_creator": "creator" in b,
                            "federate": b.get("m.federate", True)})
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=owner.token,
          label="v11: how the create event records who made the room",
          derive=lambda b, _o=owner.user_id: _creator_record(b, _o))
    message = send(c, owner, room, "m1",
                   {"msgtype": "m.text", "body": "hello"}, check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(message)}", token=owner.token,
          label="v11: an ordinary message round-trips",
          derive=lambda b: {"content": b.get("content"), "type": b.get("type")})


@scenario("rv/roomver-power-level-integers-11",
          tags=["roomver", "power-levels"], step=5)
def roomver_power_level_integers_11(c):
    """Room version 11 inherits version 10's integer-only power levels."""
    owner = register(c, "rpv11")
    _power_level_integer_checks(c, owner, "11")


@scenario("rv/roomver-redaction-target-location-11",
          tags=["roomver", "redaction"], step=5)
def roomver_redaction_target_location_11(c):
    """Where a redaction names its target: top-level up to version 10,
    `content.redacts` from version 11."""
    _redaction_target_checks(c, ("10", "11"), "rtl11")


@scenario("rv/roomver-redaction-allow-list-11",
          tags=["roomver", "redaction"], step=5)
def roomver_redaction_allow_list_11(c):
    """The redaction algorithm's content allow-list, version 10 against 11."""
    _redaction_allow_list_checks(c, ("10", "11"), "ral11")
