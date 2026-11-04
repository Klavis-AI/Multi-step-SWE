"""Step 2 -- the Server-Server API surface a client cannot reach.

`step02_federation.py` drives two real homeservers at each other and scores what
the Client-Server API shows afterwards, which is the right instrument for
membership, history and EDUs: those have a client-visible outcome the release
fixes.  It leaves a large part of release v1.2 unmeasured, because a client
cannot ask a server to serve `/_matrix/federation/v1/state_ids`, cannot replay a
transaction, and cannot send a request with a broken signature.

This module reaches those directly.  `mxconf/fakefed.py` is a homeserver
identity -- an Ed25519 key, an HTTPS listener and the wire formats for room
versions 1 to 10 -- which joins a room on the server under test and then asks it
the questions the specification says a federating server may ask.

What is scored is never a raw federation response: those carry server-chosen
detail (which PDUs are batched together, the order of an auth chain, event ids
that are hashes over events this run created) that no two implementations need
to agree on, and that would differ between the recording and the scoring run
anyway.  Every check is a *derived fact* about the answer -- whether the state
this server reports is the state that was actually sent to it, whether the auth
chain of an event contains the create event, whether replaying a transaction
changes anything -- recorded through `fact()`.  Those facts are what the release
determines.
"""

from __future__ import annotations

import time
import urllib.parse

from mxconf.fakefed import (FakeFederation, FederationError, decode_b64,
                            federation_url, pick_port, port_hint, sign_json)
from mxconf.mx import CS, fact, ok, q
from mxconf.registry import scenario

FEDERATION = "/_matrix/federation"
V = "9"


# --------------------------------------------------------------------------- #
# a room on the server under test that the fake server is a member of
# --------------------------------------------------------------------------- #
def register(c, localpart):
    body = {"username": localpart, "password": f"pw-{localpart}-123"}
    r = c.req("POST", f"{CS}/register", json_body=body, check=False,
              label=f"register {localpart}")
    if r.status == 401 and r.json and "session" in r.json:
        body["auth"] = {"type": "m.login.dummy", "session": r.json["session"]}
        r = c.req("POST", f"{CS}/register", json_body=body, check=False,
                  label=f"register {localpart}")
    ok(c, r, f"register {localpart}")
    return r.json


def joined_room(c, *, room_version=V, join=True):
    """A public room, its creator, and a fake server that has joined it."""
    server = c.extra["server"]
    fake = FakeFederation(port=pick_port(port_hint(server) + 6))
    c.extra["fake"] = fake
    fake.start()
    c.syms.aliases[fake.server_name] = "<FAKE>"
    url, name = federation_url(server), server.server_name
    c.extra["fed_url"], c.extra["fed_name"] = url, name

    creator = register(c, "ss1")
    r = c.req("POST", f"{CS}/createRoom", token=creator["access_token"],
              json_body={"room_version": room_version, "preset": "public_chat"},
              check=False, label="createRoom")
    ok(c, r, "createRoom")
    room = (r.json or {})["room_id"]
    remote = fake.user_id("remote")
    if join:
        fake.send_join(url, name, room, remote, displayname="remote D")
    return fake, creator, room, remote


def settle(seconds=1.5):
    """Give the server under test a moment to finish with what it was sent."""
    time.sleep(seconds)


