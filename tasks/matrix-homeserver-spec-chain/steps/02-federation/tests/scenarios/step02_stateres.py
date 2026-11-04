"""Step 2 -- state resolution version 2, driven from a second, forked branch of
the event DAG.

State resolution only runs when two branches of a room's DAG give different
events to the same `(type, state_key)`.  A client cannot cause that: the
Client-Server API takes a content and the server chooses the `prev_events`
itself -- always the current forward extremities -- so everything a client sends
extends one chain and there is nothing to resolve.  A fork has to be *authored*,
by something that signs its own PDUs and picks their `prev_events`.  That is
`mxconf/fakefed.py`: an Ed25519 identity with an HTTPS listener, which joins a
real room on the server under test through `make_join`/`send_join` and then
pushes a hand-built DAG into it with `PUT /_matrix/federation/v1/send/{txnId}`.

What is scored is never the federation traffic.  Every scored request in this
module is a Client-Server API read -- `GET /rooms/{roomId}/state` and
`GET /rooms/{roomId}/state/{eventType}/{stateKey}` -- because those report the
*resolved* state, which is what the specification fixes; the Server-Server API's
own wire format is full of server-chosen detail (signatures, PDU batching, which
server is asked for what) that no two implementations need to agree on.

The corpus
----------
The graphs come from Synapse's own state-resolution-v2 test corpus,
`tests/state/test_v2.py` at tag v1.160.0: `test_ban_vs_pl`,
`test_join_rule_evasion`, `test_offtopic_pl`, `test_topic_basic`,
`test_topic_reset`, `test_topic`, `test_mainline_sort`, and the
`LexicographicalTestCase` ordering expressed as a DAG.  Each case is a set of
events plus a set of edge chains; `INITIAL_EVENTS` in the corpus -- a create,
a power-levels, a join rule and four memberships -- is not replayed but *mapped
onto a real room*: the room the scenario creates through `POST /createRoom`
already has a create event, and its creator sets the power levels the corpus
starts from.  `ALICE`, `BOB`, `CHARLIE`, `ZARA` and `EVELYN` become users on the
fake server, so this server can sign for them; the room's creator is a real
account on the server under test, and stays at power level 100 in every
power-levels event the corpus sends, because a user cannot demote another user
who is at their own level and the event would be refused.

Two deliberate departures from the corpus text, both so that the *result* can be
read back through the Client-Server API at all:

* the corpus's topic events carry `content={}`, which makes the winning topic
  indistinguishable from the losing one.  Each topic event here carries
  `{"topic": "<node id>"}`, so the resolved state names which branch won.
* likewise `test_ban_vs_pl`'s two power-levels events have identical content.
  Every power-levels event here carries one extra entry in `users`, for a user
  who sends nothing, whose level is that event's position in the graph.  The
  authorisation rules do not care (the level is far below every sender's own),
  the resolution algorithm does not read it, and it makes the winner legible.

One correction: the corpus's `test_join_rule_evasion` writes its join rule under
the key `join_rules`, not the `join_rule` the specification defines, so the event
sets no join rule at all and the case only works through the receiving server's
default.  That default is the implementation's, not the specification's, so this
module writes `join_rule` and the case tests what it was written to test.

Determinism
-----------
Federation is asynchronous and the state a scored request reads is the result of
work that happens after the transaction is acknowledged.  Every scenario
therefore pushes, after the DAG, one further message from a joined user, and
polls the Client-Server API with `check=False` until that marker is visible
*and* the state has read identically three times running; only then is the
scored request issued.  Nothing sleeps for a fixed time.

The resolution algorithm breaks its final ties on `origin_server_ts` and then on
event id, and an event id here is a hash over a real room's create event, so it
differs between runs.  Every event this module builds is therefore given a
distinct `origin_server_ts`, allocated in the order the corpus itself allocates
them -- the lexicographic topological order of the case's graph -- so that no
outcome can turn on an event id.  No scored value is an event id, a timestamp or
a signature.
"""

import json
import time

from mxconf import fakefed
from mxconf.ctx import ScenarioAbort
from mxconf.fakefed import (FakeFederation, content_hash, encode_b64,
                            event_id, federation_url, pick_port, port_hint)
from mxconf.mx import CS, ok, q
from mxconf.registry import scenario


# Emptying the reference homeserver's `ip_range_blacklist`, so one loopback
# server will talk to another, now lives in `mxconf/targets.py`, where the
# recording and the scoring paths share it.


V1, V6, V9 = "1", "6", "9"

# Only messages, so a derived list of bodies is not perturbed by the membership
# and state events that also arrive.
MSG_FILTER = json.dumps({"types": ["m.room.message"]})

