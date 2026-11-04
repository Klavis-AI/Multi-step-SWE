"""Step 5 - push rules, notification counts, the notifications API, event
reports, presence refinements and login tokens (spec releases v1.7 - v1.9).

Three things about the reference shape every scenario here:

* **Push actions are computed asynchronously.**  A message is persisted and
  acknowledged before the reader's `unread_notifications` have moved.  Every
  scored read is therefore preceded by an unscored poll that stops only once
  the awaited effect has arrived *and* several further reads produced exactly
  the same value, so the scored request sees what the reference saw.

* **`/sync` responses are cached for two minutes** under a key that includes
  the `timeout` query parameter.  Polling with one fixed `timeout` would return
  the very first answer for ever, so every poll carries a fresh `timeout` from
  a per-scenario counter that starts well above any value a scored request
  uses, and each scored `/sync` in a scenario uses a `timeout` of its own.
  An initial `/sync` (no `since`) returns immediately whatever the timeout is.

* **Actions are a set, not a sequence.**  v1.9 defines each server-default
  rule's actions, but the reference emits the same actions in a different
  order, and `{"set_tweak": "highlight"}` means exactly what
  `{"set_tweak": "highlight", "value": true}` means.  So a rule's actions are
  never pinned as a list: they are reduced to "does it notify" plus the tweak
  map the specification does fix.  Rule conditions are likewise sorted, since
  v1.9 fixes which conditions a rule carries but not the order they arrive in.

Everything the specification leaves to the implementation - a login token's
value, a notification's timestamp, `last_active_ago`, the rule kinds and rule
ids the reference invents beyond the ones v1.9 names - is derived away rather
than compared.
"""

from __future__ import annotations

import itertools
import json
import time

from mxconf.ctx import ScenarioAbort
from mxconf.mx import (CS, CS1, create_room, fact, join, q, register, send,
                       set_state)
from mxconf.normalize import CONCEALABLE_ERRCODES, CONCEALED
from mxconf.registry import scenario

# Every room is created at this version explicitly, so the reference's default
# room version cannot move a transcript.
V = "10"

TAGS = ["push"]

# --------------------------------------------------------------------------- #
# What v1.9 says a rule set contains
# --------------------------------------------------------------------------- #
# The rule kinds `push_ruleset.yaml` declares.  A server is free to keep other
# buckets of its own, so a derived list is filtered to these: a candidate
# without the reference's private kinds is not wrong.
KINDS = ("content", "override", "room", "sender", "underride")

# The server-default rules v1.9 specifies, per kind, in the order the
# "Predefined Rules" section lists them (which is priority order).  Used only
# as a *filter*: the recorded transcript says which of them the server has, and
# a vendor rule beside them is nobody's business but the vendor's.
SPEC_RULE_IDS = {
    "content": (".m.rule.contains_user_name",),
    "override": (".m.rule.master", ".m.rule.suppress_notices",
                 ".m.rule.invite_for_me", ".m.rule.member_event",
                 ".m.rule.is_user_mention", ".m.rule.contains_display_name",
                 ".m.rule.is_room_mention", ".m.rule.roomnotif",
                 ".m.rule.tombstone", ".m.rule.reaction",
                 ".m.rule.room.server_acl", ".m.rule.suppress_edits"),
    "room": (),
    "sender": (),
    "underride": (".m.rule.call", ".m.rule.encrypted_room_one_to_one",
                  ".m.rule.room_one_to_one", ".m.rule.message",
                  ".m.rule.encrypted"),
}


def act_shape(actions):
    """An action list reduced to what v1.9 fixes about it.

    `notify` either is in the list or is not; a `set_tweak` either sets a tweak
    to a value or, for `highlight`, to the specified default of `true`.  The
    order the actions arrive in is not fixed by the specification - the
    reference's own order differs from the order the published rule definitions
    use - so it is never compared.
    """
    notify = False
    tweaks = {}
    other = []
    for a in actions or []:
        if a == "notify":
            notify = True
        elif isinstance(a, dict) and isinstance(a.get("set_tweak"), str):
            # "If a highlight tweak is given with no value, its value is
            # defined to be true."
            tweaks[a["set_tweak"]] = a["value"] if "value" in a else True
        elif isinstance(a, str):
            # `dont_notify` and `coalesce` are no-ops from v1.7 on, and any
            # other bare string is the implementation's own.
            if a not in ("dont_notify", "coalesce"):
                other.append(a)
    # "If no highlight tweak is given at all then the value of `highlight` is
    # defined to be false", so a rule that sets it to false and a rule that
    # leaves it out say the same thing and are recorded the same way.  The
    # published definition of `.m.rule.message` leaves it out where the
    # reference spells it out, and neither is wrong.
    highlight = bool(tweaks.pop("highlight", False))
    return {"notify": notify, "highlight": highlight,
            "tweaks": dict(sorted(tweaks.items())),
            "other_actions": sorted(other)}


def cond_shape(conditions):
    """A rule's conditions as an order-independent list."""
    return sorted(conditions or [], key=lambda c: json.dumps(c, sort_keys=True))


def rule_shape(rule, *, conditions=False, pattern=False):
    out = {
        "rule_id": rule.get("rule_id"),
        "default": rule.get("default"),
        "enabled": rule.get("enabled"),
        "actions": act_shape(rule.get("actions")),
    }
    if conditions:
        out["conditions"] = cond_shape(rule.get("conditions"))
    if pattern:
        out["pattern"] = rule.get("pattern")
    return out


def ruleset_shape(ruleset, *, conditions=False):
    """The specified content of a rule set: which kinds, which specified rules,
    in which order, with which flags and (normalised) actions."""
    out = {}
    for kind in KINDS:
        rules = ruleset.get(kind)
        wanted = set(SPEC_RULE_IDS[kind])
        out[kind] = {
            "present": rules is not None,
            "rules": [rule_shape(r, conditions=conditions,
                                 pattern=(kind == "content"))
                      for r in (rules or [])
                      if r.get("rule_id") in wanted],
        }
    return out


def user_rule_ids(ruleset, kind):
    """The user-defined rules of one kind, in the order the server returns them.

    Only rules the user made are listed: where a server-default rule sits
    relative to them is the server's business, but the order of the user's own
    rules is exactly what `before` and `after` fix.
    """
    return [r.get("rule_id") for r in (ruleset.get(kind) or [])
            if r.get("default") is False]


# --------------------------------------------------------------------------- #
# Polling
# --------------------------------------------------------------------------- #
_UNSET = object()


def _nonce(c):
    """A `timeout` value no other request in this scenario has used."""
    counter = c.extra.get("push_nonce")
    if counter is None:
        counter = itertools.count(4001)
        c.extra["push_nonce"] = counter
    return next(counter)


def _sync_body(c, user, *, timeout):
    r = c.req("GET", f"{CS}/sync", token=user.token, query={"timeout": timeout},
              check=False)
    if r.json is None:
        raise ScenarioAbort(f"/sync returned HTTP {r.status} with no JSON")
    return r.json


def room_view(body, room):
    """The facts about one joined room this step scores."""
    r = ((body.get("rooms") or {}).get("join") or {}).get(room) or {}
    tl = (r.get("timeline") or {}).get("events") or []
    return {
        "joined": bool(r),
        "bodies": [e.get("content", {}).get("body") for e in tl
                   if e.get("type") == "m.room.message"],
        "unread": r.get("unread_notifications"),
    }


def settle_room(c, user, room, ready, *, what, tries=350, delay=0.2, stable=3):
    """Poll an unscored `/sync` until the room view settles.

    The value is returned only once `ready` holds and `stable` further reads
    produced exactly the same value.  v1.9 requires the count a new event
    produces to appear in the same `/sync` as the event itself, so waiting for
    the message body is enough to know the count is final; the extra identical
    reads guard against reading between two of the reference's own updates.
    """
    prev = _UNSET
    agreed = 0
    cur = None
    for _ in range(tries):
        cur = room_view(_sync_body(c, user, timeout=_nonce(c)), room)
        if ready(cur) and cur == prev:
            agreed += 1
            if agreed >= stable:
                return cur
        else:
            agreed = 0
        prev = cur
        time.sleep(delay)
    raise ScenarioAbort(f"the notification counts did not settle: {what}; "
                        f"last read {cur!r}")


def has_body(text):
    return lambda v: text in (v.get("bodies") or [])


def scored_room(c, user, room, *, label, timeout):
    """The scored read, issued straight after the poll settled."""
    return c.req("GET", f"{CS}/sync", token=user.token,
                 query={"timeout": timeout}, label=label,
                 derive=lambda b, rid=room: room_view(b, rid))


