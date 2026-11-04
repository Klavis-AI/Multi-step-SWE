"""Cluster: the subsystem that writes addresses down and reads them back.

A cluster node keeps every peer's address in its own table, gossips it over the
bus, persists it to `nodes.conf`, hands it to clients in `CLUSTER NODES`,
`CLUSTER SLOTS` and in `-MOVED`/`-ASK` redirects, and reloads it after a
restart. That makes it the densest concentration of address text in the repo,
and every one of those renderings is recorded here as the baseline writes it.

The odd address forms are probed on a node that never joins the cluster: a
`CLUSTER MEET` the baseline accepts leaves a handshake entry that ages out on
the node-timeout clock, so probing them on a cluster member would make that
member's node table depend on how fast the machine ran.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import srv  # noqa: E402

PORTS = 8
FAMILY = "behaviour"

SLOTS = 16384

CINFO_FIELDS = ("cluster_state", "cluster_slots_assigned", "cluster_slots_ok",
                "cluster_slots_pfail", "cluster_slots_fail",
                "cluster_known_nodes", "cluster_size")

KEYS = ("alpha", "bravo", "charlie", "delta", "echo", "foxtrot",
        "{tag}one", "{tag}two", "golf", "hotel")

RANGES = ((0, 5460), (5461, 10922), (10923, 16383))

# Address spellings a client might reasonably hand to CLUSTER MEET. Which ones
# the baseline accepts, and what it stores for the ones it does, is the point of
# the probe.
MEET_FORMS = (
    ("bracketed", "[::1]"),
    ("scoped", "::1%lo"),
    ("v4-mapped", "::ffff:127.0.0.1"),
    ("v4-compatible", "::127.0.0.1"),
    ("hostname", "localhost"),
    ("unspecified", "::"),
    ("expanded-loopback", "0:0:0:0:0:0:0:1"),
    ("uppercase-mapped", "::FFFF:127.0.0.1"),
    ("documentation", "2001:db8::1"),
    ("multicast", "ff02::1"),
    ("v4-literal", "127.0.0.1"),
    ("garbage", "not-an-address"),
)


def _directives(port):
    return ctxmod.default_directives(port, "::1", extra=[
        "cluster-enabled yes",
        "cluster-config-file nodes.conf",
        "cluster-node-timeout 3000",
    ])


def _cinfo(c):
    out = {}
    blob = c.cmd("CLUSTER", "INFO")[1]
    for line in blob.decode("utf-8", "replace").splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            out[k] = v.strip()
    return out


def _wait_cinfo(c, field, want, deadline=45.0):
    end = time.time() + deadline
    while time.time() < end:
        if _cinfo(c).get(field) == want:
            return True
        time.sleep(0.1)
    return False


def _wait_known(clients, n, deadline=45.0):
    end = time.time() + deadline
    while time.time() < end:
        if all(_cinfo(c).get("cluster_known_nodes") == str(n) for c in clients):
            return True
        time.sleep(0.1)
    return False


def _wait_flag(c, node_id, want, deadline=45.0):
    """Wait until `node_id`'s row in CLUSTER NODES carries `want` in its flags."""
    end = time.time() + deadline
    while time.time() < end:
        blob = c.cmd("CLUSTER", "NODES")[1].decode("utf-8", "replace")
        for line in blob.splitlines():
            parts = line.split(" ")
            if len(parts) > 2 and parts[0] == node_id and want in parts[2]:
                return True
        time.sleep(0.1)
    return False


def _nodes_lines(c):
    """CLUSTER NODES as sorted, normalised lines.

    The reply's line order follows the node table's hash order, so it is sorted;
    every field inside a line, including the address, is compared in full.
    """
    blob = c.cmd("CLUSTER", "NODES")[1]
    return sorted(l for l in norm.normalize_text(blob).splitlines() if l.strip())


def _peer_fields(c):
    """Just the address field of every CLUSTER NODES row, sorted.

    Split out from the whole row because this is the single value the migration
    is required to re-spell, and a failure here should be legible on its own.
    """
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


# The same list c08_listeners keeps: host-kernel startup warnings.
HOST_WARNINGS = ("transparent huge pages", "overcommit_memory",
                 "somaxconn", "tcp backlog setting of",
                 "maximum number of open files")


