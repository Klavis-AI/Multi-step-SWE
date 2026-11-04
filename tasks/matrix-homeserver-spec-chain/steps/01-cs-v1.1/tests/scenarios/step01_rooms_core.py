"""Step 1 — rooms: creation, state, membership, aliases, the room directory."""

from mxconf.mx import (CS, create_room, invite, join, ok, q, register, send,
                       set_state, sync)
from mxconf.registry import scenario
from mxconf.steps import keep_types

V = "6"          # every scenario pins the room version: the default is server-chosen


def state_types(body):
    return sorted({(e.get("type"), e.get("state_key")) for e in body
                   if e.get("type") in keep_types(1, [e.get("type")])})


@scenario("cs/createroom-minimal", tags=["rooms"], step=1)
def createroom_minimal(c):
    u = register(c, "cr1")
    c.req("POST", f"{CS}/createRoom", token=u.token,
          json_body={"room_version": V}, label="createRoom minimal")
    r = c.req("POST", f"{CS}/createRoom", token=u.token, json_body={"room_version": V},
              check=False)
    room = r.json["room_id"]
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=u.token,
          label="state of a new room",
          derive=lambda b: {"state": state_types(b)})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=u.token,
          label="m.room.create content")
    # The specification fixes the documented defaults and that the creator is
    # at 100; which event types a server raises, and to what, is its own choice,
    # and so is `invite` -- create_room.yaml leaves it to the server and
    # m.room.power_levels says it "Defaults to 50 if unspecified".
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/", token=u.token,
          label="default power levels", drop=("events", "invite", "m.call.invite"))
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.join_rules/", token=u.token,
          label="default join rules")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.history_visibility/",
          token=u.token, label="default history visibility")


@scenario("cs/createroom-presets", tags=["rooms"], step=1)
def createroom_presets(c):
    u = register(c, "cr2")
    for preset in ("private_chat", "public_chat", "trusted_private_chat"):
        r = c.req("POST", f"{CS}/createRoom", token=u.token,
                  json_body={"room_version": V, "preset": preset},
                  check=False)
        room = r.json["room_id"]
        c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.join_rules/", token=u.token,
              label=f"join rules for preset {preset}")
        c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.history_visibility/",
              token=u.token, label=f"history visibility for preset {preset}")
    c.req("POST", f"{CS}/createRoom", token=u.token,
          json_body={"room_version": V, "preset": "not_a_preset"},
          label="unknown preset")


@scenario("cs/createroom-name-topic", tags=["rooms"], step=1)
def createroom_name_topic(c):
    u = register(c, "cr3")
    r = c.req("POST", f"{CS}/createRoom", token=u.token,
              json_body={"room_version": V, "name": "The Room", "topic": "A topic"},
              label="createRoom with name and topic")
    room = r.json["room_id"]
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=u.token,
          label="m.room.name")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="m.room.topic")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.name/somekey", token=u.token,
          label="m.room.name with a state key that does not exist")


@scenario("cs/createroom-initial-state", tags=["rooms"], step=1)
def createroom_initial_state(c):
    u = register(c, "cr4")
    r = c.req("POST", f"{CS}/createRoom", token=u.token, json_body={
        "room_version": V,
        "initial_state": [
            {"type": "m.room.topic", "state_key": "", "content": {"topic": "set early"}},
            {"type": "com.example.custom", "state_key": "k",
             "content": {"hello": "world"}},
        ]}, label="createRoom with initial_state")
    room = r.json["room_id"]
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="initial_state topic applied")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/com.example.custom/k", token=u.token,
          label="initial_state custom event applied")


@scenario("cs/createroom-creation-content", tags=["rooms"], step=1)
def createroom_creation_content(c):
    u = register(c, "cr5")
    r = c.req("POST", f"{CS}/createRoom", token=u.token, json_body={
        "room_version": V,
        "creation_content": {"m.federate": False, "com.example.marker": 7},
    }, label="createRoom with creation_content")
    room = r.json["room_id"]
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=u.token,
          label="creation_content lands in m.room.create")