def notif_shape(body):
    """What the release fixes about a notification list.

    Not the order.  `GET /notifications` states what a notification carries
    and that `next_token` pages through them; which end of the stream a server
    starts from it never says, and both readings page coherently.  So the
    entries are compared as a *set*, sorted by the one field this test's own
    messages make unique.
    """
    out = []
    for n in body.get("notifications") or []:
        ev = n.get("event") or {}
        out.append({
            "body": (ev.get("content") or {}).get("body"),
            "type": ev.get("type"),
            "read": n.get("read"),
            "actions": act_shape(n.get("actions")),
            "has_room_id": isinstance(n.get("room_id"), str),
            "ts_is_int": isinstance(n.get("ts"), int)
                         and not isinstance(n.get("ts"), bool),
        })
    return {"notifications": sorted(out, key=lambda n: str(n.get("body")))}


def notif_page(body):
    """As `notif_shape`, plus the paging token.

    v1.9 makes `next_token` optional and says its absence means there are no
    more results, so whether a server sends one on the *last* page is its own
    business; it is only compared where more results are known to remain.
    """
    return dict(notif_shape(body), has_next_token="next_token" in body)


def settle_notifications(c, user, ready, *, what, query=None, tries=350,
                         delay=0.2, stable=4):
    """Poll `/notifications` unscored until its projection settles.

    The reference prunes already-read, non-highlighting notifications a few
    seconds after a receipt, so the list is only stable once that has happened;
    four identical reads in a row is what says so.
    """
    prev = _UNSET
    agreed = 0
    cur = None
    for _ in range(tries):
        r = c.req("GET", f"{CS}/notifications", token=user.token,
                  query=query or None, check=False)
        cur = notif_shape(r.json or {})
        if ready(cur) and cur == prev:
            agreed += 1
            if agreed >= stable:
                return cur
        else:
            agreed = 0
        prev = cur
        time.sleep(delay)
    raise ScenarioAbort(f"the notification list did not settle: {what}; "
                        f"last read {cur!r}")


def n_bodies(v):
    return [n["body"] for n in v["notifications"]]


# --------------------------------------------------------------------------- #
# Push rules: reading the defaults
# --------------------------------------------------------------------------- #
@scenario("push/default-ruleset", tags=TAGS + ["pushrules"], step=5)
def default_ruleset(c):
    u = register(c, "pd1")
    c.req("GET", f"{CS}/pushrules/", token=u.token,
          label="the default rule set: kinds, specified rule ids and flags",
          derive=lambda b: ruleset_shape(b.get("global") or {}))
    # `GET /pushrules/{scope}/` is *not* one of the operations this release
    # defines -- the release has `GET /pushrules/` and the three per-rule forms,
    # and nothing in between -- so a server that answers it and one that refuses
    # it are both inside the specification, and it is not requested here.
    c.req("GET", f"{CS}/pushrules/global/override/{q('.m.rule.master')}",
          token=u.token, label="the master rule, off by default",
          derive=lambda b: rule_shape(b, conditions=True))
    c.req("GET", f"{CS}/pushrules/global/override/{q('.m.rule.master')}/enabled",
          token=u.token, label="the master rule's enabled sub-resource")
    c.req("GET", f"{CS}/pushrules/global/override/{q('.m.rule.master')}/actions",
          token=u.token, label="the master rule notifies about nothing",
          derive=lambda b: act_shape(b.get("actions")))
    c.req("GET", f"{CS}/pushrules/global/underride/{q('.m.rule.message')}",
          token=u.token, label="the catch-all message rule",
          derive=lambda b: rule_shape(b, conditions=True))
    c.req("GET",
          f"{CS}/pushrules/global/content/{q('.m.rule.contains_user_name')}",
          token=u.token,
          label="the content rule carries the user's local part as its pattern",
          derive=lambda b: {**rule_shape(b),
                            "pattern_is_localpart": b.get("pattern") == "pd1"})


@scenario("push/default-rule-conditions", tags=TAGS + ["pushrules"], step=5)
def default_rule_conditions(c):
    """The conditions of the rules v1.7 and v1.9 added, plus their neighbours."""
    u = register(c, "pd2")
    for rid, label in [
        (".m.rule.is_user_mention", "the v1.7 user-mention rule"),
        (".m.rule.is_room_mention", "the v1.7 room-mention rule"),
        (".m.rule.reaction", "the v1.7 reaction-suppressing rule"),
        (".m.rule.suppress_edits", "the v1.9 edit-suppressing rule"),
        (".m.rule.room.server_acl", "the server-ACL rule"),
        (".m.rule.tombstone", "the tombstone rule"),
        (".m.rule.suppress_notices", "the notice-suppressing rule"),
    ]:
        c.req("GET", f"{CS}/pushrules/global/override/{q(rid)}", token=u.token,
              label=label, derive=lambda b: rule_shape(b, conditions=True))
    c.req("GET", f"{CS}/pushrules/global/underride/{q('.m.rule.call')}",
          token=u.token, label="the call rule",
          derive=lambda b: rule_shape(b, conditions=True))


# --------------------------------------------------------------------------- #
# Push rules: the whole life of a user-defined rule
# --------------------------------------------------------------------------- #
@scenario("push/rule-create-read-delete", tags=TAGS + ["pushrules"], step=5)
def rule_create_read_delete(c):
    u = register(c, "pr1")
    rule = f"{CS}/pushrules/global/content/wordcake"
    c.req("PUT", rule, token=u.token,
          json_body={"pattern": "cake",
                     "actions": ["notify",
                                 {"set_tweak": "sound",
                                  "value": "cakealarm.wav"}]},
          label="create a content rule")
    c.req("GET", rule, token=u.token, label="read it back",
          derive=lambda b: rule_shape(b, pattern=True))
    c.req("GET", f"{CS}/pushrules/", token=u.token,
          label="it is listed among the user's content rules",
          derive=lambda b: {"user_rules": user_rule_ids(b.get("global") or {},
                                                        "content")})
    c.req("PUT", rule, token=u.token,
          json_body={"pattern": "pie", "actions": ["notify"]},
          label="a second PUT with the same id updates the rule")
    c.req("GET", rule, token=u.token, label="the update took effect",
          derive=lambda b: rule_shape(b, pattern=True))
    c.req("DELETE", rule, token=u.token, label="delete the rule")
    c.req("GET", rule, token=u.token, label="it is gone")
    c.req("DELETE", rule, token=u.token, label="deleting it again fails")
    c.req("GET", f"{CS}/pushrules/", token=u.token,
          label="and the user has no content rules left",
          derive=lambda b: {"user_rules": user_rule_ids(b.get("global") or {},
                                                        "content")})


@scenario("push/rule-enabled-subresource", tags=TAGS + ["pushrules"], step=5)
def rule_enabled_subresource(c):
    u = register(c, "pr2")
    rule = f"{CS}/pushrules/global/room/{q('!spam:example.com')}"
    c.req("PUT", rule, token=u.token, json_body={"actions": []},
          label="create a room rule")
    c.req("GET", f"{rule}/enabled", token=u.token,
          label="a new rule is enabled by default")
    c.req("PUT", f"{rule}/enabled", token=u.token, json_body={"enabled": False},
          label="disable it")
    c.req("GET", f"{rule}/enabled", token=u.token,
          label="it reads back disabled")
    c.req("GET", rule, token=u.token, label="and the rule itself says so",
          derive=lambda b: rule_shape(b))
    c.req("PUT", f"{rule}/enabled", token=u.token, json_body={"enabled": True},
          label="enable it again")
    c.req("GET", f"{rule}/enabled", token=u.token,
          label="it reads back enabled")
    c.req("PUT", f"{CS}/pushrules/global/override/{q('.m.rule.master')}/enabled",
          token=u.token, json_body={"enabled": True},
          label="a server-default rule can be enabled")
    c.req("GET", f"{CS}/pushrules/global/override/{q('.m.rule.master')}/enabled",
          token=u.token, label="and reads back enabled")
    c.req("GET", f"{CS}/pushrules/global/override/{q('.m.rule.master')}",
          token=u.token, label="the master rule keeps default: true",
          derive=lambda b: rule_shape(b))


@scenario("push/rule-actions-subresource", tags=TAGS + ["pushrules"], step=5)
def rule_actions_subresource(c):
    u = register(c, "pr3")
    rule = f"{CS}/pushrules/global/sender/{q('@spambot:example.com')}"
    c.req("PUT", rule, token=u.token, json_body={"actions": ["notify"]},
          label="create a sender rule")
    c.req("GET", f"{rule}/actions", token=u.token, label="read its actions",
          derive=lambda b: act_shape(b.get("actions")))
    c.req("PUT", f"{rule}/actions", token=u.token,
          json_body={"actions": ["notify", {"set_tweak": "highlight"}]},
          label="change its actions")
    c.req("GET", f"{rule}/actions", token=u.token,
          label="the change took effect",
          derive=lambda b: act_shape(b.get("actions")))
    c.req("GET", rule, token=u.token, label="the rule carries the new actions",
          derive=lambda b: rule_shape(b))
    c.req("PUT", f"{rule}/actions", token=u.token, json_body={"actions": []},
          label="an empty action list silences the rule")
    c.req("GET", f"{rule}/actions", token=u.token,
          label="and reads back empty",
          derive=lambda b: act_shape(b.get("actions")))
    # "This can be used to change the actions of builtin rules."
    c.req("PUT",
          f"{CS}/pushrules/global/underride/{q('.m.rule.message')}/actions",
          token=u.token, json_body={"actions": []},
          label="a server-default rule's actions can be changed too")
    c.req("GET",
          f"{CS}/pushrules/global/underride/{q('.m.rule.message')}/actions",
          token=u.token,
          label="the built-in rule now notifies about nothing",
          derive=lambda b: act_shape(b.get("actions")))


