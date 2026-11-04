"""Replication: the first place the server connects *out* to another server.

A slave resolves and dials its master, and the master then reports the slave's
address back through INFO and ROLE. Both directions are recorded.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import srv  # noqa: E402

PORTS = 4
FAMILY = "behaviour"

REPL_FIELDS = ("role", "connected_slaves", "master_host", "master_port",
               "master_link_status", "slave_read_only", "slave_repl_offset")

REPL_LOG_WORDS = ("master", "slave", "sync", "connect", "replic", "partial",
                  "backlog", "link")

# Startup warnings about the *host* kernel and limits, the same list
# c08_listeners and c11_cluster drop. "backlog" above is there for the
# replication backlog, and it also matches the host's "TCP backlog setting of
# 511 cannot be enforced because /proc/sys/net/core/somaxconn is set to the
# lower value of 128" -- a line a server prints on a kernel whose somaxconn
# default is 128 (before Linux 5.4) and never prints on one whose default is
# 4096. Measured: recording the untouched baseline with somaxconn=128 added it
# to repl/master-log, repl/slave-log and repl/slave-log-after-master-loss; with
# this filter the recording is identical to the shipped one on both kernels.
HOST_WARNINGS = ("transparent huge pages", "overcommit_memory",
                 "somaxconn", "tcp backlog setting of",
                 "maximum number of open files")


def _repl_log(server):
    return [line for line in norm.normalize_text(server.log()).splitlines()
            if any(w in line.lower() for w in REPL_LOG_WORDS)
            and not any(w in line.lower() for w in HOST_WARNINGS)]


def _repl(ctx, label, c, adv=False):
    """The stable part of INFO replication, plus the slave lines in full."""
    blob = c.cmd("INFO", "replication")[1]
    text = norm.normalize_text(blob)
    fields = {}
    slaves = []
    for line in text.splitlines():
        if line.startswith("slave") and "ip=" in line:
            slaves.append(line)
            continue
        if ":" in line and not line.startswith("#"):
            k, v = line.split(":", 1)
            if k in REPL_FIELDS:
                fields[k] = v
    for k in REPL_FIELDS:
        ctx.emit("repl/%s/%s" % (label, k), fields.get(k), adv=adv)
    ctx.emit("repl/%s/slave-lines" % label, sorted(slaves), adv=adv)


def _wait_link(c, deadline=40.0):
    return srv.wait_info(c, "replication", "master_link_status", "up", deadline)


def _wait_slaves(c, n, deadline=40.0):
    return srv.wait_info(c, "replication", "connected_slaves", str(n), deadline)


def _propagates(ctx, label, master, slave, key, value, deadline=20.0):
    master.cmd("SELECT", 0)
    slave.cmd("SELECT", 0)
    master.cmd("SET", key, value)
    end = time.time() + deadline
    got = None
    while time.time() < end:
        got = slave.cmd("GET", key)
        if got[0] == "bulk" and got[1] == value.encode():
            break
        time.sleep(0.05)
    ctx.emit_reply("repl/%s/propagated" % label, got)


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]

    master = ctx.server("master", p[0], ctxmod.default_directives(p[0], "::1"))
    master.start().wait_ready()
    slave = ctx.server("slave", p[1], ctxmod.default_directives(p[1], "::1"))
    slave.start().wait_ready()

    mc = master.client()
    sc = slave.client()
    mc.cmd("SELECT", 0)
    mc.cmd("FLUSHALL")
    mc.cmd("MSET", "a", "1", "b", "2")

    _repl(ctx, "master-standalone", mc)
    _repl(ctx, "slave-standalone", sc)
    ctx.emit_reply("repl/role-standalone", mc.cmd("ROLE"))

    # -- attach by literal v6 address --------------------------------------
    ctx.emit_reply("repl/slaveof-v6", sc.cmd("SLAVEOF", "::1", p[0]))
    ctx.emit("repl/link-up-v6", _wait_link(sc))
    ctx.emit("repl/master-sees-slave", _wait_slaves(mc, 1))
    _repl(ctx, "master-with-slave", mc)
    _repl(ctx, "slave-attached", sc)
    ctx.emit_reply("repl/role-master", mc.cmd("ROLE"), adv=True)
    ctx.emit_reply("repl/role-slave", sc.cmd("ROLE"), adv=True)
    ctx.emit_reply("repl/initial-sync-a", sc.cmd("GET", "a"))
    _propagates(ctx, "v6", mc, sc, "prop1", "value1")

    # A slave is read-only by default.
    ctx.emit_reply("repl/write-to-slave", sc.cmd("SET", "nope", "x"), adv=True)
    ctx.emit_reply("repl/read-from-slave", sc.cmd("GET", "b"))
    ctx.emit_reply("repl/wait-on-master", mc.cmd("WAIT", 1, 2000))
    ctx.emit_reply("repl/wait-too-many", mc.cmd("WAIT", 5, 200), adv=True)

    # The master's view of the slave's address.
    blob = mc.cmd("CLIENT", "LIST")[1]
    ctx.emit("repl/master-client-addrs", sorted(norm.addr_fields(blob)),
             adv=True)
    ctx.emit("repl/master-log", _repl_log(master), adv=True)
    ctx.emit("repl/slave-log", _repl_log(slave), adv=True)

    # -- detach -------------------------------------------------------------
    ctx.emit_reply("repl/slaveof-no-one", sc.cmd("SLAVEOF", "NO", "ONE"))
    ctx.emit("repl/detached-role", srv.info_field(sc, "replication", "role"))
    ctx.emit_reply("repl/write-after-detach", sc.cmd("SET", "nowok", "x"))
    ctx.emit("repl/master-lost-slave", _wait_slaves(mc, 0))

    # -- attach by hostname -------------------------------------------------
    ctx.emit_reply("repl/slaveof-hostname",
                   sc.cmd("SLAVEOF", "localhost", p[0]))
    ctx.emit("repl/link-up-hostname", _wait_link(sc, deadline=25.0), adv=True)
    ctx.emit("repl/master-host-after-hostname",
             srv.info_field(sc, "replication", "master_host"), adv=True)
    _repl(ctx, "slave-by-hostname", sc, adv=True)
    sc.cmd("SLAVEOF", "NO", "ONE")

    # -- address forms the slave should reject or normalise -----------------
    for label, host in (("bracketed", "[::1]"),
                        ("scoped", "::1%lo"),
                        ("v4-mapped", "::ffff:127.0.0.1"),
                        ("unresolvable", "no-such-host.invalid"),
                        # The master listens on ::1 only, so a v4 dial must
                        # fail rather than silently succeed.
                        ("v4-literal", "127.0.0.1")):
        ctx.emit_reply("repl/slaveof-%s-reply" % label,
                       sc.cmd("SLAVEOF", host, p[0]), adv=True)
        time.sleep(0.5)
        ctx.emit("repl/slaveof-%s-host" % label,
                 srv.info_field(sc, "replication", "master_host"), adv=True)
        ctx.emit("repl/slaveof-%s-link" % label,
                 srv.info_field(sc, "replication", "master_link_status"),
                 adv=True)
        sc.cmd("SLAVEOF", "NO", "ONE")
    ctx.emit_reply("repl/slaveof-bad-port",
                   sc.cmd("SLAVEOF", "::1", "notaport"), adv=True)
    ctx.emit_reply("repl/slaveof-arity", sc.cmd("SLAVEOF", "::1"), adv=True)

    # -- chained replication -------------------------------------------------
    sub = ctx.server("subslave", p[2], ctxmod.default_directives(p[2], "::1"))
    sub.start().wait_ready()
    subc = sub.client()
    sc.cmd("SLAVEOF", "::1", p[0])
    ctx.emit("repl/chain-mid-up", _wait_link(sc))
    subc.cmd("SLAVEOF", "::1", p[1])
    ctx.emit("repl/chain-tail-up", _wait_link(subc))
    _propagates(ctx, "chain", mc, subc, "chained", "through-two-hops")
    _repl(ctx, "chain-middle", sc, adv=True)
    _repl(ctx, "chain-tail", subc, adv=True)
    ctx.emit_reply("repl/chain-role-middle", sc.cmd("ROLE"), adv=True)
    subc.cmd("SLAVEOF", "NO", "ONE")
    subc.close()
    sub.stop()

    # -- a writable slave ----------------------------------------------------
    ctx.emit_reply("repl/set-writable",
                   sc.cmd("CONFIG", "SET", "slave-read-only", "no"))
    ctx.emit_reply("repl/write-to-writable-slave",
                   sc.cmd("SET", "local-only", "yes"), adv=True)
    ctx.emit_reply("repl/writable-slave-read",
                   sc.cmd("GET", "local-only"), adv=True)
    ctx.emit_reply("repl/master-does-not-have-it",
                   mc.cmd("GET", "local-only"), adv=True)
    sc.cmd("CONFIG", "SET", "slave-read-only", "yes")

    # -- partial resynchronisation ------------------------------------------
    before = srv.info_field(mc, "stats", "sync_partial_ok")
    ctx.emit("repl/partial-before-is-number",
             before is not None and before.isdigit())
    for line in mc.cmd("CLIENT", "LIST")[1].decode("utf-8", "replace").splitlines():
        if "flags=S" in line:
            for f in line.split(" "):
                if f.startswith("addr="):
                    mc.cmd("CLIENT", "KILL", f.split("=", 1)[1])
    ctx.emit("repl/relinked", _wait_link(sc, deadline=40.0), adv=True)
    after = srv.info_field(mc, "stats", "sync_partial_ok")
    ctx.emit("repl/partial-resync-happened",
             int(after or 0) > int(before or 0), adv=True)
    _propagates(ctx, "after-relink", mc, sc, "prop2", "value2")

    # -- diskless replication -------------------------------------------------
    ctx.emit_reply("repl/diskless-on",
                   mc.cmd("CONFIG", "SET", "repl-diskless-sync", "yes"))
    mc.cmd("CONFIG", "SET", "repl-diskless-sync-delay", 0)
    sc.cmd("SLAVEOF", "NO", "ONE")
    sc.cmd("SLAVEOF", "::1", p[0])
    ctx.emit("repl/diskless-link-up", _wait_link(sc, deadline=40.0), adv=True)
    _propagates(ctx, "diskless", mc, sc, "prop3", "value3")
    ctx.emit_reply("repl/diskless-off",
                   mc.cmd("CONFIG", "SET", "repl-diskless-sync", "no"))

    # -- the master goes away --------------------------------------------------
    mc.close()
    master.stop()
    time.sleep(1.5)
    ctx.emit("repl/link-down-status",
             srv.info_field(sc, "replication", "master_link_status"), adv=True)
    ctx.emit_reply("repl/stale-read-allowed", sc.cmd("GET", "a"), adv=True)
    ctx.emit_reply("repl/serve-stale-off",
                   sc.cmd("CONFIG", "SET", "slave-serve-stale-data", "no"),
                   adv=True)
    ctx.emit_reply("repl/stale-read-refused", sc.cmd("GET", "a"), adv=True)
    sc.cmd("CONFIG", "SET", "slave-serve-stale-data", "yes")
    ctx.emit("repl/slave-log-after-master-loss", _repl_log(slave), adv=True)
    sc.close()
    slave.stop()