# The state types every derived state listing reports.  Everything else in a
# created room (history visibility, the room's name) is the preset's business,
# not the corpus's.
WATCHED = ("m.room.join_rules", "m.room.member", "m.room.power_levels",
           "m.room.topic")


# --------------------------------------------------------------------------- #
# The corpus, as data.
# --------------------------------------------------------------------------- #
class Node:
    """One event of a corpus graph: who sends it, what it is, what it says."""

    def __init__(self, nid, sender, etype, state_key, content):
        self.nid = nid
        self.sender = sender
        self.etype = etype
        self.state_key = state_key        # None for a message
        self.content = content            # (users, creator, witness, rank) -> dict


def PL(_events=None, **levels):
    """A power-levels content, as the corpus writes it: a `users` map and
    (only where the corpus sets one) an `events` map, so that every other level
    keeps its specified default.

    The room's creator is kept at 100 in every one of them.  The corpus's rooms
    are created by ALICE; here a real account on the server under test creates
    the room, and the authorisation rules refuse an event that lowers a user who
    is at the sender's own level, so dropping the creator would have the whole
    corpus refused.  The creator sends nothing after the room is set up, so it
    takes no part in any conflict.
    """
    def build(users, creator, witness, rank):
        out = {creator: 100, witness: rank}
        for name, level in levels.items():
            out[users[name]] = level
        content = {"users": out}
        if _events:
            content["events"] = dict(_events)
        return content
    return build


def TOPIC(nid):
    """`{"topic": "<node id>"}` -- the corpus sends `{}`, which would make the
    resolved topic unreadable."""
    def build(users, creator, witness, rank):
        return {"topic": nid}
    return build


def MEMBER(membership):
    def build(users, creator, witness, rank):
        return {"membership": membership}
    return build


def JOIN_RULE(rule):
    def build(users, creator, witness, rank):
        return {"join_rule": rule}
    return build


def MSG(body):
    def build(users, creator, witness, rank):
        return {"msgtype": "m.text", "body": body}
    return build


START = Node("START", "ZARA", "m.room.message", None, MSG("START"))
END = Node("END", "ZARA", "m.room.message", None, MSG("END"))


class Case:
    def __init__(self, name, joins, events, edges, watch, tags=()):
        self.name = name
        self.joins = joins            # corpus names to join over federation
        self.events = events
        self.edges = edges
        self.watch = watch            # [(type, state_key)] read individually
        self.tags = tuple(tags)


