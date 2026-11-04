"""Step 3 -- the end-to-end-encryption surface between two servers.

`step03_e2ee.py` drives the Client-Server half: a client uploads keys, claims
them, and reads them back.  The release also puts three of those surfaces on
the federation wire -- `/user/keys/query`, `/user/keys/claim`, and the
`m.device_list_update` and `m.signing_key_update` EDUs -- and none of them has a
client-visible outcome a single server can produce on its own.

`mxconf/fakefed.py` supplies the other server: an Ed25519 identity with an
HTTPS listener that joins a room on the server under test, answers the key
questions it is asked, and can push an EDU at it.  What is scored is a derived
fact about each answer, never the raw federation response: which devices came
back, whether a claimed key was handed out twice, whether a client on this
server can see a remote user's keys afterwards.
"""

from __future__ import annotations

import time

from mxconf.fakefed import FakeFederation, federation_url, pick_port, port_hint
from mxconf.mx import CS, User, fact, ok, q
from mxconf.registry import scenario

FEDERATION = "/_matrix/federation"
V = "9"


def register(c, localpart, device_id=None):
    body = {"username": localpart, "password": f"pw-{localpart}-123"}
    if device_id:
        body["device_id"] = device_id
    r = c.req("POST", f"{CS}/register", json_body=body, check=False,
              label=f"register {localpart}")
    if r.status == 401 and r.json and "session" in r.json:
        body["auth"] = {"type": "m.login.dummy", "session": r.json["session"]}
        r = c.req("POST", f"{CS}/register", json_body=body, check=False,
                  label=f"register {localpart}")
    ok(c, r, f"register {localpart}")
    return r.json


def key_material(tag: str) -> str:
    """A deterministic 32-byte value, base64 without padding."""
    import base64
    import hashlib
    return base64.b64encode(hashlib.sha256(tag.encode()).digest()).decode().rstrip("=")


def device_keys_for(user_id: str, device_id: str, tag: str = "a") -> dict:
    return {
        "user_id": user_id,
        "device_id": device_id,
        "algorithms": ["m.olm.v1.curve25519-aes-sha2", "m.megolm.v1.aes-sha2"],
        "keys": {
            f"curve25519:{device_id}": key_material(f"{tag}/curve/{device_id}"),
            f"ed25519:{device_id}": key_material(f"{tag}/ed/{device_id}"),
        },
        "signatures": {user_id: {
            f"ed25519:{device_id}": key_material(f"{tag}/sig/{device_id}")}},
    }


def shared_room(c, *, encrypted=True):
    """A room on the server under test that the fake server has joined."""
    server = c.extra["server"]
    fake = FakeFederation(port=pick_port(port_hint(server) + 6))
    c.extra["fake"] = fake
    fake.start()
    c.syms.aliases[fake.server_name] = "<FAKE>"
    url, name = federation_url(server), server.server_name
    c.extra["fed_url"], c.extra["fed_name"] = url, name

    creator = register(c, "fk1", device_id="FKONE")
    body = {"room_version": V, "preset": "public_chat"}
    if encrypted:
        body["initial_state"] = [{"type": "m.room.encryption", "state_key": "",
                                  "content": {"algorithm": "m.megolm.v1.aes-sha2"}}]
    r = c.req("POST", f"{CS}/createRoom", token=creator["access_token"],
              json_body=body, check=False, label="createRoom")
    ok(c, r, "createRoom")
    room = (r.json or {})["room_id"]
    remote = fake.user_id("remote")
    fake.send_join(url, name, room, remote, displayname="remote D")
    return fake, creator, room, remote


def settle(seconds=1.5):
    time.sleep(seconds)


# --------------------------------------------------------------------------- #
# answering for this server's users
# --------------------------------------------------------------------------- #
@scenario("e2ee/fed-keys-query-inbound", tags=["e2ee", "federation", "keys"],
          step=3, needs=("tls",))