@scenario("push/rule-kinds", tags=TAGS + ["pushrules"], step=5)
def rule_kinds(c):
    """One user-defined rule in each of the five kinds v1.9 declares."""
    u = register(c, "pr4")
    made = [
        ("content", "kcontent", {"pattern": "waffle", "actions": ["notify"]}),
        ("room", q("!kroom:example.com"), {"actions": []}),
        ("sender", q("@ksender:example.com"), {"actions": []}),
        ("override", "koverride",
         {"actions": ["notify"],
          "conditions": [{"kind": "event_match", "key": "type",
                          "pattern": "com.example.ping"}]}),
        ("underride", "kunderride",
         {"actions": ["notify"],
          "conditions": [{"kind": "event_match", "key": "type",
                          "pattern": "com.example.pong"}]}),
    ]
    for kind, rid, body in made:
        c.req("PUT", f"{CS}/pushrules/global/{kind}/{rid}", token=u.token,
              json_body=body, label=f"create a {kind} rule")
    for kind, rid, _ in made:
        c.req("GET", f"{CS}/pushrules/global/{kind}/{rid}", token=u.token,
              label=f"read the {kind} rule back",
              derive=lambda b: rule_shape(b, conditions=True, pattern=True))
    c.req("GET", f"{CS}/pushrules/", token=u.token,
          label="every kind now carries the user's rule",
          derive=lambda b: {k: user_rule_ids(b.get("global") or {}, k)
                            for k in KINDS})


@scenario("push/rule-ordering", tags=TAGS + ["pushrules"], step=5)
def rule_ordering(c):
    """`before` and `after` against the user's own rules."""
    u = register(c, "pr5")
    base = f"{CS}/pushrules/global/content"

    def order(label):
        return c.req("GET", f"{CS}/pushrules/", token=u.token, label=label,
                     derive=lambda b: {"order": user_rule_ids(
                         b.get("global") or {}, "content")})

    for name in ("alpha", "beta", "gamma"):
        c.req("PUT", f"{base}/{name}", token=u.token,
              json_body={"pattern": name, "actions": ["notify"]}, check=False)
    order("a new rule becomes the most important user rule of its kind")
    c.req("PUT", f"{base}/delta", token=u.token, query={"before": "beta"},
          json_body={"pattern": "delta", "actions": ["notify"]},
          label="create a rule before another")
    order("before puts it immediately above the named rule")
    c.req("PUT", f"{base}/epsilon", token=u.token, query={"after": "beta"},
          json_body={"pattern": "epsilon", "actions": ["notify"]},
          label="create a rule after another")
    order("after puts it immediately below the named rule")
    c.req("PUT", f"{base}/alpha", token=u.token, query={"before": "gamma"},
          json_body={"pattern": "alpha", "actions": ["notify"]},
          label="an existing rule can be moved with before")
    order("the moved rule sits above the named rule")


@scenario("push/rule-ordering-refused", tags=TAGS + ["pushrules"], step=5)
def rule_ordering_refused(c):
    u = register(c, "pr6")
    base = f"{CS}/pushrules/global/content"
    c.req("PUT", f"{base}/anchor", token=u.token,
          json_body={"pattern": "anchor", "actions": ["notify"]}, check=False)
    c.req("PUT", f"{base}/orphan", token=u.token,
          query={"before": "nosuchrule"},
          json_body={"pattern": "orphan", "actions": ["notify"]},
          label="before an unknown rule is refused")
    c.req("PUT", f"{base}/orphan", token=u.token,
          query={"after": "nosuchrule"},
          json_body={"pattern": "orphan", "actions": ["notify"]},
          label="after an unknown rule is refused")
    # "It is not possible to add a rule relative to a predefined server rule."
    c.req("PUT", f"{base}/orphan", token=u.token,
          query={"after": ".m.rule.contains_user_name"},
          json_body={"pattern": "orphan", "actions": ["notify"]},
          label="after a server-default rule is refused")
    c.req("PUT", f"{base}/orphan", token=u.token,
          query={"before": ".m.rule.contains_user_name"},
          json_body={"pattern": "orphan", "actions": ["notify"]},
          label="before a server-default rule is refused")
    c.req("GET", f"{CS}/pushrules/", token=u.token,
          label="none of the refused rules was created",
          derive=lambda b: {"order": user_rule_ids(b.get("global") or {},
                                                   "content")})
    c.req("GET", f"{base}/orphan", token=u.token,
          label="and the rule itself does not exist")


@scenario("push/rule-unknown-id", tags=TAGS + ["pushrules"], step=5)
def rule_unknown_id(c):
    u = register(c, "pr7")
    missing = f"{CS}/pushrules/global/content/nosuchrule"
    c.req("GET", missing, token=u.token,
          label="read a rule that does not exist")
    c.req("DELETE", missing, token=u.token,
          label="delete a rule that does not exist")
    c.req("GET", f"{missing}/enabled", token=u.token,
          label="its enabled sub-resource does not exist either")
    c.req("GET", f"{missing}/actions", token=u.token,
          label="nor does its actions sub-resource")
    c.req("PUT", f"{missing}/enabled", token=u.token,
          json_body={"enabled": True},
          label="enabling a rule that does not exist fails")
    c.req("PUT", f"{missing}/actions", token=u.token,
          json_body={"actions": ["notify"]},
          label="setting its actions fails")
    c.req("GET", f"{CS}/pushrules/global/override/nosuchrule", token=u.token,
          label="the same for an override rule")
    c.req("GET", f"{CS}/pushrules/global/underride/nosuchrule", token=u.token,
          label="and for an underride rule")


@scenario("push/rule-server-defaults-protected", tags=TAGS + ["pushrules"],
          step=5)
def rule_server_defaults_protected(c):
    u = register(c, "pr8")
    c.req("DELETE", f"{CS}/pushrules/global/override/{q('.m.rule.master')}",
          token=u.token, label="a server-default rule cannot be deleted")
    c.req("DELETE", f"{CS}/pushrules/global/underride/{q('.m.rule.message')}",
          token=u.token, label="nor can another one")
    c.req("DELETE",
          f"{CS}/pushrules/global/content/{q('.m.rule.contains_user_name')}",
          token=u.token, label="nor the default content rule")
    # "If the string starts with a dot, the request MUST be rejected as this is
    # reserved for server-default rules."
    c.req("PUT", f"{CS}/pushrules/global/override/{q('.my.own.rule')}",
          token=u.token, json_body={"actions": ["notify"]},
          label="a rule id reserved for server defaults is refused")
    c.req("PUT", f"{CS}/pushrules/global/content/{q('.another')}",
          token=u.token, json_body={"pattern": "x", "actions": ["notify"]},
          label="the same for a content rule")
    c.req("GET", f"{CS}/pushrules/", token=u.token,
          label="the server-default rules are all still there",
          derive=lambda b: ruleset_shape(b.get("global") or {}))


@scenario("push/rule-refused-bodies", tags=TAGS + ["pushrules"], step=5)
def rule_refused_bodies(c):
    u = register(c, "pr9")
    base = f"{CS}/pushrules/global/content"
    c.req("PUT", f"{base}/flagbearer", token=u.token,
          json_body={"pattern": "flag", "actions": ["notify"]}, check=False)
    # `actions` is the one required property of the request body.
    c.req("PUT", f"{base}/noactions", token=u.token,
          json_body={"pattern": "x"},
          label="a rule without actions is refused")
    c.req("PUT", f"{CS}/pushrules/global/room/{q('!r:example.com')}",
          token=u.token, json_body={}, label="an empty rule body is refused")
    c.req("PUT", f"{base}/notjson", token=u.token, body=b"{not json",
          headers={"Content-Type": "application/json"},
          label="a body that is not JSON is refused")
    c.req("PUT", f"{base}/notobject", token=u.token, body=b"[1,2,3]",
          headers={"Content-Type": "application/json"},
          label="a body that is JSON but not an object is refused")
    c.req("PUT", f"{base}/flagbearer/enabled", token=u.token, json_body={},
          label="the enabled sub-resource without its flag is refused")
    c.req("PUT", f"{base}/flagbearer/actions", token=u.token, json_body={},
          label="the actions sub-resource without its list is refused")
    # A slash is not allowed in a rule id; which refusal that produces is the
    # implementation's business, so only the class of the answer is compared.
    c.req("PUT", f"{base}/{q('a/b')}", token=u.token,
          json_body={"pattern": "x", "actions": ["notify"]},
          drop=("errcode",), label="a rule id containing a slash is refused")
    # ...and neither is an empty one, which the ticket names beside the slash:
    # `PUT .../content/` is a different path from `PUT .../content/{ruleId}`,
    # and a server that treats the empty segment as a rule id would create a
    # rule no client can address again.  Which refusal it is remains the
    # implementation's business.
    c.req("PUT", f"{base}/", token=u.token,
          json_body={"pattern": "x", "actions": ["notify"]},
          drop=("errcode",), label="an empty rule id is refused")
    c.req("GET", f"{CS}/pushrules/", token=u.token,
          label="no refused rule was created and the good one is untouched",
          derive=lambda b: {k: user_rule_ids(b.get("global") or {}, k)
                            for k in KINDS})
    c.req("GET", f"{base}/flagbearer", token=u.token,
          label="the rule the refused bodies aimed at is unchanged",
          derive=lambda b: rule_shape(b, pattern=True))


