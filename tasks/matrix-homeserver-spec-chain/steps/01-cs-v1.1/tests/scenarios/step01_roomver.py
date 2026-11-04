"""Step 1 -- room versions: create-event/event-id shape, authorization-rule and
redaction-algorithm differences between room versions 1-6, room upgrades, and
canonical JSON, per matrix-spec/content/rooms/v1.md..v6.md and fragments/.
"""

import re

from mxconf.mx import CS, join, q, register, send, set_state
from mxconf.mx import create_room as _create_room
from mxconf.registry import scenario

# Event ids: v1/v2 are "$opaque:server"; v3 is "$" + unpadded *standard*
# base64 of the reference hash; v4/v5/v6 is "$" + unpadded *URL-safe* base64.
# matrix-spec/content/rooms/v3.md (reference hash), v4.md fragment
# v4-event-explainer.md ("replaces the 62nd and 63rd characters with - and _").
_STD_B64 = re.compile(r"^\$[A-Za-z0-9+/]+$")
_URLSAFE_B64 = re.compile(r"^\$[A-Za-z0-9_-]+$")


def create_room(c, user, *, check=False, **kwargs):
    return _create_room(c, user, check=check, **kwargs)


def _event_id_shape(eid: str, version: str) -> dict:
    """Facts about an event id's *format* only -- never the id itself.

    Only the base64 alphabet a given room version actually specifies is
    checked: v3 uses unpadded *standard* base64
    (matrix-spec/content/rooms/v3.md); v4-v6 use unpadded *URL-safe* base64
    (v4.md fragments/v4-event-explainer.md, "replaces the 62nd and 63rd
    characters with - and _"). Checking the *other* alphabet too would be
    non-deterministic: real base64 output for the "wrong" alphabet only
    sometimes happens to avoid that alphabet's special characters, since
    that depends on the hash's random bytes.
    """
    out = {
        "starts_with_dollar": eid.startswith("$"),
        "has_colon": ":" in eid,
    }
    if version not in ("1", "2"):
        # From version 3 the id is the base64 of a fixed-size hash, so its
        # length is determined; before that it is opaque and its length is the
        # server's own choice.
        out["length"] = len(eid)
    if version == "3":
        out["matches_standard_b64"] = bool(_STD_B64.match(eid))
    elif version in ("4", "5", "6"):
        out["matches_urlsafe_b64"] = bool(_URLSAFE_B64.match(eid))
    return out


def _create_and_check(c, version: str):
    u = register(c, f"cv{version}")
    room = create_room(c, u, room_version=version, preset="public_chat",
                        name="Room", topic="a topic")

    # `createRoom`'s preset table fixes which state events a preset *must*
    # produce; a server may write others of its own (the reference writes
    # `m.room.guest_access` for `public_chat`, which the table does not
    # mention).  So the question asked is that the required ones are all there,
    # not that nothing else is.
    REQUIRED_BY_PRESET = ("m.room.create", "m.room.member",
                          "m.room.power_levels", "m.room.join_rules",
                          "m.room.history_visibility")

    def derive_state(body):
        create_ev = next(e for e in body if e.get("type") == "m.room.create")
        present = {e.get("type") for e in body}
        return {
            "required_event_types_present": sorted(
                t for t in REQUIRED_BY_PRESET if t in present),
            "create_event_id_shape": _event_id_shape(create_ev["event_id"], version),
        }

    r = c.req("GET", f"{CS}/rooms/{q(room)}/state", token=u.token,
               label=f"v{version}: room state", derive=derive_state)
    create_ev = next(e for e in r.json if e["type"] == "m.room.create")
    eid = create_ev["event_id"]

    def derive_create(body, _eid=eid):
        content = body.get("content", {})
        return {
            **_event_id_shape(_eid, version),
            "room_version_in_content": content.get("room_version"),
            "has_creator": "creator" in content,
        }

    # Event ids are opaque, and (from room version 3 onward) are the base64
    # of a hash, so they may contain "/" -- they MUST be percent-encoded as
    # a path segment (mx.q), or a "/" in the id silently splits the path.
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(eid)}", token=u.token,
          label=f"v{version}: create event", derive=derive_create)

    mid = send(c, u, room, "m1", {"msgtype": "m.text", "body": "hello"},
               check=False)

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=u.token,
          label=f"v{version}: message event id shape",
          derive=lambda body, _mid=mid: _event_id_shape(_mid, version))
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=u.token,
          label=f"v{version}: message event shape")

    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=u.token,
          label=f"v{version}: create event bare content")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.join_rules/", token=u.token,
          label=f"v{version}: join_rules bare content")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(u.user_id)}",
          token=u.token, label=f"v{version}: creator's member bare content")
    # `users` is keyed by user id, which is not a value normalize.py
    # symbolises (only dict *values* are put through Symbols.scalar) -- so it
    # is dropped here to keep the rest of the object (the numeric defaults)
    # comparable across two independently-ported Synapse instances.
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/", token=u.token,
          # The release fixes the documented defaults and that the creator is at
          # 100; which event types a server raises, to what, and what it does
          # with `invite` for a preset is its own choice.
          label=f"v{version}: power_levels bare content",
          drop=("users", "events", "invite", "m.call.invite"))