# --------------------------------------------------------------------------- #
# transactions
# --------------------------------------------------------------------------- #
@scenario("ss/transaction-replay", tags=["federation", "transactions"], step=2, needs=("tls",))
def transaction_replay(c):
    """"The sending server should retry the transaction ... with the same
    `txnId`", and a receiving server must not act on it twice."""
    fake, creator, room, remote = joined_room(c)
    token = creator["access_token"]

    _eid, event = fake.build_event(room, etype="m.room.message", sender=remote,
                                   content={"msgtype": "m.text", "body": "once only"})
    txn = "ss-replay-1"
    first = fake.send_transaction_raw(c.extra["fed_url"], c.extra["fed_name"],
                                      [event], txn_id=txn)
    settle()
    second = fake.send_transaction_raw(c.extra["fed_url"], c.extra["fed_name"],
                                       [event], txn_id=txn)
    settle()

    def results(answer):
        status, body = answer
        pdus = (body or {}).get("pdus") or {}
        return status, {k: ("error" in (v or {})) for k, v in pdus.items()}

    s1, e1 = results(first)
    s2, e2 = results(second)
    fact(c, {"both_accepted": 200 <= s1 < 300 and 200 <= s2 < 300,
             "first_reported_no_error": e1 and not any(e1.values()),
             "replay_reported_the_same": e1 == e2},
         label="the same transaction sent twice is answered the same way both times",
         token=token)

    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=token,
          query={"dir": "b", "limit": 50},
          label="the replayed event is in the room exactly once",
          derive=lambda b: {"copies": sum(
              1 for e in b.get("chunk", [])
              if (e.get("content") or {}).get("body") == "once only")})


@scenario("ss/transaction-auth", tags=["federation", "auth"], step=2, needs=("tls",))
def transaction_auth(c):
    """Request authentication: "Servers MUST strictly enforce the JSON format".

    An unsigned request, one signed by a key the origin does not publish, and
    one whose signature is over different content are all refused; the same
    request correctly signed is accepted.
    """
    fake, creator, room, remote = joined_room(c)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]
    uri = f"{FEDERATION}/v1/send/ss-auth-1"

    _eid, event = fake.build_event(room, etype="m.room.message", sender=remote,
                                   content={"msgtype": "m.text", "body": "signed well"})
    content = {"origin": fake.server_name,
               "origin_server_ts": int(time.time() * 1000),
               "pdus": [event], "edus": []}

    def put(headers):
        import http.client
        import json as _json
        import ssl
        parsed = urllib.parse.urlsplit(url)
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        conn = http.client.HTTPSConnection(parsed.hostname, parsed.port,
                                           timeout=30, context=ctx)
        try:
            conn.request("PUT", uri, body=_json.dumps(content).encode(),
                         headers={"Content-Type": "application/json", **headers})
            resp = conn.getresponse()
            resp.read()
            return resp.status
        except Exception:                                        # noqa: BLE001
            return 0
        finally:
            conn.close()

    no_auth = put({})
    garbage = put({"Authorization": 'X-Matrix origin="%s",destination="%s",'
                                    'key="ed25519:fake",sig="AAAA"'
                                    % (fake.server_name, dest)})
    # A well-formed signature, but over a different URI: the release signs the
    # method, the URI, the origin, the destination and the content together.
    wrong_uri = put({"Authorization": fake.x_matrix_auth(
        "PUT", f"{FEDERATION}/v1/send/some-other-txn", dest, content)})

    fact(c, {# "the request MUST be refused": which 4xx a server picks
             # for a missing or bad `Authorization` is not fixed by the
             # release, so the class of the answer is what is compared.
             "unsigned_refused": 400 <= no_auth < 500,
             "bad_signature_refused": 400 <= garbage < 500,
             "signature_over_another_uri_refused": 400 <= wrong_uri < 500},
         label="an unsigned, a badly signed and a mis-signed transaction are all refused",
         token=token)

    status, _ = fake.send_transaction_raw(url, dest, [event], txn_id="ss-auth-2")
    settle()
    fact(c, {"correctly_signed_accepted": 200 <= status < 300},
         label="the same transaction, correctly signed, is accepted", token=token)
    c.req("GET", f"{CS}/rooms/{q(room)}/messages", token=token,
          query={"dir": "b", "limit": 50},
          label="only the correctly signed copy is in the room",
          derive=lambda b: {"copies": sum(
              1 for e in b.get("chunk", [])
              if (e.get("content") or {}).get("body") == "signed well")})


