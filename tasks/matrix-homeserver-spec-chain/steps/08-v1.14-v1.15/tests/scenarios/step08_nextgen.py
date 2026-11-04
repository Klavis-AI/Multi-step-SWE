"""Step 8 — the additions of specification releases v1.14 and v1.15.

Covered here:

* `GET /_matrix/client/v1/room_summary/{roomIdOrAlias}` (v1.15),
* the `m.topic` content block on `m.room.topic` (v1.15),
* `POST /users/{userId}/report` (v1.14),
* `allowed_room_ids` / `encryption` / `room_version` in the entries of
  `GET /_matrix/client/v1/rooms/{roomId}/hierarchy` (v1.15),
* `/rooms/{roomId}/initialSync`, no longer deprecated (v1.15),
* `via` on `POST /join` and `POST /knock`, and the removal of `server_name`,
* `GET /_matrix/client/v1/auth_metadata`, the discovery endpoint of the
  next-generation (OAuth 2.0) authentication API (v1.15), and the token
  refusal that API specifies,
* `/versions` and `/capabilities` at this release.

Nothing here pins a value the release leaves to the implementation.  Where the
release permits a choice (which spec versions a deployment advertises, which
room versions it offers, whether `allowed_room_ids` is carried on the client
hierarchy), the check is written with `derive=` as a question a conforming
server answers the same way whichever choice it made.
"""

import json
import re

from mxconf.mx import (CS, CS1, create_room, fact, invite, join, q, register, send,
                       set_state, sync)
from mxconf.registry import scenario

# Every room is created at an explicit version: the default is server-chosen.
V = "11"
# A second version, so a hierarchy entry's `room_version` says something.
V10 = "10"
# A server name nothing ever listens on, so a request routed to it fails at
# once and identically on every run.  It tells "this server acted on the host I
# named" apart from "this server refused without trying".
DEAD = "127.0.0.1:1"

SPEC_ROOM_VERSIONS = [str(n) for n in range(1, 12)]     # v1.15 publishes 1..11
VERSION_RE = re.compile(r"^(?:v\d+\.\d+|r\d+\.\d+\.\d+)$")


# --------------------------------------------------------------------------- #
# derives
# --------------------------------------------------------------------------- #
# What `GET /_matrix/client/v1/room_summary/{roomIdOrAlias}` must carry, and
# what it may.  The release fixes the required set; a server that adds a
# documented optional field, or one of its own, has not said anything
# different, so the *required* ones are what is compared.
SUMMARY_REQUIRED = ("room_id", "num_joined_members", "world_readable",
                    "guest_can_join")


def summary_facts(body):
    """The room-summary fields release v1.15 documents, as facts."""
    if not isinstance(body, dict):
        return {"not_an_object": True}
    return {
        "required_fields_present": sorted(f for f in SUMMARY_REQUIRED
                                          if f in body),
        # Present and an integer; not its value -- see
        # `ng/summary-membership-values` for why the count itself is not pinned.
        "num_joined_members_is_a_count":
            isinstance(body.get("num_joined_members"), int),
        # "When not present, the room is assumed to be `public`" -- the
        # release's own default, so the two spellings are read the same way.
        "join_rule": body.get("join_rule", "public"),
        "world_readable": body.get("world_readable"),
        "guest_can_join": body.get("guest_can_join"),
        "membership": body.get("membership", "<absent>"),
        # `room_type` and `encryption` are absent here because these rooms are
        # neither a space nor encrypted: a server reporting either would be
        # reporting something untrue, so absence is the answer and not a pin.
        "room_type": body.get("room_type", "<absent>"),
        "encryption": body.get("encryption", "<absent>"),
        # `room_version` and `topic` are optional in the summary schema, and
        # the ticket asks for four fields and a membership.  So a server that
        # reports them must report them right, and one that leaves them out is
        # conforming -- which is what these two say, rather than their values.
        "room_version_if_reported_is_this_rooms":
            body.get("room_version") in (None, V),
        "topic_if_reported_is_a_string":
            body.get("topic") is None or isinstance(body.get("topic"), str),
    }


def topic_block(body):
    """An `m.room.topic` content reduced to what v1.15 defines for it."""
    if not isinstance(body, dict):
        return {"not_an_object": True}
    block = body.get("m.topic")
    out = {
        "topic": body.get("topic", "<absent>"),
        "has_m_topic": isinstance(block, dict),
    }
    if isinstance(block, dict):
        reps = block.get("m.text")
        out["m_text_is_an_array"] = isinstance(reps, list)
        if isinstance(reps, list):
            out["m_text"] = [
                # "mimetype: The mime type of the representation ... Defaults
                # to text/plain", so a server that says `text/plain` and one
                # that leaves it out have said the same thing.
                {"mimetype": r.get("mimetype") or "text/plain",
                 "body": r.get("body")}
                if isinstance(r, dict) else {"not_an_object": True}
                for r in reps
            ]
    return out


def sync_topics(body):
    """Every `m.room.topic` a /sync response carries, wherever it carries it."""
    found = []
    joined = (body.get("rooms") or {}).get("join") if isinstance(body, dict) else None
    for room in (joined or {}).values():
        for where in ("state", "timeline"):
            for e in ((room.get(where) or {}).get("events") or []):
                if e.get("type") == "m.room.topic":
                    found.append({"where": where,
                                  "content": topic_block(e.get("content") or {})})
    found.sort(key=lambda item: json.dumps(item, sort_keys=True))
    return {"topics": found}


def initial_sync_shape(body):
    """A room snapshot in the terms release v1.15 defines for this endpoint.

    Only the fields that release's own schema declares are looked at: the
    reference also answers with `presence` and `receipts`, which v1.15 does not
    define here, so they are no business of this check.
    """
    if not isinstance(body, dict):
        return {"not_an_object": True}
    messages = body.get("messages") or {}
    return {
        "has_room_id": "room_id" in body,
        "has_messages": "messages" in body,
        "has_state": "state" in body,
        # The published enum includes `leave`, and the release does not say
        # what a *peek* reports, so what is compared is that whatever comes
        # back is in the enum -- and, where the caller is a member, that it is
        # the membership they hold (`ng/initialsync-room-get`'s own check).
        "membership_is_one_the_release_names":
            body.get("membership") in (None, "invite", "join", "leave", "ban",
                                       "knock"),
        # `chunk` and `end` are the required keys; `start` "may be omitted ...
        # if no earlier events are available", so its presence is the server's
        # to decide.  And the release does not order this chunk, so the types
        # are compared as a multiset.
        "message_fields_required_present":
            isinstance(messages, dict) and {"chunk", "end"} <= set(messages),
        "chunk_types": sorted(e.get("type") or ""
                              for e in (messages.get("chunk") or [])),
        "state": sorted({(e.get("type"), e.get("state_key"))
                         for e in (body.get("state") or [])}),
        "topic": [topic_block(e.get("content") or {})
                  for e in (body.get("state") or [])
                  if e.get("type") == "m.room.topic"],
    }