@scenario("cs/roomver-create-v1", tags=["roomver", "create"], step=1)
def create_v1(c):
    _create_and_check(c, "1")


@scenario("cs/roomver-create-v2", tags=["roomver", "create"], step=1)
def create_v2(c):
    _create_and_check(c, "2")


@scenario("cs/roomver-create-v3", tags=["roomver", "create"], step=1)
def create_v3(c):
    _create_and_check(c, "3")


@scenario("cs/roomver-create-v4", tags=["roomver", "create"], step=1)
def create_v4(c):
    _create_and_check(c, "4")


@scenario("cs/roomver-create-v5", tags=["roomver", "create"], step=1)
def create_v5(c):
    _create_and_check(c, "5")


@scenario("cs/roomver-create-v6", tags=["roomver", "create"], step=1)
def create_v6(c):
    _create_and_check(c, "6")


# --------------------------------------------------------------------------- #
# Redaction algorithm: which content keys survive, per event type, per room
# version.  matrix-spec/content/rooms/fragments/v1-redactions.md (v1-v5) vs
# v6-redactions.md: identical except m.room.aliases's allow-listed `aliases`
# key, which v6 drops entirely (that type is no longer special-cased).
def _content_keys(body):
    return sorted((body.get("content") or {}).keys())


def _bare_content_keys(body):
    # GET /rooms/{roomId}/state/{eventType}/{stateKey} returns the content
    # object itself (see the "... bare content" checks in _create_and_check)
    # -- unlike GET /event/{id} or the /state array, which envelope content
    # under a "content" key. Do not reuse _content_keys here.
    return sorted((body or {}).keys())


def _create_keys_after_redaction(body):
    # rooms/fragments/v1-redactions.md and v6-redactions.md: "`m.room.create`
    # allows key `creator`".  A server may refuse the redaction (and keep the
    # whole content) or apply it (and keep only what the algorithm keeps), so
    # what is fixed is that `creator` survives and nothing outside the original
    # content appears.
    keys = set((body or {}).keys())
    return {"keeps_creator": "creator" in keys,
            "nothing_beyond_creator_and_room_version":
                keys <= {"creator", "room_version"}}


def _seed_redactable_events(c, u, room):
    """One event of each of the 7 named types, each carrying a junk content
    key that the redaction algorithm must strip regardless of room version.
    Returns {event_type: event_id}, insertion-ordered (stable iteration).
    """
    ids = {}
    state = c.req("GET", f"{CS}/rooms/{q(room)}/state", token=u.token,
                   check=False).json
    ids["m.room.create"] = next(
        e["event_id"] for e in state if e["type"] == "m.room.create")
    ids["m.room.member"] = set_state(
        c, u, room, "m.room.member",
        {"membership": "join", "displayname": "junk-name", "extra_junk": "x"},
        state_key=u.user_id, check=False)
    ids["m.room.join_rules"] = set_state(
        c, u, room, "m.room.join_rules",
        {"join_rule": "public", "extra_junk": "x"}, check=False)
    ids["m.room.power_levels"] = set_state(
        c, u, room, "m.room.power_levels",
        {"ban": 50, "kick": 50, "redact": 50, "invite": 0,
         "events_default": 0, "state_default": 50, "users_default": 0,
         "events": {}, "users": {u.user_id: 100}, "extra_junk": "x"},
        check=False)
    ids["m.room.history_visibility"] = set_state(
        c, u, room, "m.room.history_visibility",
        {"history_visibility": "shared", "extra_junk": "x"}, check=False)
    ids["m.room.aliases"] = set_state(
        c, u, room, "m.room.aliases",
        {"aliases": [f"#a:{c.server_name}"], "extra_junk": "x"},
        state_key=c.server_name, check=False)
    ids["m.room.message"] = send(
        c, u, room, "msg1",
        {"msgtype": "m.text", "body": "hello", "extra_junk": "x"}, check=False)
    return ids


def _redact(c, u, room, eid, txn, *, label=None, check=True, learn=True):
    return c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(eid)}/{q(txn)}",
                 token=u.token, json_body={"reason": "spec test"}, label=label,
                 check=check, learn=learn)


def _redact_content_keys_scenario(c, version):
    u = register(c, f"rk{version}")
    room = create_room(c, u, room_version=version, preset="public_chat",
                        name="Room", topic="t")
    ids = _seed_redactable_events(c, u, room)

    # m.room.create: no numbered rule in rooms/v*.md's auth-rules fragments
    # says a redaction of it is refused outright, and the reference refuses it
    # with a 403 -- so the attempt is made (it must not wedge what follows) and
    # *not* scored.  What the release does fix is the outcome, checked next:
    # whether the server refused the redaction or applied it, `creator` is on
    # these versions' allow-list and survives, and nothing else appears.
    # Either answer may carry a redaction event id or none, so the response
    # binds no symbol (Ctx.req `learn`).
    create_eid = ids.pop("m.room.create")
    _redact(c, u, room, create_eid, "tx-m.room.create", check=False,
            learn=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=u.token,
          label=f"v{version}: m.room.create keeps what the redaction algorithm keeps",
          derive=_create_keys_after_redaction)

    # m.room.aliases: a reference-Synapse-only quirk (GET
    # /rooms/{roomId}/event/{eventId} 404s for *any* m.room.aliases event,
    # independent of redaction) is deliberately NOT pinned here -- it has no
    # numbered rule behind it anywhere in rooms/v*.md's auth-rules fragments,
    # so scoring it would fail a spec-correct candidate that simply does not
    # replicate that quirk. The post-redaction content is instead read via
    # GET /state/{eventType}/{stateKey}, which does show the actual
    # documented v1-vs-v6 redaction-algorithm difference this scenario pins
    # (v1-redactions.md keeps the `aliases` key; v6-redactions.md does not).
    aliases_eid = ids.pop("m.room.aliases")
    _redact(c, u, room, aliases_eid, "tx-m.room.aliases",
            label=f"v{version}: redact m.room.aliases")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.aliases/{q(c.server_name)}",
          token=u.token,
          label=f"v{version}: m.room.aliases content keys after redaction (read via /state, not /event)",
          derive=_bare_content_keys)

    for etype, eid in ids.items():
        _redact(c, u, room, eid, f"tx-{etype}", label=f"v{version}: redact {etype}")
        c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(eid)}", token=u.token,
              label=f"v{version}: {etype} content keys after redaction",
              derive=_content_keys)


