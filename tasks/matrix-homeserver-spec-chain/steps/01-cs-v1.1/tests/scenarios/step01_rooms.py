"""Step 1 -- rooms: gaps not covered by step01_rooms_core.py.

This file assumes step01_rooms_core.py already exercises the bulk of room
creation, state, membership and directory behaviour. It adds: third-party
invite rejection, GET /rooms/{roomId}/aliases, the `at` and combined
membership/not_membership filters on /members, `since` paging on
POST /publicRooms, per-event-type entries in the power_levels `events` map,
m.room.canonical_alias's `alt_aliases`, banning a user who was never in the
room, refusing a second m.room.create, the state_key-starts-with-@ auth
rule, and the `m.federate` creation_content flag.
"""

from mxconf.mx import CS, create_room, invite, join, q, register, send, set_state, sync
from mxconf.registry import scenario

V = "6"


@scenario("cs/third-party-invite-state-key-rejected", tags=["membership", "third-party-invite"], step=1)
def third_party_invite_state_key_rejected(c):
    creator = register(c, "tpi1")
    target = register(c, "tpi2")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    # A well-formed-looking, but unbacked, third-party invite: no matching
    # m.room.third_party_invite event exists in the room, so the invite must
    # be refused even though the sender (the room creator) has full power
    # and would otherwise be allowed to invite `target` directly.
    bogus = {
        "membership": "invite",
        "third_party_invite": {
            "signed": {
                "mxid": target.user_id,
                "token": "no-such-token",
                "signatures": {"id.example.org":
                               {"ed25519:0": "A" * 44}},
            },
        },
    }
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(target.user_id)}",
          token=creator.token, json_body=bogus,
          label="an unbacked third-party invite is refused despite the sender's power")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(target.user_id)}",
          token=creator.token,
          label="the target's membership is unaffected by the refused attempt")


@scenario("cs/create-room-invite-3pid-missing-field", tags=["createRoom", "third-party-invite"], step=1)
def create_room_invite_3pid_missing_field(c):
    creator = register(c, "tpm1")
    # invite_3pid entries require id_server, id_access_token, medium and
    # address; a request missing any one of them must be refused outright,
    # with no room created either way.
    c.req("POST", f"{CS}/createRoom", token=creator.token,
          json_body={"room_version": V, "preset": "private_chat",
                     "invite_3pid": [{"id_server": "id.example.org",
                                      "id_access_token": "tok",
                                      "medium": "email"}]},
          label="createRoom with invite_3pid missing 'address' is refused",
          derive=lambda b: {"room_id_present": "room_id" in b})
    c.req("POST", f"{CS}/createRoom", token=creator.token,
          json_body={"room_version": V, "preset": "private_chat",
                     "invite_3pid": [{"id_access_token": "tok", "medium": "email",
                                      "address": "a@example.org"}]},
          label="createRoom with invite_3pid missing 'id_server' is refused",
          derive=lambda b: {"room_id_present": "room_id" in b})


@scenario("cs/aliases-list-endpoint", tags=["aliases"], step=1)
def aliases_list_endpoint(c):
    creator = register(c, "ali1")
    outsider = register(c, "ali2")
    joiner = register(c, "ali3")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    c.req("GET", f"{CS}/rooms/{q(room)}/aliases", token=creator.token,
          label="no local aliases yet: an empty list")
    for local in ("alialpha", "alibeta"):
        c.req("PUT", f"{CS}/directory/room/{q('#' + local + ':' + c.server_name)}",
              token=creator.token, json_body={"room_id": room}, check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/aliases", token=creator.token,
          label="both local aliases are listed",
          derive=lambda b: {"aliases": sorted(b.get("aliases", []))})
    invite(c, creator, room, joiner.user_id)
    join(c, joiner, room)
    c.req("GET", f"{CS}/rooms/{q(room)}/aliases", token=joiner.token,
          label="a joined non-creator member can also list the aliases",
          derive=lambda b: {"aliases": sorted(b.get("aliases", []))})
    c.req("GET", f"{CS}/rooms/{q(room)}/aliases", token=outsider.token,
          label="a non-member cannot list the room's aliases")