# --------------------------------------------------------------------------- #
# serving the room back
# --------------------------------------------------------------------------- #
@scenario("ss/state-and-state-ids", tags=["federation", "state"], step=2, needs=("tls",))
def state_and_state_ids(c):
    """`/state/{roomId}` and `/state_ids/{roomId}` describe the same state."""
    fake, creator, room, remote = joined_room(c)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]

    r = c.req("PUT", f"{CS}/rooms/{q(room)}/state/m.room.topic/", token=token,
              json_body={"topic": "state over federation"}, check=False,
              label="a topic to find in the state")
    ok(c, r, "set topic")
    # The state *at* an event is the state before it, so the anchor has to be a
    # later event for the topic to be in what comes back.
    r = c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/ss-anchor",
              token=token, json_body={"msgtype": "m.text", "body": "anchor"},
              check=False, label="a message to anchor the state query at")
    ok(c, r, "anchor")
    anchor = (r.json or {})["event_id"]
    settle()

    esc = urllib.parse.quote(anchor, safe="")
    sid_status, sids = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/state_ids/{urllib.parse.quote(room, safe='')}"
        f"?event_id={esc}")
    st_status, st = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/state/{urllib.parse.quote(room, safe='')}"
        f"?event_id={esc}")

    ids = set((sids or {}).get("pdu_ids") or [])
    pdus = (st or {}).get("pdus") or []
    types = {(p.get("type"), p.get("state_key")) for p in pdus}
    fact(c, {"state_ids_ok": sid_status == 200,
             "state_ok": st_status == 200,
             "both_describe_the_same_state":
                 bool(ids) and len(pdus) == len(ids),
             "creation_is_in_it": ("m.room.create", "") in types,
             "power_levels_is_in_it": ("m.room.power_levels", "") in types,
             "the_topic_is_in_it": ("m.room.topic", "") in types,
             "the_remote_join_is_in_it": ("m.room.member", remote) in types,
             "the_anchor_itself_is_not_state": anchor not in ids},
         label="state_ids and state agree, and carry the room's state at that event",
         token=token)

    auth_ids = set((sids or {}).get("auth_chain_ids") or [])
    chain = {(p.get("type"), p.get("state_key"))
             for p in ((st or {}).get("auth_chain") or [])}
    fact(c, {"auth_chain_ids_present": bool(auth_ids),
             "auth_chain_has_the_creation": ("m.room.create", "") in chain,
             "auth_chain_has_the_power_levels":
                 ("m.room.power_levels", "") in chain},
         label="the auth chain that comes with it reaches back to the creation",
         token=token)

    bad_status, _ = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/state_ids/{urllib.parse.quote(room, safe='')}"
        "?event_id=%24nosuchevent%3Aexample.invalid")
    fact(c, {"unknown_event_refused": bad_status in (400, 404)},
         label="state_ids for an event this server does not have is refused",
         token=token)