@scenario("cs/roomver-redact-v1-content-keys", tags=["roomver", "redaction"], step=1)
def redact_v1_content_keys(c):
    _redact_content_keys_scenario(c, "1")


@scenario("cs/roomver-redact-v6-content-keys", tags=["roomver", "redaction"], step=1)
def redact_v6_content_keys(c):
    _redact_content_keys_scenario(c, "6")


@scenario("cs/roomver-redacted-because", tags=["roomver", "redaction"], step=1)
def redacted_because(c):
    u = register(c, "rbec")
    room = create_room(c, u, room_version="6", preset="public_chat")
    mid = send(c, u, room, "m1", {"msgtype": "m.text", "body": "secret"},
               check=False)

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=u.token,
          label="before redaction: no redacted_because yet",
          derive=lambda b: "redacted_because" in (b.get("unsigned") or {}))

    r = c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx1", token=u.token,
              json_body={"reason": "because"}, check=False)
    rid = r.json["event_id"]

    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=u.token,
          label="after redaction: unsigned.redacted_because names the redaction event",
          derive=lambda b, _rid=rid: {
              "present": "redacted_because" in (b.get("unsigned") or {}),
              "is_the_redaction_event": (b.get("unsigned", {})
                                          .get("redacted_because", {})
                                          .get("event_id")) == _rid,
              "type_is_m_room_redaction": (b.get("unsigned", {})
                                            .get("redacted_because", {})
                                            .get("type")) == "m.room.redaction",
              "reason_preserved": (b.get("unsigned", {})
                                    .get("redacted_because", {})
                                    .get("content", {})
                                    .get("reason")) == "because",
          })

    # A redacted STATE event: content is stripped but the state entry stays
    # "live" (it is not simply undone) -- see the client-server spec's
    # Redactions boxes/note ("Redacted events can still affect the state of
    # the room ... it causes the topic to be removed from the room").
    tid = set_state(c, u, room, "m.room.topic", {"topic": "before"}, check=False)
    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(tid)}/tx2", token=u.token,
          json_body={}, check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="redacted state event: content is stripped (m.room.topic has no allow-list)",
          derive=lambda b: b)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(tid)}", token=u.token,
          label="the redacted state event also carries redacted_because",
          derive=lambda b: "redacted_because" in (b.get("unsigned") or {}))


@scenario("cs/roomver-redact-events-override-gate",
          tags=["roomver", "redaction", "power-levels"], step=1)
def redact_events_override_gate(c):
    """data/api/client-server/redaction.yaml: 'Any user with a power level
    greater than or equal to the m.room.redaction event power level may send
    redaction events in the room. If the user's power level [is] also
    greater than or equal to the redact power level of the room, the user
    may redact events sent by other users.'  These are two independent
    gates; this scenario isolates the first (generic per-event-type send
    gate), which -- unlike the redact-level/domain check below -- rejects
    outright and so is observable even on a single, non-federated homeserver.
    """
    u = register(c, "rgat1")
    v = register(c, "rgat2")
    room = create_room(c, u, room_version="6", preset="public_chat")
    join(c, v, room, check=False)
    mid = send(c, u, room, "m1", {"msgtype": "m.text", "body": "hi"}, check=False)

    pl = c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
               token=u.token, check=False).json
    pl["events"] = dict(pl.get("events", {}))
    pl["events"]["m.room.redaction"] = 100
    set_state(c, u, room, "m.room.power_levels", pl, check=False)

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-blocked", token=v.token,
          json_body={"reason": "should be blocked"},
          label="a member below the events[m.room.redaction] level cannot even send a redaction")
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=u.token,
          label="...so the content survives the blocked attempt",
          derive=_content_keys)

    c.req("PUT", f"{CS}/rooms/{q(room)}/redact/{q(mid)}/tx-allowed", token=u.token,
          json_body={"reason": "creator can"},
          label="the creator (at the required events[m.room.redaction] level) can redact")
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid)}", token=u.token,
          label="...and the content is empty once the allowed redaction lands",
          derive=_content_keys)