def hierarchy_entries(body):
    """One fact per hierarchy entry: the v1.15 additions, keyed by room name."""
    out = {}
    for room in (body.get("rooms") or []):
        allowed = room.get("allowed_room_ids")
        out[room.get("name") or "<unnamed>"] = {
            "room_version": room.get("room_version", "<absent>"),
            "encryption": room.get("encryption", "<absent>"),
            "join_rule": room.get("join_rule", "<absent>"),
            "room_type": room.get("room_type", "<absent>"),
            # v1.15 declares `allowed_room_ids` optional on this endpoint, so
            # the question asked is one a conforming server answers the same
            # way whether or not it chooses to carry the field.
            "allowed_room_ids_absent_or_a_list_of_rooms":
                allowed is None or (isinstance(allowed, list)
                                    and all(isinstance(r, str) and r.startswith("!")
                                            for r in allowed)),
            "children": [str(e.get("state_key"))
                         for e in (room.get("children_state") or [])],
        }
    return {"entries": out, "has_next_batch": "next_batch" in (body or {})}


def room_version_facts(body):
    """`m.room_versions` as facts, never as the literal map: which versions are
    listed, and whether the default is one of them."""
    caps = (body.get("capabilities") or {}).get("m.room_versions") or {}
    available = caps.get("available")
    default = caps.get("default")
    if not isinstance(available, dict):
        return {"available_is_a_map": False}
    return {
        "available_is_a_map": True,
        "default_is_a_string": isinstance(default, str),
        "default_is_listed": default in available,
        "default_is_listed_stable": available.get(default) == "stable",
        "default_is_a_version_this_release_defines": default in SPEC_ROOM_VERSIONS,
        # Every version release v1.15 publishes a page for is offered...
        "every_release_version_listed": all(v in available
                                            for v in SPEC_ROOM_VERSIONS),
        # ...and offered as stable, none of them being experimental here.
        "every_release_version_stable": all(available.get(v) == "stable"
                                            for v in SPEC_ROOM_VERSIONS),
        "every_value_is_stable_or_unstable": all(
            v in ("stable", "unstable") for v in available.values()),
    }


def versions_facts(body):
    """`/versions` as facts: the release grammar and the range advertised."""
    vs = body.get("versions")
    if not isinstance(vs, list):
        return {"versions_is_a_list": False}
    v1 = [v for v in vs if isinstance(v, str) and v.startswith("v1.")]
    nums = sorted(int(v.split(".", 1)[1]) for v in v1 if v.split(".", 1)[1].isdigit())
    features = body.get("unstable_features") or {}
    return {
        "versions_is_a_list": True,
        "all_well_formed": all(isinstance(v, str) and VERSION_RE.match(v) for v in vs),
        "no_duplicates": len(vs) == len(set(vs)),
        "advertises_v1_1": "v1.1" in vs,
        # A server that has absorbed these releases advertises at least every
        # release the chain has already implemented...
        "advertises_every_release_up_to_v1_12": all(f"v1.{n}" in vs
                                                    for n in range(1, 13)),
        # ...and nothing beyond v1.15, which is the newest release there is.
        "claims_no_release_after_v1_15": all(n <= 15 for n in nums),
        "v1_releases_are_contiguous": (nums == list(range(1, max(nums) + 1)))
                                      if nums else False,
        "unstable_features_are_booleans": all(isinstance(v, bool)
                                              for v in features.values()),
    }


def login_flow_facts(body):
    """Legacy-API discovery: `GET /login` still answers with a flow list."""
    flows = body.get("flows")
    return {
        "flows_is_a_list": isinstance(flows, list),
        "every_flow_has_a_type": isinstance(flows, list) and all(
            isinstance(f, dict) and isinstance(f.get("type"), str) for f in flows),
        "offers_m_login_password": isinstance(flows, list) and any(
            isinstance(f, dict) and f.get("type") == "m.login.password" for f in flows),
    }


# --------------------------------------------------------------------------- #
# GET /_matrix/client/v1/room_summary/{roomIdOrAlias}
# --------------------------------------------------------------------------- #
@scenario("ng/summary-joined", tags=["room-summary", "v1.15"], step=8)
def summary_joined(c):
    """The summary of a room the caller is in."""
    u = register(c, "sj1")
    v = register(c, "sj2")
    room = create_room(c, u, room_version=V, preset="public_chat",
                       name="Summary Room", topic="A plain topic")
    # The raw body is not compared: it would require every optional field the
    # reference chose to send (`name`, `join_rule`, `room_version`, `topic`),
    # and the schema requires four.  `summary_facts` below says what the
    # release fixes, and says it in a way a server that sends fewer fields can
    # still satisfy.
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="summary of a room the caller is joined to",
          derive=lambda b: {"name_if_reported_is_the_rooms":
                            b.get("name") in (None, "Summary Room")})
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="the fields the summary carries", derive=summary_facts)
    join(c, v, room)
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="num_joined_members follows the membership",
          derive=lambda b: {"n": b.get("num_joined_members")})
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          label="summary as the second member")
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=v.token, json_body={},
          check=False)
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          label="membership after leaving", derive=summary_facts)
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="the creator still sees a join", derive=summary_facts)


@scenario("ng/summary-public-not-joined", tags=["room-summary", "v1.15"], step=8)
def summary_public_not_joined(c):
    """A public room the caller is not in, authenticated and not."""
    u = register(c, "sp1")
    v = register(c, "sp2")
    room = create_room(c, u, room_version=V, preset="public_chat",
                       name="Open Room", topic="Anyone may look")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          label="summary of a public room the caller is not in")
    c.req("GET", f"{CS1}/room_summary/{q(room)}",
          label="the same summary without authentication")
    c.req("GET", f"{CS1}/room_summary/{q(room)}",
          label="membership is absent when unauthenticated",
          derive=lambda b: {"has_membership": "membership" in b})
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          label="membership is leave for a stranger who is authenticated",
          derive=lambda b: {"membership": b.get("membership")})
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token="not-a-real-token",
          label="an invalid token is refused rather than treated as absent",
          drop=("soft_logout",))
    private = create_room(c, u, room_version=V, preset="private_chat",
                          name="Closed Room", topic="Members only")
    c.req("GET", f"{CS1}/room_summary/{q(private)}", token=v.token,
          label="a room the caller may not see")
    c.req("GET", f"{CS1}/room_summary/{q(private)}",
          label="the same room unauthenticated")


@scenario("ng/summary-by-alias", tags=["room-summary", "v1.15"], step=8)
def summary_by_alias(c):
    """The summary may be asked for by alias as well as by room id."""
    u = register(c, "sa1")
    v = register(c, "sa2")
    room = create_room(c, u, room_version=V, preset="public_chat",
                       room_alias_name="ng-summary", visibility="public",
                       name="Aliased", topic="By alias")
    alias = f"#ng-summary:{c.server_name}"
    c.req("GET", f"{CS1}/room_summary/{q(alias)}", token=v.token,
          label="summary by alias")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          label="summary by room id")
    c.req("GET", f"{CS1}/room_summary/{q(alias)}", token=v.token,
          label="the alias resolves to the same room", derive=summary_facts)
    c.req("GET", f"{CS1}/room_summary/{q(f'#ng-nothing:{c.server_name}')}",
          token=v.token, label="an alias that resolves to nothing")
    c.req("GET", f"{CS1}/room_summary/{q('not-an-identifier')}", token=v.token,
          label="an identifier that is neither a room id nor an alias")