@scenario("ss/event-and-event-auth", tags=["federation", "events"], step=2, needs=("tls",))
def event_and_event_auth(c):
    """`/event/{eventId}` serves one PDU; `/event_auth` serves its auth chain."""
    fake, creator, room, remote = joined_room(c)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]

    r = c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/ss-ev-1",
              token=token, json_body={"msgtype": "m.text", "body": "fetch me"},
              check=False, label="an event to fetch over federation")
    ok(c, r, "send")
    eid = (r.json or {})["event_id"]
    esc = urllib.parse.quote(eid, safe="")
    settle()

    ev_status, ev = fake.request(url, dest, "GET", f"{FEDERATION}/v1/event/{esc}")
    pdus = (ev or {}).get("pdus") or []
    got = pdus[0] if pdus else {}
    fact(c, {"served": ev_status == 200,
             "one_pdu": len(pdus) == 1,
             "is_the_event": (got.get("type") == "m.room.message"
                              and (got.get("content") or {}).get("body") == "fetch me"),
             "carries_its_room": got.get("room_id") == room,
             "carries_hashes_and_signatures":
                 bool(got.get("hashes")) and bool(got.get("signatures")),
             "transaction_names_its_origin": bool((ev or {}).get("origin")
                                                  or (ev or {}).get("origin_server_ts"))},
         label="the event is served as a PDU inside a transaction", token=token)

    auth_status, auth = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/event_auth/{urllib.parse.quote(room, safe='')}/{esc}")
    chain = {(p.get("type"), p.get("state_key"))
             for p in ((auth or {}).get("auth_chain") or [])}
    fact(c, {"served": auth_status == 200,
             "has_the_creation": ("m.room.create", "") in chain,
             "has_the_power_levels": ("m.room.power_levels", "") in chain,
             "has_the_senders_membership":
                 ("m.room.member", creator["user_id"]) in chain,
             "does_not_contain_the_event_itself":
                 eid not in {p.get("event_id") for p in
                             ((auth or {}).get("auth_chain") or [])}},
         label="its auth chain is the events that authorise it", token=token)

    missing, _ = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/event/%24nosuchevent%3Aexample.invalid")
    fact(c, {"unknown_event_refused": missing in (400, 404)},
         label="an event this server does not have is refused", token=token)


@scenario("ss/backfill", tags=["federation", "history"], step=2, needs=("tls",))
def backfill(c):
    """`/backfill/{roomId}` walks back from the events it is given."""
    fake, creator, room, remote = joined_room(c)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]

    last = None
    for i in range(6):
        r = c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/ss-bf-{i}",
                  token=token, json_body={"msgtype": "m.text", "body": f"back {i}"},
                  check=False, label=f"message {i}")
        ok(c, r, "send")
        last = (r.json or {})["event_id"]
    settle()

    esc = urllib.parse.quote(last, safe="")
    status, body = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/backfill/{urllib.parse.quote(room, safe='')}"
        f"?v={esc}&limit=4")
    pdus = (body or {}).get("pdus") or []
    bodies = [(p.get("content") or {}).get("body") for p in pdus
              if p.get("type") == "m.room.message"]
    fact(c, {"served": status == 200,
             "respected_the_limit": len(pdus) <= 4,
             "walked_backwards": bodies == sorted(bodies, reverse=True)
                                 or bodies == ["back 5", "back 4", "back 3"],
             "started_at_the_event_asked_for": "back 5" in bodies,
             "did_not_reach_the_oldest": "back 0" not in bodies},
         label="backfill returns the events before the one it was given",
         token=token)

    status2, body2 = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/backfill/{urllib.parse.quote(room, safe='')}"
        f"?v={esc}&limit=100")
    all_bodies = {(p.get("content") or {}).get("body")
                  for p in ((body2 or {}).get("pdus") or [])}
    fact(c, {"served": status2 == 200,
             "a_generous_limit_reaches_the_first_message":
                 "back 0" in all_bodies,
             "and_every_message_between": all(f"back {i}" in all_bodies
                                              for i in range(6))},
         label="a generous limit reaches the start of the room", token=token)