@scenario("cs/roomver-capabilities-versions", tags=["roomver", "capabilities"], step=1)
def capabilities_versions(c):
    u = register(c, "cvcap")

    def derive(b):
        rv = b.get("capabilities", {}).get("m.room_versions", {})
        avail = rv.get("available", {})
        default = rv.get("default")
        return {
            "default_is_listed_as_available": default in avail,
            "v1_to_v6_all_present_in_available_map": all(
                str(v) in avail for v in range(1, 7)),
        }

    c.req("GET", f"{CS}/capabilities", token=u.token,
          label="capabilities: m.room_versions coverage of v1-v6", derive=derive)

    r = c.req("GET", f"{CS}/capabilities", token=u.token, check=False)
    default_version = r.json["capabilities"]["m.room_versions"]["default"]
    room = create_room(c, u, preset="public_chat", room_version=default_version)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=u.token,
          label="the advertised default room_version is actually usable",
          derive=lambda body, dv=default_version: body.get("room_version") == dv)

    room2 = create_room(c, u, preset="public_chat")
    c.req("GET", f"{CS}/rooms/{q(room2)}/state/m.room.create/", token=u.token,
          label="createRoom with room_version omitted falls back to that default",
          derive=lambda body, dv=default_version: body.get("room_version", "1") == dv)

    c.req("POST", f"{CS}/createRoom", token=u.token,
          json_body={"preset": "public_chat", "room_version": "abc-not-a-version"},
          label="createRoom: an unsupported room_version is rejected")


# --------------------------------------------------------------------------- #
# Room upgrades: gaps not already covered by cs/room-upgrade in
# step01_rooms_core.py (which pins the tombstone, predecessor, name/topic
# transfer, sending into the old room, and upgrading to an unknown version).
# matrix-spec/content/client-server-api/modules/room_upgrades.md.
@scenario("cs/roomver-upgrade-state-transfer-values",
          tags=["roomver", "upgrade"], step=1)
def upgrade_state_transfer_values(c):
    """room_upgrades.md step 3: the recommended transferable state includes
    m.room.power_levels, m.room.join_rules and m.room.avatar among others.
    cs/room-upgrade already confirms the *name* and *topic* survive an
    upgrade; this scenario checks that non-default *values* (not just
    presence) of three more of the recommended types actually carry over
    unchanged into the replacement room: a customised power_levels map, a
    non-default join_rule, and an avatar URL.
    """
    u = register(c, "ust1")
    room = create_room(c, u, room_version="1", preset="public_chat",
                        name="Old", topic="Old topic")
    set_state(c, u, room, "m.room.join_rules", {"join_rule": "invite"},
              check=False)
    set_state(c, u, room, "m.room.avatar",
              {"url": "mxc://example.org/UpgradeAvatar"}, check=False)
    pl = c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
               token=u.token, check=False).json
    pl["events"] = dict(pl.get("events", {}))
    pl["events"]["m.room.custom_marker"] = 25
    set_state(c, u, room, "m.room.power_levels", pl, check=False)

    r = c.req("POST", f"{CS}/rooms/{q(room)}/upgrade", token=u.token,
              json_body={"new_version": "6"},
              label="upgrade v1 -> v6 after customising join_rules/avatar/power_levels")
    new_room = r.json["replacement_room"]

    c.req("GET", f"{CS}/rooms/{q(new_room)}/state/m.room.join_rules/",
          token=u.token,
          label="the non-default join_rule VALUE (not just its presence) carried over",
          derive=lambda b: b.get("join_rule"))
    c.req("GET", f"{CS}/rooms/{q(new_room)}/state/m.room.avatar/", token=u.token,
          label="the avatar URL carried over",
          derive=lambda b: b.get("url"))
    c.req("GET", f"{CS}/rooms/{q(new_room)}/state/m.room.power_levels/",
          token=u.token,
          label="the custom events[] power-level entry carried over",
          derive=lambda b: b.get("events", {}).get("m.room.custom_marker"))


@scenario("cs/roomver-upgrade-old-room-pl-restriction",
          tags=["roomver", "upgrade", "power-levels"], step=1)
def upgrade_old_room_pl_restriction(c):
    """room_upgrades.md step 6: 'If possible, the power levels in the old
    room should also be modified to prevent sending of events and inviting
    new users. For example, setting events_default and invite to the
    greater of 50 and users_default + 1.' This is an 'if possible'
    recommendation, not a MUST, so this scenario pins the actual values
    pinned Synapse applies -- with users_default raised to 60 before the
    upgrade, so the 'users_default + 1' branch (not just the constant-50
    floor) is exercised -- and then confirms the practical effect: a
    plain (users_default-level) member who could post in the old room
    before the upgrade can no longer post or invite afterwards.
    """
    u = register(c, "opl1")
    v = register(c, "opl2")
    w = register(c, "opl3")
    room = create_room(c, u, room_version="1", preset="public_chat")
    pl = c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
               token=u.token, check=False).json
    pl["users_default"] = 60
    set_state(c, u, room, "m.room.power_levels", pl, check=False)
    join(c, v, room, check=False)

    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/before-upgrade",
          token=v.token, json_body={"msgtype": "m.text", "body": "before"},
          label="a users_default (60) member can send in the old room before the upgrade")

    c.req("POST", f"{CS}/rooms/{q(room)}/upgrade", token=u.token,
          json_body={"new_version": "6"}, check=False)

    # "If possible, the power levels in the old room *should* also be modified
    # ... For example, setting events_default and invite to the greater of 50
    # and users_default + 1."  An example inside a recommendation: the numbers
    # are not the requirement, so what is scored is that an ordinary member can
    # no longer post or invite -- which the two requests after this one check
    # directly -- and that the levels moved above users_default at all.
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=u.token,
          label="the old room's levels are raised above an ordinary member",
          derive=lambda b: {
              "events_default_above_users_default":
                  isinstance(b.get("events_default"), int)
                  and isinstance(b.get("users_default"), int)
                  and b["events_default"] > b["users_default"],
              "invite_above_users_default":
                  isinstance(b.get("invite"), int)
                  and isinstance(b.get("users_default"), int)
                  and b["invite"] > b["users_default"],
              "users_default_unchanged": b.get("users_default")})

    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/after-upgrade",
          token=v.token, json_body={"msgtype": "m.text", "body": "after"},
          label="that same member is now blocked from sending in the old room")
    c.req("POST", f"{CS}/rooms/{q(room)}/invite", token=v.token,
          json_body={"user_id": w.user_id},
          label="...and from inviting a new (not-yet-member) user into the old room")