@scenario("push/rules-need-a-token", tags=TAGS + ["pushrules"], step=5)
def rules_need_a_token(c):
    register(c, "pra")
    c.req("GET", f"{CS}/pushrules/",
          label="the rule set needs an access token")
    c.req("GET", f"{CS}/pushrules/global/content/x",
          label="so does one rule")
    c.req("PUT", f"{CS}/pushrules/global/content/x",
          json_body={"pattern": "x", "actions": ["notify"]},
          label="so does creating one")
    c.req("DELETE", f"{CS}/pushrules/global/content/x",
          label="so does deleting one")
    # `soft_logout` is an optional field of the unknown-token error and has no
    # published schema on this endpoint, so it is not compared.
    c.req("GET", f"{CS}/pushrules/", token="syt_not_a_real_token",
          drop=("soft_logout",), label="an unknown token is rejected")


# --------------------------------------------------------------------------- #
# Notification counting through /sync
# --------------------------------------------------------------------------- #
def two_user_room(c, a, b, **kwargs):
    room = create_room(c, a, room_version=V, preset="public_chat", **kwargs)
    join(c, b, room)
    return room


@scenario("push/unread-ordinary-message", tags=TAGS + ["notifications"], step=5)
def unread_ordinary_message(c):
    a = register(c, "un1")
    b = register(c, "un2")
    room = two_user_room(c, a, b)
    settle_room(c, b, room, lambda v: v.get("joined"), what="the join")
    scored_room(c, b, room, label="no notifications before anything is said",
                timeout=0)
    send(c, a, room, "u1", {"msgtype": "m.text", "body": "the first remark"})
    settle_room(c, b, room, has_body("the first remark"),
                what="the first message")
    scored_room(c, b, room,
                label="an ordinary message raises notification_count",
                timeout=1)
    send(c, a, room, "u2", {"msgtype": "m.text", "body": "the second remark"})
    settle_room(c, b, room, has_body("the second remark"),
                what="the second message")
    scored_room(c, b, room, label="a second message raises it again", timeout=2)
    settle_room(c, a, room, has_body("the second remark"),
                what="the sender's own view")
    scored_room(c, a, room, label="the sender is notified about nothing",
                timeout=3)


@scenario("push/unread-user-mention", tags=TAGS + ["notifications"], step=5)
def unread_user_mention(c):
    """`m.mentions.user_ids` naming the reader raises highlight_count (v1.7)."""
    a = register(c, "un3")
    b = register(c, "un4")
    room = two_user_room(c, a, b)
    settle_room(c, b, room, lambda v: v.get("joined"), what="the join")
    send(c, a, room, "m1",
         {"msgtype": "m.text", "body": "a plain opening remark"})
    settle_room(c, b, room, has_body("a plain opening remark"),
                what="the plain message")
    scored_room(c, b, room, label="the plain message highlights nothing",
                timeout=0)
    send(c, a, room, "m2",
         {"msgtype": "m.text", "body": "would you look at this",
          "m.mentions": {"user_ids": [b.user_id]}})
    settle_room(c, b, room, has_body("would you look at this"),
                what="the mention")
    scored_room(c, b, room,
                label="a mention of the reader raises highlight_count",
                timeout=1)
    send(c, a, room, "m3",
         {"msgtype": "m.text", "body": "and this one is for nobody",
          "m.mentions": {"user_ids": [a.user_id]}})
    settle_room(c, b, room, has_body("and this one is for nobody"),
                what="the mention of someone else")
    scored_room(c, b, room,
                label="a mention of someone else does not highlight", timeout=2)
    settle_notifications(
        c, b, lambda v: len(v["notifications"]) == 3,
        what="three notifications for the reader")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="only the mention of the reader carries the highlight tweak",
          derive=notif_shape)
    settle_notifications(
        c, b, lambda v: len(v["notifications"]) == 1,
        what="one highlight", query={"only": "highlight"})
    c.req("GET", f"{CS}/notifications", token=b.token,
          query={"only": "highlight"},
          label="and it is the only highlight", derive=notif_shape)


@scenario("push/unread-room-mention", tags=TAGS + ["notifications"], step=5)
def unread_room_mention(c):
    """`m.mentions.room` from a sender who has the notification power level."""
    a = register(c, "un5")
    b = register(c, "un6")
    room = two_user_room(c, a, b)
    settle_room(c, b, room, lambda v: v.get("joined"), what="the join")
    send(c, a, room, "r1",
         {"msgtype": "m.text", "body": "listen up everyone",
          "m.mentions": {"room": True}})
    settle_room(c, b, room, has_body("listen up everyone"),
                what="the room mention")
    scored_room(c, b, room,
                label="a room mention from a privileged sender highlights",
                timeout=0)
    settle_room(c, a, room, has_body("listen up everyone"),
                what="the sender's own view")
    scored_room(c, a, room, label="but not for the sender", timeout=1)
    c.req("GET",
          f"{CS}/pushrules/global/override/{q('.m.rule.is_room_mention')}",
          token=b.token,
          label="the rule that did it checks the sender's power level",
          derive=lambda x: rule_shape(x, conditions=True))
    settle_notifications(c, b, lambda v: len(v["notifications"]) == 1,
                         what="one notification for the reader")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="the notification carries the highlight tweak",
          derive=notif_shape)
    settle_notifications(c, a, lambda v: not v["notifications"],
                         what="the sender's empty list")
    c.req("GET", f"{CS}/notifications", token=a.token,
          label="the sender was notified about nothing", derive=notif_shape)


@scenario("push/unread-room-mention-unprivileged",
          tags=TAGS + ["notifications"], step=5)
def unread_room_mention_unprivileged(c):
    """The same mention from a member without `notifications.room` power."""
    a = register(c, "un7")
    b = register(c, "un8")
    d = register(c, "un9")
    room = two_user_room(c, a, b)
    join(c, d, room)
    settle_room(c, b, room, lambda v: v.get("joined"), what="the joins")
    send(c, d, room, "r1",
         {"msgtype": "m.text", "body": "may i have your attention",
          "m.mentions": {"room": True}})
    settle_room(c, b, room, has_body("may i have your attention"),
                what="the room mention")
    scored_room(c, b, room,
                label="a room mention without the power level only notifies",
                timeout=0)
    settle_notifications(c, b, lambda v: len(v["notifications"]) == 1,
                         what="one notification")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="and it carries no highlight tweak", derive=notif_shape)
    settle_notifications(c, b, lambda v: not v["notifications"],
                         what="no highlights", query={"only": "highlight"})
    c.req("GET", f"{CS}/notifications", token=b.token,
          query={"only": "highlight"},
          label="so only=highlight returns nothing", derive=notif_shape)
    set_state(c, a, room, "m.room.power_levels",
              {"users": {a.user_id: 100, d.user_id: 60}, "users_default": 0,
               "events_default": 0, "state_default": 50, "ban": 50, "kick": 50,
               "redact": 50, "invite": 50, "notifications": {"room": 50}},
              check=False)
    send(c, d, room, "r2",
         {"msgtype": "m.text", "body": "now can i have it",
          "m.mentions": {"room": True}})
    settle_room(c, b, room, has_body("now can i have it"),
                what="the second room mention")
    scored_room(c, b, room,
                label="once the sender has the power level it highlights",
                timeout=1)
    settle_notifications(c, b, lambda v: len(v["notifications"]) == 1,
                         what="one highlight", query={"only": "highlight"})
    c.req("GET", f"{CS}/notifications", token=b.token,
          query={"only": "highlight"},
          label="the second mention is the highlighted one",
          derive=notif_shape)