@scenario("ng/summary-via", tags=["room-summary", "v1.15"], step=8)
def summary_via(c):
    """`via` names servers to ask; a room this server already holds is answered
    from its own state whatever `via` says."""
    u = register(c, "sv1")
    v = register(c, "sv2")
    room = create_room(c, u, room_version=V, preset="public_chat",
                       name="Via Room", topic="Local")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          query={"via": DEAD}, label="via is ignored for a room held locally")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          query={"via": [DEAD, "second.invalid"]},
          label="several via are accepted", derive=summary_facts)
    c.req("GET", f"{CS1}/room_summary/{q(room)}", query={"via": c.server_name},
          label="via naming this server, unauthenticated")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          query={"via": []}, label="an empty via list")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          label="the same room without via at all", derive=summary_facts)


@scenario("ng/summary-missing-room", tags=["room-summary", "v1.15"], step=8)
def summary_missing_room(c):
    """A room this server cannot summarise."""
    u = register(c, "sm1")
    create_room(c, u, room_version=V, preset="public_chat", name="Exists")
    c.req("GET", f"{CS1}/room_summary/{q(f'#ng-absent:{c.server_name}')}",
          token=u.token, label="an alias that does not exist")
    c.req("GET", f"{CS1}/room_summary/{q(f'#ng-absent:{c.server_name}')}",
          label="the same, unauthenticated")
    c.req("GET", f"{CS1}/room_summary/{q(f'!ng-absent:{c.server_name}')}",
          token=u.token, label="a room id this server does not know")
    c.req("GET", f"{CS1}/room_summary/{q('!ng-absent:other.invalid')}",
          token=u.token, label="a room id on a server this one cannot reach")
    c.req("GET", f"{CS1}/room_summary/{q(f'#ng-absent:{c.server_name}')}",
          token=u.token, label="the refusal names no room",
          derive=lambda b: {"errcode": b.get("errcode"),
                            "leaks_a_room_id": "room_id" in b})


@scenario("ng/summary-room-fields", tags=["room-summary", "v1.15"], step=8)
def summary_room_fields(c):
    """`room_type`, `encryption`, `room_version` and `allowed_room_ids`."""
    u = register(c, "sf1")
    space = create_room(c, u, room_version=V, preset="public_chat", name="NG Space",
                        creation_content={"type": "m.space"})
    child = create_room(c, u, room_version=V, name="NG Child", initial_state=[
        {"type": "m.room.join_rules", "state_key": "",
         "content": {"join_rule": "restricted",
                     "allow": [{"type": "m.room_membership", "room_id": space}]}},
        {"type": "m.room.encryption", "state_key": "",
         "content": {"algorithm": "m.megolm.v1.aes-sha2"}},
    ])
    plain = create_room(c, u, room_version=V10, preset="public_chat", name="NG Plain")
    c.req("GET", f"{CS1}/room_summary/{q(space)}", token=u.token,
          label="a space carries room_type")
    c.req("GET", f"{CS1}/room_summary/{q(child)}", token=u.token,
          label="a restricted, encrypted room")
    c.req("GET", f"{CS1}/room_summary/{q(child)}", token=u.token,
          label="encryption and allowed_room_ids as facts", derive=summary_facts)
    c.req("GET", f"{CS1}/room_summary/{q(child)}", token=u.token,
          label="allowed_room_ids names the space",
          derive=lambda b: {"allowed": b.get("allowed_room_ids"),
                            "join_rule": b.get("join_rule")})
    c.req("GET", f"{CS1}/room_summary/{q(plain)}", token=u.token,
          label="a room at another version carries that version",
          derive=lambda b: {"room_version": b.get("room_version", "<absent>"),
                            "has_encryption": "encryption" in b,
                            "has_room_type": "room_type" in b,
                            # "Empty or omitted" for a room whose join rule is
                            # not restricted, so both are the same answer.
                            "names_any_allowed_rooms":
                                bool(b.get("allowed_room_ids"))})
    c.req("GET", f"{CS1}/room_summary/{q(plain)}", token=u.token,
          label="an unencrypted, untyped room")


@scenario("ng/summary-membership-values", tags=["room-summary", "v1.15"], step=8)
def summary_membership_values(c):
    """Every membership the summary may report."""
    u = register(c, "mv1")
    v = register(c, "mv2")
    w = register(c, "mv3")
    knockable = create_room(c, u, room_version=V, name="Knockable",
                            initial_state=[{"type": "m.room.join_rules",
                                            "state_key": "",
                                            "content": {"join_rule": "knock"}}])
    open_room = create_room(c, u, room_version=V, preset="public_chat", name="Open")
    c.req("GET", f"{CS1}/room_summary/{q(knockable)}", token=v.token,
          label="membership before knocking",
          derive=lambda b: {"membership": b.get("membership"),
                            "join_rule": b.get("join_rule")})
    c.req("POST", f"{CS}/knock/{q(knockable)}", token=v.token, json_body={},
          query={"via": c.server_name}, check=False)
    c.req("GET", f"{CS1}/room_summary/{q(knockable)}", token=v.token,
          label="membership after knocking",
          derive=lambda b: {"membership": b.get("membership")})
    invite(c, u, open_room, w.user_id)
    c.req("GET", f"{CS1}/room_summary/{q(open_room)}", token=w.token,
          label="membership while invited",
          derive=lambda b: {"membership": b.get("membership")})
    join(c, w, open_room)
    # `num_joined_members` is "the number of members joined to the room", and
    # the recording reference reports one more than the room has -- its summary
    # counts from a statistics table rather than from the room's state.  The
    # release is unambiguous, so the count is not pinned here; `ng/summary-
    # room-fields` still checks that the field is present and an integer.
    c.req("GET", f"{CS1}/room_summary/{q(open_room)}", token=w.token,
          label="membership after joining",
          derive=lambda b: {"membership": b.get("membership")})
    c.req("POST", f"{CS}/rooms/{q(open_room)}/ban", token=u.token,
          json_body={"user_id": w.user_id}, check=False)
    c.req("GET", f"{CS1}/room_summary/{q(open_room)}", token=w.token,
          label="membership after being banned",
          derive=lambda b: {"membership": b.get("membership")})
    c.req("GET", f"{CS1}/room_summary/{q(open_room)}", token=u.token,
          label="the room itself is unchanged", derive=summary_facts)


# --------------------------------------------------------------------------- #
# the m.topic content block
# --------------------------------------------------------------------------- #
@scenario("ng/topic-block-set-and-read", tags=["topic", "v1.15"], step=8)
def topic_block_set_and_read(c):
    """`m.room.topic` set with both `topic` and `m.topic`, read back."""
    u = register(c, "tb1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Topic Room")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          json_body={"topic": "The plain topic",
                     "m.topic": {"m.text": [
                         {"mimetype": "text/html", "body": "The <em>plain</em> topic"},
                         {"body": "The plain topic"}]}},
          label="set a topic with an m.topic block",
          derive=lambda b: {"has_event_id": "event_id" in b})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="the block is returned verbatim")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="the block as facts", derive=topic_block)
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=u.token,
          label="the block in the room state",
          derive=lambda b: {"topic": [topic_block(e.get("content") or {})
                                      for e in b if e.get("type") == "m.room.topic"]})
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="the summary reports the plain representation",
          derive=lambda b: {"topic": b.get("topic", "<absent>")})
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          json_body={"topic": "second", "m.topic": {"m.text": [{"body": "second"}]}},
          label="replacing the topic replaces the block",
          derive=lambda b: {"has_event_id": "event_id" in b})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="the replacement is what is read back", derive=topic_block)