@scenario("cs/roomver-upgrade-new-user-joins-old-room",
          tags=["roomver", "upgrade", "membership"], step=1)
def upgrade_new_user_joins_old_room(c):
    """Nothing in room_upgrades.md's server procedure revokes the old
    room's join_rules or membership algorithm -- the tombstone is
    documented purely as a client-side hint ('Client behaviour' above in
    the same module). This scenario pins what a brand-new user, never a
    member of the old room, actually observes joining a *public* old room
    after its upgrade: the join itself still succeeds (join_rules is
    untouched by the upgrade procedure), the tombstone is visible once
    joined, but the same post-upgrade events_default restriction
    (cs/roomver-upgrade-old-room-pl-restriction) still blocks them from
    posting, at the users_default=0 floor case (events_default becomes
    exactly max(50, 0+1) = 50).
    """
    u = register(c, "njo1")
    room = create_room(c, u, room_version="1", preset="public_chat")
    c.req("POST", f"{CS}/rooms/{q(room)}/upgrade", token=u.token,
          json_body={"new_version": "6"}, check=False)

    v = register(c, "njo2")
    c.req("POST", f"{CS}/rooms/{q(room)}/join", token=v.token, json_body={},
          label="a brand-new user can still join a public old (tombstoned) room")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.tombstone/", token=v.token,
          label="...and can see the tombstone once joined",
          derive=lambda b: "replacement_room" in b)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(v.user_id)}",
          token=v.token,
          label="...their membership in the old room is a normal join",
          derive=lambda b: b.get("membership"))
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/newbie", token=v.token,
          json_body={"msgtype": "m.text", "body": "hi"},
          label="...but is blocked from sending, by the same post-upgrade events_default restriction")


# --------------------------------------------------------------------------- #
# Common authorization rules: identical text across all six room versions
# (rooms/fragments/v1-auth-rules.md == v3-auth-rules.md; v6.md keeps these
# rules, only renumbered). Pinned here because step01_rooms_core.py's
# power-level/membership scenarios do not exercise these specific clauses.
@scenario("cs/roomver-auth-power-level-non-integer",
          tags=["roomver", "power-levels", "auth"], step=1)
def auth_power_level_non_integer(c):
    """Auth rule 10.1 (v1/v3-5) / 9.1 (v6): 'If users key in content is not
    a dictionary with keys that are valid user IDs with values that are
    integers (OR A STRING THAT IS AN INTEGER), reject.' Identical wording
    at v1 and v6, so pinned once at each to confirm it really is invariant.

    The rule text's own numeral-string carve-out turns out to be
    unreachable through the client-server API: confirmed empirically
    (a standalone diagnostic against pinned Synapse, not an assumption)
    that Synapse's own request-body JSON-schema validation for this
    endpoint requires a true JSON integer and rejects a numeral *string*
    with 400 M_BAD_JSON -- before the room-version auth algorithm this
    scenario is pinning is ever consulted. So all three of a non-numeral
    string, a numeral string, and a real integer are exercised here, and
    only the real integer is accepted.
    """
    for version in ("1", "6"):
        u = register(c, f"pni{version}")
        room = create_room(c, u, room_version=version, preset="public_chat")
        pl = c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
                   token=u.token, check=False).json

        bad = dict(pl)
        bad["users"] = dict(pl.get("users", {}))
        bad["users"]["@nobody:" + c.server_name] = "not-an-integer"
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
              token=u.token, json_body=bad,
              label=f"v{version}: a users{{}} value that cannot even parse "
                    "as an integer is rejected")

        # A numeral *string* is what `rooms/fragments/v1-stringy-power-levels.md`
        # explicitly allows at these versions ("each of the integer-valued
        # properties ... can be encoded as strings instead of integers"), and
        # the reference refuses it before the room-version rules are consulted.
        # Both are defensible, so the request is made and not scored, and an
        # accepting server's event id binds no symbol (Ctx.req `learn`).
        numeral_string = dict(pl)
        numeral_string["users"] = dict(pl.get("users", {}))
        numeral_string["users"]["@nobody:" + c.server_name] = "50"
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
              token=u.token, json_body=numeral_string, check=False,
              learn=False)

        good = dict(pl)
        good["users"] = dict(pl.get("users", {}))
        good["users"]["@nobody:" + c.server_name] = 50
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
              token=u.token, json_body=good,
              label=f"v{version}: a genuine JSON integer value is accepted")