# `test_ban_vs_pl`: ALICE bans BOB on one branch while BOB sends a power-levels
# event on the other.  The ban must survive.
BAN_VS_PL = Case(
    name="ban-vs-pl",
    joins=("ALICE", "BOB", "ZARA"),
    events=[
        Node("PA", "ALICE", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
        Node("MA", "ALICE", "m.room.member", "ALICE", MEMBER("join")),
        Node("MB", "ALICE", "m.room.member", "BOB", MEMBER("ban")),
        Node("PB", "BOB", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
    ],
    edges=[["END", "MB", "MA", "PA", "START"], ["END", "PB", "PA"]],
    watch=[("m.room.member", "BOB"), ("m.room.power_levels", "")],
    tags=["ban", "power-levels"],
)

# `test_join_rule_evasion`: EVELYN joins on one branch while the join rule is
# closed on the other.  The closed rule must survive and the join must not.
JOIN_RULE_EVASION = Case(
    name="join-rule-evasion",
    joins=("ALICE", "ZARA"),
    events=[
        Node("JR", "ALICE", "m.room.join_rules", "", JOIN_RULE("invite")),
        Node("ME", "EVELYN", "m.room.member", "EVELYN", MEMBER("join")),
    ],
    edges=[["END", "JR", "START"], ["END", "ME", "START"]],
    watch=[("m.room.join_rules", ""), ("m.room.member", "EVELYN")],
    tags=["join-rules", "membership"],
)

# `test_offtopic_pl`: a chain of power-levels events against the one they all
# descend from.  The end of the chain must survive.
OFFTOPIC_PL = Case(
    name="offtopic-pl",
    joins=("ALICE", "BOB", "CHARLIE", "ZARA"),
    events=[
        Node("PA", "ALICE", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
        Node("PB", "BOB", "m.room.power_levels", "",
             PL(ALICE=100, BOB=50, CHARLIE=50)),
        Node("PC", "CHARLIE", "m.room.power_levels", "",
             PL(ALICE=100, BOB=50, CHARLIE=0)),
    ],
    edges=[["END", "PC", "PB", "PA", "START"], ["END", "PA"]],
    watch=[("m.room.power_levels", "")],
    tags=["power-levels"],
)

# `test_topic_basic`: two topics, one on each branch, with a power-levels event
# on each branch that decides who was allowed to set one.
TOPIC_BASIC = Case(
    name="topic-basic",
    joins=("ALICE", "BOB", "ZARA"),
    events=[
        Node("T1", "ALICE", "m.room.topic", "", TOPIC("T1")),
        Node("PA1", "ALICE", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
        Node("T2", "ALICE", "m.room.topic", "", TOPIC("T2")),
        Node("PA2", "ALICE", "m.room.power_levels", "", PL(ALICE=100, BOB=0)),
        Node("PB", "BOB", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
        Node("T3", "BOB", "m.room.topic", "", TOPIC("T3")),
    ],
    edges=[["END", "PA2", "T2", "PA1", "T1", "START"], ["END", "T3", "PB", "PA1"]],
    watch=[("m.room.topic", ""), ("m.room.power_levels", "")],
    tags=["topic", "power-levels"],
)

# `test_topic_reset`: a ban on one branch against the branch that never saw the
# events leading to it; the state must fall back to the topic from before.
TOPIC_RESET = Case(
    name="topic-reset",
    joins=("ALICE", "BOB", "ZARA"),
    events=[
        Node("T1", "ALICE", "m.room.topic", "", TOPIC("T1")),
        Node("PA", "ALICE", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
        Node("T2", "BOB", "m.room.topic", "", TOPIC("T2")),
        Node("MB", "ALICE", "m.room.member", "BOB", MEMBER("ban")),
    ],
    edges=[["END", "MB", "T2", "PA", "T1", "START"], ["END", "T1"]],
    watch=[("m.room.topic", ""), ("m.room.member", "BOB")],
    tags=["topic", "ban"],
)

# `test_topic`: the two branches meet twice -- at a message and then at the end
# -- so the resolution at the first meeting decides what the events after it are
# authorised against.
TOPIC_CASE = Case(
    name="topic",
    joins=("ALICE", "BOB", "ZARA"),
    events=[
        Node("T1", "ALICE", "m.room.topic", "", TOPIC("T1")),
        Node("PA1", "ALICE", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
        Node("T2", "ALICE", "m.room.topic", "", TOPIC("T2")),
        Node("PA2", "ALICE", "m.room.power_levels", "", PL(ALICE=100, BOB=0)),
        Node("PB", "BOB", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
        Node("T3", "BOB", "m.room.topic", "", TOPIC("T3")),
        Node("MZ1", "ZARA", "m.room.message", None, MSG("MZ1")),
        Node("T4", "ALICE", "m.room.topic", "", TOPIC("T4")),
    ],
    edges=[
        ["END", "T4", "MZ1", "PA2", "T2", "PA1", "T1", "START"],
        ["END", "MZ1", "T3", "PB", "PA1"],
    ],
    watch=[("m.room.topic", ""), ("m.room.power_levels", "")],
    tags=["topic", "power-levels"],
)

# `test_mainline_sort`: the topic sent *earlier* must win, because it descends
# from the later power-levels event.  Without mainline ordering the later topic
# would win, which is what this case is for.
MAINLINE_SORT = Case(
    name="mainline-sort",
    joins=("ALICE", "BOB", "ZARA"),
    events=[
        Node("T1", "ALICE", "m.room.topic", "", TOPIC("T1")),
        Node("PA1", "ALICE", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
        Node("T2", "ALICE", "m.room.topic", "", TOPIC("T2")),
        Node("PA2", "ALICE", "m.room.power_levels", "",
             PL({"m.room.power_levels": 100}, ALICE=100, BOB=50)),
        Node("PB", "BOB", "m.room.power_levels", "", PL(ALICE=100, BOB=50)),
        Node("T3", "BOB", "m.room.topic", "", TOPIC("T3")),
        Node("T4", "ALICE", "m.room.topic", "", TOPIC("T4")),
    ],
    edges=[["END", "T3", "PA2", "T2", "PA1", "T1", "START"],
           ["END", "T4", "PB", "PA1"]],
    watch=[("m.room.topic", ""), ("m.room.power_levels", "")],
    tags=["topic", "mainline"],
)

# `LexicographicalTestCase.test_simple` as a DAG.  The corpus asserts that the
# graph `l->o, m->{n,o}, n->o, o->{}, p->o` sorts to `[o, l, n, m, p]`; that sort
# is the reverse topological power ordering, which is not observable on its own,
# so the five nodes are made conflicting power-levels events whose authorisation
# depends on the order they are applied in.  With the corpus's ordering, N
# demotes CHARLIE before M is applied and M demotes nobody, so M survives and P
# -- sent by CHARLIE -- is refused; with L and N the other way round, M would be
# refused and P would survive.  The resolved `users` map therefore names the
# ordering.  Every sender is at the same level and every event has a distinct
# `origin_server_ts`, so the ordering is decided by the graph and the timestamps
# and never by an event id.
LEXICOGRAPHICAL = Case(
    name="lexicographic-order",
    joins=("ALICE", "BOB", "CHARLIE", "ZARA"),
    events=[
        Node("O", "ALICE", "m.room.power_levels", "",
             PL(ALICE=100, BOB=50, CHARLIE=50)),
        Node("L", "ALICE", "m.room.power_levels", "",
             PL(ALICE=100, BOB=0, CHARLIE=50)),
        Node("N", "ALICE", "m.room.power_levels", "",
             PL(ALICE=100, BOB=50, CHARLIE=0)),
        Node("M", "BOB", "m.room.power_levels", "",
             PL(ALICE=100, BOB=50, CHARLIE=0)),
        Node("P", "CHARLIE", "m.room.power_levels", "",
             PL(ALICE=100, BOB=50, CHARLIE=50)),
    ],
    edges=[["O", "START"], ["L", "O"], ["N", "O"], ["M", "N"], ["M", "O"],
           ["P", "O"], ["END", "L"], ["END", "M"], ["END", "P"]],
    watch=[("m.room.power_levels", "")],
    tags=["power-levels", "ordering"],
)


# --------------------------------------------------------------------------- #
# Graph machinery
# --------------------------------------------------------------------------- #
def build_graph(case):
    """`{node id: set of the nodes it points back at}`, the corpus's own shape.

    The corpus's `INITIAL_EDGES` end `... -> START -> ... -> CREATE` and begin
    `END -> START`, so END always points back at START as well as at the
    branches; that edge is kept, because it puts the state from *before* the
    fork into the resolution as a third state set, which is what the corpus
    resolves.
    """
    graph = {"START": set(), "END": {"START"}}
    for node in case.events:
        graph[node.nid] = set()
    for chain in case.edges:
        for child, parent in zip(chain, chain[1:]):
            graph[child].add(parent)
    return graph


def lexicographic_topological_sort(graph):
    """Kahn's algorithm, always taking the smallest available node.

    This is the order in which the corpus allocates `origin_server_ts`, and this
    module allocates them the same way so that the two agree about which of two
    events is the earlier.
    """
    import heapq

    children = {n: set() for n in graph}
    remaining = {n: len(deps) for n, deps in graph.items()}
    for node, deps in graph.items():
        for dep in deps:
            children[dep].add(node)
    heap = sorted(n for n, left in remaining.items() if left == 0)
    heapq.heapify(heap)
    order = []
    while heap:
        node = heapq.heappop(heap)
        order.append(node)
        for child in sorted(children[node]):
            remaining[child] -= 1
            if remaining[child] == 0:
                heapq.heappush(heap, child)
    if len(order) != len(graph):
        raise ScenarioAbort(f"the case's graph has a cycle: {graph!r}")
    return order


def push_case(c, ff, room, case, users, creator_id, witness):
    """Build the case's DAG on top of the real room, and push it.

    Every event is signed by the fake server, carries the `prev_events` the
    corpus's edges give it, and takes its `auth_events` from the state before it
    exactly as the authorisation rules define -- the create event, the power
    levels, the sender's membership, plus the join rules and the target's
    membership for a membership event.  A receiving server checks that an
    event's `auth_events` are drawn from precisely that set, so nothing else
    can be put there.

    Where two branches meet, the state before the meeting point is taken from
    the branch whose tip is latest in the graph's own order.  That choice only
    ever decides which power-levels event a *message* is authorised against, and
    every branch authorises the messages this corpus sends; the resolution
    itself is the server's to perform.
    """
    graph = build_graph(case)
    order = lexicographic_topological_sort(graph)
    rank = {nid: i for i, nid in enumerate(order)}
    by_id = {node.nid: node for node in [START, END] + list(case.events)}

    room_state = ff.rooms[room]
    base_state = dict(room_state.state)
    base_prevs = list(room_state.tips)
    base_ts = int(time.time() * 1000)

    real_id = {}
    state_after = {}
    pdus = []
    for nid in order:
        node = by_id[nid]
        parents = sorted(graph[nid], key=lambda p: rank[p])
        if parents:
            prev_ids = [real_id[p] for p in parents]
            before = {}
            for parent in parents:
                before.update(state_after[parent])
        else:
            prev_ids = list(base_prevs)
            before = dict(base_state)
        content = node.content(users, creator_id, witness, rank[nid])
        sender = users[node.sender]
        state_key = users[node.state_key] if node.state_key in users \
            else node.state_key
        auth = ff.auth_events_for(room, node.etype, sender, state_key, content,
                                  before)
        eid, event = ff.build_event(
            room, etype=node.etype, sender=sender, content=content,
            state_key=state_key, prev_events=prev_ids, auth_events=auth,
            origin_server_ts=base_ts + rank[nid])
        real_id[nid] = eid
        after = dict(before)
        if state_key is not None:
            after[(node.etype, state_key)] = eid
        state_after[nid] = after
        ff.state_after[eid] = after
        pdus.append(event)

    # One more message, after the whole graph, from a user every branch agrees
    # is joined, authorised against the state from before the fork so that its
    # own acceptance cannot depend on how the fork resolves.  Its arrival is
    # what tells the scenario the graph has been processed: a server handles the
    # PDUs of a transaction in order.
    marker_sender = users["ZARA"]
    marker_auth = ff.auth_events_for(room, "m.room.message", marker_sender, None,
                                     {}, base_state)
    marker_id, marker = ff.build_event(
        room, etype="m.room.message", sender=marker_sender,
        content={"msgtype": "m.text", "body": "SETTLED"},
        prev_events=[real_id["END"]], auth_events=marker_auth,
        origin_server_ts=base_ts + len(order))
    ff.state_after[marker_id] = dict(state_after["END"])
    pdus.append(marker)
    room_state.tips = [marker_id]

    fed = c.extra["fed_url"]
    results = ff.send_transaction(fed, c.extra["fed_name"], pdus)
    return {"ids": real_id, "results": results, "order": order}


# --------------------------------------------------------------------------- #
# Reads
# --------------------------------------------------------------------------- #
UNSET = "<unset>"
ABSENT = "<absent>"


def _field(etype, content):
    """The one content field a case is about, as a string.

    A string, because these are sorted together and because what the corpus
    fixes is *which event won a key*, not the shape of its content.
    """
    if etype == "m.room.power_levels":
        users = (content or {}).get("users") or {}
        return "users " + " ".join(f"{u}={users[u]}" for u in sorted(users))
    key = {"m.room.topic": "topic", "m.room.member": "membership",
           "m.room.join_rules": "join_rule"}.get(etype)
    value = (content or {}).get(key) if key else None
    return UNSET if value is None else str(value)


def state_value(etype, body):
    """`[type, field]` for a single-key state read.

    A key that state resolution dropped is not an error the specification
    leaves open -- the read is a 404 and the event is simply not state -- so it
    is recorded as such rather than as a missing field.
    """
    if isinstance(body, dict) and "errcode" in body:
        return [etype, ABSENT]
    return [etype, _field(etype, body or {})]


def state_rows(body, types=WATCHED):
    """`[type, state_key, the field the case is about]` for the current state.

    A whole state event is not compared: it carries an event id, an
    `origin_server_ts` and `unsigned` bookkeeping, none of which the
    specification fixes.  What the corpus is about is which event won each key,
    and the field this list keeps is what says so.
    """
    rows = []
    for event in body if isinstance(body, list) else []:
        etype = event.get("type")
        if etype not in types:
            continue
        rows.append([str(etype), str(event.get("state_key")),
                     _field(etype, event.get("content") or {})])
    return sorted(rows)


def read_rows(c, token, room):
    r = c.req("GET", f"{CS}/rooms/{q(room)}/state", token=token, check=False)
    return state_rows(r.json) if r.status == 200 else None


def read_bodies(c, token, room):
    r = c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=token,
              query={"dir": "b", "limit": 40, "filter": MSG_FILTER}, check=False)
    if r.status != 200:
        return []
    return [(e.get("content") or {}).get("body")
            for e in (r.json or {}).get("chunk", [])]


def message_rows(body):
    """`[type, body]` for a `/messages` chunk, newest first.

    The body is kept as it is rather than dropped, because an event whose content
    hash does not match its content is not discarded by the specification -- the
    receiving server falls back to the redacted copy -- and a redacted copy has
    no body at all.  Keeping the field is what makes the difference visible.
    """
    return [[str(e.get("type")), (e.get("content") or {}).get("body")]
            for e in (body or {}).get("chunk", [])]


def settle(c, read, ready=None, *, what, tries=600, delay=0.25):
    """Poll an unscored read until it has arrived and then stopped changing.

    `ready` says whether the awaited effect is there; the value is returned only
    once it holds *and* two further reads produced exactly the same value, so
    that the scored request issued next sees a room nothing is still arriving
    into.
    """
    ready = ready or (lambda value: bool(value))
    previous = object()
    agreed = 0
    current = None
    for _ in range(tries):
        current = read()
        if ready(current) and current == previous:
            agreed += 1
            if agreed >= 2:
                return current
        else:
            agreed = 0
        previous = current
        time.sleep(delay)
    raise ScenarioAbort(f"federation did not converge: {what}; "
                        f"last read {current!r}")


# --------------------------------------------------------------------------- #
# Setup shared by every scenario here
# --------------------------------------------------------------------------- #
# The corpus's cast.  Every one of them is a user on the fake server, because
# only the server that owns a user id can sign an event from it, and the whole
# point of these scenarios is to sign events nobody else would.
CAST = ("ALICE", "BOB", "CHARLIE", "EVELYN", "ZARA")


def setup(c, room_version, joins):
    """A real room on the server under test that the fake server has joined.

    Returns (fake server, creator, room id, {corpus name: user id}, witness).
    """
    server = c.extra["server"]
    fake = FakeFederation(port=pick_port(port_hint(server) + 4))
    c.extra["fake"] = fake
    fake.start()
    # The fake server's name is in every user id the corpus sends, so it is
    # aliased before anything that carries it can be recorded.
    c.syms.aliases[fake.server_name] = "<FAKE>"
    c.extra["fed_url"] = federation_url(server)
    c.extra["fed_name"] = server.server_name

    creator = register(c, "creator")
    users = {name: fake.user_id(name.lower()) for name in CAST}
    # The extra entry every power-levels event in this module carries, so that
    # two events with the same corpus content are still told apart.  It sends
    # nothing and is never a member.
    witness = fake.user_id("witness")

    r = c.req("POST", f"{CS}/createRoom", token=creator["token"],
              json_body={"room_version": room_version, "preset": "public_chat"},
              check=False, label="createRoom")
    ok(c, r, "createRoom")
    room = (r.json or {})["room_id"]

    # The power levels the corpus starts from: ALICE at 100, everyone else at
    # the default, and no per-event overrides, so a power-levels or a topic
    # event needs `state_default`, which is 50.  The room's own creator stays at
    # 100 (see `PL`).  This replaces the preset's power levels, which put
    # `m.room.power_levels` itself at 100 and would stop BOB sending one.
    levels = {"users": {creator["user_id"]: 100, users["ALICE"]: 100},
              "users_default": 0, "events": {}, "events_default": 0,
              "state_default": 50, "ban": 50, "kick": 50, "redact": 50,
              "invite": 0}
    r = c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.power_levels/",
              token=creator["token"], json_body=levels, check=False,
              label="set power levels")
    ok(c, r, "set power levels")

    for name in joins:
        fake.send_join(c.extra["fed_url"], c.extra["fed_name"], room,
                       users[name], displayname=f"{name.lower()} D")
    return fake, creator, room, users, witness


def register(c, localpart):
    body = {"username": localpart, "password": f"pw-{localpart}-123"}
    r = c.req("POST", f"{CS}/register", json_body=body, check=False,
              label=f"register {localpart}")
    if r.status == 401 and r.json and "session" in r.json:
        body["auth"] = {"type": "m.login.dummy", "session": r.json["session"]}
        r = c.req("POST", f"{CS}/register", json_body=body, check=False,
                  label=f"register {localpart}")
    ok(c, r, f"register {localpart}")
    j = r.json or {}
    user = {"user_id": j.get("user_id", ""), "token": j.get("access_token", "")}
    c.req("PUT", f"{CS}/profile/{q(user['user_id'])}/displayname",
          token=user["token"], json_body={"displayname": f"{localpart} D"},
          check=False)
    return user


def teardown(c):
    fake = c.extra.pop("fake", None)
    if fake is not None:
        fake.stop()


# --------------------------------------------------------------------------- #
# One corpus case
# --------------------------------------------------------------------------- #
def run_case(c, case, room_version):
    fake, creator, room, users, witness = setup(c, room_version, case.joins)
    try:
        pushed = push_case(c, fake, room, case, users, creator["user_id"],
                           witness)
        refused = {eid: info for eid, info in (pushed["results"] or {}).items()
                   if isinstance(info, dict) and info.get("error")}
        if refused:
            raise ScenarioAbort(f"the server refused part of the graph: {refused}")

        settle(c, lambda: read_bodies(c, creator["token"], room),
               lambda bodies: "SETTLED" in bodies,
               what="the marker after the forked graph")
        settle(c, lambda: read_rows(c, creator["token"], room),
               what=f"the resolved state of {case.name}")

        c.req("GET", f"{CS}/rooms/{q(room)}/state", token=creator["token"],
              derive=state_rows, label=f"resolved state of {case.name}")
        for etype, state_key in case.watch:
            key = users.get(state_key, state_key)
            c.req("GET", f"{CS}/rooms/{q(room)}/state/{etype}/{q(key)}",
                  token=creator["token"],
                  derive=lambda body, t=etype: state_value(t, body),
                  label=f"resolved {etype} {state_key} of {case.name}")
    finally:
        teardown(c)


def _register_case(case, room_version, suffix=""):
    sid = f"sr/{case.name}{suffix}"

    @scenario(sid, tags=["federation", "state-resolution", f"v{room_version}",
                         *case.tags], step=2, needs=("tls",))
    def run(c, _case=case, _version=room_version):
        run_case(c, _case, _version)

    run.__name__ = "sr_" + case.name.replace("-", "_") + suffix.replace("-", "_")
    return run


for _case in (BAN_VS_PL, JOIN_RULE_EVASION, OFFTOPIC_PL, TOPIC_BASIC,
              TOPIC_RESET, TOPIC_CASE, MAINLINE_SORT, LEXICOGRAPHICAL):
    _register_case(_case, V9)

# Two of the cases are run again in room version 6, the last version before
# knocking (v7) and the restricted join rule (v8/v9), so that both sets of
# authorisation rules the corpus can meet are exercised.  The two versions also
# redact `m.room.member` differently -- v9 keeps `join_authorised_via_users_server`
# and v6 does not -- and an event id is a hash of the redacted event, so the whole
# room is identified differently while the resolved state must come out the same.
_register_case(BAN_VS_PL, V6, "-v6")
_register_case(TOPIC_BASIC, V6, "-v6")

# And once at room version 1, which is the only version the release resolves
# with the *first* state-resolution algorithm -- a different algorithm reached
# through a different event format, where an event carries its own id and names
# its parents as `[event_id, {"sha256": ...}]` pairs.  Version 1's algorithm has
# no auth difference and no mainline: it resolves conflicts type by type.  The
# two cases chosen are the ones whose answer the two algorithms agree on, so
# what is scored is that the room reaches the right state at all -- an
# implementation that only ever runs the second algorithm gets the event format
# wrong long before it gets to the resolution.
_register_case(TOPIC_BASIC, V1, "-v1")
_register_case(BAN_VS_PL, V1, "-v1")


# --------------------------------------------------------------------------- #
# What the same machinery has to refuse.
#
# These three events cannot be produced by a homeserver that is working
# correctly, which is why they are worth a scenario: a signature that does not
# verify, a content hash that does not match the content, and an event whose
# sender was never in the room.  Each is pushed together with an ordinary event
# from a joined member, so that the scenario can tell "the bad event was refused"
# from "nothing arrived at all".
# --------------------------------------------------------------------------- #
def _forged_setup(c, room_version=V9):
    fake, creator, room, users, witness = setup(c, room_version,
                                                ("ALICE", "ZARA"))
    return fake, creator, room, users


def _control(fake, room, sender, prev, auth_state, ts):
    eid, event = fake.build_event(
        room, etype="m.room.message", sender=sender,
        content={"msgtype": "m.text", "body": "CONTROL"},
        prev_events=prev,
        auth_events=fake.auth_events_for(room, "m.room.message", sender, None,
                                         {}, auth_state),
        origin_server_ts=ts)
    return eid, event


def _read_forged(c, creator, room, case_name):
    """Poll until the control event is in, then record what is visible."""
    settle(c, lambda: read_bodies(c, creator["token"], room),
           lambda bodies: "CONTROL" in bodies,
           what=f"the control event of {case_name}")
    settle(c, lambda: read_rows(c, creator["token"], room),
           what=f"the state of {case_name}")

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=creator["token"],
          query={"dir": "b", "limit": 40, "filter": MSG_FILTER},
          derive=message_rows, label=f"timeline after {case_name}")
    c.req("GET", f"{CS}/rooms/{q(room)}/state", token=creator["token"],
          derive=state_rows, label=f"state after {case_name}")
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.topic/",
          token=creator["token"], label=f"topic after {case_name}")


@scenario("sr/reject-bad-signature",
          tags=["federation", "state-resolution", "signatures", "auth"],
          step=2, needs=("tls",))
def sr_reject_bad_signature(c):
    """A `m.room.topic` whose signature does not verify must not become state.

    The event is built correctly and then one byte of its signature is flipped.
    Its event id is the reference hash of the *redacted* event, from which
    `signatures` is removed, so the tampering does not change the id: this is
    exactly an event that claims to be from the fake server and is not.
    """
    fake, creator, room, users = _forged_setup(c)
    try:
        state = dict(fake.rooms[room].state)
        tip = list(fake.rooms[room].tips)
        base_ts = int(time.time() * 1000)
        alice = users["ALICE"]
        eid, event = fake.build_event(
            room, etype="m.room.topic", sender=alice, state_key="",
            content={"topic": "FORGED-SIGNATURE"}, prev_events=tip,
            auth_events=fake.auth_events_for(room, "m.room.topic", alice, "",
                                             {}, state),
            origin_server_ts=base_ts)
        sigs = event["signatures"][fake.server_name]
        raw = bytearray(fakefed.decode_b64(sigs[fake.KEY_ID]))
        raw[0] ^= 0xFF
        sigs[fake.KEY_ID] = encode_b64(bytes(raw))

        _, control = _control(fake, room, users["ZARA"], tip, state, base_ts + 1)
        fake.send_transaction(c.extra["fed_url"], c.extra["fed_name"],
                              [event, control])
        _read_forged(c, creator, room, "a bad signature")
    finally:
        teardown(c)


@scenario("sr/reject-bad-content-hash",
          tags=["federation", "state-resolution", "hashes"],
          step=2, needs=("tls",))
def sr_reject_bad_content_hash(c):
    """An event whose content hash does not cover its content loses the content.

    The content hash is computed over one content and the event then carries
    another; the signature still verifies, because it is taken over the
    *redacted* event, which for a message keeps no content at all -- only the
    `hashes` object, which is what ties the redacted copy to the full one.  The
    specification's answer to a hash that does not match is not to discard the
    event but to use the redacted copy, so what a client must not see is the
    content that was substituted.
    """
    fake, creator, room, users = _forged_setup(c)
    try:
        state = dict(fake.rooms[room].state)
        tip = list(fake.rooms[room].tips)
        base_ts = int(time.time() * 1000)
        alice = users["ALICE"]
        eid, event = fake.build_event(
            room, etype="m.room.message", sender=alice,
            content={"msgtype": "m.text", "body": "HASHED-CONTENT"},
            prev_events=tip,
            auth_events=fake.auth_events_for(room, "m.room.message", alice,
                                             None, {}, state),
            origin_server_ts=base_ts, sign=False)
        event["hashes"] = {"sha256": encode_b64(content_hash(event))}
        event["content"] = {"msgtype": "m.text", "body": "SUBSTITUTED-CONTENT"}
        # The signature is taken over the redacted event, which carries the
        # (now stale) `hashes` but no content, so it still verifies.
        version = fake.rooms[room].version
        stripped = fakefed.redact(event, version)
        stripped.pop("signatures", None)
        event["signatures"] = fakefed.sign_json(
            stripped, fake.server_name, fake.KEY_ID, fake.signing_key
        )["signatures"]
        fake.events[event_id(event, version)] = event

        _, control = _control(fake, room, users["ZARA"], tip, state, base_ts + 1)
        fake.send_transaction(c.extra["fed_url"], c.extra["fed_name"],
                              [event, control])
        _read_forged(c, creator, room, "a bad content hash")
    finally:
        teardown(c)


@scenario("sr/reject-unauthorised-sender",
          tags=["federation", "state-resolution", "auth"],
          step=2, needs=("tls",))
def sr_reject_unauthorised_sender(c):
    """A correctly signed event from a user who never joined must be refused.

    Nothing about the event is malformed: the fake server really does control
    `@stranger`, and the signature and both hashes are right.  It fails the
    authorisation rules, and only those, because the sender has no membership in
    the room.
    """
    fake, creator, room, users = _forged_setup(c)
    try:
        state = dict(fake.rooms[room].state)
        tip = list(fake.rooms[room].tips)
        base_ts = int(time.time() * 1000)
        stranger = fake.user_id("stranger")
        eid, event = fake.build_event(
            room, etype="m.room.topic", sender=stranger, state_key="",
            content={"topic": "FORGED-UNAUTHORISED"}, prev_events=tip,
            auth_events=fake.auth_events_for(room, "m.room.topic", stranger, "",
                                             {}, state),
            origin_server_ts=base_ts)

        _, control = _control(fake, room, users["ZARA"], tip, state, base_ts + 1)
        fake.send_transaction(c.extra["fed_url"], c.extra["fed_name"],
                              [event, control])
        _read_forged(c, creator, room, "an unauthorised sender")
    finally:
        teardown(c)