@scenario("ss/get-missing-events", tags=["federation", "history"], step=2, needs=("tls",))
def get_missing_events(c):
    """`/get_missing_events/{roomId}`: the gap between what a server has and
    what it has just been told about."""
    fake, creator, room, remote = joined_room(c)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]

    ids = []
    for i in range(5):
        r = c.req("PUT", f"{CS}/rooms/{q(room)}/send/m.room.message/ss-gm-{i}",
                  token=token, json_body={"msgtype": "m.text", "body": f"gap {i}"},
                  check=False, label=f"message {i}")
        ok(c, r, "send")
        ids.append((r.json or {})["event_id"])
    settle()

    status, body = fake.request(
        url, dest, "POST",
        f"{FEDERATION}/v1/get_missing_events/{urllib.parse.quote(room, safe='')}",
        {"earliest_events": [ids[0]], "latest_events": [ids[4]],
         "limit": 10, "min_depth": 0})
    events = (body or {}).get("events") or []
    bodies = {(e.get("content") or {}).get("body") for e in events}
    fact(c, {"served": status == 200,
             "returns_the_events_between": {"gap 1", "gap 2", "gap 3"} <= bodies,
             "excludes_the_earliest_it_was_given": "gap 0" not in bodies},
         label="the events between the earliest and the latest are returned",
         token=token)

    status2, body2 = fake.request(
        url, dest, "POST",
        f"{FEDERATION}/v1/get_missing_events/{urllib.parse.quote(room, safe='')}",
        {"earliest_events": [ids[0]], "latest_events": [ids[4]],
         "limit": 1, "min_depth": 0})
    fact(c, {"served": status2 == 200,
             "respected_the_limit": len((body2 or {}).get("events") or []) <= 1},
         label="the limit bounds how many come back", token=token)


# --------------------------------------------------------------------------- #
# keys
# --------------------------------------------------------------------------- #
def _verify_self_signature(doc, name):
    """Does this key document verify under a key it publishes?"""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PublicKey)
        sigs = (doc.get("signatures") or {}).get(name) or {}
        keys = doc.get("verify_keys") or {}
        unsigned = {k: v for k, v in doc.items()
                    if k not in ("signatures", "unsigned")}
        from mxconf.fakefed import canonical_json
        payload = canonical_json(unsigned)
        for key_id, sig in sigs.items():
            entry = keys.get(key_id) or {}
            raw = entry.get("key")
            if not raw:
                continue
            pub = Ed25519PublicKey.from_public_bytes(decode_b64(raw))
            pub.verify(decode_b64(sig), payload)
            return True
    except Exception:                                            # noqa: BLE001
        return False
    return False


@scenario("ss/key-server-document", tags=["federation", "keys"], step=2, needs=("tls",))
def key_server_document(c):
    """`GET /_matrix/key/v2/server`: what this server publishes about itself."""
    fake, creator, room, remote = joined_room(c, join=False)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]

    status, doc = fake.request(url, dest, "GET", "/_matrix/key/v2/server")
    keys = (doc or {}).get("verify_keys") or {}
    now = int(time.time() * 1000)
    fact(c, {"served": status == 200,
             "names_itself": (doc or {}).get("server_name") == dest,
             "publishes_at_least_one_key": bool(keys),
             "every_key_id_is_an_algorithm_and_an_identifier":
                 all(":" in k and k.split(":")[0] == "ed25519" for k in keys),
             "every_key_is_unpadded_base64":
                 all(isinstance((v or {}).get("key"), str) and "=" not in v["key"]
                     for v in keys.values()),
             "valid_until_is_in_the_future":
                 isinstance((doc or {}).get("valid_until_ts"), int)
                 and doc["valid_until_ts"] > now,
             "signs_itself_with_a_key_it_publishes":
                 _verify_self_signature(doc, dest)},
         label="the server's own key document is well formed and self-signed",
         token=token)

    # The key document is public: the specification's own note is that this
    # endpoint is not authenticated.
    import http.client
    import json as _json
    import ssl
    parsed = urllib.parse.urlsplit(url)
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    conn = http.client.HTTPSConnection(parsed.hostname, parsed.port, timeout=30,
                                       context=ctx)
    conn.request("GET", "/_matrix/key/v2/server")
    resp = conn.getresponse()
    raw = resp.read()
    conn.close()
    try:
        unauth = _json.loads(raw.decode())
    except Exception:                                            # noqa: BLE001
        unauth = None
    fact(c, {"served_without_authentication": resp.status == 200,
             "the_same_document": (unauth or {}).get("server_name") == dest},
         label="the key document needs no X-Matrix authentication", token=token)