@scenario("push/unread-own-message", tags=TAGS + ["notifications"], step=5)
def unread_own_message(c):
    a = register(c, "uo1")
    b = register(c, "uo2")
    room = two_user_room(c, a, b)
    settle_room(c, b, room, lambda v: v.get("joined"), what="the join")
    send(c, b, room, "o1", {"msgtype": "m.text", "body": "talking to myself"})
    settle_room(c, b, room, has_body("talking to myself"),
                what="the own message")
    scored_room(c, b, room,
                label="a message the reader sent counts for nothing", timeout=0)
    send(c, b, room, "o2",
         {"msgtype": "m.text", "body": "still talking to myself",
          "m.mentions": {"user_ids": [b.user_id], "room": True}})
    settle_room(c, b, room, has_body("still talking to myself"),
                what="the own mention")
    scored_room(c, b, room, label="not even when it mentions the sender",
                timeout=1)
    settle_room(c, a, room, has_body("still talking to myself"),
                what="the other member's view")
    scored_room(c, a, room, label="the other member is notified for both",
                timeout=2)
    settle_notifications(c, b, lambda v: not v["notifications"],
                         what="the sender's empty list")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="and the sender's notification list stays empty",
          derive=notif_shape)
    settle_notifications(c, a, lambda v: len(v["notifications"]) == 2,
                         what="the other member's two notifications")
    c.req("GET", f"{CS}/notifications", token=a.token,
          label="while the other member has both", derive=notif_shape)


@scenario("push/unread-receipt-clears",
          tags=TAGS + ["notifications", "receipts"], step=5)
def unread_receipt_clears(c):
    a = register(c, "uc1")
    b = register(c, "uc2")
    room = two_user_room(c, a, b)
    settle_room(c, b, room, lambda v: v.get("joined"), what="the join")
    first = send(c, a, room, "c1",
                 {"msgtype": "m.text", "body": "one for the pile"})
    second = send(c, a, room, "c2",
                  {"msgtype": "m.text", "body": "two for the pile",
                   "m.mentions": {"user_ids": [b.user_id]}})
    settle_room(c, b, room, has_body("two for the pile"), what="both messages")
    scored_room(c, b, room, label="both messages are counted", timeout=0)
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(first)}",
          token=b.token, json_body={}, check=False)
    settle_room(c, b, room,
                lambda v: (v.get("unread") or {}).get("notification_count") == 1,
                what="the receipt for the first message")
    scored_room(c, b, room,
                label="a receipt clears the counts up to its own event",
                timeout=1)
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(second)}",
          token=b.token, json_body={},
          label="post a receipt for the last event")
    settle_room(c, b, room,
                lambda v: (v.get("unread") or {}).get("notification_count") == 0,
                what="the receipt for the last message")
    scored_room(c, b, room,
                label="a receipt for the last event clears both counts",
                timeout=2)
    send(c, a, room, "c3", {"msgtype": "m.text", "body": "three for the pile"})
    settle_room(c, b, room, has_body("three for the pile"),
                what="a message after the receipt")
    scored_room(c, b, room, label="a later message is counted again", timeout=3)


@scenario("push/unread-master-rule-silences", tags=TAGS + ["notifications"],
          step=5)
def unread_master_rule_silences(c):
    """`.m.rule.master` is off by default; switching it on silences all."""
    a = register(c, "um1")
    b = register(c, "um2")
    room = two_user_room(c, a, b)
    settle_room(c, b, room, lambda v: v.get("joined"), what="the join")
    c.req("PUT", f"{CS}/pushrules/global/override/{q('.m.rule.master')}/enabled",
          token=b.token, json_body={"enabled": True},
          label="switch the master rule on")
    c.req("GET", f"{CS}/pushrules/global/override/{q('.m.rule.master')}/enabled",
          token=b.token, label="it reads back on")
    send(c, a, room, "s1", {"msgtype": "m.text", "body": "into the void"})
    send(c, a, room, "s2",
         {"msgtype": "m.text", "body": "also into the void",
          "m.mentions": {"user_ids": [b.user_id], "room": True}})
    settle_room(c, b, room, has_body("also into the void"),
                what="the silenced messages")
    scored_room(c, b, room,
                label="with the master rule on nothing is counted", timeout=0)
    c.req("PUT", f"{CS}/pushrules/global/override/{q('.m.rule.master')}/enabled",
          token=b.token, json_body={"enabled": False},
          label="switch the master rule off again")
    send(c, a, room, "s3", {"msgtype": "m.text", "body": "back in the world"})
    settle_room(c, b, room, has_body("back in the world"),
                what="the message after re-enabling notifications")
    scored_room(c, b, room, label="and counting resumes", timeout=1)


@scenario("push/unread-suppressed-event-types", tags=TAGS + ["notifications"],
          step=5)
def unread_suppressed_event_types(c):
    """Rules whose actions are empty: notices, member events."""
    a = register(c, "us1")
    b = register(c, "us2")
    room = two_user_room(c, a, b)
    settle_room(c, b, room, lambda v: v.get("joined"), what="the join")
    scored_room(c, b, room, label="joining notifies nobody", timeout=0)
    send(c, a, room, "n1",
         {"msgtype": "m.notice", "body": "a notice from the robot"})
    settle_room(c, b, room, has_body("a notice from the robot"),
                what="the notice")
    scored_room(c, b, room, label="a notice is suppressed", timeout=1)
    send(c, a, room, "n2", {"msgtype": "m.text", "body": "an ordinary line"})
    settle_room(c, b, room, has_body("an ordinary line"),
                what="the ordinary message")
    scored_room(c, b, room, label="but an ordinary message is not", timeout=2)
    settle_notifications(c, b, lambda v: len(v["notifications"]) == 1,
                         what="one notification")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="the notice never reached the notification list",
          derive=notif_shape)
    c.req("GET", f"{CS}/pushrules/global/override/"
                 f"{q('.m.rule.suppress_notices')}", token=b.token,
          label="the rule that suppressed it",
          derive=lambda x: rule_shape(x, conditions=True))


# --------------------------------------------------------------------------- #
# GET /notifications
# --------------------------------------------------------------------------- #
@scenario("push/notifications-list", tags=TAGS + ["notifications"], step=5)
def notifications_list(c):
    a = register(c, "nl1")
    b = register(c, "nl2")
    room = two_user_room(c, a, b)
    settle_notifications(c, b, lambda v: not v["notifications"],
                         what="an empty notification list")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="nothing has happened yet", derive=notif_shape)
    for i, body in enumerate(["the opening line", "the middle line",
                              "the closing line"]):
        send(c, a, room, f"n{i}", {"msgtype": "m.text", "body": body})
    settle_notifications(c, b,
                         lambda v: sorted(n_bodies(v)) == ["the closing line",
                                                           "the middle line",
                                                           "the opening line"],
                         what="three notifications")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="the three events are all listed", derive=notif_shape)
    settle_notifications(c, a, lambda v: not v["notifications"],
                         what="the sender's empty list")
    c.req("GET", f"{CS}/notifications", token=a.token,
          label="the sender was notified about none of them",
          derive=notif_shape)
    c.req("GET", f"{CS}/notifications", token=b.token, query={"limit": 20},
          label="a generous limit returns them all", derive=notif_shape)
    # Which two a server keeps is the other half of the order question: the
    # release fixes the count and that they came from this list, not the end
    # of the stream they were taken from.
    c.req("GET", f"{CS}/notifications", token=b.token, query={"limit": 2},
          label="a limit of two returns two of them",
          derive=lambda b2: {
              "count": len(b2.get("notifications") or []),
              "all_three_were_candidates": set(
                  (n.get("event") or {}).get("content", {}).get("body")
                  for n in (b2.get("notifications") or [])
              ) <= {"the opening line", "the middle line", "the closing line"},
              "has_next_token": "next_token" in b2,
          })
    c.req("GET", f"{CS}/notifications",
          label="the endpoint needs an access token")