@scenario("ng/topic-block-in-sync", tags=["topic", "sync", "v1.15"], step=8)
def topic_block_in_sync(c):
    """The block survives /sync, /messages and /event."""
    u = register(c, "ts1")
    v = register(c, "ts2")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Sync Topic")
    first = sync(c, u, check=False)
    since = (first.json or {}).get("next_batch")
    r = c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
              json_body={"topic": "synced",
                         "m.topic": {"m.text": [
                             {"body": "synced"},
                             {"mimetype": "text/html", "body": "<b>synced</b>"}]}},
              check=False)
    event_id = (r.json or {})["event_id"]
    c.req("GET", f"{CS}/sync", token=u.token, query={"timeout": 0, "since": since},
          label="the block arrives through an incremental sync",
          derive=sync_topics)
    join(c, v, room)
    c.req("GET", f"{CS}/sync", token=v.token, query={"timeout": 0},
          label="and through a second member's initial sync", derive=sync_topics)
    c.req("GET", f"{CS}/rooms/{q(room)}/event/{q(event_id)}", token=u.token,
          label="the event itself carries the block",
          derive=lambda b: topic_block(b.get("content") or {}))
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=u.token,
          query={"dir": "b", "limit": 20},
          label="and so does the event in /messages",
          derive=lambda b: {"topics": [topic_block(e.get("content") or {})
                                       for e in (b.get("chunk") or [])
                                       if e.get("type") == "m.room.topic"]})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="the state endpoint agrees with the timeline", derive=topic_block)


@scenario("ng/topic-plain-only", tags=["topic", "v1.15"], step=8)
def topic_plain_only(c):
    """A topic set with only `topic`."""
    u = register(c, "tp1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Plain Topic")
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          json_body={"topic": "only the plain one"},
          label="set a topic with no m.topic block",
          derive=lambda b: {"has_event_id": "event_id" in b})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="what a plain-only topic reads back as", derive=topic_block)
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="the content as returned")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="the summary carries the plain topic",
          derive=lambda b: {"topic": b.get("topic", "<absent>")})
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          json_body={"topic": 42}, label="a topic that is not a string")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="a refused topic leaves the old one standing", derive=topic_block)


@scenario("ng/topic-absent-null-empty", tags=["topic", "v1.15"], step=8)
def topic_absent_null_empty(c):
    """An empty `topic` unsets the topic.

    The release's prose says an absent, `null` or empty `topic` leaves the topic
    unset; its published content schema marks `topic` required.  Whether a
    server refuses the first two therefore depends on which of its own halves
    the release is read by, so those attempts are made against a scratch room
    that nothing else reads, and are deliberately **not** scored.  The empty
    string, which prose and schema agree on, is.
    """
    u = register(c, "tn1")
    scratch = create_room(c, u, room_version=V, preset="public_chat",
                          name="Scratch", topic="Something to unset")
    c.req("PUT", f"{CS}/rooms/{q(scratch)}/state/m.room.topic/", token=u.token,
          json_body={}, check=False, learn=False,
          label="a content with no topic at all")
    c.req("PUT", f"{CS}/rooms/{q(scratch)}/state/m.room.topic/", token=u.token,
          json_body={"topic": None}, check=False, learn=False,
          label="a null topic")
    c.req("PUT", f"{CS}/rooms/{q(scratch)}/state/m.room.topic/", token=u.token,
          json_body={"m.topic": {"m.text": [{"body": "block only"}]}},
          check=False, learn=False, label="an m.topic block with no plain topic")

    room = create_room(c, u, room_version=V, preset="public_chat", name="Unset Topic",
                       topic="Something to unset")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="the topic before any of this", derive=topic_block)
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          json_body={"topic": ""}, label="an empty topic",
          derive=lambda b: {"has_event_id": "event_id" in b})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="an empty topic unsets the topic", derive=topic_block)
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="and the summary follows",
          derive=lambda b: {"topic": b.get("topic", "<absent>")})


@scenario("ng/topic-created-and-upgraded", tags=["topic", "v1.15"], step=8)
def topic_created_and_upgraded(c):
    """`/createRoom`'s `topic`, and a topic carried through an upgrade."""
    u = register(c, "tc1")
    room = create_room(c, u, room_version=V, preset="public_chat",
                       name="Created", topic="Set at creation")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="createRoom's topic key, as facts", derive=topic_block)
    # The block here is one the *server* generated from `/createRoom`'s `topic`,
    # so how it spells the representation is its own business -- `mimetype`
    # defaults to `text/plain` and a server may write the default out.  The
    # facts check above reads it with that default applied; this one asks only
    # that the plain key and the block agree.
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          label="createRoom's topic key, as returned",
          derive=lambda b: {
              "topic": b.get("topic"),
              "block_says_the_same":
                  [r.get("body") for r in
                   ((b.get("m.topic") or {}).get("m.text") or [])] ==
                  [b.get("topic")]})
    other = create_room(c, u, room_version=V, preset="public_chat", name="Initial",
                        initial_state=[{"type": "m.room.topic", "state_key": "",
                                        "content": {"topic": "from initial_state",
                                                    "m.topic": {"m.text": [
                                                        {"body":
                                                         "from initial_state"}]}}}])
    c.req("GET", f"{CS}/rooms/{q(other)}/state/m.room.topic/", token=u.token,
          label="a block supplied through initial_state", derive=topic_block)
    c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=u.token,
          json_body={"topic": "carried",
                     "m.topic": {"m.text": [{"body": "carried"},
                                            {"mimetype": "text/html",
                                             "body": "<i>carried</i>"}]}},
          check=False)
    r = c.req("POST", f"{CS}/rooms/{q(room)}/upgrade", token=u.token,
              json_body={"new_version": V}, check=False)
    new_room = (r.json or {})["replacement_room"]
    c.req("GET", f"{CS}/rooms/{q(new_room)}/state/m.room.topic/", token=u.token,
          label="the block is carried into the upgraded room", derive=topic_block)
    c.req("GET", f"{CS1}/room_summary/{q(new_room)}", token=u.token,
          label="the upgraded room summarises with the same topic",
          derive=lambda b: {"topic": b.get("topic", "<absent>")})


@scenario("ng/topic-in-summaries", tags=["topic", "v1.15"], step=8)
def topic_in_summaries(c):
    """The plain topic is what the summarising endpoints report."""
    u = register(c, "ti1")
    room = create_room(c, u, room_version=V, preset="public_chat", visibility="public",
                       name="Summarised", topic="Plain for summaries")
    set_state(c, u, room, "m.room.topic",
              {"topic": "Plain for summaries",
               "m.topic": {"m.text": [{"body": "Plain for summaries"},
                                      {"mimetype": "text/html",
                                       "body": "<b>Plain</b> for summaries"}]}}, "")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="room_summary reports the plain topic",
          derive=lambda b: {"topic": b.get("topic", "<absent>"),
                            "has_m_topic": "m.topic" in b})
    c.req("GET", f"{CS}/publicRooms", token=u.token,
          label="the published-rooms chunk reports the plain topic",
          derive=lambda b: {"topics": sorted(str(x.get("topic"))
                                             for x in (b.get("chunk") or []))})
    c.req("GET", f"{CS1}/rooms/{q(room)}/hierarchy", token=u.token,
          label="a hierarchy entry reports the plain topic",
          derive=lambda b: {"topics": [r.get("topic", "<absent>")
                                       for r in (b.get("rooms") or [])]})
    bare = create_room(c, u, room_version=V, preset="public_chat",
                       visibility="public", name="No Topic")
    c.req("GET", f"{CS1}/room_summary/{q(bare)}", token=u.token,
          label="a room with no topic at all",
          derive=lambda b: {"topic": b.get("topic", "<absent>")})
    c.req("GET", f"{CS1}/rooms/{q(bare)}/hierarchy", token=u.token,
          label="and its hierarchy entry",
          derive=lambda b: {"topics": [r.get("topic", "<absent>")
                                       for r in (b.get("rooms") or [])]})