@scenario("cs/alias-wrong-domain-rejected", tags=["aliases"], step=1)
def alias_wrong_domain_rejected(c):
    creator = register(c, "adom1")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    other_domain_alias = "#nothere:some-other-domain.invalid"
    same_domain_alias = f"#validhere:{c.server_name}"
    c.req("PUT", f"{CS}/directory/room/{q(other_domain_alias)}", token=creator.token,
          json_body={"room_id": room},
          label="an alias whose domain is not this server is refused")
    c.req("PUT", f"{CS}/directory/room/{q(same_domain_alias)}", token=creator.token,
          json_body={"room_id": room},
          label="the same request with this server's own domain succeeds")
    c.req("GET", f"{CS}/directory/room/{q(same_domain_alias)}", token=creator.token,
          label="the same-domain alias resolves to the room",
          derive=lambda b: {"room_id_matches": b.get("room_id") == room})


@scenario("cs/alias-delete-forbidden-for-non-creator", tags=["aliases"], step=1)
def alias_delete_forbidden_for_non_creator(c):
    creator = register(c, "adel1")
    other = register(c, "adel2")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    invite(c, creator, room, other.user_id)
    join(c, other, room)
    alias = f"#adelmine:{c.server_name}"
    c.req("PUT", f"{CS}/directory/room/{q(alias)}", token=creator.token,
          json_body={"room_id": room}, check=False)
    c.req("DELETE", f"{CS}/directory/room/{q(alias)}", token=other.token,
          label="a member who didn't create the alias cannot delete it")
    c.req("GET", f"{CS}/directory/room/{q(alias)}", token=other.token,
          label="the alias still resolves after the refused delete",
          derive=lambda b: {"room_id_matches": b.get("room_id") == room})
    c.req("DELETE", f"{CS}/directory/room/{q(alias)}", token=creator.token,
          label="the creator can delete their own alias")
    c.req("GET", f"{CS}/directory/room/{q(alias)}", token=creator.token,
          label="the alias is gone after the creator deletes it")


@scenario("cs/members-at-point-in-time", tags=["membership", "members"], step=1)
def members_at_point_in_time(c):
    a = register(c, "atp1")
    b = register(c, "atp2")
    room = create_room(c, a, room_version=V, preset="private_chat")
    send(c, a, room, "txn-atp-1", {"body": "hello", "msgtype": "m.text"})
    r1 = sync(c, a, since=None, timeout=0, check=False)
    next_batch = (r1.json or {}).get("next_batch")
    invite(c, a, room, b.user_id)
    join(c, b, room)
    r2 = sync(c, a, since=next_batch, timeout=0, check=False)
    room_block = (r2.json or {}).get("rooms", {}).get("join", {}).get(room, {})
    at_token = (room_block.get("timeline") or {}).get("prev_batch") or next_batch
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          query={"at": at_token},
          label="members at a point in time before b joined excludes b",
          derive=lambda body: {"members": sorted(
              m.get("state_key") for m in body.get("chunk", []))})
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=a.token,
          label="members with no `at` reflects the current state, including b",
          derive=lambda body: {"members": sorted(
              m.get("state_key") for m in body.get("chunk", []))})


@scenario("cs/members-forbidden-for-outsider", tags=["membership", "members"], step=1)
def members_forbidden_for_outsider(c):
    creator = register(c, "mfo1")
    outsider = register(c, "mfo2")
    invited = register(c, "mfo3")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    invite(c, creator, room, invited.user_id)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=outsider.token,
          label="members is forbidden for a user who was never in the room")
    c.req("GET", f"{CS}/rooms/{q(room)}/joined_members", token=outsider.token,
          label="joined_members is forbidden for a user who was never in the room")
    # The documented 403 is "you aren't a member and weren't previously a
    # member", which is false of an invitee, and the 200 is defined only for
    # joined or left callers. The release does not say what an invitee gets, so
    # nothing here is scored.
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=invited.token, check=False,
          label="members is forbidden for a user who is only invited, not joined")
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=creator.token,
          label="members succeeds for the creator",
          derive=lambda b: {"n": len(b.get("chunk", []))})