@scenario("push/notifications-paging", tags=TAGS + ["notifications"], step=5)
def notifications_paging(c):
    a = register(c, "np1")
    b = register(c, "np2")
    room = two_user_room(c, a, b)
    wanted = [f"page line {i}" for i in range(5)]
    for i, body in enumerate(wanted):
        send(c, a, room, f"p{i}", {"msgtype": "m.text", "body": body})
    settle_notifications(c, b, lambda v: n_bodies(v) == sorted(wanted),
                         what="five notifications")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="the whole list", derive=notif_shape)
    # Paging is scored as the property the release states -- the pages
    # partition the list, none is longer than `limit`, and `next_token` runs
    # out when they are used up -- rather than as which end of the stream the
    # server starts from, which it never says.
    page_count = lambda b2: {                                    # noqa: E731
        "count": len(b2.get("notifications") or []),
        "has_next_token": "next_token" in b2}
    r1 = c.req("GET", f"{CS}/notifications", token=b.token, query={"limit": 2},
               label="the first page of two", derive=page_count)
    tok1 = (r1.json or {}).get("next_token")
    if not tok1:
        raise ScenarioAbort("the first page carried no next_token")
    r2 = c.req("GET", f"{CS}/notifications", token=b.token,
               query={"limit": 2, "from": tok1},
               label="the second page follows next_token", derive=page_count)
    tok2 = (r2.json or {}).get("next_token")
    if not tok2:
        raise ScenarioAbort("the second page carried no next_token")
    # The last page: `next_token` is "omitted if there are no more results",
    # and a server that discovers that on this request and one that discovers
    # it on the next have both read the release correctly -- so the count is
    # compared here and the token is not.  (Pages one and two are different:
    # more results are known to remain, so a token has to be there.)
    r3 = c.req("GET", f"{CS}/notifications", token=b.token,
               query={"limit": 2, "from": tok2},
               label="the third page holds the remainder",
               derive=lambda b2: {"count": len(b2.get("notifications") or [])})
    pages = [[(n.get("event") or {}).get("content", {}).get("body")
              for n in ((r.json or {}).get("notifications") or [])]
             for r in (r1, r2, r3)]
    flat = [x for page in pages for x in page]
    fact(c, {"the_three_pages_are_the_whole_list": sorted(flat) == sorted(wanted),
             "no_notification_appears_twice": len(flat) == len(set(flat)),
             "no_page_is_longer_than_the_limit": all(len(p) <= 2 for p in pages)},
         label="the pages partition the list", token=b.token)
    c.req("GET", f"{CS}/notifications", token=b.token, query={"limit": 5},
          label="one page of five has the same contents", derive=notif_shape)
    c.req("GET", f"{CS}/notifications", token=b.token, query={"limit": 1},
          label="a limit of one returns one", derive=page_count)


@scenario("push/notifications-only-highlight", tags=TAGS + ["notifications"],
          step=5)
def notifications_only_highlight(c):
    a = register(c, "nh1")
    b = register(c, "nh2")
    room = two_user_room(c, a, b)
    plan = [("quiet line one", False), ("loud line one", True),
            ("quiet line two", False), ("loud line two", True)]
    for i, (body, loud) in enumerate(plan):
        content = {"msgtype": "m.text", "body": body}
        if loud:
            content["m.mentions"] = {"user_ids": [b.user_id]}
        send(c, a, room, f"h{i}", content)
    every = [body for body, _ in reversed(plan)]
    settle_notifications(c, b, lambda v: n_bodies(v) == sorted(every),
                         what="four notifications")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="all four notifications", derive=notif_shape)
    settle_notifications(
        c, b, lambda v: n_bodies(v) == ["loud line one", "loud line two"],
        what="two highlights", query={"only": "highlight"})
    c.req("GET", f"{CS}/notifications", token=b.token,
          query={"only": "highlight"},
          label="only=highlight keeps the highlighted ones",
          derive=notif_shape)
    c.req("GET", f"{CS}/notifications", token=b.token,
          query={"only": "highlight", "limit": 1},
          label="only=highlight pages like the unfiltered list",
          derive=notif_page)
    settle_notifications(c, a, lambda v: not v["notifications"],
                         what="the sender's empty highlight list",
                         query={"only": "highlight"})
    c.req("GET", f"{CS}/notifications", token=a.token,
          query={"only": "highlight"},
          label="the sender has no highlights", derive=notif_shape)


@scenario("push/notifications-read-flag",
          tags=TAGS + ["notifications", "receipts"], step=5)
def notifications_read_flag(c):
    """The `read` flag moves when a receipt overtakes a notification.

    Every notification here is a highlight: the reference drops an already-read
    notification that highlights nothing a few seconds after the receipt, and
    which notifications a server keeps is not something v1.9 fixes.
    """
    a = register(c, "nr1")
    b = register(c, "nr2")
    room = two_user_room(c, a, b)
    ids = []
    for i, body in enumerate(["shout one", "shout two", "shout three"]):
        ids.append(send(c, a, room, f"r{i}",
                        {"msgtype": "m.text", "body": body,
                         "m.mentions": {"user_ids": [b.user_id]}}))
    every = ["shout three", "shout two", "shout one"]
    settle_notifications(c, b, lambda v: n_bodies(v) == sorted(every),
                         what="three highlights")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="none of them is read yet", derive=notif_shape)
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(ids[1])}",
          token=b.token, json_body={}, label="read up to the second one")
    # Keyed by body rather than by position: the list is compared as a set
    # (`notif_shape`), so which end of the stream a server starts from must not
    # decide whether this scenario proceeds.
    read_map = lambda v: {n["body"]: n["read"] for n in v["notifications"]}  # noqa: E731
    settle_notifications(
        c, b,
        lambda v: read_map(v) == {"shout one": True, "shout two": True,
                                  "shout three": False},
        what="the read flag moving")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="the receipt marked the first two read", derive=notif_shape)
    c.req("POST", f"{CS}/rooms/{q(room)}/receipt/m.read/{q(ids[2])}",
          token=b.token, json_body={}, label="read the last one too")
    settle_notifications(
        c, b,
        lambda v: set(read_map(v).values()) == {True} and len(read_map(v)) == 3,
        what="every notification read")
    c.req("GET", f"{CS}/notifications", token=b.token,
          label="now they are all read", derive=notif_shape)


# --------------------------------------------------------------------------- #
# POST /rooms/{roomId}/report/{eventId}
# --------------------------------------------------------------------------- #
@scenario("push/report-event", tags=TAGS + ["report"], step=5)
def report_event(c):
    a = register(c, "rp1")
    b = register(c, "rp2")
    room = two_user_room(c, a, b)
    eid = send(c, a, room, "x1",
               {"msgtype": "m.text", "body": "the bad message"})
    path = f"{CS}/rooms/{q(room)}/report/{q(eid)}"
    c.req("POST", path, token=b.token,
          json_body={"score": -100, "reason": "this makes me sad"},
          label="report an event with a score and a reason")
    c.req("POST", path, token=b.token, json_body={"reason": "and again"},
          label="the score is optional")
    c.req("POST", path, token=b.token, json_body={"score": 0},
          label="the reason is optional")
    c.req("POST", path, token=b.token, json_body={},
          label="both are optional")
    c.req("POST", path, token=b.token, json_body={"score": -100, "reason": ""},
          label="the reason may be blank")
    c.req("POST", path, token=a.token,
          json_body={"score": -50, "reason": "my own words"},
          label="a member may report their own event")
    c.req("POST", path, json_body={"score": -100},
          label="reporting needs an access token")


@scenario("push/report-refused", tags=TAGS + ["report"], step=5)
def report_refused(c):
    """v1.8 made the caller's membership of the room a requirement."""
    a = register(c, "rp3")
    b = register(c, "rp4")
    # `d` never joins: that, and not "has left", is the case v1.9 pins down.
    # The reference keeps letting a member who *has left* report an event they
    # could still see, so leaving is deliberately not exercised here.
    d = register(c, "rp5")
    room = two_user_room(c, a, b)
    eid = send(c, a, room, "y1", {"msgtype": "m.text", "body": "reportable"})
    path = f"{CS}/rooms/{q(room)}/report/{q(eid)}"
    c.req("POST", path, token=d.token,
          json_body={"score": -100, "reason": "no"},
          label="a user who is not in the room cannot report")
    c.req("POST", f"{CS}/rooms/{q(room)}/report/{q('$nosuchevent')}",
          token=a.token, json_body={"score": -100, "reason": "no"},
          label="an event that does not exist cannot be reported")
    c.req("POST",
          f"{CS}/rooms/{q('!nosuchroom:' + c.server_name)}/report/{q(eid)}",
          token=a.token, json_body={"score": -100, "reason": "no"},
          label="nor can one in a room that does not exist")
    c.req("POST", path, token="syt_not_a_real_token",
          json_body={"score": -100, "reason": "no"},
          label="an unknown access token cannot report")
    c.req("POST", f"{CS}/rooms/{q(room)}/report/{q(eid)}", token=a.token,
          json_body={"score": -100, "reason": "still here"},
          label="a member who is joined still can")
    c.req("POST", f"{CS}/rooms/{q(room)}/report/{q(eid)}", token=b.token,
          json_body={"score": -100, "reason": "me too"},
          label="and so can the other member")