# --------------------------------------------------------------------------- #
# POST /_matrix/client/v3/users/{userId}/report
# --------------------------------------------------------------------------- #
@scenario("ng/report-user-accepted", tags=["reporting", "v1.14"], step=8)
def report_user_accepted(c):
    """Reporting a user of this server."""
    u = register(c, "ru1")
    v = register(c, "ru2")
    c.req("POST", f"{CS}/users/{q(v.user_id)}/report", token=u.token,
          json_body={"reason": "this makes me sad"}, label="report a user")
    c.req("POST", f"{CS}/users/{q(v.user_id)}/report", token=u.token,
          json_body={"reason": ""}, label="the reason may be blank")
    c.req("POST", f"{CS}/users/{q(v.user_id)}/report", token=u.token,
          json_body={"reason": "again"},
          label="the same user may be reported twice")
    c.req("POST", f"{CS}/users/{q(u.user_id)}/report", token=u.token,
          json_body={"reason": "myself"}, label="a user may report themselves")
    c.req("POST", f"{CS}/users/{q(v.user_id)}/report", token=u.token,
          json_body={"reason": "shape"},
          label="the response body is an empty object",
          derive=lambda b: {"required_fields_present":
                            sorted(f for f in SUMMARY_REQUIRED if f in b)
                            if isinstance(b, dict) else None})
    room = create_room(c, u, room_version=V, preset="public_chat", name="Reportable")
    c.req("POST", f"{CS}/rooms/{q(room)}/report", token=v.token,
          json_body={"reason": "and a room"},
          label="reporting a room still works alongside it")


@scenario("ng/report-user-unknown", tags=["reporting", "v1.14"], step=8)
def report_user_unknown(c):
    """A user that does not exist on this server.

    Release v1.14 documents a 404 for this and also says a homeserver that
    wishes to conceal whether the account exists MAY answer 200 regardless.
    What is recorded here is the reference's choice between the two; the check
    is about this endpoint handling an absent account at all, not about which
    of the two answers the specification prefers.
    """
    u = register(c, "rk1")
    absent = f"@nosuchuser:{c.server_name}"
    c.req("POST", f"{CS}/users/{q(absent)}/report", token=u.token,
          json_body={"reason": "absent"},
          label="report a local user that does not exist")
    c.req("POST", f"{CS}/users/{q(absent)}/report", token=u.token,
          json_body={"reason": "absent"}, label="the same, as facts",
          derive=lambda b: {"required_fields_present":
                            sorted(f for f in SUMMARY_REQUIRED if f in b)
                            if isinstance(b, dict) else None,
                            "errcode": (b or {}).get("errcode", "<absent>")})
    c.req("POST", f"{CS}/users/{q('@someone:other.invalid')}/report",
          token=u.token, json_body={"reason": "remote"},
          label="report a user of a server this one cannot reach")
    c.req("POST", f"{CS}/users/{q(absent)}/report", token=u.token,
          json_body={"reason": "x" * 400}, label="a long but acceptable reason")
    c.req("POST", f"{CS}/users/{q('@anotherghost:' + c.server_name)}/report",
          token=u.token, json_body={"reason": "absent too"},
          label="a second account that does not exist is answered the same way")


@scenario("ng/report-user-refusals", tags=["reporting", "v1.14"], step=8)
def report_user_refusals(c):
    """Requests this endpoint must refuse."""
    u = register(c, "rr1")
    v = register(c, "rr2")
    c.req("POST", f"{CS}/users/{q(v.user_id)}/report", json_body={"reason": "x"},
          label="reporting without a token")
    c.req("POST", f"{CS}/users/{q(v.user_id)}/report", token="not-a-real-token",
          json_body={"reason": "x"}, label="reporting with an invalid token",
          drop=("soft_logout",))
    c.req("POST", f"{CS}/users/{q(v.user_id)}/report", token=u.token,
          json_body={}, label="reason is required")
    c.req("POST", f"{CS}/users/{q(v.user_id)}/report", token=u.token,
          body=b"not json", headers={"Content-Type": "application/json"},
          label="a body that is not JSON")
    c.req("GET", f"{CS}/users/{q(v.user_id)}/report", token=u.token,
          label="the endpoint is POST only")


# --------------------------------------------------------------------------- #
# GET /_matrix/client/v1/rooms/{roomId}/hierarchy
# --------------------------------------------------------------------------- #
@scenario("ng/hierarchy-restricted-child", tags=["hierarchy", "spaces", "v1.15"],
          step=8)
def hierarchy_restricted_child(c):
    """A space whose child is restricted and encrypted."""
    u = register(c, "hr1")
    space = create_room(c, u, room_version=V, preset="public_chat", name="Root Space",
                        creation_content={"type": "m.space"})
    child = create_room(c, u, room_version=V, name="Restricted Child", initial_state=[
        {"type": "m.room.join_rules", "state_key": "",
         "content": {"join_rule": "restricted",
                     "allow": [{"type": "m.room_membership", "room_id": space}]}},
        {"type": "m.room.encryption", "state_key": "",
         "content": {"algorithm": "m.megolm.v1.aes-sha2"}},
    ])
    set_state(c, u, space, "m.space.child", {"via": [c.server_name]}, child)
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
          label="the hierarchy of the space")
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
          label="the v1.15 additions in each entry", derive=hierarchy_entries)
    c.req("GET", f"{CS1}/rooms/{q(child)}/hierarchy", token=u.token,
          label="the child's own hierarchy", derive=hierarchy_entries)
    c.req("GET", f"{CS1}/room_summary/{q(child)}", token=u.token,
          label="the summary of the same room carries allowed_room_ids",
          derive=lambda b: {"allowed": b.get("allowed_room_ids"),
                            "encryption": b.get("encryption", "<absent>"),
                            "room_version": b.get("room_version", "<absent>")})
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
          query={"suggested_only": True},
          label="suggested_only leaves the unsuggested child out",
          derive=hierarchy_entries)


@scenario("ng/hierarchy-version-and-encryption",
          tags=["hierarchy", "spaces", "v1.15"], step=8)
def hierarchy_version_and_encryption(c):
    """`room_version` and `encryption` on children at different versions."""
    u = register(c, "hv1")
    space = create_room(c, u, room_version=V, preset="public_chat", name="Versions",
                        creation_content={"type": "m.space"})
    modern = create_room(c, u, room_version=V, preset="public_chat", name="Modern")
    older = create_room(c, u, room_version=V10, preset="public_chat", name="Older",
                        initial_state=[{"type": "m.room.encryption", "state_key": "",
                                        "content": {"algorithm":
                                                    "m.megolm.v1.aes-sha2"}}])
    set_state(c, u, space, "m.space.child",
              {"via": [c.server_name], "suggested": True}, modern)
    set_state(c, u, space, "m.space.child", {"via": [c.server_name]}, older)
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
          label="every entry carries its own room_version",
          derive=hierarchy_entries)
    # As above: the raw hierarchy body would require the reference's own choice
    # of optional entry fields *and* its order of `children_state`, which the
    # release does not fix.  `hierarchy_entries` keys by room name instead.
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
          label="the hierarchy as returned", derive=hierarchy_entries)
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
          query={"suggested_only": True}, label="only the suggested child",
          derive=hierarchy_entries)
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
          query={"max_depth": 0}, label="max_depth 0 stops at the space",
          derive=hierarchy_entries)
    c.req("GET", f"{CS1}/room_summary/{q(older)}", token=u.token,
          label="the summary of the older child agrees",
          derive=lambda b: {"room_version": b.get("room_version", "<absent>"),
                            "encryption": b.get("encryption", "<absent>")})


