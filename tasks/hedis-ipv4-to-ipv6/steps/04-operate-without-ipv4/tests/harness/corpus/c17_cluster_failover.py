"""Cluster paths that only run when a node changes its mind about a peer.

`c11_cluster` records a cluster that forms, serves, redirects and restarts. Its
node table is written once and read many times. This scenario records the code
that *rewrites* it: a manual failover, in which a slave asks its master to stand
down, collects votes from the other masters over the cluster bus, takes the
slots, and broadcasts a new address for them -- after which every other node,
and the demoted master itself, has to agree.

Three smaller paths ride along, each chosen because it is a distinct piece of
code that carries an address and that nothing else in the suite reaches:

  * `PUBLISH` in cluster mode, which forwards the message over the bus rather
    than over a client connection;
  * a client blocked in `BLPOP` on a slot that moves out from under it, which is
    redirected from the cluster's timer rather than from the command;
  * `CLUSTER FORGET`, which puts a node id on a blacklist so gossip cannot
    immediately re-add the peer it just removed.

Four nodes: three masters holding the slot map, one slave of the first. Three
masters is the minimum -- a failover needs a majority of the masters that hold
slots to vote for it, and with two there is no majority that excludes the node
being replaced.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402

PORTS = 6
FAMILY = "behaviour"

RANGES = ((0, 5460), (5461, 10922), (10923, 16383))

CINFO_FIELDS = ("cluster_state", "cluster_slots_assigned", "cluster_slots_ok",
                "cluster_known_nodes", "cluster_size")


def _directives(port, extra=()):
    return ctxmod.default_directives(port, "::1", extra=[
        "cluster-enabled yes",
        "cluster-config-file nodes.conf",
        # Long enough that gossip never declares a node failed on its own: every
        # role change in this scenario is one this scenario asked for.
        "cluster-node-timeout 15000",
    ] + list(extra))


def _cinfo(c):
    out = {}
    blob = c.cmd("CLUSTER", "INFO")[1]
    for line in blob.decode("utf-8", "replace").splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            out[k] = v.strip()
    return out


def _wait_cinfo(c, field, want, deadline=60.0):
    end = time.time() + deadline
    while time.time() < end:
        if _cinfo(c).get(field) == want:
            return True
        time.sleep(0.1)
    return False


def _wait_all_ok(clients, deadline=60.0):
    return all(_wait_cinfo(c, "cluster_state", "ok", deadline) for c in clients)


def _rows(c):
    """CLUSTER NODES as (id, addr, flags, master-id) tuples, unnormalised.

    Node ids are needed as themselves here -- they address the failover -- so
    this reads the raw reply rather than the normalised one.
    """
    blob = c.cmd("CLUSTER", "NODES")[1].decode("utf-8", "replace")
    out = []
    for line in blob.splitlines():
        f = line.split(" ")
        if len(f) > 3:
            out.append((f[0], f[1], f[2], f[3]))
    return out


def _role_of(c, node_id):
    for nid, _addr, flags, _m in _rows(c):
        if nid == node_id:
            return "slave" if "slave" in flags else "master"
    return None


def _wait_role(c, node_id, want, deadline=60.0):
    end = time.time() + deadline
    while time.time() < end:
        if _role_of(c, node_id) == want:
            return True
        time.sleep(0.1)
    return False


def _wait_all_roles(clients, node_id, want, deadline=60.0):
    return all(_wait_role(c, node_id, want, deadline) for c in clients)


def _peer_fields(c):
    blob = c.cmd("CLUSTER", "NODES")[1]
    out = []
    for line in norm.normalize_text(blob).splitlines():
        parts = line.split(" ")
        if len(parts) > 2 and parts[0] == "<NODEID>":
            out.append(parts[1])
    return sorted(out)


def _conf_lines(path):
    try:
        with srv.open_regular(path) as f:  # no link followed
            blob = f.read()
    except OSError as e:
        return ["read-failed: %s" % e.__class__.__name__]
    return sorted(l for l in norm.normalize_text(blob).splitlines() if l.strip())


def _slot_addrs(c, masters_only=False):
    """The addresses `CLUSTER SLOTS` names, sorted and deduplicated.

    This is the slot map as a client consumes it. `masters_only` keeps the first
    node of each range and drops its replicas: the failover swaps a master for
    its replica, so the set of *all* addresses involved is the same before and
    after, and only the master column moves.
    """
    r = c.cmd("CLUSTER", "SLOTS")
    out = set()
    if r[0] != "array":
        return []
    for entry in r[1]:
        served = entry[1][2:3] if masters_only else entry[1][2:]
        for node in served:
            host = node[1][0][1]
            port = node[1][1][1]
            if isinstance(host, (bytes, bytearray)):
                host = host.decode("utf-8", "replace")
            out.add("%s:%s" % (host, port))
    return sorted(out)


def _find_empty_slot(c, owner_lo, owner_hi):
    """A slot in [owner_lo, owner_hi] that holds no keys.

    A slot with keys cannot be handed to another node without migrating them,
    and the blocked-client redirect below needs a slot that can simply move.
    """
    for slot in range(owner_lo, owner_lo + 64):
        if c.cmd("CLUSTER", "COUNTKEYSINSLOT", slot)[1] == 0:
            return slot
    return owner_lo


def _key_owned_by(c, lo, hi):
    """A key name whose slot is in [lo, hi], asked of the tree rather than
    computed here: which slot a key lands in is the tree's arithmetic."""
    for i in range(4096):
        k = "witness-%d" % i
        if lo <= c.cmd("CLUSTER", "KEYSLOT", k)[1] <= hi:
            return k
    return "witness-0"


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]
    nodes = []
    for i in range(4):
        s = ctx.server("node%d" % i, p[i], _directives(p[i]))
        s.start().wait_ready()
        nodes.append(s)
    clients = [s.client(timeout=30.0) for s in nodes]

    for i in (1, 2, 3):
        clients[0].cmd("CLUSTER", "MEET", "::1", p[i])
    ctx.emit("cfail/all-know-four",
             all(_wait_cinfo(c, "cluster_known_nodes", "4") for c in clients))
    for i, (lo, hi) in enumerate(RANGES):
        clients[i].cmd("CLUSTER", "ADDSLOTS", *[str(s) for s in range(lo, hi + 1)])
    ctx.emit("cfail/state-ok", _wait_all_ok(clients))

    ids = [c.cmd("CLUSTER", "MYID")[1].decode() for c in clients]
    ctx.emit_reply("cfail/replicate", clients[3].cmd("CLUSTER", "REPLICATE",
                                                     ids[0]))
    ctx.emit("cfail/replica-took-role", _wait_all_roles(clients, ids[3],
                                                        "slave"))
    ctx.emit("cfail/replica-link-up",
             srv.wait_info(clients[3], "replication", "master_link_status",
                           "up"))
    ctx.emit_reply("cfail/slaves-of-master",
                   clients[0].cmd("CLUSTER", "SLAVES", ids[0]), sort=True)
    ctx.emit("cfail/slot-addrs-before", _slot_addrs(clients[1]))
    ctx.emit("cfail/slot-masters-before", _slot_addrs(clients[1], True))

    # -- pub/sub across the bus ------------------------------------------------
    # A message published on one node reaches a subscriber on another only by
    # being forwarded over the cluster bus, which is a separate sender from the
    # one that serves clients.
    sub = nodes[2].client(timeout=30.0)
    ctx.emit_reply("cfail/subscribe", sub.cmd("SUBSCRIBE", "chan"))
    ctx.emit_reply("cfail/publish-from-other-node",
                   clients[0].cmd("PUBLISH", "chan", "over-the-bus"))
    try:
        ctx.emit_reply("cfail/subscriber-got-message", sub.reader.read())
    except (resp.Timeout, OSError) as e:
        ctx.emit("cfail/subscriber-got-message", "failed: %s" % type(e).__name__)
    ctx.emit_reply("cfail/unsubscribe", sub.cmd("UNSUBSCRIBE", "chan"))
    sub.close()

    # -- a client blocked on a slot that moves ---------------------------------
    # The redirect here is not produced by the command: `BLPOP` already returned
    # nothing and parked the client. What answers it is the cluster's own timer
    # noticing the slot no longer belongs here -- a separate formatter, on a
    # separate code path, rendering the same address.
    slot = _find_empty_slot(clients[0], RANGES[0][0], RANGES[0][1])
    ctx.emit("cfail/blocking-slot-is-node0s", RANGES[0][0] <= slot <= RANGES[0][1])
    ctx.emit_reply("cfail/countkeysinslot-empty",
                   clients[0].cmd("CLUSTER", "COUNTKEYSINSLOT", slot))
    ctx.emit_reply("cfail/getkeysinslot-empty",
                   clients[0].cmd("CLUSTER", "GETKEYSINSLOT", slot, 10))
    blocked = nodes[0].client(timeout=30.0)
    key = "blocked-%d" % slot
    # Send BLPOP without reading: the reply is whatever the redirect turns out
    # to be, and it only exists once the slot has moved.
    blocked.send_raw(resp.encode_command(["BLPOP", key, "0"]))
    blocked_seen = False
    end = time.time() + 10.0
    while time.time() < end:
        if srv.info_field(clients[0], "clients", "blocked_clients") != "0":
            blocked_seen = True
            break
        time.sleep(0.05)
    ctx.emit("cfail/client-is-blocked", blocked_seen, adv=True)
    ctx.emit_reply("cfail/setslot-node",
                   clients[0].cmd("CLUSTER", "SETSLOT", slot, "NODE", ids[1]))
    try:
        ctx.emit_reply("cfail/blocked-client-redirect", blocked.reader.read(),
                       adv=True)
    except (resp.Timeout, OSError) as e:
        ctx.emit("cfail/blocked-client-redirect",
                 "failed: %s" % type(e).__name__, adv=True)
    blocked.close()
    # Put it back, on the same node that gave it away. `SETSLOT` edits the
    # receiving node's table and nothing else, so node 1 was never told it had
    # acquired the slot and must not be told it has lost one.
    ctx.emit_reply("cfail/setslot-back",
                   clients[0].cmd("CLUSTER", "SETSLOT", slot, "NODE", ids[0]))
    ctx.emit("cfail/slot-returned", _wait_all_ok(clients))
    ctx.emit("cfail/slot-addrs-restored",
             _slot_addrs(clients[1]) == _slot_addrs(clients[0]))

    # -- the manual failover ---------------------------------------------------
    # `CLUSTER FAILOVER` with no option is the coordinated form: the slave asks
    # the master to pause, waits until it has the master's whole offset, and only
    # then asks the other masters for their votes. Nothing is lost and nothing
    # depends on a timeout.
    before = _conf_lines(os.path.join(nodes[1].dir, "nodes.conf"))
    masters_before = _slot_addrs(clients[1], True)
    witness = _key_owned_by(clients[0], RANGES[0][0], RANGES[0][1])
    ctx.emit_reply("cfail/witness-write", clients[0].cmd("SET", witness, "v"))
    ctx.emit_reply("cfail/failover-on-master",
                   clients[0].cmd("CLUSTER", "FAILOVER"), adv=True)
    ctx.emit_reply("cfail/failover", clients[3].cmd("CLUSTER", "FAILOVER"))
    ctx.emit("cfail/slave-promoted", _wait_role(clients[3], ids[3], "master"))
    ctx.emit("cfail/master-demoted", _wait_role(clients[0], ids[0], "slave"))
    ctx.emit("cfail/peers-agree-new-master",
             _wait_all_roles(clients, ids[3], "master"))
    ctx.emit("cfail/peers-agree-demotion",
             _wait_all_roles(clients, ids[0], "slave"))
    ctx.emit("cfail/state-ok-after", _wait_all_ok(clients))
    ctx.emit_reply("cfail/data-survived", clients[3].cmd("GET", witness))

    # The slot map now names a different address for the first third of the key
    # space, and every node has to have written that address down for itself.
    ctx.emit("cfail/slot-addrs-after", _slot_addrs(clients[1]))
    ctx.emit("cfail/slot-masters-after", _slot_addrs(clients[1], True))
    ctx.emit("cfail/slot-masters-moved",
             _slot_addrs(clients[1], True) != masters_before)
    ctx.emit("cfail/slot-addrs-agree",
             _slot_addrs(clients[1]) == _slot_addrs(clients[2]))
    ctx.emit("cfail/peer-fields-after", _peer_fields(clients[1]))
    ctx.emit("cfail/nodes-conf-after",
             _conf_lines(os.path.join(nodes[1].dir, "nodes.conf")))
    ctx.emit("cfail/nodes-conf-changed",
             _conf_lines(os.path.join(nodes[1].dir, "nodes.conf")) != before,
             adv=True)
    ctx.emit_reply("cfail/slaves-of-new-master",
                   clients[3].cmd("CLUSTER", "SLAVES", ids[3]), sort=True)
    # A write sent to the demoted node comes back as a redirect naming the
    # promoted one: the failover's address change, seen from a client.
    ctx.emit_reply("cfail/moved-to-new-master",
                   clients[0].cmd("SET", witness, "again"), adv=True)
    for f in CINFO_FIELDS:
        ctx.emit("cfail/after/%s" % f, _cinfo(clients[1]).get(f))

    # -- forget, which has to outlast the gossip that would undo it ------------
    # Node 2 is told to drop node 1. Removing a peer that three other nodes are
    # still talking about only sticks because the id goes on a blacklist first.
    ctx.emit_reply("cfail/forget-self",
                   clients[2].cmd("CLUSTER", "FORGET", ids[2]), adv=True)
    ctx.emit_reply("cfail/forget-my-master",
                   clients[0].cmd("CLUSTER", "FORGET", ids[3]), adv=True)
    ctx.emit_reply("cfail/forget-peer",
                   clients[2].cmd("CLUSTER", "FORGET", ids[1]))
    ctx.emit("cfail/forgotten-known-nodes",
             _wait_cinfo(clients[2], "cluster_known_nodes", "3", 20.0))
    ctx.emit("cfail/forgotten-stays-forgotten",
             not _wait_cinfo(clients[2], "cluster_known_nodes", "4", 10.0))
    ctx.emit("cfail/forgotten-peer-fields", _peer_fields(clients[2]), adv=True)
    ctx.emit_reply("cfail/forget-again",
                   clients[2].cmd("CLUSTER", "FORGET", ids[1]), adv=True)

    for c in clients:
        c.close()
    for s in nodes:
        s.stop()
