"""Everything the product does, on a host whose kernel has no IPv4.

`g06` asks whether the repository still writes an IPv4 address down. This asks
the other half, and it is the half the step is named for: with
`socket(AF_INET, ...)` failing `EAFNOSUPPORT` and every other family untouched,
the tree starts on the configuration file it ships, serves, replicates, forms
and reshards a cluster, carries Sentinel through a failover, and is reachable
by its own client and benchmark with no `-h` given.

Every address in here is written the way an operator writes one: `localhost`,
not `::1` -- everywhere the product accepts a name at all. That is deliberate
and it is the whole difference between this step and the three before it. A name has an address of each family behind it, and
the tree the baseline ships decides which family to ask the kernel for by
looking at whether the operator's *string* contains a colon -- so `bind
localhost` means `AF_INET`, and on a host with no IPv4 the server exits before
it has listened to anything. Nothing about that is visible when the
configuration says `::1`, which is why the earlier scenarios, which all say
`::1`, pass on the baseline under this same filter. What step 4 asks for is
that the family come from what the resolver returned.

Two sets of observations, then, and they are scored the same way but they mean
different things:

  * the ones that only the finished tree can answer -- a server, a replica, a
    cluster, a sentinel, a client, all named rather than spelled;
  * the explicit `::1` cases beside them, which the baseline already answers
    and which must therefore keep being answered. They are what a tree that
    "passed" by breaking IPv6 literals would fail.

The one argument written as a literal on purpose is `CLUSTER MEET`'s. That
command does not take a name and is not being asked to start taking one:
`clusterStartHandshake()` parses its argument with the resolver in its
numeric-only mode and answers `ERR Invalid node address specified` for anything
else, which is upstream behaviour and which `c11_cluster` already scores in
`cluster/meet-hostname`. Handing it `localhost` here would have recorded that
refusal as the golden and called a cluster that never formed progress. The
place the cluster does reach a peer by name is `MIGRATE`, whose host argument
goes to `anetTcpGenericConnect()` -- the server opening a connection of its
own, from text, with the family coming from the resolver.

The filter itself is `lib/no4.py`, and its known answer -- including the
unfiltered control, without which a box with no IPv4 configured would read the
same as a working filter -- is emitted here before anything it is used to find.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import no4  # noqa: E402
import norm  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402

PORTS = 12
FAMILY = "behaviour"

# The name every configuration in this scenario is written with. One name, one
# reason: it has an address of each family behind it, so it is the case where
# choosing the family from the text and choosing it from the resolver give
# different answers.
NAME = "localhost"

RANGES = ((0, 5460), (5461, 10922), (10923, 16383))
KEY = "alpha"
MASTER = "mymaster"

# How long anything here waits for a thing that is going to happen. Nothing
# waits it out: every wait below also ends when the process it is waiting on
# has exited, which is what ends the cases the baseline fails. Generous on
# purpose -- a deadline tight enough to be informative is one that expires on a
# loaded host, and then the recording says a working tree stopped working.
SETTLE = 60.0
CLUSTER_DEADLINE = 90.0
FAILOVER_DEADLINE = 120.0


def _reachable(host, port, timeout=5.0):
    try:
        with resp.Client(host, port, timeout=timeout) as c:
            return c.cmd("PING")[1] == b"PONG"
    except Exception:
        return False


def _settle(server, port, hosts=("::1",), deadline=SETTLE):
    """Wait until the server answers, and never raise if it never does.

    Raising here would lose every observation after the wait, so a tree that
    failed to start one server would score the same as one that failed to
    build. The baseline fails most of these, and it has to fail them one cell
    at a time.
    """
    end = time.time() + deadline
    while time.time() < end:
        for h in hosts:
            if _reachable(h, port, timeout=1.0):
                return True
        if server.proc is not None and server.proc.poll() is not None:
            return False
        time.sleep(0.1)
    return False


def _client(port, host="::1"):
    """A client to a server that may not be there, or None."""
    try:
        c = resp.Client(host, port, timeout=10.0)
        c.cmd("PING")
        return c
    except Exception:
        return None


def _cmd(client, *args):
    """A reply as JSON, or a marker saying there was nobody to ask.

    Every observation below goes through this. A scenario whose first server
    did not start still has to emit the rest of its cells, or a tree that
    stopped at the first hurdle would be indistinguishable from one that
    crashed the harness -- and `<no server>` is a value like any other, so the
    baseline records it and a candidate that reproduces it scores nothing for
    the cell.
    """
    if client is None:
        return "<no server>"
    try:
        return norm.reply_to_json(client.cmd(*args))
    except Exception as e:
        return "<error: %s>" % e.__class__.__name__


def _info_field(client, section, field):
    if client is None:
        return "<no server>"
    try:
        return srv.info_field(client, section, field)
    except Exception as e:
        return "<error: %s>" % e.__class__.__name__


def _wait(fn, want, deadline, alive=None):
    """Poll `fn` until it returns `want`; report what it last returned.

    `alive` is what keeps the baseline's runtime finite. Most of the waits
    below are for something a server does, and on the baseline the server is
    not there to do it -- so the wait would run its whole deadline out, once
    per cell, and a scenario with a dozen of them would take half an hour to
    record a tree that failed in the first second. Waiting stops as soon as the
    processes being waited on have gone, and what is recorded is what the last
    poll saw, which is the same value the deadline would have returned.
    """
    end = time.time() + deadline
    last = fn()
    while last != want:
        if time.time() >= end:
            break
        if alive is not None and not alive():
            break
        time.sleep(0.1)
        last = fn()
    return last


class _Reconnecting(object):
    """A client that reopens itself, for a question asked across a failover.

    Sentinel's reconfiguration is a transaction that ends in `CLIENT KILL TYPE
    normal` (`sentinel.c`), so every ordinary connection to an instance it
    promotes is dropped on purpose. A question asked down a socket opened
    before the failover therefore answers with the name of a socket exception,
    which is a fact about this harness and not about the tree. It is asked
    again on a new connection instead, and only a server that cannot be
    reached at all still answers `<no server>`.
    """

    def __init__(self, port, host="::1"):
        self.host = host
        self.port = port
        self.c = _client(port, host)

    def info_field(self, section, field):
        v = _info_field(self.c, section, field)
        if v == "<no server>" or (isinstance(v, str)
                                  and v.startswith("<error:")):
            self.close()
            self.c = _client(self.port, self.host)
            v = _info_field(self.c, section, field)
        return v

    def close(self):
        if self.c is not None:
            try:
                self.c.close()
            except Exception:
                pass
            self.c = None


def _alive(*servers):
    """A predicate: every one of these processes is still running."""
    def ok():
        return all(s.proc is not None and s.proc.poll() is None
                   for s in servers)
    return ok


def _cinfo(client, field):
    if client is None:
        return "<no server>"
    try:
        blob = client.cmd("CLUSTER", "INFO")[1].decode("utf-8", "replace")
    except Exception as e:
        return "<error: %s>" % e.__class__.__name__
    for line in blob.splitlines():
        if line.startswith(field + ":"):
            return line.split(":", 1)[1].strip()
    return None


def _shipped_conf(ctx, port, name):
    """The `redis.conf` the tree ships, with only where-to-run appended.

    Not a configuration of this scenario's own: the question is whether the
    file the project hands an operator works on a host without IPv4, so the
    file is copied as it is and the four directives that say where this
    particular run should put itself are appended. Redis takes the last
    occurrence of a directive, so appending is enough and nothing above has to
    be edited -- `bind` above all, which is the line being asked about.
    """
    d = ctx.workspace.sub(name)
    try:
        with open(os.path.join(ctx.tree.root, "redis.conf"), "rb") as f:
            text = f.read().decode("utf-8", "replace")
    except OSError:
        return None, d
    conf = os.path.join(d, "%s.conf" % name)
    srv.write_new(conf, text.rstrip("\n")
                  + "\n\n# appended by the scorer: where to run, nothing else\n"
                  + "port %d\ndir .\nlogfile server.log\ndaemonize no\n" % port)
    # (`srv.write_new`: nothing in the score uid's directory is followed.)
    return conf, d


def _sentinel_directives(port, master_port):
    return [
        "bind %s" % NAME,
        "port %d" % port,
        "dir .",
        "logfile sentinel.log",
        "daemonize no",
        "sentinel monitor %s %s %d 2" % (MASTER, NAME, master_port),
        "sentinel down-after-milliseconds %s 1000" % MASTER,
        "sentinel failover-timeout %s 10000" % MASTER,
        "sentinel parallel-syncs %s 1" % MASTER,
    ]


def _myid(client):
    """A node's own id, unmasked, or None."""
    if client is None:
        return None
    try:
        v = client.cmd("CLUSTER", "MYID")[1]
    except Exception:
        return None
    return v.decode("utf-8", "replace") if isinstance(v, bytes) else None


def _master_addr(client):
    """SENTINEL get-master-addr-by-name as (host, port), or None."""
    if client is None:
        return None
    try:
        r = client.cmd("SENTINEL", "get-master-addr-by-name", MASTER)
    except Exception:
        return None
    if r[0] != "array" or not r[1]:
        return None
    return tuple(e[1].decode("utf-8", "replace") for e in r[1])


def _shipped(ctx, launcher):
    """Checks 1-3 and 8-9: the file the project ships, and its own clients."""
    port = ctx.port(0)
    conf, d = _shipped_conf(ctx, port, "shipped")
    ctx.emit("no4/shipped/conf-present", conf is not None)
    if conf is None:
        client = None
        ctx.emit("no4/shipped/started", False)
    else:
        s = ctx.server("shipped", port, (), host="::1", launcher=launcher,
                       datadir=d, conf=conf)
        s.start()
        ctx.emit("no4/shipped/started", _settle(s, port))
        client = _client(port)
    ctx.emit("no4/shipped/ping", _cmd(client, "PING"))
    ctx.emit("no4/shipped/set", _cmd(client, "SET", "k", "v"))
    ctx.emit("no4/shipped/get", _cmd(client, "GET", "k"))
    ctx.emit("no4/shipped/dbsize", _cmd(client, "DBSIZE"))

    # The tree's own client and benchmark, with no `-h`: whatever host they
    # default to has to be one that exists on this kernel, and the server above
    # is where the shipped configuration put one.
    rc, out = srv.run_tool(ctx.tree, "redis-cli", ["-p", port, "PING"],
                           timeout=30, stdin=b"", launcher=launcher)
    ctx.emit("no4/default-host/cli-rc", rc)
    ctx.emit_text("no4/default-host/cli-out", out)

    # The default host is not only a string in the usage text. It is the host
    # the client actually goes to when the operator says nothing -- which is
    # how the client is used far more often than with `-h` -- and these are the
    # three commands that prove it went somewhere that works: `PING` alone
    # proves a connection, and a write, a read of what was written and a count
    # of the keyspace prove the connection is to a server and carries data both
    # ways. A tree that reached the right host but could only manage a
    # handshake over it has not finished. Same shape as above and no `-h`
    # anywhere, under the same filter.
    rc, _out = srv.run_tool(ctx.tree, "redis-cli", ["-p", port, "SET", "dh", "yes"],
                            timeout=30, stdin=b"", launcher=launcher)
    ctx.emit("no4/default-host/cli-set", rc)
    _rc, out = srv.run_tool(ctx.tree, "redis-cli", ["-p", port, "GET", "dh"],
                            timeout=30, stdin=b"", launcher=launcher)
    ctx.emit_text("no4/default-host/cli-get", out)
    _rc, out = srv.run_tool(ctx.tree, "redis-cli", ["-p", port, "DBSIZE"],
                            timeout=30, stdin=b"", launcher=launcher)
    ctx.emit_text("no4/default-host/cli-dbsize", out)
    rc, out = srv.run_benchmark(
        ctx.tree, ["-p", port, "-n", 10, "-c", 1, "-t", "set", "-q"],
        timeout=60, launcher=launcher)
    ctx.emit("no4/default-host/bench-rc", rc)
    # Only whether it ran: the benchmark prints a requests-per-second figure,
    # and that is the machine's number rather than the tree's.
    ctx.emit("no4/default-host/bench-said-something", bool(out))
    ctx.emit("no4/default-host/bench-aborted", srv.BENCH_ABORT in (out or b""))
    if client is not None:
        client.close()


def _replication(ctx, launcher):
    """Check 4: a replica that found its master by name, under the filter."""
    mp, sp = ctx.port(1), ctx.port(2)
    m = ctx.server("repl-master", mp, ctxmod.default_directives(mp, NAME),
                   host="::1", launcher=launcher)
    m.start()
    ctx.emit("no4/repl/master-started", _settle(m, mp))
    s = ctx.server("repl-slave", sp,
                   ctxmod.default_directives(sp, NAME,
                                             extra=["slaveof %s %d" % (NAME, mp)]),
                   host="::1", launcher=launcher)
    s.start()
    ctx.emit("no4/repl/slave-started", _settle(s, sp))
    mc, sc = _client(mp), _client(sp)
    ctx.emit("no4/repl/master-role", _info_field(mc, "replication", "role"))
    ctx.emit("no4/repl/slave-role", _info_field(sc, "replication", "role"))
    up = _alive(m, s)
    ctx.emit("no4/repl/link-status",
             _wait(lambda: _info_field(sc, "replication", "master_link_status"),
                   "up", SETTLE, up))
    ctx.emit("no4/repl/connected-slaves",
             _wait(lambda: _info_field(mc, "replication", "connected_slaves"),
                   "1", SETTLE, up))
    ctx.emit("no4/repl/set-on-master", _cmd(mc, "SET", "replicated", "yes"))
    ctx.emit("no4/repl/get-on-slave",
             _wait(lambda: _cmd(sc, "GET", "replicated"),
                   ["bulk", "yes"], SETTLE, up))
    for c in (mc, sc):
        if c is not None:
            c.close()
    s.stop()
    m.stop()


def _cluster(ctx, launcher):
    """Checks 5-6: three nodes configured by name, and a slot moved between two."""
    ports = [ctx.port(3), ctx.port(4), ctx.port(5)]
    nodes = []
    for i, p in enumerate(ports):
        s = ctx.server("cnode%d" % i, p,
                       ctxmod.default_directives(p, NAME, extra=[
                           "cluster-enabled yes",
                           "cluster-config-file nodes.conf",
                           "cluster-node-timeout 3000"]),
                       host="::1", launcher=launcher)
        s.start()
        nodes.append(s)
    started = [_settle(s, p) for s, p in zip(nodes, ports)]
    ctx.emit("no4/cluster/nodes-started", started)
    clients = [_client(p) for p in ports]

    # The one literal in this scenario, and it is the product's rule rather
    # than a convenience: `CLUSTER MEET` parses its argument numerically and
    # refuses a name -- `c11_cluster`'s `cluster/meet-hostname` scores exactly
    # that refusal. The nodes are still *configured* by name; what they are
    # introduced to each other by is an address, as it is upstream.
    for i in (1, 2):
        ctx.emit("no4/cluster/meet-%d" % i,
                 _cmd(clients[0], "CLUSTER", "MEET", "::1", ports[i]))
    up = _alive(*nodes)
    ctx.emit("no4/cluster/known-nodes",
             _wait(lambda: [_cinfo(c, "cluster_known_nodes") for c in clients],
                   ["3", "3", "3"], CLUSTER_DEADLINE, up))
    for i, (lo, hi) in enumerate(RANGES):
        ctx.emit("no4/cluster/addslots-%d" % i,
                 _cmd(clients[i], "CLUSTER", "ADDSLOTS",
                      *[str(x) for x in range(lo, hi + 1)]))
    ctx.emit("no4/cluster/state",
             _wait(lambda: [_cinfo(c, "cluster_state") for c in clients],
                   ["ok", "ok", "ok"], CLUSTER_DEADLINE, up))

    # -- the reshard, by the path `redis-trib` takes -------------------------
    # Done by hand rather than with the shipped Ruby helper: the verifier image
    # has no Ruby in it, and the helper is a client of exactly these four
    # commands. `MIGRATE` is the one that matters here -- it is the server
    # opening a connection of its own to an address it was handed as text.
    slot = None
    r = _cmd(clients[0], "CLUSTER", "KEYSLOT", KEY)
    if isinstance(r, list) and len(r) == 2 and r[0] == "int":
        slot = r[1]
    ctx.emit("no4/cluster/keyslot", r)
    src = dst = None
    if slot is not None:
        src = next(i for i, (lo, hi) in enumerate(RANGES) if lo <= slot <= hi)
        dst = (src + 1) % 3
    ctx.emit("no4/cluster/slot-owned-by-a-node", src is not None)
    # Read raw, not through `_cmd`: a node id is exactly the kind of
    # run-to-run identifier `norm` masks, and a masked id cannot be handed back
    # to `CLUSTER SETSLOT`.
    ids = [_myid(c) for c in clients]
    ctx.emit("no4/cluster/ids-are-40-hex",
             [isinstance(x, str) and len(x) == 40 for x in ids])

    if src is None or ids[src] is None or ids[dst] is None:
        ctx.emit("no4/cluster/set-before-migrate", "<no cluster>")
        ctx.emit("no4/cluster/setslot-importing", "<no cluster>")
        ctx.emit("no4/cluster/setslot-migrating", "<no cluster>")
        ctx.emit("no4/cluster/keys-in-slot", "<no cluster>")
        ctx.emit("no4/cluster/migrate", "<no cluster>")
        ctx.emit("no4/cluster/keys-left-behind", "<no cluster>")
        ctx.emit("no4/cluster/setslot-node-src", "<no cluster>")
        ctx.emit("no4/cluster/setslot-node-dst", "<no cluster>")
        ctx.emit("no4/cluster/read-from-new-owner", "<no cluster>")
    else:
        ctx.emit("no4/cluster/set-before-migrate",
                 _cmd(clients[src], "SET", KEY, "moved"))
        ctx.emit("no4/cluster/setslot-importing",
                 _cmd(clients[dst], "CLUSTER", "SETSLOT", slot, "IMPORTING",
                      ids[src]))
        ctx.emit("no4/cluster/setslot-migrating",
                 _cmd(clients[src], "CLUSTER", "SETSLOT", slot, "MIGRATING",
                      ids[dst]))
        keys = _cmd(clients[src], "CLUSTER", "GETKEYSINSLOT", slot, 10)
        ctx.emit("no4/cluster/keys-in-slot", keys)
        ctx.emit("no4/cluster/migrate",
                 _cmd(clients[src], "MIGRATE", NAME, ports[dst], KEY, 0, 5000))
        ctx.emit("no4/cluster/keys-left-behind",
                 _cmd(clients[src], "CLUSTER", "GETKEYSINSLOT", slot, 10))
        ctx.emit("no4/cluster/setslot-node-src",
                 _cmd(clients[src], "CLUSTER", "SETSLOT", slot, "NODE", ids[dst]))
        ctx.emit("no4/cluster/setslot-node-dst",
                 _cmd(clients[dst], "CLUSTER", "SETSLOT", slot, "NODE", ids[dst]))
        ctx.emit("no4/cluster/read-from-new-owner",
                 _wait(lambda: _cmd(clients[dst], "GET", KEY),
                       ["bulk", "moved"], CLUSTER_DEADLINE, up))

    for c in clients:
        if c is not None:
            c.close()
    for s in reversed(nodes):
        s.stop()


def _sentinel(ctx, launcher):
    """Check 7: three sentinels monitoring a master by name, then a failover."""
    mp, sp = ctx.port(6), ctx.port(7)
    sentinel_ports = [ctx.port(8), ctx.port(9), ctx.port(10)]
    master = ctx.server("sent-master", mp, ctxmod.default_directives(mp, NAME),
                        host="::1", launcher=launcher)
    master.start()
    ctx.emit("no4/sentinel/master-started", _settle(master, mp))
    slave = ctx.server("sent-slave", sp,
                       ctxmod.default_directives(sp, NAME,
                                                 extra=["slaveof %s %d" % (NAME, mp)]),
                       host="::1", launcher=launcher)
    slave.start()
    ctx.emit("no4/sentinel/slave-started", _settle(slave, sp))
    sc = _Reconnecting(sp)
    ctx.emit("no4/sentinel/pre-link",
             _wait(lambda: sc.info_field("replication", "master_link_status"),
                   "up", SETTLE))

    sentinels = []
    for i, p in enumerate(sentinel_ports):
        s = ctx.server("sentinel%d" % i, p, _sentinel_directives(p, mp),
                       binary="redis-sentinel", logfile="sentinel.log",
                       host="::1", launcher=launcher)
        s.start()
        sentinels.append(s)
    ctx.emit("no4/sentinel/sentinels-started",
             [_settle(s, p) for s, p in zip(sentinels, sentinel_ports)])
    scl = [_client(p) for p in sentinel_ports]

    up = _alive(*sentinels)
    ctx.emit("no4/sentinel/pre-addr-port",
             _wait(lambda: (_master_addr(scl[0]) or ("", ""))[1], str(mp),
                   SETTLE, up))
    ctx.emit("no4/sentinel/num-slaves",
             _wait(lambda: _sentinel_field(scl[0], "num-slaves"), "1", SETTLE,
                   up))
    ctx.emit("no4/sentinel/num-other-sentinels",
             _wait(lambda: _sentinel_field(scl[0], "num-other-sentinels"),
                   "2", SETTLE, up))

    # The failover itself: the master goes away and the sentinels have to agree
    # on the slave, reach it, reconfigure it, and tell each other about it --
    # every one of those steps over an address they resolved from a name.
    master.stop()
    ctx.emit("no4/sentinel/promoted",
             _wait(lambda: (_master_addr(scl[0]) or ("", ""))[1],
                   str(sp), FAILOVER_DEADLINE, up))
    ctx.emit("no4/sentinel/peers-agree",
             _wait(lambda: [(_master_addr(c) or ("", ""))[1] for c in scl[1:]],
                   [str(sp), str(sp)], FAILOVER_DEADLINE, up))
    ctx.emit("no4/sentinel/slave-became-master",
             _wait(lambda: sc.info_field("replication", "role"), "master",
                   FAILOVER_DEADLINE, up))
    # What the sentinels wrote down. The address is the tree's rendering of a
    # peer it learned at run time, and it is persisted -- so a restart would
    # read it back.
    ctx.emit("no4/sentinel/rewrote-conf",
             [_conf_monitor_addr(s) for s in sentinels])

    for c in scl:
        if c is not None:
            c.close()
    sc.close()
    for s in reversed(sentinels):
        s.stop()
    slave.stop()


def _sentinel_field(client, field):
    """One field of `SENTINEL master <name>`, as text."""
    if client is None:
        return "<no sentinel>"
    try:
        r = client.cmd("SENTINEL", "master", MASTER)
    except Exception as e:
        return "<error: %s>" % e.__class__.__name__
    if r[0] != "array":
        return "<no master>"
    flat = [e[1] for e in r[1]]
    for i in range(0, len(flat) - 1, 2):
        if flat[i] == field.encode():
            v = flat[i + 1]
            return v.decode("utf-8", "replace") if isinstance(v, bytes) else v
    return None


def _conf_monitor_addr(server):
    """The address on the `sentinel monitor` line the sentinel rewrote."""
    raw = srv.read_regular(server.conf)  # no link followed
    if raw is None:
        return "<unreadable>"
    text = raw.decode("utf-8", "replace")
    for line in text.splitlines():
        words = line.split()
        if len(words) >= 5 and words[0].lower() == "sentinel" \
                and words[1].lower() == "monitor" and words[2] == MASTER:
            return "%s %s" % (words[3], words[4])
    return "<no monitor line>"


def _explicit_v6(ctx, launcher):
    """The cases the baseline already answers, which must keep being answered.

    A tree that made this step's headline observations true by removing IPv4
    from the resolver, or by hard-coding `::1` wherever a name used to be, is
    caught here rather than praised: an explicit IPv6 literal worked before this
    step and works after it, filter or no filter.
    """
    port = ctx.port(11)
    s = ctx.server("explicit-v6", port, ctxmod.default_directives(port, "::1"),
                   host="::1", launcher=launcher)
    s.start()
    ctx.emit("no4/explicit-v6/started", _settle(s, port), adv=True)
    c = _client(port)
    ctx.emit("no4/explicit-v6/ping", _cmd(c, "PING"), adv=True)
    ctx.emit("no4/explicit-v6/round-trip",
             [_cmd(c, "SET", "e", "1"), _cmd(c, "GET", "e")], adv=True)
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", "::1", "-p", port, "PING"], timeout=30,
                           stdin=b"", launcher=launcher)
    ctx.emit("no4/explicit-v6/cli-rc", rc, adv=True)
    ctx.emit_text("no4/explicit-v6/cli-out", out, adv=True)
    # The connecting side's questions, while there is a server on `::1` that
    # every tree in the line -- the 2016 baseline included -- can start. See
    # `_connecting`.
    _connecting(ctx, launcher, port)
    if c is not None:
        c.close()
    s.stop()


def _connecting(ctx, launcher, port):
    """The client's own `socket()`, against a destination of each shape.

    Everything else here watches a server choose a family for a listener. This
    watches the other end choose one for a connection: `anetTcpGenericConnect`
    walks the list `getaddrinfo` returned and is supposed to walk past an entry
    whose `socket()` the kernel refused rather than stopping at the first one.

    These are regression cells rather than progress ones, and deliberately so.
    The walk is step 2's work -- the instruction says at :42-43 that the
    connecting side already does this and keeps doing it -- so there is no
    input on this host on which it can be made to move at step 4:
    `getaddrinfo` never puts an IPv4 address ahead of `::1`. What these cells
    are is the input that proves the filter did not break it. A tree that got
    the listener onto `::1` by deleting the walk behind it would answer every
    other cell in this scenario correctly and fail these.

    Two destinations, because one of them cannot tell the two behaviours apart:

      * `v4only.test` -- one address, IPv4, and nothing to walk on to. Under
        the filter the client cannot even create the socket, so it has to fail
        and has to say so rather than hang or crash.
      * `multi.test` -- five addresses, in the order `::1`, `127.0.0.1`,
        `127.99.99.1`, `2001:db8::1`, `2001:db8::2`. Two of them are ones
        whose `socket()` the filter refuses outright and two more have no
        route. It answers on `::1`, which is first; the pin asks the resolver
        for `AF_INET` alone and so never sees it.

    Both are asked of `_explicit_v6`'s server rather than of the shipped one,
    and that choice is the whole of what makes them a net. A regression cell
    has to be answerable by the tree these cells are protecting -- step 2's --
    and the shipped server does not come up under the filter until step 4, so
    a `multi.test` probe aimed at it would record "refused" for every tree
    before this one and composition would classify the pair as progress: two
    more cells paid for reaching the listener, which twelve other cells
    already pay for, and no net at all. `explicit-v6`'s server is bound to a
    literal `::1` and `no4/explicit-v6/started` is recorded true on the 2016
    baseline itself, so the destination exists on every tree in the line. The
    pair then reads the same at step 3 and at step 4 and differently on the
    pin, which is a regression cell in the role rule's own terms. It costs no
    port: `_explicit_v6` already owns `ctx.port(11)`.

    On this host the walk is never made to cross a refusal, because glibc sorts
    `::1` to the head of every name here that has one -- `deep-tail.test` is
    built to be walked but both of its reachable addresses are IPv4, which the
    filter refuses outright. So what `multi-*` records is narrower than the
    name suggests: the client reached a destination whose address list is
    mostly unusable, and the unusable entries did not take the connection with
    them.
    """
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", "v4only.test", "-p", port, "PING"],
                           timeout=30, stdin=b"", launcher=launcher)
    ctx.emit("no4/connect/v4only-rc", rc, adv=True)
    # Not the text: the message a client prints for a destination it could not
    # reach is wording two later steps may still change. That it printed one
    # is the fact being kept.
    ctx.emit("no4/connect/v4only-said-something", bool(out), adv=True)
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", "multi.test", "-p", port, "PING"],
                           timeout=30, stdin=b"", launcher=launcher)
    ctx.emit("no4/connect/multi-rc", rc, adv=True)
    ctx.emit_text("no4/connect/multi-out", out, adv=True)


def run(ctx):
    work = ctx.workspace.sub("no4")
    launcher = list(no4.launcher(work))

    # The instrument, before any finding it produces.
    known = no4.check(work)
    for key, value in sorted(known.items()):
        # The unfiltered control is the adversarial one: it is what separates a
        # filter that works from a host that had no IPv4 to begin with, and a
        # tree cannot make it true by being correct.
        ctx.emit("no4/instrument/%s" % key, value,
                 adv=(key == "unfiltered/AF_INET"))

    # Every observation below is taken through the filter, and `no4.launcher`
    # returns an empty launcher when it could not build one -- so on a host
    # with no compiler this scenario would run unfiltered end to end and a tree
    # that still needs IPv4 would answer every cell exactly like a finished
    # one. A cell reporting that would not help: `runner.py`'s `cmd_replay`
    # scores the keys of the *expectation* file against the replay, so an
    # instrument cell only says whether the two runs agreed about the
    # instrument, and a candidate measured without a filter against a recording
    # made without one agrees. Raising is the signal. `run_one.py`'s `main`
    # catches `BaseException`, writes `status: "error"` with the traceback,
    # keeps the workspace and exits non-zero, which is what an operator sees.
    if (known.get("launcher-available") is not True
            or known.get("filtered/AF_INET") != "EAFNOSUPPORT"
            or known.get("unfiltered/AF_INET") != "ok"):
        raise RuntimeError(
            "no-ipv4 filter is not the known answer: %r" % (known,))

    _shipped(ctx, launcher)
    _explicit_v6(ctx, launcher)
    _replication(ctx, launcher)
    _cluster(ctx, launcher)
    _sentinel(ctx, launcher)

    ctx.note("launcher: %s" % (no4.ensure(work)[1],))