@scenario("ng/hierarchy-paging-and-errors", tags=["hierarchy", "spaces", "v1.15"],
          step=8)
def hierarchy_paging_and_errors(c):
    """Paging through a hierarchy, and what it refuses."""
    u = register(c, "hp1")
    v = register(c, "hp2")
    space = create_room(c, u, room_version=V, preset="public_chat", name="Paged",
                        creation_content={"type": "m.space"})
    first = create_room(c, u, room_version=V, preset="public_chat", name="First")
    second = create_room(c, u, room_version=V, preset="public_chat", name="Second")
    set_state(c, u, space, "m.space.child", {"via": [c.server_name]}, first)
    set_state(c, u, space, "m.space.child", {"via": [c.server_name]}, second)
    # Which child lands on which page is the server's own ordering -- the
    # release fixes neither the order of `rooms` nor how a server splits it --
    # so what is scored is that a page honours the limit, that it offers a way
    # to continue, and that the pages together cover the space and its
    # children.
    def page(b):
        return {"n": len(b.get("rooms") or []),
                "has_next_batch": isinstance(b.get("next_batch"), str)}

    r = c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
              query={"limit": 1}, label="a limited page", derive=page)
    token = (r.json or {}).get("next_batch")
    r2 = c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
               query={"limit": 1, "from": token}, label="the next page",
               derive=page)
    seen = {e.get("name") for e in ((r.json or {}).get("rooms") or [])}
    seen |= {e.get("name") for e in ((r2.json or {}).get("rooms") or [])}
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
          query={"limit": 50}, label="and the unpaged answer holds them all",
          derive=lambda b, _s=seen: {
              "names": sorted(e.get("name") for e in (b.get("rooms") or [])),
              "the_pages_were_a_prefix_of_it": _s.issubset(
                  {e.get("name") for e in (b.get("rooms") or [])})})
    # "Must be an integer greater than zero": the release refuses the request
    # and does not fix which of its error codes says so.
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy", token=u.token,
          query={"limit": -1}, drop=("errcode",),
          label="a limit that is not positive")
    c.req("GET", f"{CS1}/rooms/{q(f'!ng-nothing:{c.server_name}')}/hierarchy",
          token=u.token, label="a room this server does not know")
    c.req("GET", f"{CS1}/rooms/{q(space)}/hierarchy",
          label="the hierarchy needs authentication")
    private = create_room(c, v, room_version=V, preset="private_chat", name="Hidden")
    c.req("GET", f"{CS1}/rooms/{q(private)}/hierarchy", token=u.token,
          label="a room the caller may not see")


# --------------------------------------------------------------------------- #
# /rooms/{roomId}/initialSync
# --------------------------------------------------------------------------- #
@scenario("ng/initialsync-room-get", tags=["initial-sync", "v1.15"], step=8)
def initialsync_room_get(c):
    """`GET /rooms/{roomId}/initialSync` for a joined room."""
    u = register(c, "is1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Snapshot",
                       topic="A snapshot topic")
    send(c, u, room, "is-1", {"msgtype": "m.text", "body": "hello"})
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync", token=u.token,
          label="the snapshot of a joined room",
          derive=lambda b: dict(initial_sync_shape(b),
                                membership_of_a_member=b.get("membership")))
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync", token=u.token,
          label="its membership and message keys",
          derive=lambda b: {"membership": b.get("membership"),
                            "message_fields": sorted(b.get("messages") or {}),
                            "room_id_is_the_room": b.get("room_id") == room})
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync", token=u.token,
          label="the timeline it returns",
          derive=lambda b: {"types": [e.get("type") for e in
                                      ((b.get("messages") or {}).get("chunk") or [])]})
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync", token=u.token,
          label="the state it returns",
          derive=lambda b: {"state": sorted({(e.get("type"), e.get("state_key"))
                                             for e in (b.get("state") or [])})})
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync", token=u.token,
          label="and the m.topic block within it",
          derive=lambda b: {"topic": [topic_block(e.get("content") or {})
                                      for e in (b.get("state") or [])
                                      if e.get("type") == "m.room.topic"]})


@scenario("ng/initialsync-room-post", tags=["initial-sync", "v1.15"], step=8)
def initialsync_room_post(c):
    """The POST form of the same endpoint, alongside the GET form."""
    u = register(c, "ip1")
    room = create_room(c, u, room_version=V, preset="public_chat", name="Posted")
    c.req("POST", f"{CS}/rooms/{q(room)}/initialSync", token=u.token, json_body={},
          label="POST to the room snapshot endpoint")
    c.req("POST", f"{CS}/rooms/{q(room)}/initialSync", token=u.token, json_body={},
          label="what that answer says",
          derive=lambda b: {"errcode": (b or {}).get("errcode", "<absent>"),
                            "has_room_id": isinstance(b, dict) and "room_id" in b})
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync", token=u.token,
          label="the GET form answers",
          derive=lambda b: {"has_room_id": "room_id" in b,
                            "has_messages": "messages" in b,
                            "has_state": "state" in b})
    c.req("PUT", f"{CS}/rooms/{q(room)}/initialSync", token=u.token, json_body={},
          label="PUT is not this endpoint either")
    c.req("GET", f"{CS}/rooms/{q(room)}/initialSync",
          label="the snapshot needs authentication")


@scenario("ng/initialsync-room-peek", tags=["initial-sync", "v1.15"], step=8)
def initialsync_room_peek(c):
    """The snapshot endpoint is for peeking into rooms you are not in."""
    u = register(c, "ik1")
    v = register(c, "ik2")
    readable = create_room(c, u, room_version=V, preset="public_chat", name="Readable",
                           initial_state=[{"type": "m.room.history_visibility",
                                           "state_key": "",
                                           "content": {"history_visibility":
                                                       "world_readable"}}])
    shared = create_room(c, u, room_version=V, preset="public_chat", name="Shared")
    private = create_room(c, u, room_version=V, preset="private_chat", name="Private")
    c.req("GET", f"{CS}/rooms/{q(readable)}/initialSync", token=v.token,
          label="peeking into a world-readable room", derive=initial_sync_shape)
    c.req("GET", f"{CS}/rooms/{q(shared)}/initialSync", token=v.token,
          label="a room that is neither published nor world-readable",
          derive=lambda b: {"errcode": (b or {}).get("errcode", "<absent>"),
                            "has_state": isinstance(b, dict) and "state" in b})
    c.req("GET", f"{CS}/rooms/{q(private)}/initialSync", token=v.token,
          label="a room the caller may not peek into")
    c.req("GET", f"{CS}/rooms/{q(f'!ng-nothing:{c.server_name}')}/initialSync",
          token=v.token, label="a room this server does not know")
    join(c, v, shared)
    c.req("GET", f"{CS}/rooms/{q(shared)}/initialSync", token=v.token,
          label="the same room once joined",
          derive=lambda b: {"membership": b.get("membership"),
                            "has_state": "state" in b,
                            "has_messages": "messages" in b})