@scenario("cs/roomver-auth-users-default-above-own",
          tags=["roomver", "power-levels", "auth"], step=1)
def auth_users_default_above_own(c):
    """Auth rule 10.3 (v1/v3-5) / 9.3 (v6): for users_default (among
    others), 'if the new value is higher than the sender's current power
    level, reject.' Unchanged across all six room versions; pinned at v6.

    The member is put at exactly the power level actually REQUIRED to send
    an m.room.power_levels event in this room at all -- read from the
    room's own events["m.room.power_levels"] override, not assumed to be
    state_default. (Confirmed empirically, via a standalone diagnostic
    against pinned Synapse: its default power_levels content overrides
    m.room.power_levels' own required send level to 100, well above
    state_default's 50, for every room version -- so a member merely at
    state_default could never send a power_levels event at all, which
    would test the send gate, not this rule.) At exactly the required
    level, the member is shown unable to raise users_default *above* their
    own level, while raising it to *exactly* that level (not higher) is
    allowed -- the boundary the rule text turns on.
    """
    u = register(c, "uda1")
    v = register(c, "uda2")
    room = create_room(c, u, room_version="6", preset="public_chat")
    join(c, v, room, check=False)
    pl = c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
               token=u.token, check=False).json
    required = pl.get("events", {}).get("m.room.power_levels",
                                          pl.get("state_default", 50))
    pl["users"] = dict(pl.get("users", {}))
    pl["users"][v.user_id] = required
    set_state(c, u, room, "m.room.power_levels", pl, check=False)

    attempt = dict(pl)
    attempt["users_default"] = required + 10
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=v.token, json_body=attempt,
          label="a member at exactly the power_levels-send-required level "
                "cannot raise users_default above their own level")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=u.token,
          label="users_default is unchanged by the rejected attempt",
          derive=lambda b: b.get("users_default"))

    attempt_ok = dict(pl)
    attempt_ok["users_default"] = required
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=v.token, json_body=attempt_ok,
          label="...but raising it to exactly their own level (not higher) is allowed")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
          token=u.token,
          label="...and users_default is now exactly the member's own level",
          derive=lambda b: b.get("users_default"))


@scenario("cs/roomver-auth-invalid-membership",
          tags=["roomver", "membership", "auth"], step=1)
def auth_invalid_membership(c):
    """Membership auth rule, final catch-all (rule 5.6 in v1/v3-5, rule 4.6
    in v6 -- the sub-algorithm's own numbering shifts because v6 drops the
    old rule 4 for m.room.aliases, but the membership text itself is
    unchanged): 'Otherwise, the membership is unknown. Reject.' A value
    for content.membership other than join/invite/leave/ban is rejected
    outright, at both v1 and v6.
    """
    for version in ("1", "6"):
        u = register(c, f"aim{version}")
        room = create_room(c, u, room_version=version, preset="public_chat")
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(u.user_id)}",
              token=u.token, json_body={"membership": "not-a-real-membership"},
              label=f"v{version}: an unrecognised membership value is rejected")
        c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(u.user_id)}",
              token=u.token,
              label=f"v{version}: the member's real membership is unaffected",
              derive=lambda b: b.get("membership"))


# --------------------------------------------------------------------------- #
# Two directly-observable v1-vs-v6 AUTHORIZATION-rule differences (not
# merely redaction-content differences): both hinge on values the client
# freely chooses (a state_key string; a power-level number), so -- unlike
# the redaction domain-match clause -- they need no second real homeserver
# to observe.
@scenario("cs/roomver-aliases-domain-mismatch-auth",
          tags=["roomver", "auth", "aliases"], step=1)
def aliases_domain_mismatch_auth(c):
    """matrix-spec/content/rooms/fragments/v1-auth-rules.md rule 4 (also in
    v3-auth-rules.md, so unchanged through v5): 'If type is m.room.aliases:
    ... If sender's domain doesn't match state_key, reject.' vs
    matrix-spec/content/rooms/v6.md: 'Rule 4, which related specifically to
    events of type m.room.aliases, is removed. m.room.aliases events must
    still pass authorization checks relating to state events [i.e. only
    the generic state-event rules still apply].' A state_key that is
    simply not our server's own name is a 'domain mismatch' regardless of
    how many real servers exist, so this is observable on a single,
    non-federated homeserver.
    """
    for version, expect_note in (("1", "rejected -- rule 4.2 still applies"),
                                  ("6", "allowed -- rule 4 removed")):
        u = register(c, f"adm{version}")
        room = create_room(c, u, room_version=version, preset="public_chat")
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.aliases/"
                     f"{q('not-our-domain.example')}",
              token=u.token, json_body={"aliases": []},
              label=f"v{version}: m.room.aliases state_key domain mismatch is {expect_note}")
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.aliases/{q(c.server_name)}",
              token=u.token, json_body={"aliases": []},
              label=f"v{version}: m.room.aliases with the sender's own domain as "
                    "state_key is allowed at both versions")