def _startup_log(server):
    """A server's own log up to the line announcing it is serving.

    Only the timestamped lines: the ASCII-art banner wraps a box around them
    whose spacing carries no information. Stopping at "Ready to accept" keeps
    gossip and failure-detection chatter -- which is timed off the node-timeout
    clock, not off anything this scenario controls -- out of the observation.
    """
    out = []
    for line in norm.normalize_text(server.log()).splitlines():
        if not line.startswith("<LOGSTAMP>"):
            continue
        # Startup warnings about the *host* kernel and limits (THP,
        # overcommit, somaxconn, the open-files raise): whether they appear
        # depends on the machine the scoring runs on, not on the tree, so they
        # are dropped here exactly as c08_listeners drops them. The recorded
        # expectation had the same filter applied to it.
        if any(w in line.lower() for w in HOST_WARNINGS):
            continue
        out.append(line)
        if "ready to accept connections" in line:
            break
    return out


def _wait_all_ok(clients, deadline=45.0):
    """Every node agrees the cluster is up.

    Waiting on one node is not enough: slot ownership reaches the others by
    gossip, and with `cluster-require-full-coverage` a node that has not heard
    the whole map yet answers -CLUSTERDOWN to any key command.
    """
    return all(_wait_cinfo(c, "cluster_state", "ok", deadline) for c in clients)


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]
    nodes = []
    for i in range(4):
        s = ctx.server("node%d" % i, p[i], _directives(p[i]))
        s.start().wait_ready()
        nodes.append(s)
    clients = [s.client() for s in nodes]

    # -- a fresh, unjoined node --------------------------------------------
    fresh = _cinfo(clients[0])
    for f in CINFO_FIELDS:
        ctx.emit("cluster/fresh/%s" % f, fresh.get(f))
    # `cluster_enabled` lives in INFO, not in CLUSTER INFO; reading it from the
    # right place is what proves cluster mode actually came up.
    ctx.emit("cluster/fresh/info-cluster-enabled",
             srv.info_field(clients[0], "cluster", "cluster_enabled"))
    myid = clients[0].cmd("CLUSTER", "MYID")
    ctx.emit("cluster/myid-kind", myid[0])
    ctx.emit("cluster/myid-is-40-hex",
             len(myid[1]) == 40 and all(chr(b) in "0123456789abcdef"
                                        for b in myid[1]))
    ctx.emit("cluster/fresh-nodes-count", len(_nodes_lines(clients[0])))
    # A node that has not yet heard its own address from a peer prints an empty
    # host, which is baseline truth and worth pinning.
    ctx.emit("cluster/fresh-peer-fields", _peer_fields(clients[0]))
    ctx.emit_reply("cluster/slots-empty", clients[0].cmd("CLUSTER", "SLOTS"))
    ctx.emit_reply("cluster/countkeysinslot-empty",
                   clients[0].cmd("CLUSTER", "COUNTKEYSINSLOT", 0))

    # Which slot a key maps to is a property of the tree, not of the address
    # layer; it is recorded so a migration that disturbs hashing is caught.
    for k in KEYS:
        ctx.emit_reply("cluster/keyslot-%s" % k,
                       clients[0].cmd("CLUSTER", "KEYSLOT", k))

    # -- forming the cluster over IPv6 literals -----------------------------
    for i in (1, 2, 3):
        ctx.emit_reply("cluster/meet-%d" % i,
                       clients[0].cmd("CLUSTER", "MEET", "::1", p[i]))
    ctx.emit("cluster/all-know-four", _wait_known(clients, 4))

    for i, (lo, hi) in enumerate(RANGES):
        ctx.emit_reply("cluster/addslots-%d" % i,
                       clients[i].cmd("CLUSTER", "ADDSLOTS",
                                      *[str(s) for s in range(lo, hi + 1)]))
    ctx.emit("cluster/state-ok", _wait_all_ok(clients))

    ids = [c.cmd("CLUSTER", "MYID")[1].decode() for c in clients]
    ctx.emit_reply("cluster/replicate",
                   clients[3].cmd("CLUSTER", "REPLICATE", ids[0]))
    ctx.emit("cluster/replica-took-role",
             _wait_flag(clients[0], ids[3], "slave"))
    # The role change is instant; the sync behind it is not. Waiting for the
    # link keeps the master's replication snapshot from landing on disk at a
    # moment of its own choosing -- which decides whether the restart further
    # down has a dump to load.
    ctx.emit("cluster/replica-link-up",
             srv.wait_info(clients[3], "replication", "master_link_status",
                           "up"))

    formed = _cinfo(clients[0])
    for f in CINFO_FIELDS:
        ctx.emit("cluster/formed/%s" % f, formed.get(f))
    ctx.emit_reply("cluster/addslots-again",
                   clients[0].cmd("CLUSTER", "ADDSLOTS", "0"), adv=True)
    ctx.emit_reply("cluster/addslots-out-of-range",
                   clients[0].cmd("CLUSTER", "ADDSLOTS", str(SLOTS)), adv=True)
    ctx.emit_reply("cluster/countkeysinslot-bad",
                   clients[0].cmd("CLUSTER", "COUNTKEYSINSLOT", str(SLOTS)),
                   adv=True)

    # -- how the formed cluster writes every peer address -------------------
    ctx.emit("cluster/nodes-lines", _nodes_lines(clients[0]))
    ctx.emit("cluster/peer-fields", _peer_fields(clients[0]))
    ctx.emit("cluster/peer-fields-node1", _peer_fields(clients[1]))
    ctx.emit("cluster/peer-fields-replica", _peer_fields(clients[3]))
    ctx.emit_reply("cluster/slots", clients[0].cmd("CLUSTER", "SLOTS"),
                   sort=True)
    # CLUSTER SLAVES answers with a whole node description line, address and
    # all -- a second, independently formatted rendering of the same address.
    ctx.emit_reply("cluster/slaves-of-master",
                   clients[0].cmd("CLUSTER", "SLAVES", ids[0]), sort=True)
    ctx.emit_reply("cluster/slaves-of-replica",
                   clients[0].cmd("CLUSTER", "SLAVES", ids[3]), adv=True)
    ctx.emit("cluster/nodes-conf",
             _conf_lines(os.path.join(nodes[0].dir, "nodes.conf")))
    ctx.emit("cluster/nodes-conf-replica",
             _conf_lines(os.path.join(nodes[3].dir, "nodes.conf")))

    # -- redirects ----------------------------------------------------------
    # Every key is written through node 0; the ones that do not hash into node
    # 0's range come back as a redirect, and the redirect carries an address.
    ctx.emit("cluster/state-ok-before-redirects", _wait_all_ok(clients))
    for k in KEYS:
        ctx.emit_reply("cluster/set-%s" % k, clients[0].cmd("SET", k, "v"),
                       adv=True)
    ctx.emit_reply("cluster/get-alpha", clients[0].cmd("GET", "alpha"),
                   adv=True)
    ctx.emit_reply("cluster/multikey-crossslot",
                   clients[0].cmd("MGET", "alpha", "bravo"), adv=True)
    ctx.emit_reply("cluster/multikey-same-tag",
                   clients[0].cmd("MSET", "{tag}one", "1", "{tag}two", "2"),
                   adv=True)
    ctx.emit_reply("cluster/read-from-replica",
                   clients[3].cmd("GET", "alpha"), adv=True)
    ctx.emit_reply("cluster/readonly", clients[3].cmd("READONLY"), adv=True)
    ctx.emit_reply("cluster/read-after-readonly",
                   clients[3].cmd("GET", "alpha"), adv=True)
    ctx.emit_reply("cluster/readwrite", clients[3].cmd("READWRITE"), adv=True)

    # A migrating/importing slot pair is the only way to see an -ASK redirect,
    # which renders its address through the same formatter as -MOVED. The hash
    # tag forces the probe key into the same slot as `alpha`, so the slot being
    # migrated is one this scenario already knows node 0 owns.
    moving = clients[0].cmd("CLUSTER", "KEYSLOT", "alpha")[1]
    ctx.emit("cluster/migrating-slot-is-node0s", moving <= RANGES[0][1])
    ctx.emit_reply("cluster/setslot-migrating",
                   clients[0].cmd("CLUSTER", "SETSLOT", moving, "MIGRATING",
                                  ids[1]))
    ctx.emit_reply("cluster/setslot-importing",
                   clients[1].cmd("CLUSTER", "SETSLOT", moving, "IMPORTING",
                                  ids[0]))
    ctx.emit_reply("cluster/ask-redirect",
                   clients[0].cmd("GET", "{alpha}absent"), adv=True)
    ctx.emit_reply("cluster/present-key-still-served",
                   clients[0].cmd("GET", "alpha"), adv=True)
    ctx.emit_reply("cluster/importing-without-asking",
                   clients[1].cmd("GET", "{alpha}absent"), adv=True)
    ctx.emit_reply("cluster/asking", clients[1].cmd("ASKING"), adv=True)
    ctx.emit_reply("cluster/get-after-asking",
                   clients[1].cmd("GET", "{alpha}absent"), adv=True)
    ctx.emit_reply("cluster/setslot-stable-0",
                   clients[0].cmd("CLUSTER", "SETSLOT", moving, "STABLE"))
    ctx.emit_reply("cluster/setslot-stable-1",
                   clients[1].cmd("CLUSTER", "SETSLOT", moving, "STABLE"))
    ctx.emit_reply("cluster/setslot-unknown-node",
                   clients[0].cmd("CLUSTER", "SETSLOT", moving, "MIGRATING",
                                  "0" * 40), adv=True)
    ctx.emit_reply("cluster/setslot-bad-subcommand",
                   clients[0].cmd("CLUSTER", "SETSLOT", moving, "SIDEWAYS"),
                   adv=True)

    # -- the tree's own client following a redirect -------------------------
    # `redis-cli -c` has to parse the address out of the redirect it was just
    # handed; how it splits that text is the thing under test.
    for label, extra in (("plain", []), ("follow-redirects", ["-c"])):
        rc, out = srv.run_tool(ctx.tree, "redis-cli",
                               ["-h", "::1", "-p", p[0]] + extra +
                               ["SET", "bravo", "from-cli"],
                               timeout=30, stdin=b"")
        ctx.emit("cluster/cli-%s-rc" % label, rc, adv=True)
        ctx.emit_text("cluster/cli-%s-out" % label, out, adv=True)
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", "::1", "-p", p[0], "-c", "GET", "alpha"],
                           timeout=30, stdin=b"")
    ctx.emit("cluster/cli-local-key-rc", rc, adv=True)
    ctx.emit_text("cluster/cli-local-key-out", out, adv=True)

    # -- a restart has to read its own nodes.conf back ----------------------
    before = _peer_fields(clients[0])
    clients[0].close()
    nodes[0].stop()
    restarted = ctx.server("node0b", p[0],
                           _directives(p[0]) + ["logfile restart.log"],
                           datadir=nodes[0].dir, logfile="restart.log")
    restarted.start().wait_ready()
    c0 = restarted.client()
    ctx.emit("cluster/restart-known-nodes",
             _wait_cinfo(c0, "cluster_known_nodes", "4"))
    ctx.emit("cluster/restart-state-ok", _wait_cinfo(c0, "cluster_state", "ok"))
    ctx.emit("cluster/restart-same-myid",
             c0.cmd("CLUSTER", "MYID")[1].decode() == ids[0])
    ctx.emit("cluster/restart-peer-fields", _peer_fields(c0))
    ctx.emit("cluster/restart-peers-match", _peer_fields(c0) == before)
    ctx.emit_reply("cluster/restart-slots", c0.cmd("CLUSTER", "SLOTS"),
                   sort=True)
    ctx.emit("cluster/restart-log", _startup_log(restarted), adv=True)

    # -- address forms, on a node that is not in the cluster ----------------
    # p[5] has nothing listening on it, so an accepted MEET stays in handshake
    # and the reply is all this probe is after.
    probe = ctx.server("probe", p[4], _directives(p[4]))
    probe.start().wait_ready()
    pc = probe.client()
    for label, host in MEET_FORMS:
        ctx.emit_reply("cluster/meet-%s" % label,
                       pc.cmd("CLUSTER", "MEET", host, p[5]), adv=True)
    ctx.emit_reply("cluster/meet-bad-port",
                   pc.cmd("CLUSTER", "MEET", "::1", "notaport"), adv=True)
    ctx.emit_reply("cluster/meet-negative-port",
                   pc.cmd("CLUSTER", "MEET", "::1", "-1"), adv=True)
    ctx.emit_reply("cluster/meet-arity", pc.cmd("CLUSTER", "MEET", "::1"),
                   adv=True)
    # Read back immediately: whichever forms were accepted are in the table now,
    # before any handshake timer can retire them.
    ctx.emit("cluster/probe-peer-fields", _peer_fields(pc), adv=True)

    # -- forget, reset, and the guards around them --------------------------
    ctx.emit_reply("cluster/forget-non-peer",
                   pc.cmd("CLUSTER", "FORGET", ids[0]), adv=True)
    ctx.emit_reply("cluster/forget-unknown",
                   pc.cmd("CLUSTER", "FORGET", "0" * 40), adv=True)
    ctx.emit_reply("cluster/forget-bad-id",
                   pc.cmd("CLUSTER", "FORGET", "nope"), adv=True)
    ctx.emit_reply("cluster/set-config-epoch-with-peers",
                   pc.cmd("CLUSTER", "SET-CONFIG-EPOCH", "1"), adv=True)
    ctx.emit_reply("cluster/reset-hard", pc.cmd("CLUSTER", "RESET", "HARD"),
                   adv=True)
    ctx.emit("cluster/peer-fields-after-reset", _peer_fields(pc), adv=True)
    ctx.emit_reply("cluster/set-config-epoch-alone",
                   pc.cmd("CLUSTER", "SET-CONFIG-EPOCH", "1"), adv=True)
    ctx.emit_reply("cluster/bad-subcommand", pc.cmd("CLUSTER", "NOSUCHSUB"),
                   adv=True)

    pc.close()
    probe.stop()
    c0.close()
    restarted.stop()
    for c in clients[1:]:
        c.close()
    for s in nodes[1:]:
        s.stop()