@scenario("cs/createroom-power-override", tags=["rooms", "power-levels"], step=1)
def createroom_power_override(c):
    u = register(c, "cr6")
    r = c.req("POST", f"{CS}/createRoom", token=u.token, json_body={
        "room_version": V,
        "power_level_content_override": {"events_default": 25, "state_default": 75,
                                         "invite": 60},
    }, label="createRoom with power_level_content_override")
    room = r.json["room_id"]
    # Which event types a server raises above the default, and to what, is its
    # own business -- `cs/createroom-minimal` already treats the `events` map
    # that way.  What the release fixes is that the override takes effect.
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/", token=u.token,
          label="overridden power levels", drop=("events", "users"))


@scenario("cs/createroom-bad-version", tags=["rooms", "room-versions"], step=1)
def createroom_bad_version(c):
    u = register(c, "cr7")
    c.req("POST", f"{CS}/createRoom", token=u.token,
          json_body={"room_version": "99"}, label="unknown room version")
    c.req("POST", f"{CS}/createRoom", token=u.token,
          json_body={"room_version": "not a version"},
          label="malformed room version")


@scenario("cs/createroom-invite", tags=["rooms", "membership"], step=1)
def createroom_invite(c):
    u = register(c, "ci1")
    v = register(c, "ci2")
    r = c.req("POST", f"{CS}/createRoom", token=u.token,
              json_body={"room_version": V, "invite": [v.user_id],
                         "is_direct": True},
              label="createRoom with an invite")
    room = r.json["room_id"]
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(v.user_id)}",
          token=u.token, label="invited membership event")
    c.req("GET", f"{CS}/joined_rooms", token=v.token,
          label="an invitee has not joined")


@scenario("cs/state-set-and-read", tags=["rooms", "state"], step=1)
def state_set_and_read(c):
    u = register(c, "st1")
    room = create_room(c, u, room_version=V)
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=u.token,
          json_body={"name": "renamed"}, label="set state with an empty state key")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=u.token,
          label="read it back")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/com.example.thing/subject",
          token=u.token, json_body={"v": 1}, label="set state with a state key")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/com.example.thing/subject",
          token=u.token, label="read the keyed state back")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/com.example.thing/missing",
          token=u.token, label="a state key that was never set")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/com.example.never/", token=u.token,
          label="a state type that was never set")


@scenario("cs/state-non-member", tags=["rooms", "state", "auth"], step=1)
def state_non_member(c):
    u = register(c, "sn1")
    v = register(c, "sn2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=v.token,
          json_body={"name": "not mine"}, label="set state without being a member")
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=v.token,
          label="read state without being a member",
          derive=lambda b: {"n": len(b) if isinstance(b, list) else None})


@scenario("cs/state-power-level-enforced", tags=["power-levels", "auth"], step=1)
def state_power_enforced(c):
    u = register(c, "pl1")
    v = register(c, "pl2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=v.token,
          json_body={"name": "nope"},
          label="a user below state_default cannot set state")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=v.token,
          json_body={"topic": "nope"},
          label="the same for another state type")
    set_state(c, u, room, "m.room.power_levels",
              {"users": {u.user_id: 100, v.user_id: 50}, "users_default": 0,
               "events_default": 0, "state_default": 50, "ban": 50, "kick": 50,
               "redact": 50, "invite": 0}, "")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=v.token,
          json_body={"name": "now allowed"},
          label="the same user at state_default may set it")


@scenario("cs/power-level-self-escalation", tags=["power-levels", "auth"], step=1)
def power_self_escalation(c):
    u = register(c, "pe1")
    v = register(c, "pe2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    set_state(c, u, room, "m.room.power_levels",
              {"users": {u.user_id: 100, v.user_id: 50}, "users_default": 0,
               "events_default": 0, "state_default": 50, "ban": 50, "kick": 50,
               "redact": 50, "invite": 0}, "")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/", token=v.token,
          json_body={"users": {u.user_id: 100, v.user_id: 100}, "users_default": 0,
                     "events_default": 0, "state_default": 50, "ban": 50,
                     "kick": 50, "redact": 50, "invite": 0},
          label="a user cannot raise their own level")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/", token=v.token,
          json_body={"users": {u.user_id: 50, v.user_id: 50}, "users_default": 0,
                     "events_default": 0, "state_default": 50, "ban": 50,
                     "kick": 50, "redact": 50, "invite": 0},
          label="a user cannot demote someone above them")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/", token=v.token,
          json_body={"users": {u.user_id: 100, v.user_id: 25}, "users_default": 0,
                     "events_default": 0, "state_default": 50, "ban": 50,
                     "kick": 50, "redact": 50, "invite": 0},
          label="a user may demote themselves")