def fed_keys_query_inbound(c):
    """`POST /_matrix/federation/v1/user/keys/query`, answered for a local user."""
    fake, creator, _room, _remote = shared_room(c)
    token, user = creator["access_token"], creator["user_id"]
    c.req("POST", f"{CS}/keys/upload", token=token, check=False,
          json_body={"device_keys": device_keys_for(user, "FKONE")},
          label="the local user uploads device keys")

    status, body = fake.request(c.extra["fed_url"], c.extra["fed_name"], "POST",
                                f"{FEDERATION}/v1/user/keys/query",
                                {"device_keys": {user: []}})
    devices = ((body or {}).get("device_keys") or {}).get(user) or {}
    one = devices.get("FKONE") or {}
    fact(c, {
        "status": status,
        "the_device_came_back": "FKONE" in devices,
        "it_names_its_owner": one.get("user_id") == user,
        "it_names_itself": one.get("device_id") == "FKONE",
        "its_curve_key_is_the_uploaded_one":
            (one.get("keys") or {}).get("curve25519:FKONE")
            == key_material("a/curve/FKONE"),
        "its_algorithms_came_back": sorted(one.get("algorithms") or []) == sorted(
            ["m.olm.v1.curve25519-aes-sha2", "m.megolm.v1.aes-sha2"]),
    }, label="the federation query answers for a local user", token=token)

    status2, body2 = fake.request(c.extra["fed_url"], c.extra["fed_name"], "POST",
                                  f"{FEDERATION}/v1/user/keys/query",
                                  {"device_keys": {
                                      f"@nobody:{c.extra['fed_name']}": []}})
    fact(c, {
        "status": status2,
        "a_user_with_no_keys_yields_none":
            not (((body2 or {}).get("device_keys") or {})
                 .get(f"@nobody:{c.extra['fed_name']}")),
    }, label="and says nothing about a user who has uploaded none", token=token)


@scenario("e2ee/fed-keys-claim-inbound", tags=["e2ee", "federation", "keys"],
          step=3, needs=("tls",))
def fed_keys_claim_inbound(c):
    """`/user/keys/claim` hands a one-time key out once, and only once."""
    fake, creator, _room, _remote = shared_room(c)
    token, user = creator["access_token"], creator["user_id"]
    c.req("POST", f"{CS}/keys/upload", token=token, check=False,
          json_body={
              "device_keys": device_keys_for(user, "FKONE"),
              "one_time_keys": {
                  "signed_curve25519:AAAAAA": {
                      "key": key_material("otk/a"),
                      "signatures": {user: {
                          "ed25519:FKONE": key_material("otk/a/sig")}}},
              },
          }, label="the local user uploads one one-time key")

    def claim():
        return fake.request(c.extra["fed_url"], c.extra["fed_name"], "POST",
                            f"{FEDERATION}/v1/user/keys/claim",
                            {"one_time_keys": {user: {"FKONE":
                                                      "signed_curve25519"}}})

    status, body = claim()
    got = (((body or {}).get("one_time_keys") or {}).get(user) or {}).get("FKONE") or {}
    status2, body2 = claim()
    again = (((body2 or {}).get("one_time_keys") or {}).get(user) or {}).get("FKONE") or {}
    fact(c, {
        "first_claim_status": status,
        "the_key_came_back": "signed_curve25519:AAAAAA" in got,
        "it_is_the_key_that_was_uploaded":
            (got.get("signed_curve25519:AAAAAA") or {}).get("key")
            == key_material("otk/a"),
        "second_claim_status": status2,
        "the_same_key_is_not_served_twice":
            "signed_curve25519:AAAAAA" not in again,
    }, label="a claimed one-time key is handed out once", token=token)

    c.req("GET", f"{CS}/sync", token=token, query={"timeout": 0},
          label="and the count the owner sees has gone down",
          derive=lambda b: {"signed_curve25519": int(
              (b.get("device_one_time_keys_count") or {})
              .get("signed_curve25519") or 0)})


# --------------------------------------------------------------------------- #
# asking another server
# --------------------------------------------------------------------------- #
@scenario("e2ee/fed-keys-query-outbound", tags=["e2ee", "federation", "keys"],
          step=3, needs=("tls",))
def fed_keys_query_outbound(c):
    """A client asks for a remote user's keys; the server has to go and ask."""
    fake, creator, _room, remote = shared_room(c)
    token = creator["access_token"]
    fake.device_keys[remote] = {
        "RFONE": device_keys_for(remote, "RFONE", "r"),
        "RFTWO": device_keys_for(remote, "RFTWO", "r"),
    }
    fake.one_time_keys[remote] = {
        "RFONE": {"signed_curve25519:RRRRRR": {"key": key_material("r/otk")}},
    }
    settle()

    c.req("POST", f"{CS}/keys/query", token=token,
          json_body={"device_keys": {remote: []}},
          label="a remote user's device keys come back through the server",
          derive=lambda b, u=remote: {
              "devices": sorted((b.get("device_keys") or {}).get(u) or {}),
              "curve_key_of_the_first":
                  (((b.get("device_keys") or {}).get(u) or {})
                   .get("RFONE") or {}).get("keys", {}).get("curve25519:RFONE"),
              "failures": sorted(b.get("failures") or {}),
          })
    c.req("POST", f"{CS}/keys/query", token=token,
          json_body={"device_keys": {remote: ["RFTWO"]}},
          label="and a single remote device can be asked for on its own",
          derive=lambda b, u=remote: {
              "devices": sorted((b.get("device_keys") or {}).get(u) or {})})
    c.req("POST", f"{CS}/keys/claim", token=token,
          json_body={"one_time_keys": {remote: {"RFONE": "signed_curve25519"}}},
          label="claiming a remote one-time key reaches the other server",
          derive=lambda b, u=remote: {
              "keys": sorted(((b.get("one_time_keys") or {}).get(u) or {})
                             .get("RFONE") or {}),
              "failures": sorted(b.get("failures") or {})})