# --------------------------------------------------------------------------- #
# via and server_name on join / knock
# --------------------------------------------------------------------------- #
@scenario("ng/join-via", tags=["membership", "via", "v1.15"], step=8)
def join_via(c):
    """`via` is the way to name a resident server."""
    u = register(c, "jv1")
    v = register(c, "jv2")
    w = register(c, "jv3")
    room = create_room(c, u, room_version=V, preset="public_chat",
                       room_alias_name="ng-join", visibility="public",
                       name="Joinable")
    alias = f"#ng-join:{c.server_name}"
    c.req("POST", f"{CS}/join/{q(room)}", token=v.token, json_body={},
          query={"via": c.server_name}, label="join by id with via")
    c.req("POST", f"{CS}/join/{q(alias)}", token=w.token, json_body={},
          query={"via": c.server_name}, label="join by alias with via")
    c.req("POST", f"{CS}/join/{q(room)}", token=v.token, json_body={},
          query={"via": [c.server_name, DEAD]},
          label="a via list naming a server that is not resident too")
    c.req("POST", f"{CS}/join/{q(room)}", token=v.token, json_body={},
          query={"via": DEAD},
          label="via naming only an unreachable server, for a room held here")
    c.req("GET", f"{CS}/joined_rooms", token=v.token,
          label="the joins took effect",
          derive=lambda b: {"n": len(b.get("joined_rooms") or [])})
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="and the summary counts them",
          derive=lambda b: {"n": b.get("num_joined_members")})


@scenario("ng/join-server-name-ignored", tags=["membership", "via", "v1.15"], step=8)
def join_server_name_ignored(c):
    """`server_name` was removed from `POST /join/{roomIdOrAlias}` in v1.15.

    The first three requests differ only in how they name a resident server for
    a room this homeserver does not hold: not at all, through `via`, and through
    `server_name`.  Under v1.15 the third must not get the benefit the second
    gets -- but the recorded reference still falls back to `server_name` when
    `via` is absent, so that one request is recorded unscored.  What is scored
    is that `server_name` neither helps nor breaks anything else.
    """
    u = register(c, "js1")
    c.req("POST", f"{CS}/join/{q(f'!ng-unheld-1:{c.server_name}')}", token=u.token,
          json_body={}, label="a room this server does not hold, naming no server")
    # A room id in this server's own namespace that it does not hold, with a
    # `via` it cannot reach: a server that tries the peer and reports the
    # failure, and one that answers "no such room" without dialling, are both
    # inside the release, so only the refusal is scored.
    r_via = c.req("POST", f"{CS}/join/{q(f'!ng-unheld-2:{c.server_name}')}", token=u.token,
                  json_body={}, query={"via": DEAD}, check=False)
    fact(c, {"refused": r_via.status >= 400},
         label="the same, naming an unreachable server through via",
         token=u.token)
    c.req("POST", f"{CS}/join/{q(f'!ng-unheld-3:{c.server_name}')}", token=u.token,
          json_body={}, query={"server_name": DEAD}, check=False,
          label="the same, naming it only through server_name")
    c.req("GET", f"{CS}/joined_rooms", token=u.token,
          label="none of the three joined anything",
          derive=lambda b: {"joined_rooms": b.get("joined_rooms")})
    room = create_room(c, u, room_version=V, preset="public_chat", name="Held")
    v = register(c, "js2")
    c.req("POST", f"{CS}/join/{q(room)}", token=v.token, json_body={},
          query={"server_name": DEAD},
          label="server_name does not break a join this server can serve itself")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=u.token,
          label="that join happened",
          derive=lambda b: {"n": b.get("num_joined_members")})


@scenario("ng/knock-via", tags=["membership", "knock", "via", "v1.15"], step=8)
def knock_via(c):
    """`via` on `POST /knock/{roomIdOrAlias}`."""
    u = register(c, "kv1")
    v = register(c, "kv2")
    room = create_room(c, u, room_version=V, name="Knock Room",
                       room_alias_name="ng-knock",
                       initial_state=[{"type": "m.room.join_rules", "state_key": "",
                                       "content": {"join_rule": "knock"}}])
    alias = f"#ng-knock:{c.server_name}"
    c.req("POST", f"{CS}/knock/{q(room)}", token=v.token,
          json_body={"reason": "hello"}, query={"via": c.server_name},
          label="knock by id with via")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          label="the knock shows in the summary",
          derive=lambda b: {"membership": b.get("membership"),
                            "join_rule": b.get("join_rule")})
    c.req("POST", f"{CS}/knock/{q(alias)}", token=v.token, json_body={},
          query={"via": c.server_name}, label="knocking again, by alias")
    c.req("POST", f"{CS}/rooms/{q(room)}/leave", token=v.token, json_body={},
          check=False)
    c.req("POST", f"{CS}/knock/{q(room)}", token=v.token, json_body={},
          query={"via": [c.server_name, DEAD]},
          label="a via list with an unreachable server in it")
    public = create_room(c, u, room_version=V, preset="public_chat",
                         name="Not Knockable")
    c.req("POST", f"{CS}/knock/{q(public)}", token=v.token, json_body={},
          query={"via": c.server_name},
          label="knocking at a room that is not knockable")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          label="the membership after all of it",
          derive=lambda b: {"membership": b.get("membership")})


@scenario("ng/knock-server-name-ignored",
          tags=["membership", "knock", "via", "v1.15"], step=8)
def knock_server_name_ignored(c):
    """`server_name` was removed from `POST /knock/{roomIdOrAlias}` in v1.15.

    As with the join: the recorded reference still falls back to `server_name`,
    so that request is recorded unscored and what is scored is that naming a
    server this way neither helps nor breaks anything else.
    """
    u = register(c, "ks1")
    c.req("POST", f"{CS}/knock/{q(f'!ng-unknocked-1:{c.server_name}')}", token=u.token,
          json_body={}, label="a room this server does not hold, naming no server")
    # A room id in this server's own namespace that it does not hold, with a
    # `via` it cannot reach: a server that tries the peer and reports the
    # failure, and one that answers "no such room" without dialling, are both
    # inside the release, so only the refusal is scored.
    r_via = c.req("POST", f"{CS}/knock/{q(f'!ng-unknocked-2:{c.server_name}')}", token=u.token,
                  json_body={}, query={"via": DEAD}, check=False)
    fact(c, {"refused": r_via.status >= 400},
         label="the same, naming an unreachable server through via",
         token=u.token)
    c.req("POST", f"{CS}/knock/{q(f'!ng-unknocked-3:{c.server_name}')}", token=u.token,
          json_body={}, query={"server_name": DEAD}, check=False,
          label="the same, naming it only through server_name")
    room = create_room(c, u, room_version=V, name="Knockable Held",
                       initial_state=[{"type": "m.room.join_rules", "state_key": "",
                                       "content": {"join_rule": "knock"}}])
    v = register(c, "ks2")
    c.req("POST", f"{CS}/knock/{q(room)}", token=v.token, json_body={},
          query={"server_name": DEAD},
          label="server_name does not break a knock this server can serve itself")
    c.req("GET", f"{CS1}/room_summary/{q(room)}", token=v.token,
          label="that knock happened",
          derive=lambda b: {"membership": b.get("membership")})