@scenario("cs/events-default-blocks-messages", tags=["power-levels"], step=1)
def events_default_blocks(c):
    u = register(c, "ed1")
    v = register(c, "ed2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/t1", token=v.token,
          json_body={"msgtype": "m.text", "body": "before"},
          label="a message is allowed at events_default 0")
    set_state(c, u, room, "m.room.power_levels",
              {"users": {u.user_id: 100}, "users_default": 0, "events_default": 50,
               "state_default": 50, "ban": 50, "kick": 50, "redact": 50,
               "invite": 0}, "")
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/t2", token=v.token,
          json_body={"msgtype": "m.text", "body": "after"},
          label="the same message is refused once events_default is raised")


@scenario("cs/membership-invite-join-leave", tags=["membership"], step=1)
def membership_flow(c):
    u = register(c, "mb1")
    v = register(c, "mb2")
    room = create_room(c, u, room_version=V)
    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=u.token,
          json_body={"user_id": v.user_id}, label="invite")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(v.user_id)}",
          # `is_direct` on an invitation is "a flag to the invitee" the release
          # does not require a server to store, and two conforming servers
          # differ about it.
          token=u.token, label="membership after invite",
          drop=("is_direct", "displayname", "avatar_url"))
    c.req("POST", f"{CS}/rooms/{q(room)}/join", token=v.token, json_body={},
          label="join")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(v.user_id)}",
          token=u.token, label="membership after join")
    c.req("GET", f"{CS}/joined_rooms", token=v.token, label="joined_rooms")
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=v.token,
          json_body={"reason": "bye"}, label="leave with a reason")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(v.user_id)}",
          token=u.token, label="membership after leave",
          drop=("displayname", "avatar_url"))
    c.req("POST", f"{CS}/rooms/{q(room)}/forget", token=v.token, json_body={},
          label="forget")
    c.req("GET", f"{CS}/joined_rooms", token=v.token, label="joined_rooms after leave")


@scenario("cs/membership-errors", tags=["membership"], step=1)
def membership_errors(c):
    u = register(c, "me1")
    v = register(c, "me2")
    room = create_room(c, u, room_version=V)
    c.req("POST", f"{CS}/rooms/{q(room)}/join", token=v.token, json_body={},
          label="join an invite-only room without an invite")
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=v.token, json_body={},
          check=False, label="leave a room you were never in")
    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=v.token,
          json_body={"user_id": v.user_id}, label="invite while not a member")
    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=u.token,
          json_body={"user_id": u.user_id}, label="invite yourself while joined")
    invite(c, u, room, v.user_id)
    join(c, v, room)
    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=u.token,
          json_body={"user_id": v.user_id}, label="invite a user who is joined")


@scenario("cs/kick-ban-unban", tags=["membership", "power-levels"], step=1)
def kick_ban(c):
    u = register(c, "kb1")
    v = register(c, "kb2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    c.req("POST", f"{CS}/rooms/{q(room)}/kick", token=u.token,
          json_body={"user_id": v.user_id, "reason": "out"}, label="kick")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(v.user_id)}",
          token=u.token, label="membership after kick",
          drop=("displayname", "avatar_url"))
    c.req("POST", f"{CS}/rooms/{q(room)}/join", token=v.token, json_body={},
          label="a kicked user may rejoin a public room")
    c.req("POST", f"{CS}/rooms/{q(room)}/ban", token=u.token,
          json_body={"user_id": v.user_id, "reason": "banned"}, label="ban")
    # As after a kick: whether a ban event carries the target's profile is the
    # server's choice -- the release fixes `membership` and nothing else about
    # a membership written by someone other than its subject.
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(v.user_id)}",
          token=u.token, label="membership after ban",
          drop=("displayname", "avatar_url"))
    c.req("POST", f"{CS}/rooms/{q(room)}/join", token=v.token, json_body={},
          label="a banned user cannot rejoin")
    c.req("POST", f"{CS}/rooms/{q(room)}/unban", token=u.token,
          json_body={"user_id": v.user_id}, label="unban")
    c.req("POST", f"{CS}/rooms/{q(room)}/join", token=v.token, json_body={},
          label="join after unban")
    c.req("POST", f"{CS}/rooms/{q(room)}/kick", token=v.token,
          json_body={"user_id": u.user_id},
          label="a user below the kick level cannot kick")