@scenario("push/report-score-and-types", tags=TAGS + ["report"], step=5)
def report_score_and_types(c):
    """v1.9 declares `score` an integer and bounds it only in prose.

    A value inside the range is accepted; a value of the wrong *type* is
    refused.  What a server does with an integer outside the range is not
    settled by the release -- the reference takes it, and refusing it is just
    as defensible -- so those two are issued (they must not wedge what follows)
    and not scored."""
    a = register(c, "rp6")
    room = create_room(c, a, room_version=V, preset="public_chat")
    eid = send(c, a, room, "z1", {"msgtype": "m.text", "body": "scored"})
    path = f"{CS}/rooms/{q(room)}/report/{q(eid)}"
    c.req("POST", path, token=a.token, json_body={"score": -100},
          label="the most offensive score in range")
    c.req("POST", path, token=a.token, json_body={"score": 0},
          label="the least offensive score in range")
    c.req("POST", path, token=a.token, json_body={"score": -1000}, check=False)
    c.req("POST", path, token=a.token, json_body={"score": 50}, check=False)
    # Only the class of these refusals is compared: v1.9 documents no error
    # code for a body that does not match the declared types.
    c.req("POST", path, token=a.token, json_body={"score": "very bad"},
          drop=("errcode",), label="a score that is not an integer is refused")
    c.req("POST", path, token=a.token, json_body={"reason": 5},
          drop=("errcode",), label="a reason that is not a string is refused")
    c.req("POST", path, token=a.token, body=b"[1,2,3]",
          headers={"Content-Type": "application/json"}, drop=("errcode",),
          label="a body that is not an object is refused")


# --------------------------------------------------------------------------- #
# Presence
# --------------------------------------------------------------------------- #
# `PUT /presence/{userId}/status` is rate limited by the reference with a burst
# of one, and a 429 is a deployment's policy rather than a specified behaviour,
# so every user below sets their presence exactly once.
def presence_core(body):
    """The part of a presence state v1.9 fixes outright.

    `currently_active` is optional; see below for what is asked of it.
    """
    return {
        "presence": body.get("presence"),
        "status_msg": body.get("status_msg"),
        # The release says a server *may* include `currently_active` while a
        # user is online, so its value is not compared -- a server that never
        # sets it is conforming.  What the release does fix is what the field
        # means when it is there.
        "currently_active_is_a_bool_if_present":
            not isinstance(body, dict) or "currently_active" not in body
            or isinstance(body["currently_active"], bool),
        "currently_active_only_while_online":
            not body.get("currently_active") or body.get("presence") == "online",
    }


def presence_shape(body):
    """`presence_core`, plus the shape of the value that moves on its own.

    `last_active_ago` counts milliseconds since the user last did anything, so
    it changes between any two reads: only that it is there and is a whole
    number is ever compared, never what it says.
    """
    ago = body.get("last_active_ago")
    if "last_active_ago" not in body:
        kind = "absent"
    elif isinstance(ago, bool) or not isinstance(ago, int):
        kind = type(ago).__name__
    else:
        kind = "int"
    return dict(presence_core(body), last_active_ago_type=kind)


@scenario("push/presence-status", tags=TAGS + ["presence"], step=5)
def presence_status(c):
    a = register(c, "ps1")
    b = register(c, "ps2")
    two_user_room(c, a, b)
    c.req("PUT", f"{CS}/presence/{q(a.user_id)}/status", token=a.token,
          json_body={"presence": "online", "status_msg": "at my desk"},
          label="set presence online with a status message")
    c.req("GET", f"{CS}/presence/{q(a.user_id)}/status", token=a.token,
          label="read it back", derive=presence_shape)
    c.req("GET", f"{CS}/presence/{q(a.user_id)}/status", token=b.token,
          label="a member of the same room may read it", derive=presence_shape)
    c.req("PUT", f"{CS}/presence/{q(b.user_id)}/status", token=b.token,
          json_body={"presence": "unavailable"},
          label="another user sets themselves unavailable")
    # Whether a server remembers an activity time for a user who has only ever
    # announced themselves unavailable is its own business, so this read is
    # compared on the state itself.
    c.req("GET", f"{CS}/presence/{q(b.user_id)}/status", token=a.token,
          label="unavailable is not currently active", derive=presence_core)
    c.req("PUT", f"{CS}/presence/{q(b.user_id)}/status", token=a.token,
          json_body={"presence": "online"},
          label="you cannot set another user's presence")


def presence_section(body):
    """The `/sync` presence section, without the value that moves."""
    events = ((body.get("presence") or {}).get("events") or [])
    return {
        "types": sorted({e.get("type") for e in events}),
        "senders": sorted({e.get("sender") for e in events}),
        "contents": sorted(
            (presence_shape(e.get("content") or {}) for e in events),
            key=lambda d: json.dumps(d, sort_keys=True)),
    }


def others_presence(me):
    """The `/sync` presence section limited to other users: sync.yaml's
    presence is "The updates to the presence status of other users", so
    whether a server also echoes the caller's own is not compared."""
    def derive(body, _me=me):
        events = [e for e in ((body.get("presence") or {}).get("events") or [])
                  if e.get("sender") != _me]
        return presence_section({"presence": {"events": events}})
    return derive


def settle_presence(c, user, ready, *, what, tries=300, delay=0.2, stable=3):
    prev = _UNSET
    agreed = 0
    cur = None
    for _ in range(tries):
        cur = presence_section(_sync_body(c, user, timeout=_nonce(c)))
        if ready(cur) and cur == prev:
            agreed += 1
            if agreed >= stable:
                return cur
        else:
            agreed = 0
        prev = cur
        time.sleep(delay)
    raise ScenarioAbort(f"the presence section did not settle: {what}; "
                        f"last read {cur!r}")


@scenario("push/presence-in-sync", tags=TAGS + ["presence"], step=5)
def presence_in_sync(c):
    a = register(c, "ps3")
    b = register(c, "ps4")
    two_user_room(c, a, b)
    # Both members sync once unscored first: a client that has never synced has
    # never told the server it is there, and which users a first `/sync`
    # already knows about would otherwise depend on the order of two requests.
    _sync_body(c, a, timeout=_nonce(c))
    _sync_body(c, b, timeout=_nonce(c))
    c.req("PUT", f"{CS}/presence/{q(a.user_id)}/status", token=a.token,
          json_body={"presence": "online", "status_msg": "reading the room"},
          label="one member announces themselves")
    settle_presence(c, b, lambda v: a.user_id in v["senders"],
                    what="the other member's presence")
    c.req("GET", f"{CS}/sync", token=b.token, query={"timeout": 0},
          label="the /sync presence section carries m.presence for the room",
          derive=others_presence(b.user_id))
    settle_presence(c, a, lambda v: b.user_id in v["senders"],
                    what="the other member's presence from the other side")
    c.req("GET", f"{CS}/sync", token=a.token, query={"timeout": 0},
          label="and the other member sees it too",
          derive=others_presence(a.user_id))
    c.req("GET", f"{CS}/presence/{q(a.user_id)}/status", token=b.token,
          label="the same state through the presence endpoint",
          derive=presence_shape)
    c.req("GET", f"{CS}/presence/{q(b.user_id)}/status", token=a.token,
          label="and the other member's state the same way",
          derive=presence_shape)


def _refusal(body):
    code = (body or {}).get("errcode")
    return {"errcode": CONCEALED if code in CONCEALABLE_ERRCODES else code}


@scenario("push/presence-refused", tags=TAGS + ["presence"], step=5)
def presence_refused(c):
    a = register(c, "ps5")
    b = register(c, "ps6")
    create_room(c, a, room_version=V, preset="public_chat")
    c.req("PUT", f"{CS}/presence/{q(a.user_id)}/status", token=a.token,
          json_body={"presence": "online"}, label="set presence")
    # presence.yaml documents both a 403 ("not allowed to see") and a 404
    # ("may not exist or isn't exposing presence information to you") for
    # these, so the two refusals are one token on both sides.
    c.req("GET", f"{CS}/presence/{q(b.user_id)}/status", token=a.token,
          label="a user who shares no room may be refused", derive=_refusal)
    c.req("GET", f"{CS}/presence/{q('@nobody:' + c.server_name)}/status",
          token=a.token, label="a user who does not exist", derive=_refusal)
    c.req("GET", f"{CS}/presence/{q('@somebody:elsewhere.example')}/status",
          token=a.token, label="a user on another server", derive=_refusal)
    c.req("GET", f"{CS}/presence/{q(a.user_id)}/status",
          label="reading presence needs an access token")
    c.req("PUT", f"{CS}/presence/{q(a.user_id)}/status",
          json_body={"presence": "offline"},
          label="setting presence needs one too")


# --------------------------------------------------------------------------- #
# Login tokens (v1.7) and refresh tokens
# --------------------------------------------------------------------------- #
# `POST /login/get_token` is optional in release v1.9.  The server under test
# offers it -- there is no switch on that side, which is why nothing here
# configures it -- while the reference keeps it behind a deployment setting of
# its own, so these scenarios ask for that setting through `needs`
# (`mxconf/runner.py`'s ORACLE_CONFIG) and score only the behaviour.


def login_flows(body):
    flows = body.get("flows") or []
    return {
        "has_password_flow": any(f.get("type") == "m.login.password"
                                 for f in flows),
        "has_token_flow": any(f.get("type") == "m.login.token" for f in flows),
        "get_login_token": [f.get("get_login_token") for f in flows
                            if f.get("type") == "m.login.token"],
    }