# --------------------------------------------------------------------------- #
# next-generation authentication
# --------------------------------------------------------------------------- #
@scenario("ng/auth-metadata-unconfigured", tags=["oauth", "v1.15"], step=8)
def auth_metadata_unconfigured(c):
    """`GET /_matrix/client/v1/auth_metadata` with no delegated issuer.

    Release v1.15: "In both cases, the server SHOULD respond with 404 and an
    `M_UNRECOGNIZED` error code if the corresponding API is not supported."
    This deployment is configured with no authorization server, so that is the
    answer pinned.  The status is compared by class, the published operation
    documenting only a 200.
    """
    u = register(c, "am1")
    c.req("GET", f"{CS1}/auth_metadata",
          label="auth_metadata with no delegated issuer configured")
    c.req("GET", f"{CS1}/auth_metadata", token=u.token,
          label="the same with an access token")
    c.req("GET", f"{CS1}/auth_metadata", label="the answer names no issuer",
          derive=lambda b: {"errcode": (b or {}).get("errcode", "<absent>"),
                            "has_issuer": isinstance(b, dict) and "issuer" in b})
    c.req("GET", f"{CS}/login",
          label="the legacy API is the one this deployment offers",
          derive=login_flow_facts)
    # Which unstable feature flags a server advertises is its own business --
    # the harness drops `unstable_features` everywhere else for that reason --
    # so what is scored here is that the endpoint still answers.
    c.req("GET", "/_matrix/client/versions",
          label="and /versions still answers",
          derive=lambda b: {"versions_is_a_list":
                            isinstance(b.get("versions"), list)
                            and bool(b.get("versions"))})


@scenario("ng/auth-token-refusal", tags=["oauth", "auth", "v1.15"], step=8)
def auth_token_refusal(c):
    """How an unknown access token is refused.

    The OAuth 2.0 API of v1.15 refuses an inactive or unknown token with
    `M_UNKNOWN_TOKEN` and `soft_logout: false`; the legacy API says the same of
    a token that was never valid.  Both agree here, so the refusal is pinned
    without this deployment needing an authorization server.
    """
    u = register(c, "at1")
    c.req("GET", f"{CS}/account/whoami", token="not-a-real-token",
          label="whoami with a token that was never valid")
    c.req("GET", f"{CS}/account/whoami", token="not-a-real-token",
          label="the refusal leaves no soft-logged-out session",
          derive=lambda b: {"errcode": b.get("errcode"),
                            # omitted and false mean the same thing at v1.15
                            "soft_logout": bool(b.get("soft_logout", False))})
    c.req("GET", f"{CS}/account/whoami", label="whoami with no token at all")
    c.req("GET", f"{CS}/account/whoami", token=u.token,
          label="whoami with a token this server issued",
          derive=lambda b: {"user_id_is_mine": b.get("user_id") == u.user_id,
                            "is_guest": bool(b.get("is_guest", False))})
    c.req("GET", f"{CS}/devices", token="not-a-real-token",
          label="the device endpoints refuse the same way",
          derive=lambda b: {"errcode": b.get("errcode"),
                            "soft_logout": bool(b.get("soft_logout", False))})
    c.req("GET", f"{CS}/devices", token=u.token,
          label="and answer for a token this server issued",
          derive=lambda b: {"n": len(b.get("devices") or [])})


@scenario("ng/versions-advertised", tags=["versions", "v1.15"], step=8)
def versions_advertised(c):
    """What `GET /_matrix/client/versions` advertises at this release.

    Which releases a deployment claims is its own to decide, so every check
    here is a question about the list rather than the list itself.
    """
    u = register(c, "vs1")
    c.req("GET", "/_matrix/client/versions",
          label="the version list, unauthenticated", derive=versions_facts)
    c.req("GET", "/_matrix/client/versions", token=u.token,
          label="the version list with a token", derive=versions_facts)
    c.req("GET", "/_matrix/client/versions", label="the response's own fields",
          derive=lambda b: {"has_versions": "versions" in b,
                            "versions_is_a_list": isinstance(b.get("versions"), list),
                            "unstable_features_is_a_map_or_absent":
                            isinstance(b.get("unstable_features", {}), dict)})
    c.req("GET", "/_matrix/client/versions",
          label="unstable features are namespaced booleans",
          derive=lambda b: {"all_values_are_booleans":
                            all(isinstance(x, bool)
                                for x in (b.get("unstable_features") or {}).values()),
                            "no_key_is_a_release_name":
                            not any(VERSION_RE.match(k)
                                    for k in (b.get("unstable_features") or {}))})
    c.req("GET", "/_matrix/client/versions", label="the same answer twice running",
          derive=versions_facts)


@scenario("ng/capabilities-room-versions",
          tags=["capabilities", "room-versions", "v1.15"], step=8)
def capabilities_room_versions(c):
    """`m.room_versions` at this release: which versions are listed, and
    whether the default is one of them."""
    u = register(c, "cp1")
    doomed = create_room(c, u, room_version=V, preset="private_chat", name="Upgradable")
    c.req("GET", f"{CS}/capabilities", token=u.token,
          label="which room versions are listed and whether the default is one",
          derive=room_version_facts)
    c.req("GET", f"{CS}/capabilities", token=u.token,
          label="the capability block's own shape",
          derive=lambda b: {"has_room_versions":
                            "m.room_versions" in (b.get("capabilities") or {}),
                            "room_versions_fields":
                            sorted((b.get("capabilities") or {})
                                   .get("m.room_versions") or {})})
    c.req("GET", f"{CS}/capabilities", label="capabilities needs authentication")
    c.req("POST", f"{CS}/createRoom", token=u.token, json_body={"room_version": V},
          label="a listed, stable version is accepted by createRoom",
          derive=lambda b: {"made_a_room": "room_id" in b})
    c.req("POST", f"{CS}/createRoom", token=u.token,
          json_body={"room_version": "ng.not.a.version"},
          label="a version that is not listed is refused")
    c.req("POST", f"{CS}/rooms/{q(doomed)}/upgrade", token=u.token,
          json_body={"new_version": "ng.not.a.version"},
          label="and an upgrade to it is refused too")

# --------------------------------------------------------------------------- #
# The pre-flight, on the routes this milestone adds
# --------------------------------------------------------------------------- #
# Every step's ticket restates the release's CORS requirement -- every endpoint
# answers an `OPTIONS` request, and answering it performs none of the endpoint's
# own logic -- and step 1 scores it on the routes that existed then.  A route
# added later is a route that can be added without it, so each milestone asks
# the same two questions of its own.  Nothing here needs a room, a user or an
# upload to exist: that is the point of the second question.


@scenario("ng/options-preflight", tags=["cors"], step=8)
def ng_options_preflight(c):
    """`OPTIONS` on the endpoints this release adds."""
    empty = lambda b: {"body_carries_nothing_of_substance": not b}   # noqa: E731
    for path, method, label in (
            (f"{CS}/users/{q('@nobody:' + c.server_name)}/report", "POST",
          "the user-report endpoint"),
            (f"{CS1}/room_summary/{q('!nonexistentroom00000000000:' + c.server_name)}", "GET", "the room-summary endpoint")
    ):
        c.req("OPTIONS", path,
              headers={"Origin": "https://example.invalid",
                       "Access-Control-Request-Method": method},
              label=f"OPTIONS preflight on {label}", derive=empty)