@scenario("ss/key-notary-query", tags=["federation", "keys"], step=2, needs=("tls",))
def key_notary_query(c):
    """The notary forms of `/_matrix/key/v2/query`.

    A server asked about *itself* answers from its own key document, which is
    the one case every homeserver can serve without talking to anyone else.
    """
    fake, creator, room, remote = joined_room(c, join=False)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]

    post_status, post_body = fake.request(
        url, dest, "POST", "/_matrix/key/v2/query",
        {"server_keys": {dest: {}}})
    get_status, get_body = fake.request(
        url, dest, "GET",
        f"/_matrix/key/v2/query/{urllib.parse.quote(dest, safe='')}")

    def names(body):
        return {d.get("server_name") for d in ((body or {}).get("server_keys") or [])}

    def has_keys(body):
        return all((d.get("verify_keys") or {})
                   for d in ((body or {}).get("server_keys") or []))

    # Whether a notary vouches for a server at all -- including for itself --
    # is its own policy: the reference returns an empty list rather than
    # answering for a key it did not fetch, and a server that answers with its
    # own key document is doing nothing the release forbids.  So the *number*
    # of documents is not compared; what is compared is that the endpoint
    # answers in the documented shape in both its forms, and that whatever it
    # does return is about a server that was asked about and carries verify
    # keys.  (Those last two are weak when the list is empty.  They are as
    # strong as the release allows: `ss/key-server-document` is where a key
    # document's own shape is pinned, and that one a server must produce.)
    fact(c, {"post_served": post_status == 200,
             "post_returns_a_list": isinstance((post_body or {}).get("server_keys"), list),
             "post_invents_nothing": names(post_body) <= {dest},
             "post_documents_carry_verify_keys": has_keys(post_body),
             "get_served": get_status == 200,
             "get_returns_a_list": isinstance((get_body or {}).get("server_keys"), list),
             "get_invents_nothing": names(get_body) <= {dest},
             "get_documents_carry_verify_keys": has_keys(get_body)},
         label="the notary answers in the documented shape and vouches for nobody else",
         token=token)

    unknown_status, unknown_body = fake.request(
        url, dest, "POST", "/_matrix/key/v2/query",
        {"server_keys": {"unreachable.invalid": {}}})
    fact(c, {"a_server_it_cannot_reach_is_not_invented":
             unknown_status in (200, 502, 504)
             and "unreachable.invalid" not in names(unknown_body)},
         label="a server it cannot reach produces no key document", token=token)


@scenario("ss/query-user-devices", tags=["federation", "queries"], step=2, needs=("tls",))
def query_user_devices(c):
    """`GET /_matrix/federation/v1/user/devices/{userId}`."""
    fake, creator, room, remote = joined_room(c, join=False)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]
    uid = creator["user_id"]

    status, body = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/user/devices/{urllib.parse.quote(uid, safe='')}")
    devices = (body or {}).get("devices")
    fact(c, {"served": status == 200,
             "names_the_user": (body or {}).get("user_id") == uid,
             "carries_a_stream_id": isinstance((body or {}).get("stream_id"), int),
             "carries_a_device_list": isinstance(devices, list),
             # Which devices appear is the server's own business -- the
             # reference lists only those with end-to-end keys, which is a
             # later step's subject -- so only their shape is pinned.
             "every_device_named_has_an_identifier":
                 isinstance(devices, list)
                 and all(isinstance(d, dict) and d.get("device_id") for d in devices)},
         label="the devices of a local user are served to another server",
         token=token)

    status2, body2 = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/user/devices/"
        + urllib.parse.quote(f"@nobody-at-all:{dest}", safe=""))
    fact(c, {"an_unknown_local_user_is_answered_a_sanctioned_way":
             status2 in (200, 404)
             and (status2 == 404
                  or not ((body2 or {}).get("devices") or []))},
         label="an unknown local user has no devices to serve", token=token)