@scenario("cs/members-membership-or-not-membership", tags=["membership", "members"], step=1)
def members_membership_or_not_membership(c):
    creator = register(c, "mor1")
    joined = register(c, "mor2")
    invited = register(c, "mor3")
    banned = register(c, "mor4")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    invite(c, creator, room, joined.user_id)
    join(c, joined, room)
    invite(c, creator, room, invited.user_id)
    c.req("POST", f"{CS}/rooms/{q(room)}/ban", token=creator.token,
          json_body={"user_id": banned.user_id, "reason": "test"}, check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=creator.token,
          query={"membership": "join"},
          label="membership=join alone: only actually-joined users",
          derive=lambda b: {"members": sorted(
              m.get("state_key") for m in b.get("chunk", []))})
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=creator.token,
          query={"not_membership": "ban"},
          label="not_membership=ban alone: everyone except the banned user",
          derive=lambda b: {"members": sorted(
              m.get("state_key") for m in b.get("chunk", []))})
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=creator.token,
          query={"not_membership": "join"},
          label="not_membership=join alone: everyone except the joined users",
          derive=lambda b: {"members": sorted(
              m.get("state_key") for m in b.get("chunk", []))})
    c.req("GET", f"{CS}/rooms/{q(room)}/members", token=creator.token,
          query={"membership": "ban"},
          label="membership=ban alone: only the banned user",
          derive=lambda b: {"members": sorted(
              m.get("state_key") for m in b.get("chunk", []))})


@scenario("cs/public-rooms-since-paging", tags=["public-rooms", "pagination"], step=1)
def public_rooms_since_paging(c):
    creator = register(c, "prsp1")
    for i in range(3):
        create_room(c, creator, room_version=V, preset="public_chat",
                    visibility="public", name=f"paged room {i}")
    page1 = c.req("POST", f"{CS}/publicRooms", token=creator.token,
                  json_body={"limit": 1},
                  label="publicRooms page 1: one room, a next_batch token",
                  derive=lambda b: {"n": len(b.get("chunk", [])),
                                    "has_next_batch": bool(b.get("next_batch"))})
    next_batch = (page1.json or {}).get("next_batch")
    page1_room = ((page1.json or {}).get("chunk") or [{}])[0].get("room_id")
    page2 = c.req("POST", f"{CS}/publicRooms", token=creator.token,
                  json_body={"limit": 1, "since": next_batch},
                  label="publicRooms page 2: a disjoint room from page 1",
                  derive=lambda b: {
                      "n": len(b.get("chunk", [])),
                      "different_room_than_page1":
                          (b.get("chunk") or [{}])[0].get("room_id") != page1_room,
                  })
    next_batch2 = (page2.json or {}).get("next_batch")
    c.req("POST", f"{CS}/publicRooms", token=creator.token,
          json_body={"limit": 1, "since": next_batch2},
          label="publicRooms page 3: the last room, pagination then ends",
          derive=lambda b: {"n": len(b.get("chunk", [])),
                            "has_next_batch": bool(b.get("next_batch"))})


@scenario("cs/public-rooms-search-matches-topic", tags=["public-rooms"], step=1)
def public_rooms_search_matches_topic(c):
    creator = register(c, "prst1")
    with_topic = create_room(c, creator, room_version=V, preset="public_chat",
                              visibility="public", name="Alpha Room",
                              topic="a room about xenoncrystals")
    create_room(c, creator, room_version=V, preset="public_chat",
                visibility="public", name="Beta Room", topic="just chatting")
    c.req("POST", f"{CS}/publicRooms", token=creator.token,
          json_body={"filter": {"generic_search_term": "xenoncrystals"}},
          label="generic_search_term matches a room's topic, not just its name",
          derive=lambda b: {"matched_only_the_topic_room":
                            [r.get("room_id") for r in b.get("chunk", [])] == [with_topic]})
    c.req("POST", f"{CS}/publicRooms", token=creator.token,
          json_body={"filter": {"generic_search_term": "nonexistent-keyword-zzz"}},
          label="a term matching neither room's name nor topic returns nothing",
          derive=lambda b: {"n": len(b.get("chunk", []))})