@scenario("cs/members-endpoints", tags=["membership"], step=1)
def members_endpoints(c):
    u = register(c, "mm1")
    v = register(c, "mm2")
    w = register(c, "mm3")
    room = create_room(c, u, room_version=V, preset="public_chat")
    join(c, v, room)
    invite(c, u, room, w.user_id)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=u.token, label="all members")
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=u.token,
          query={"membership": "join"}, label="members filtered to join")
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=u.token,
          query={"not_membership": "leave"}, label="members excluding leave")
    c.req("GET", f"{CS}/rooms/{q(room)}/joined_members", token=u.token,
          label="joined_members")
    # Same as members-forbidden-for-outsider[2]: the release does not fix what
    # an invited-but-not-joined caller gets here.
    c.req("GET", f"{CS}/rooms/{q(room)}/joined_members", token=w.token, check=False,
          label="joined_members as an invited user")


@scenario("cs/aliases", tags=["directory"], step=1)
def aliases(c):
    u = register(c, "al1")
    v = register(c, "al2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    alias = f"#alias-one:{c.server_name}"
    c.req("PUT", f"{CS}/directory/room/{q(alias)}", token=u.token,
          json_body={"room_id": room}, label="create an alias")
    c.req("GET", f"{CS}/directory/room/{q(alias)}", label="resolve the alias")
    c.req("PUT", f"{CS}/directory/room/{q(alias)}", token=u.token,
          json_body={"room_id": room}, label="create a duplicate alias")
    c.req("GET", f"{CS}/directory/room/{q(f'#nothing:{c.server_name}')}",
          label="resolve an unknown alias")
    c.req("POST", f"{CS}/join/{q(alias)}", token=v.token, json_body={},
          label="join by alias")
    c.req("DELETE", f"{CS}/directory/room/{q(alias)}", token=u.token,
          label="delete the alias")
    c.req("GET", f"{CS}/directory/room/{q(alias)}", label="the alias is gone")


@scenario("cs/createroom-alias", tags=["directory"], step=1)
def createroom_alias(c):
    u = register(c, "ca1")
    r = c.req("POST", f"{CS}/createRoom", token=u.token,
              json_body={"room_version": V, "room_alias_name": "made-with-room",
                         "preset": "public_chat"},
              label="createRoom with room_alias_name")
    room = r.json["room_id"]
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/", token=u.token,
          label="canonical alias state")
    c.req("GET", f"{CS}/directory/room/{q(f'#made-with-room:{c.server_name}')}",
          label="the alias resolves")
    c.req("POST", f"{CS}/createRoom", token=u.token,
          json_body={"room_version": V, "room_alias_name": "made-with-room"},
          label="a second room cannot take the same alias")


@scenario("cs/public-rooms", tags=["directory"], step=1)
def public_rooms(c):
    u = register(c, "pr1")
    create_room(c, u, room_version=V, preset="public_chat", visibility="public",
                name="Alpha public")
    create_room(c, u, room_version=V, preset="public_chat", visibility="public",
                name="Beta public")
    create_room(c, u, room_version=V, preset="private_chat", name="Hidden")
    c.req("GET", f"{CS}/publicRooms", token=u.token, label="public rooms",
          derive=lambda b: {"names": sorted(
              str(x.get("name")) for x in b.get("chunk", []))})
    c.req("POST", f"{CS}/publicRooms", token=u.token,
          json_body={"filter": {"generic_search_term": "Alpha"}},
          label="public rooms filtered",
          derive=lambda b: {"names": sorted(
              str(x.get("name")) for x in b.get("chunk", []))})
    c.req("GET", f"{CS}/publicRooms", token=u.token, query={"limit": 1},
          label="public rooms with a limit",
          derive=lambda b: {"n": len(b.get("chunk", []))})