@scenario("ss/query-profile-and-directory", tags=["federation", "queries"], step=2, needs=("tls",))
def query_profile_and_directory(c):
    """`/query/profile` and `/query/directory` answered for another server."""
    fake, creator, room, remote = joined_room(c, join=False)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]
    uid = creator["user_id"]

    r = c.req("PUT", f"{CS}/profile/{q(uid)}/displayname", token=token,
              json_body={"displayname": "Queried Over Federation"}, check=False,
              label="a display name to query")
    ok(c, r, "displayname")

    status, body = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/query/profile?user_id={urllib.parse.quote(uid, safe='')}")
    fact(c, {"served": status == 200,
             "carries_the_display_name":
                 (body or {}).get("displayname") == "Queried Over Federation"},
         label="a local user's profile is served to another server", token=token)

    status_f, body_f = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/query/profile?user_id={urllib.parse.quote(uid, safe='')}"
        "&field=displayname")
    fact(c, {"served": status_f == 200,
             "carries_the_display_name":
                 (body_f or {}).get("displayname") == "Queried Over Federation",
             "did_not_volunteer_the_avatar": "avatar_url" not in (body_f or {})},
         label="asking for one field answers with that field", token=token)

    r = c.req("POST", f"{CS}/createRoom", token=token,
              json_body={"room_version": V, "preset": "public_chat",
                         "room_alias_name": "ss-directory"},
              check=False, label="a room with an alias to resolve")
    ok(c, r, "createRoom")
    room_id = (r.json or {})["room_id"]
    alias = f"#ss-directory:{dest}"

    status_d, body_d = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/query/directory?room_alias="
        + urllib.parse.quote(alias, safe=""))
    fact(c, {"served": status_d == 200,
             "resolves_to_the_room": (body_d or {}).get("room_id") == room_id,
             "names_a_resident_server":
                 dest in ((body_d or {}).get("servers") or [])},
         label="a local alias is resolved for another server", token=token)

    status_u, _ = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/query/directory?room_alias="
        + urllib.parse.quote(f"#no-such-alias-here:{dest}", safe=""))
    fact(c, {"unknown_alias_refused": status_u in (400, 404)},
         label="an alias this server does not hold is refused", token=token)


# --------------------------------------------------------------------------- #
# joining, at the wire level
# --------------------------------------------------------------------------- #
@scenario("ss/make-join-template", tags=["federation", "membership"], step=2, needs=("tls",))
def make_join_template(c):
    """`make_join` hands back a template the joining server completes."""
    fake, creator, room, remote = joined_room(c, join=False)
    token = creator["access_token"]
    url, dest = c.extra["fed_url"], c.extra["fed_name"]
    user = fake.user_id("templated")

    version, template = fake.make_join(url, dest, room, user)
    content = template.get("content") or {}
    fact(c, {"names_the_room_version": version == V,
             "is_a_membership_event": template.get("type") == "m.room.member",
             "for_the_user_asked_about": template.get("state_key") == user,
             "sent_by_that_user": template.get("sender") == user,
             "asks_for_a_join": content.get("membership") == "join",
             "in_the_room_asked_about": template.get("room_id") == room,
             "carries_no_signature_yet": "signatures" not in template},
         label="the make_join template is this user's join, unsigned", token=token)

    unsupported_status, _ = fake.request(
        url, dest, "GET",
        f"{FEDERATION}/v1/make_join/{urllib.parse.quote(room, safe='')}/"
        f"{urllib.parse.quote(user, safe='')}?ver=99")
    fact(c, {"a_room_version_the_caller_cannot_speak_is_refused":
             unsupported_status in (400, 404)},
         label="make_join refuses when the caller supports no version of the room",
         token=token)

    try:
        fake.send_join(url, dest, room, user, displayname="templated D")
        joined = True
    except FederationError:
        joined = False
    settle()
    fact(c, {"the_completed_join_was_accepted": joined},
         label="the completed template is accepted as a join", token=token)
    c.req("GET", f"{CS}/rooms/{q(room)}/joined_members", token=token,
          label="and the user is a member",
          derive=lambda b, u=user: {"is_member": u in (b.get("joined") or {})})