@scenario("cs/public-rooms-chunk-reflects-preset-flags", tags=["public-rooms"], step=1)
def public_rooms_chunk_reflects_preset_flags(c):
    creator = register(c, "prpf1")
    pub_room = create_room(c, creator, room_version=V, preset="public_chat",
                           visibility="public", name="Flags Room")
    guest_room = create_room(
        c, creator, room_version=V, preset="public_chat", visibility="public",
        name="Guest OK Room",
        initial_state=[{"type": "m.room.guest_access",
                        "content": {"guest_access": "can_join"}}])
    c.req("POST", f"{CS}/publicRooms", token=creator.token, json_body={},
          label="public_chat preset: guest_can_join=false, world_readable=false",
          derive=lambda b: next(
              ({"guest_can_join": r.get("guest_can_join"),
                "world_readable": r.get("world_readable"),
                "num_joined_members": r.get("num_joined_members")}
               for r in b.get("chunk", []) if r.get("room_id") == pub_room), {}))
    c.req("POST", f"{CS}/publicRooms", token=creator.token, json_body={},
          label="an initial_state guest_access override is reflected in the chunk",
          derive=lambda b: next(
              ({"guest_can_join": r.get("guest_can_join")}
               for r in b.get("chunk", []) if r.get("room_id") == guest_room), {}))


@scenario("cs/power-levels-per-event-type", tags=["power-levels", "room-state"], step=1)
def power_levels_per_event_type(c):
    creator = register(c, "plpe1")
    mid = register(c, "plpe2")
    room = create_room(
        c, creator, room_version=V, preset="private_chat",
        power_level_content_override={
            "users": {creator.user_id: 100, mid.user_id: 50},
            "events": {"m.room.name": 60},
        })
    invite(c, creator, room, mid.user_id)
    join(c, mid, room)
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=mid.token,
          json_body={"name": "renamed by mid"},
          label="a per-event-type power requirement blocks a user below it")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/org.example.custom/", token=mid.token,
          json_body={"k": "v"},
          label="the same user can still set a state type with no per-type override")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=creator.token,
          label="m.room.name was not changed by the refused attempt")
    set_state(c, creator, room, "m.room.power_levels",
              {"users": {mid.user_id: 60}, "events": {"m.room.name": 60}})
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=mid.token,
          json_body={"name": "renamed by mid now at 60"},
          label="raising the user to the per-type threshold allows it")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.name/", token=creator.token,
          label="m.room.name now reflects mid's successful rename",
          derive=lambda b: {"name": b.get("name")})


@scenario("cs/invite-below-invite-level-rejected", tags=["membership", "power-levels"], step=1)
def invite_below_invite_level_rejected(c):
    creator = register(c, "bil1")
    low = register(c, "bil2")
    target = register(c, "bil3")
    room = create_room(
        c, creator, room_version=V, preset="private_chat",
        power_level_content_override={"invite": 50,
                                       "users": {creator.user_id: 100, low.user_id: 0}})
    invite(c, creator, room, low.user_id)
    join(c, low, room)
    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=low.token,
          json_body={"user_id": target.user_id},
          label="a joined member below the invite power level cannot invite")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(target.user_id)}",
          token=creator.token,
          label="the refused invite left no member event for the target")
    set_state(c, creator, room, "m.room.power_levels",
              {"invite": 50, "users": {low.user_id: 50}})
    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=low.token,
          json_body={"user_id": target.user_id},
          label="raising the user to the invite power level allows it")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(target.user_id)}",
          token=creator.token,
          label="the target now has a pending invite",
          derive=lambda b: {"membership": b.get("membership")})