@scenario("cs/roomver-power-levels-notifications-auth",
          tags=["roomver", "auth", "power-levels"], step=1)
def power_levels_notifications_auth(c):
    """matrix-spec/content/rooms/v6.md: 'the authorization rules for events
    of type m.room.power_levels now include the content key notifications.
    This new rule takes the place of rule 10.4, which checked the events
    and users keys' -- so as of v6, rule 9.4 also covers notifications
    entries ('if the current/new value is higher than the sender's current
    power level, reject'), where v1 through v5's rule 10.4 only ever
    covered events and users. A member is put at exactly the power level
    actually REQUIRED to send an m.room.power_levels event at all (read
    from the room's own events["m.room.power_levels"] override -- see
    cs/roomver-auth-users-default-above-own's docstring for why this,
    rather than state_default, must be used), then sets notifications.room
    to exactly one above their own level and changes nothing else: v1-v5
    must allow it (untouched by rule 10.4); v6 must reject it (caught by
    the new rule 9.4).
    """
    for version, expect_note in (
        ("1", "allowed -- notifications is not checked by rule 10.4"),
        ("6", "rejected -- caught by the new rule 9.4"),
    ):
        u = register(c, f"pln{version}")
        v = register(c, f"pln{version}b")
        room = create_room(c, u, room_version=version, preset="public_chat")
        join(c, v, room, check=False)
        pl = c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
                   token=u.token, check=False).json
        required = pl.get("events", {}).get("m.room.power_levels",
                                              pl.get("state_default", 50))
        pl["users"] = dict(pl.get("users", {}))
        pl["users"][v.user_id] = required
        set_state(c, u, room, "m.room.power_levels", pl, check=False)

        attempt = dict(pl)
        attempt["notifications"] = dict(pl.get("notifications", {}))
        attempt["notifications"]["room"] = required + 1
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
              token=v.token, json_body=attempt,
              label=f"v{version}: notifications.room set one above sender's "
                    f"own level is {expect_note}")
        c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
              token=u.token,
              label=f"v{version}: resulting notifications content",
              derive=lambda b: b.get("notifications"))


# --------------------------------------------------------------------------- #
# Canonical JSON: matrix-spec/content/appendices.md #canonical-json defines
# it (integers restricted to [-(2**53)+1, (2**53)-1]; floats forbidden).
# fragments/v1-canonical-json.md (used by v1-v5): 'Servers MUST NOT
# strictly enforce' it. fragments/v6-canonical-json.md (v6 only): 'Servers
# MUST strictly enforce' it, '400 M_BAD_JSON ... on most endpoints.'
@scenario("cs/roomver-canonical-json-roundtrip",
          tags=["roomver", "canonical-json"], step=1)
def canonical_json_roundtrip(c):
    """v1-5's explicit leniency (fragments/v1-canonical-json.md), pinned at
    v1: an out-of-canonical-range integer (beyond 2**53-1), a float value,
    an empty string used as a content key, and a non-BMP unicode character
    (outside the Basic Multilingual Plane, so UTF-16-based JSON encoders
    would need a surrogate pair) in a value, must all round-trip exactly.
    Contrast with cs/roomver-canonical-json-v6-strictness, which throws the
    same integer at a v6 room.
    """
    u = register(c, "cjr1")
    room = create_room(c, u, room_version="1", preset="public_chat")

    mid1 = send(c, u, room, "cjr1",
                {"msgtype": "m.text", "body": "big int",
                 "an_out_of_canonical_range_integer": 2**60}, check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid1)}", token=u.token,
          label="v1: an out-of-canonical-json-range integer (beyond 2**53-1) round-trips",
          derive=lambda b: b.get("content", {}).get("an_out_of_canonical_range_integer"))

    mid2 = send(c, u, room, "cjr2",
                {"msgtype": "m.text", "body": "float",
                 "a_float_value": 3.14159}, check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid2)}", token=u.token,
          label="v1: a float value (forbidden by canonical JSON) round-trips",
          derive=lambda b: b.get("content", {}).get("a_float_value"))

    mid3 = send(c, u, room, "cjr3",
                {"msgtype": "m.text", "body": "surrogate-pair emoji: \U0001F600",
                 "": "empty-string-key"}, check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(mid3)}", token=u.token,
          label="v1: an empty-string content key and a non-BMP unicode character round-trip",
          derive=lambda b: b.get("content"))

    set_state(c, u, room, "m.room.topic", {"topic": "big", "n": 2**60}, check=False)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="v1: the same out-of-range integer round-trips through a STATE event, too",
          derive=lambda b: b.get("n"))


@scenario("cs/roomver-canonical-json-v6-strictness",
          tags=["roomver", "canonical-json"], step=1)