# --------------------------------------------------------------------------- #
# the two EDUs
# --------------------------------------------------------------------------- #
@scenario("e2ee/fed-device-list-update-inbound",
          tags=["e2ee", "federation", "device-lists"], step=3, needs=("tls",))
def fed_device_list_update_inbound(c):
    """`m.device_list_update` puts its subject in `device_lists.changed`."""
    fake, creator, _room, remote = shared_room(c)
    token = creator["access_token"]
    fake.device_keys[remote] = {"RFONE": device_keys_for(remote, "RFONE", "r")}
    settle()
    since = (c.req("GET", f"{CS}/sync", token=token, query={"timeout": 0},
                   check=False).json or {})["next_batch"]

    fake.device_stream_id = 2
    fake.send_transaction_raw(c.extra["fed_url"], c.extra["fed_name"], [], edus=[{
        "edu_type": "m.device_list_update",
        "content": {
            "user_id": remote,
            "device_id": "RFONE",
            "stream_id": 2,
            "prev_id": [],
            "device_display_name": "the remote device",
            "keys": device_keys_for(remote, "RFONE", "r"),
        },
    }])
    settle(2.0)

    c.req("GET", f"{CS}/sync", token=token,
          query={"timeout": 1500, "since": since},
          label="the remote user is reported as changed",
          derive=lambda b: {"changed": sorted(
              (b.get("device_lists") or {}).get("changed") or [])})
    c.req("POST", f"{CS}/keys/query", token=token,
          json_body={"device_keys": {remote: []}},
          label="and the device the update carried is served",
          derive=lambda b, u=remote: {
              "devices": sorted((b.get("device_keys") or {}).get(u) or {}),
              # `device_display_name` is optional in the EDU that carried it
              # and optional again in `/keys/query`'s `unsigned`, so a server
              # that keeps it and one that does not are both conforming.  And
              # the peer offers the name *twice* -- once in the EDU, once in
              # its answer to `/user/devices/{userId}`, which a server may
              # resync from -- so which of the two it ends up serving is its
              # own business too.  What is scored is the part that is not:
              # whatever name it serves came from the peer, and was not
              # invented here.
              "any_display_name_served_came_from_the_peer":
                  (((b.get("device_keys") or {}).get(u) or {})
                   .get("RFONE") or {}).get("unsigned", {})
                  .get("device_display_name")
                  in (None, "the remote device", f"RFONE of {u}")})


@scenario("e2ee/fed-signing-key-update-inbound",
          tags=["e2ee", "federation", "cross-signing"], step=3, needs=("tls",))
def fed_signing_key_update_inbound(c):
    """`m.signing_key_update` carries a remote user's cross-signing keys."""
    fake, creator, _room, remote = shared_room(c)
    token = creator["access_token"]
    master = {"user_id": remote, "usage": ["master"],
              "keys": {f"ed25519:{key_material('r/master')}":
                       key_material("r/master")}}
    self_signing = {"user_id": remote, "usage": ["self_signing"],
                    "keys": {f"ed25519:{key_material('r/self')}":
                             key_material("r/self")}}
    fake.device_keys[remote] = {"RFONE": device_keys_for(remote, "RFONE", "r")}
    settle()

    fake.send_transaction_raw(c.extra["fed_url"], c.extra["fed_name"], [], edus=[{
        "edu_type": "m.signing_key_update",
        "content": {"user_id": remote, "master_key": master,
                    "self_signing_key": self_signing},
    }])
    settle(2.0)

    c.req("POST", f"{CS}/keys/query", token=token,
          json_body={"device_keys": {remote: []}},
          label="the remote user's cross-signing keys are served",
          derive=lambda b, u=remote: {
              "master_usage": ((b.get("master_keys") or {}).get(u) or {})
                              .get("usage"),
              "master_owner": ((b.get("master_keys") or {}).get(u) or {})
                              .get("user_id"),
              "self_signing_usage": ((b.get("self_signing_keys") or {}).get(u)
                                     or {}).get("usage"),
          })