@scenario("cs/canonical-alias-alt-aliases", tags=["aliases", "canonical-alias"], step=1)
def canonical_alias_alt_aliases(c):
    creator = register(c, "caa1")
    room = create_room(c, creator, room_version=V, preset="private_chat",
                       room_alias_name="caahome")
    home_alias = f"#caahome:{c.server_name}"
    alt1 = f"#caaalt1:{c.server_name}"
    alt2 = f"#caaalt2:{c.server_name}"
    for a in (alt1, alt2):
        c.req("PUT", f"{CS}/directory/room/{q(a)}", token=creator.token,
              json_body={"room_id": room}, check=False)
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/",
          token=creator.token,
          json_body={"alias": home_alias, "alt_aliases": [alt1, alt2]},
          label="canonical_alias with alt_aliases pointing at real local aliases")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/",
          token=creator.token,
          label="canonical_alias readback preserves alias and alt_aliases",
          derive=lambda b: {"alias": b.get("alias"),
                            "alt_aliases": sorted(b.get("alt_aliases", []))})
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/",
          token=creator.token,
          json_body={"alias": home_alias, "alt_aliases": [alt1]},
          label="alt_aliases can be updated to a smaller set")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/",
          token=creator.token,
          label="readback reflects the smaller alt_aliases set",
          derive=lambda b: {"alias": b.get("alias"),
                            "alt_aliases": sorted(b.get("alt_aliases", []))})


@scenario("cs/ban-user-never-joined", tags=["membership", "ban"], step=1)
def ban_user_never_joined(c):
    creator = register(c, "bnj1")
    target = register(c, "bnj2")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    c.req("POST", f"{CS}/rooms/{q(room)}/ban", token=creator.token,
          json_body={"user_id": target.user_id, "reason": "preemptive ban"},
          label="banning a user who was never in the room succeeds")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(target.user_id)}",
          token=creator.token,
          label="the target's member event shows membership=ban with a reason",
          derive=lambda b: {"membership": b.get("membership"),
                            "reason": b.get("reason")})
    c.req("POST", f"{CS}/join/{q(room)}", token=target.token, json_body={},
          label="the banned user cannot join")
    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=creator.token,
          json_body={"user_id": target.user_id},
          label="the banned user cannot even be invited while banned")


@scenario("cs/second-create-event-refused", tags=["room-state", "create"], step=1)
def second_create_event_refused(c):
    creator = register(c, "sce1")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=creator.token,
          json_body={"creator": creator.user_id, "room_version": V},
          label="a second m.room.create event is refused even for the creator")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=creator.token,
          label="the original create event content is unchanged",
          derive=lambda b: {"room_version": b.get("room_version"),
                            "creator": b.get("creator")})
    eid = send(c, creator, room, "txn-sce-after",
              {"body": "still works", "msgtype": "m.text"})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(eid)}", token=creator.token,
          label="the room still functions normally after the refused create",
          derive=lambda b: {"body": (b.get("content") or {}).get("body")})


@scenario("cs/state-key-at-prefix-not-sender", tags=["room-state", "auth-rules"], step=1)
def state_key_at_prefix_not_sender(c):
    creator = register(c, "skp1")
    other = register(c, "skp2")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/org.example.marker/{q(other.user_id)}",
          token=creator.token, json_body={"k": "v"},
          label="a state_key starting with @ that isn't the sender is refused")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/org.example.marker/{q(other.user_id)}",
          token=creator.token,
          label="no state landed at the mismatched state_key")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/org.example.marker/{q(creator.user_id)}",
          token=creator.token, json_body={"k": "v"},
          label="the same state_key, matching the sender's own id, is accepted")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/org.example.marker/{q(creator.user_id)}",
          token=creator.token,
          label="the sender-matching state landed",
          derive=lambda b: {"k": b.get("k")})