@scenario("ss/capabilities-room-versions", tags=["federation", "room-versions"],
          step=2, needs=("tls",))
def capabilities_room_versions(c):
    """`/capabilities` and `/createRoom` agree about versions 7, 8 and 9."""
    creator = register(c, "ssver")
    token = creator["access_token"]

    c.req("GET", f"{CS}/capabilities", token=token,
          label="the room versions this server offers",
          derive=lambda b: {
              "7_8_9_all_available": all(
                  v in ((b.get("capabilities") or {}).get("m.room_versions")
                        or {}).get("available", {}) for v in ("7", "8", "9")),
              "default_is_available": ((b.get("capabilities") or {})
                                       .get("m.room_versions") or {}).get("default")
              in (((b.get("capabilities") or {}).get("m.room_versions") or {})
                  .get("available") or {}),
              "every_value_is_stable_or_unstable": all(
                  v in ("stable", "unstable") for v in
                  (((b.get("capabilities") or {}).get("m.room_versions") or {})
                   .get("available") or {}).values())})

    for version in ("7", "8", "9"):
        r = c.req("POST", f"{CS}/createRoom", token=token,
                  json_body={"room_version": version, "preset": "public_chat"},
                  check=False, label=f"createRoom at version {version}")
        ok(c, r, f"createRoom {version}")
        room = (r.json or {})["room_id"]
        c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.create/", token=token,
              label=f"version {version} is what the room says it is",
              derive=lambda b, v=version: {"room_version": b.get("room_version")})


@scenario("ss/join-v1-wire", tags=["federation", "membership"], step=2,
          needs=("tls",))
def join_v1_wire(c):
    """The older `send_join` wire, which the release still specifies.

    A partner that speaks the version 2 endpoint never exercises version 1, so
    a server could publish only the newer one and never be noticed.  The
    release keeps both: version 1 answers with `[200, {...}]` -- a two-element
    array whose second element is the body version 2 returns on its own -- and
    a joining server that speaks only the older wire has to end up as much of a
    member as one that speaks the newer.
    """
    fake, creator, room, _remote = joined_room(c, join=False)
    token = creator["access_token"]
    joiner = fake.user_id("v1joiner")
    fake.send_join(c.extra["fed_url"], c.extra["fed_name"], room, joiner,
                   displayname="v1 D", wire="v1")
    settle()

    envelope = getattr(fake, "last_v1_envelope", None)
    body = envelope[1] if isinstance(envelope, list) and len(envelope) == 2 else {}
    fact(c, {
        "answer_is_a_two_element_array":
            isinstance(envelope, list) and len(envelope) == 2,
        "first_element_is_the_status":
            isinstance(envelope, list) and bool(envelope) and envelope[0] == 200,
        "body_carries_state": isinstance(body, dict)
                              and isinstance(body.get("state"), list)
                              and bool(body.get("state")),
        "body_carries_auth_chain": isinstance(body, dict)
                                   and isinstance(body.get("auth_chain"), list)
                                   and bool(body.get("auth_chain")),
        "state_holds_the_create_event": any(
            p.get("type") == "m.room.create"
            for p in (body.get("state") or []) if isinstance(p, dict)),
    }, label="the version 1 send_join envelope", token=token)

    c.req("GET", f"{CS}/rooms/{q(room)}/joined_members", token=token,
          label="a join over the older wire is a join",
          derive=lambda b, u=joiner: {"is_member": u in (b.get("joined") or {})})
    c.req("GET", f"{CS}/rooms/{q(room)}/state/m.room.member/{q(joiner)}",
          token=token, label="and its membership event is in the room state",
          derive=lambda b: {"membership": b.get("membership"),
                            "displayname": b.get("displayname")})