@scenario("e2ee/fed-device-list-update-outbound",
          tags=["e2ee", "federation", "device-lists"], step=3, needs=("tls",))
def fed_device_list_update_outbound(c):
    """A local device change is told to the servers that share a room."""
    fake, creator, _room, _remote = shared_room(c)
    token, user = creator["access_token"], creator["user_id"]
    c.req("POST", f"{CS}/keys/upload", token=token, check=False,
          json_body={"device_keys": device_keys_for(user, "FKONE")},
          label="the local user uploads device keys")
    settle(2.5)

    updates = [e for e in fake.received_edus
               if e.get("edu_type") == "m.device_list_update"]
    mine = [e for e in updates
            if (e.get("content") or {}).get("user_id") == user]
    first = (mine[0].get("content") if mine else {}) or {}
    fact(c, {
        "an_update_was_sent": bool(mine),
        "it_names_the_user": first.get("user_id") == user,
        "it_names_the_device": first.get("device_id") == "FKONE",
        "it_carries_a_stream_id": isinstance(first.get("stream_id"), int),
        # `keys` is optional in the EDU's schema ("may be absent if the device
        # has no E2E keys defined"), so a server that sends the update and
        # lets the peer come and ask is conforming: what is scored is that if
        # the keys are there, they are this device's.
        "any_keys_it_carries_name_the_device":
            "keys" not in first
            or (first.get("keys") or {}).get("device_id") == "FKONE",
    }, label="the other server was told the device list moved", token=token)


@scenario("e2ee/fed-signing-key-update-outbound",
          tags=["e2ee", "federation", "cross-signing"], step=3, needs=("tls",))
def fed_signing_key_update_outbound(c):
    """A local cross-signing upload is told to the servers that share a room.

    The inbound direction is `e2ee/fed-signing-key-update-inbound`; the ticket
    asks for both, so this is the other one.  What the EDU has to carry is
    fixed by its schema -- the user, and the master and self-signing keys as
    they were uploaded -- and the *user-signing* key is deliberately not
    compared: it is private to its owner, the schema marks it optional, and a
    server that keeps it to itself is doing the right thing.
    """
    from .step03_e2ee import cross_signing_keys, first_cross_signing

    fake, creator, _room, _remote = shared_room(c)
    token, user = creator["access_token"], creator["user_id"]
    local = User(localpart=user.split(":")[0].lstrip("@"), user_id=user,
                 token=token, device_id=creator.get("device_id") or "FKONE",
                 password=f"pw-{user.split(':')[0].lstrip('@')}-123")
    c.req("POST", f"{CS}/keys/upload", token=token, check=False,
          json_body={"device_keys": device_keys_for(user, local.device_id)},
          label="the local user uploads device keys")
    ks = cross_signing_keys(local)
    first_cross_signing(c, local, ks["body"])
    settle(2.5)

    mine = [(e.get("content") or {}) for e in fake.received_edus
            if e.get("edu_type") == "m.signing_key_update"
            and (e.get("content") or {}).get("user_id") == user]

    def carried(section, usage):
        """Whether every copy of this key that was sent is the uploaded one.

        Both key sections are optional in the EDU's schema, and a server may
        send one update or several, so the question asked of each is the
        conditional one: if it went out, was it this user's key, and did it say
        what it is for?  A server that sends the master key and leaves the peer
        to ask for the rest is conforming, and so is one that sends both.
        """
        sent = [c2[section] for c2 in mine if c2.get(section)]
        return all(k.get("keys") == ks[usage]["key"]["keys"]
                   and k.get("usage") == [usage] for k in sent)

    fact(c, {
        "an_update_was_sent": bool(mine),
        "it_names_the_user": all(c2.get("user_id") == user for c2 in mine),
        "any_master_key_it_carries_is_this_users":
            carried("master_key", "master"),
        "any_self_signing_key_it_carries_is_this_users":
            carried("self_signing_key", "self_signing"),
        # The user-signing key is private to its owner and is not federated:
        # a server that sent it would be leaking, so its absence is the check.
        "the_user_signing_key_stayed_home":
            not any(c2.get("user_signing_key") for c2 in mine),
    }, label="the other server was told the cross-signing keys arrived",
       token=token)