@scenario("cs/creation-content-federate-flag", tags=["createRoom", "federation"], step=1)
def creation_content_federate_flag(c):
    creator = register(c, "fed1")
    r_false = create_room(c, creator, room_version=V, preset="private_chat",
                          creation_content={"m.federate": False})
    r_true = create_room(c, creator, room_version=V, preset="private_chat",
                         creation_content={"m.federate": True})
    r_default = create_room(c, creator, room_version=V, preset="private_chat")
    c.req("GET", f"{CS}/rooms/{q(r_false)}/state/m.room.create/", token=creator.token,
          label="m.federate: False is preserved in the create event",
          derive=lambda b: {"m.federate": b.get("m.federate")})
    c.req("GET", f"{CS}/rooms/{q(r_true)}/state/m.room.create/", token=creator.token,
          label="m.federate: True is preserved in the create event",
          derive=lambda b: {"m.federate": b.get("m.federate")})
    c.req("GET", f"{CS}/rooms/{q(r_default)}/state/m.room.create/", token=creator.token,
          label="without m.federate given, whatever the server fills in is bool-typed",
          derive=lambda b: {"m.federate_is_bool_or_absent":
                            b.get("m.federate") is None or isinstance(b.get("m.federate"), bool)})


@scenario("cs/get-event-not-a-member", tags=["events", "membership"], step=1)
def get_event_not_a_member(c):
    creator = register(c, "gem1")
    outsider = register(c, "gem2")
    leaver = register(c, "gem3")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    invite(c, creator, room, leaver.user_id)
    join(c, leaver, room)
    event_id = send(c, creator, room, "txn-gem-1",
                    {"body": "secret", "msgtype": "m.text"})
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=leaver.token, json_body={},
          check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(event_id)}", token=outsider.token,
          label="an outsider who was never in the room cannot GET an event in it")
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(event_id)}", token=creator.token,
          label="a current member can GET the event",
          derive=lambda b: {"event_id_matches": b.get("event_id") == event_id,
                            "type": b.get("type")})
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(event_id)}", token=leaver.token,
          label="a user who has since left can still fetch an event from while they were present")


@scenario("cs/directory-list-unknown-room", tags=["public-rooms"], step=1)
def directory_list_unknown_room(c):
    creator = register(c, "dlu1")
    room = create_room(c, creator, room_version=V, preset="private_chat")
    fake_room = "!this-room-does-not-exist:" + c.server_name
    c.req("PUT", f"{CS}/directory/list/room/{q(fake_room)}", token=creator.token,
          json_body={"visibility": "public"},
          label="PUT directory visibility for an unknown room is refused")
    c.req("GET", f"{CS}/directory/list/room/{q(fake_room)}", token=creator.token,
          label="GET directory visibility for an unknown room is also 404")
    c.req("GET", f"{CS}/directory/list/room/{q(room)}", token=creator.token,
          label="a real room's visibility can still be read (contrast)")


@scenario("cs/canonical-alias-nonempty-key-accepted", tags=["aliases", "canonical-alias"], step=1)
def canonical_alias_nonempty_key_accepted(c):
    creator = register(c, "cak1")
    room = create_room(c, creator, room_version=V, preset="private_chat",
                       room_alias_name="cakhome")
    home_alias = f"#cakhome:{c.server_name}"
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/",
          token=creator.token,
          json_body={"alias": home_alias, "alt_aliases": []}, check=False)
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/notempty",
          token=creator.token, json_body={"alias": home_alias},
          label="a non-empty state_key is accepted like any other state event")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/notempty",
          token=creator.token,
          label="the extra keyed instance's content round-trips",
          derive=lambda b: {"alias": b.get("alias")})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.canonical_alias/",
          token=creator.token,
          label="the room's actual canonical_alias is unaffected by the extra instance",
          derive=lambda b: {"alias": b.get("alias"),
                            "alt_aliases": sorted(b.get("alt_aliases", []))})