def canonical_json_v6_strictness(c):
    """fragments/v6-canonical-json.md: 'Servers MUST strictly enforce the
    JSON format ... This translates to a 400 M_BAD_JSON error on most
    endpoints' -- the exact opposite of v1-5's explicit leniency. Both
    M_BAD_JSON and M_UNKNOWN are excluded from errcode comparison by this
    harness (mxconf/normalize.py), so what is actually pinned here is the
    *status class* (2xx vs 4xx): the same payloads that
    cs/roomver-canonical-json-roundtrip shows a v1 room accepting are
    thrown at a v6 room instead, recording whatever pinned Synapse
    actually does rather than assuming a rejection -- including for a
    bare NaN value, which is not valid JSON at all.
    """
    u = register(c, "cjv6")
    room = create_room(c, u, room_version="6", preset="public_chat")

    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/v6big",
          token=u.token,
          json_body={"msgtype": "m.text", "body": "big int",
                     "an_out_of_canonical_range_integer": 2**60},
          label="v6: an out-of-canonical-json-range integer, strictly enforced (contrast with v1)")
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/v6float",
          token=u.token,
          json_body={"msgtype": "m.text", "body": "float", "a_float_value": 3.14159},
          label="v6: a float value, strictly enforced (contrast with v1)")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/",
          token=u.token, json_body={"topic": "big", "n": 2**60},
          label="v6: the same out-of-range integer in a STATE event, strictly enforced too")
    c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/v6nan",
          token=u.token,
          json_body={"msgtype": "m.text", "body": "nan", "a_nan_value": float("nan")},
          label="v6: a bare NaN value (not valid JSON at all) -- pin whatever Synapse actually does")


# --------------------------------------------------------------------------- #
# Two more common (all-version) authorization rules, chosen because they are
# reachable through ordinary client-server calls (unlike auth_events-shape
# rules 2/3, which the server computes itself) and because the second one
# directly explains the "generic state-event checks still apply" sentence
# in v6.md's own description of the m.room.aliases rule-4 removal.
@scenario("cs/roomver-second-create-event-rejected",
          tags=["roomver", "auth", "create"], step=1)
def second_create_event_rejected(c):
    """v1-auth-rules.md rule 1.1 (unchanged in v3-auth-rules.md, so v1-v5;
    v6.md keeps it, verbatim, as rule 1.1: 'If it has any previous events,
    reject.') A second m.room.create sent into an already-populated room
    necessarily has previous events at its DAG position, so it is rejected
    structurally -- by where it falls in the room's history, not by a
    power-level gate. Pinned at both v1 and v6 to confirm the rule text is
    truly unchanged; a second, lower-power (just-joined) member's identical
    attempt is also rejected, showing the rejection does not depend on who
    is attempting it.
    """
    for version in ("1", "6"):
        u = register(c, f"sc{version}")
        v = register(c, f"sc{version}b")
        room = create_room(c, u, room_version=version, preset="public_chat")
        join(c, v, room, check=False)

        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=u.token,
              json_body={"creator": u.user_id, "room_version": version},
              label=f"v{version}: the room's own creator cannot insert a second m.room.create")
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=v.token,
              json_body={"creator": v.user_id, "room_version": version},
              label=f"v{version}: neither can a lower-power member -- rejection is structural, not a power-level gate")
        c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=u.token,
              label=f"v{version}: the original create content is unaffected by both attempts",
              derive=lambda b: {"creator": b.get("creator"), "room_version": b.get("room_version")})
        c.req("GET", f"{CS}/rooms/{q(room)}/state", token=u.token,
              label=f"v{version}: the room's state still contains exactly one m.room.create event",
              derive=lambda b: len([e for e in b if e.get("type") == "m.room.create"]))


@scenario("cs/roomver-state-key-sigil-auth",
          tags=["roomver", "auth"], step=1)
def state_key_sigil_auth(c):
    """v1-auth-rules.md rule 9 (identical text in v3-auth-rules.md, so
    v1-v5) / v6.md rule 8 (renumbered only, because v6 drops the old rule 4
    for m.room.aliases -- both read, verbatim: 'If the event has a
    state_key that starts with an @ and does not match the sender,
    reject.') This is the generic rule v6.md itself points to when it says
    'm.room.aliases events must still pass authorization checks relating
    to state events' after removing the aliases-specific rule 4 -- so it
    is pinned here on an arbitrary custom state event type, independently
    of m.room.aliases, at both v1 and v6, to confirm it really is
    identical (unlike rule 4) across every room version.
    """
    for version in ("1", "6"):
        u = register(c, f"sig{version}")
        v = register(c, f"sig{version}b")
        room = create_room(c, u, room_version=version, preset="public_chat")
        join(c, v, room, check=False)
        etype = "org.example.spec_test_state"

        c.req("PUT", f"{CS}/rooms/{q(room)}/state/{etype}/{q(u.user_id)}",
              token=u.token, json_body={"x": 1},
              label=f"v{version}: a state_key of the sender's own user id "
                    "(starts with @, matches sender) is allowed")
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/{etype}/{q(v.user_id)}",
              token=u.token, json_body={"x": 1},
              label=f"v{version}: a state_key that starts with @ but names a "
                    "DIFFERENT (real) user is rejected")
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/{etype}/"
                     f"{q('@nobody:' + c.server_name)}",
              token=u.token, json_body={"x": 1},
              label=f"v{version}: ...even when that @-prefixed id belongs to "
                    "no real user, it is still rejected")
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/{etype}/{q('somekey')}",
              token=u.token, json_body={"x": 1},
              label=f"v{version}: a state_key not starting with @ at all is "
                    "unaffected by this rule")
        c.req("PUT", f"{CS}/rooms/{q(room)}/state/{etype}/",
              token=u.token, json_body={"x": 1},
              label=f"v{version}: an empty-string state_key (does not start "
                    "with @) is likewise unaffected")