@scenario("cs/directory-visibility", tags=["directory"], step=1)
def directory_visibility(c):
    u = register(c, "dv1")
    room = create_room(c, u, room_version=V, preset="public_chat")
    c.req("GET", f"{CS}/directory/list/room/{q(room)}", label="visibility of a new room")
    c.req("PUT", f"{CS}/directory/list/room/{q(room)}", token=u.token,
          json_body={"visibility": "public"}, label="publish the room")
    c.req("GET", f"{CS}/directory/list/room/{q(room)}", label="visibility after publish")
    c.req("PUT", f"{CS}/directory/list/room/{q(room)}", token=u.token,
          json_body={"visibility": "private"}, label="unpublish the room")
    c.req("GET", f"{CS}/directory/list/room/{q(room)}",
          label="visibility after unpublish")


@scenario("cs/send-and-event", tags=["messages"], step=1)
def send_and_event(c):
    u = register(c, "se1")
    v = register(c, "se2")
    room = create_room(c, u, room_version=V, preset="public_chat")
    r = c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/tx-1", token=u.token,
              json_body={"msgtype": "m.text", "body": "one"}, label="send a message")
    eid = r.json["event_id"]
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/tx-1", token=u.token,
          json_body={"msgtype": "m.text", "body": "one"},
          label="the same transaction id returns the same event")
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/tx-2", token=u.token,
          json_body={"msgtype": "m.text", "body": "two"},
          label="a different transaction id makes a new event")
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(eid)}", token=u.token,
          label="fetch the event")
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q('$notanevent')}", token=u.token,
          label="fetch an unknown event")
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/tx-3", token=v.token,
          json_body={"msgtype": "m.text", "body": "outsider"},
          label="send to a room you are not in")


@scenario("cs/send-size-limits", tags=["messages", "limits"], step=1)
def send_size_limits(c):
    u = register(c, "sz1")
    room = create_room(c, u, room_version=V)
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/big", token=u.token,
          json_body={"msgtype": "m.text", "body": "x" * 70000},
          label="an event above the 65536-byte limit")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/{'t' * 300}/", token=u.token,
          json_body={"a": 1}, label="an event type above the 255-byte limit")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/com.example.k/{'s' * 300}",
          token=u.token, json_body={"a": 1},
          label="a state key above the 255-byte limit")


@scenario("cs/room-upgrade", tags=["rooms", "room-versions"], step=1)
def room_upgrade(c):
    u = register(c, "up1")
    room = create_room(c, u, room_version="1", name="Old room", topic="Old topic",
                       preset="public_chat")
    c.req("POST", f"{CS}/rooms/{q(room)}/upgrade", token=u.token,
          json_body={"new_version": "6"}, label="upgrade the room")
    r = c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.tombstone/", token=u.token,
              label="tombstone in the old room",
              derive=lambda b: {"has_replacement_room": "replacement_room" in b,
                                "body_is_a_string": isinstance(b.get("body"), str)})
    new_room = r.json.get("replacement_room")
    c.req("GET", f"{CS}/rooms/{q(new_room)}/state/m.room.create/", token=u.token,
          label="the new room's create event carries a predecessor",
          derive=lambda b: {"room_version": b.get("room_version"),
                            "has_predecessor": "predecessor" in b,
                            "predecessor_keys": sorted(
                                (b.get("predecessor") or {}).keys())})
    c.req("GET", f"{CS}/rooms/{q(new_room)}/state/m.room.name/", token=u.token,
          label="the name was carried over")
    c.req("GET", f"{CS}/rooms/{q(new_room)}/state/m.room.topic/", token=u.token,
          label="the topic was carried over")
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/after", token=u.token,
          json_body={"msgtype": "m.text", "body": "still open?"},
          label="sending into the old room after the upgrade")
    c.req("POST", f"{CS}/rooms/{q(room)}/upgrade", token=u.token,
          json_body={"new_version": "99"}, label="upgrade to an unknown version")