def get_login_token_capability(body):
    caps = body.get("capabilities") or {}
    return {"present": "m.get_login_token" in caps,
            "capability": caps.get("m.get_login_token")}


@scenario("push/login-token-advertised", tags=TAGS + ["login"], step=5,
          needs=("logintoken",))
def login_token_advertised(c):
    u = register(c, "lt1")
    c.req("GET", f"{CS}/login",
          label="an unauthenticated client sees the m.login.token flow",
          derive=login_flows)
    c.req("GET", f"{CS}/capabilities", token=u.token,
          label="an authenticated one sees the m.get_login_token capability",
          derive=get_login_token_capability)
    # How many flows a server offers, and in what order, is its own business
    # (this server supports two login types); what the release requires is a
    # challenge carrying a session and a flow this client can complete.
    c.req("POST", f"{CS1}/login/get_token", token=u.token, json_body={},
          drop=("params", "flows"),
          label="the endpoint answers with a user-interactive auth challenge",
          derive=lambda b: {
              "a_session_to_retry_against": bool(b.get("session")),
              "offers_a_password_flow":
                  ["m.login.password"] in [f.get("stages")
                                           for f in (b.get("flows") or [])],
              "nothing_completed_yet": not b.get("completed")})
    # Which of the release's authentication error codes an endpoint answers a
    # missing token with is not fixed per endpoint, so the status carries the
    # check and the body is asked only to be one of them.
    c.req("POST", f"{CS1}/login/get_token", json_body={},
          label="and needs an access token of its own",
          derive=lambda b: {"errcode_if_any_is_one_the_release_names":
                            (b or {}).get("errcode") in
                            (None, "M_MISSING_TOKEN", "M_UNKNOWN_TOKEN",
                             "M_FORBIDDEN")})


@scenario("push/login-token-single-use", tags=TAGS + ["login"], step=5,
          needs=("logintoken",))
def login_token_single_use(c):
    u = register(c, "lt2")
    r = c.req("POST", f"{CS1}/login/get_token", token=u.token, json_body={},
              drop=("params", "flows"),
              label="the user-interactive auth challenge")
    session = (r.json or {}).get("session")
    if not session:
        raise ScenarioAbort("the challenge carried no session")
    auth = {"type": "m.login.password", "session": session,
            "identifier": {"type": "m.id.user", "user": u.localpart},
            "password": u.password}
    r2 = c.req("POST", f"{CS1}/login/get_token", token=u.token,
               json_body={"auth": auth},
               label="passing the challenge mints a login token",
               derive=lambda b: {
                   "login_token_is_string":
                       isinstance(b.get("login_token"), str)
                       and bool(b.get("login_token")),
                   "expires_in_ms_is_int":
                       isinstance(b.get("expires_in_ms"), int)
                       and not isinstance(b.get("expires_in_ms"), bool),
               })
    token = (r2.json or {}).get("login_token")
    if not token:
        raise ScenarioAbort("no login token was minted")
    r3 = c.req("POST", f"{CS}/login",
               json_body={"type": "m.login.token", "token": token},
               label="the token logs in as the user who minted it")
    new_token = (r3.json or {}).get("access_token")
    c.req("GET", f"{CS}/account/whoami", token=new_token,
          label="the session it produced is that user's")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.token", "token": token},
          label="the same token cannot be used twice")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.token", "token": "syl_not_a_token"},
          label="and a token nobody minted is refused")
    c.req("POST", f"{CS}/login", json_body={"type": "m.login.token"},
          label="m.login.token without a token is refused")


@scenario("push/login-token-refusals", tags=TAGS + ["login"], step=5,
          needs=("logintoken",))
def login_token_refusals(c):
    """What the login-token surfaces refuse.

    The release calls `POST /login/get_token` optional; what it requires is
    that the three places a client can look -- `GET /login`, `GET /capabilities`
    and the endpoint itself -- agree with each other, and that a token nobody
    minted is refused like any other bad credential.
    """
    u = register(c, "lt3")
    c.req("GET", f"{CS}/login", label="the advertised login flows",
          derive=login_flows)
    c.req("GET", f"{CS}/capabilities", token=u.token,
          label="the m.get_login_token capability",
          derive=get_login_token_capability)
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.token", "token": "syl_not_a_token"},
          label="a made-up login token is refused")
    c.req("GET", f"{CS}/capabilities",
          label="the capabilities endpoint needs a token")


@scenario("push/refresh-rotation", tags=TAGS + ["login", "refresh"], step=5)
def refresh_rotation(c):
    u = register(c, "rf1")
    r = c.req("POST", f"{CS}/login",
              json_body={"type": "m.login.password",
                         "identifier": {"type": "m.id.user",
                                        "user": u.localpart},
                         "password": u.password, "refresh_token": True},
              label="a login that asks for a refresh token",
              derive=lambda b: {
                  "keys": sorted(k for k in b
                                 if k in ("access_token", "device_id",
                                          "user_id", "refresh_token",
                                          "expires_in_ms")),
                  "user_id": b.get("user_id"),
                  "expires_in_ms_is_int":
                      isinstance(b.get("expires_in_ms"), int)
                      and not isinstance(b.get("expires_in_ms"), bool),
              })
    first = (r.json or {}).get("refresh_token")
    if not first:
        raise ScenarioAbort("the login returned no refresh token")
    r2 = c.req("POST", f"{CS}/refresh", json_body={"refresh_token": first},
               label="the refresh token buys a new pair")
    second_access = (r2.json or {}).get("access_token")
    second_refresh = (r2.json or {}).get("refresh_token")
    if not second_access or not second_refresh:
        raise ScenarioAbort("the refresh returned no new pair")
    c.req("GET", f"{CS}/account/whoami", token=second_access,
          label="the new access token works")
    # "The old refresh token remains valid until the new access token or
    # refresh token is used, at which point the old refresh token is revoked."
    # Which refusal that produces is not something v1.9 pins down: it documents
    # 401 M_UNKNOWN_TOKEN while the reference answers 403, so only the class of
    # the answer is compared here.
    c.req("POST", f"{CS}/refresh", json_body={"refresh_token": first},
          drop=("errcode",),
          label="using the new access token revoked the old refresh token")
    r3 = c.req("POST", f"{CS}/refresh",
               json_body={"refresh_token": second_refresh},
               label="the new refresh token still works")
    c.req("GET", f"{CS}/account/whoami",
          token=(r3.json or {}).get("access_token"),
          label="and buys another working access token")


@scenario("push/refresh-refused", tags=TAGS + ["login", "refresh"], step=5)
def refresh_refused(c):
    u = register(c, "rf2")
    c.req("POST", f"{CS}/refresh", json_body={"refresh_token": "syr_nonsense"},
          label="a refresh token nobody minted is refused")
    c.req("POST", f"{CS}/refresh", json_body={}, drop=("errcode",),
          label="a body without a refresh token is refused")
    c.req("POST", f"{CS}/refresh", json_body={"refresh_token": u.token},
          label="an access token is not a refresh token")
    c.req("POST", f"{CS}/refresh", body=b"{not json",
          headers={"Content-Type": "application/json"},
          label="a body that is not JSON is refused")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password",
                     "identifier": {"type": "m.id.user", "user": u.localpart},
                     "password": u.password},
          label="a login that does not ask for a refresh token gets none",
          derive=lambda b: {
              "has_refresh_token": "refresh_token" in b,
              "has_expires_in_ms": "expires_in_ms" in b,
              "user_id": b.get("user_id"),
          })

# --------------------------------------------------------------------------- #
# The pre-flight, on the routes this milestone adds
# --------------------------------------------------------------------------- #
# Every step's ticket restates the release's CORS requirement -- every endpoint
# answers an `OPTIONS` request, and answering it performs none of the endpoint's
# own logic -- and step 1 scores it on the routes that existed then.  A route
# added later is a route that can be added without it, so each milestone asks
# the same two questions of its own.  Nothing here needs a room, a user or an
# upload to exist: that is the point of the second question.


@scenario("push/options-preflight", tags=["cors"], step=5)
def push_options_preflight(c):
    """`OPTIONS` on the endpoints this release adds."""
    empty = lambda b: {"body_carries_nothing_of_substance": not b}   # noqa: E731
    for path, method, label in (
            (f"{CS}/notifications", "GET", "the notifications endpoint"),
            (f"{CS}/pushrules/", "GET", "the push-rules tree"),
            (f"{CS}/rooms/{q('!nonexistentroom00000000000:' + c.server_name)}/report/{q('$nonexistenteventaaaaaaaaaaaaaaaaaaaaaaaaaaa')}", "POST",
          "the event-report endpoint")
    ):
        c.req("OPTIONS", path,
              headers={"Origin": "https://example.invalid",
                       "Access-Control-Request-Method": method},
              label=f"OPTIONS preflight on {label}", derive=empty)
