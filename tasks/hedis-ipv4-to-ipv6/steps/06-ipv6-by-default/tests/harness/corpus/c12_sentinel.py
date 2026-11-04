"""Sentinel: addresses that are discovered, gossiped, persisted and rewritten.

Sentinel learns a slave's address from the master's `INFO`, learns another
sentinel's address from a hello message, prints both into its event log, writes
both back into its own configuration file, and -- during a failover -- hands one
of them to `SLAVEOF`. Every one of those is a separate piece of address text
produced by a separate piece of code, so the whole life cycle is driven here:
monitor, discover, fail over, restart from the rewritten file.
"""

import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import srv  # noqa: E402

PORTS = 8
FAMILY = "behaviour"

MASTER = "mymaster"

# Fields of `SENTINEL master` that are settings or identities rather than
# measurements. Everything left out -- info-refresh, last-ping-reply,
# role-reported-time, the various *-time counters -- is a clock reading.
MASTER_FIELDS = ("name", "ip", "port", "flags", "num-slaves",
                 "num-other-sentinels", "quorum", "down-after-milliseconds",
                 "failover-timeout", "parallel-syncs", "role-reported")

SLAVE_FIELDS = ("name", "ip", "port", "flags", "master-host", "master-port",
                "master-link-status", "slave-priority", "role-reported")

SENTINEL_FIELDS = ("name", "ip", "port", "flags")

# Event lines whose text is an address rendering. Each is collected separately
# so a failure names the code path that produced it.
LOG_EVENTS = ("+monitor", "+slave ", "+sentinel ", "+switch-master")


def _redis_directives(port, extra=()):
    return ctxmod.default_directives(port, "::1", extra=extra)


def _sentinel_directives(port, master_port):
    return [
        "bind ::1",
        "port %d" % port,
        "dir .",
        "logfile sentinel.log",
        "daemonize no",
        "sentinel monitor %s ::1 %d 2" % (MASTER, master_port),
        "sentinel down-after-milliseconds %s 2000" % MASTER,
        "sentinel failover-timeout %s 10000" % MASTER,
        "sentinel parallel-syncs %s 1" % MASTER,
    ]


def _kv(reply):
    """A sentinel flat-map reply (k1, v1, k2, v2, ...) as a normalised dict."""
    if reply[0] != "array" or not reply[1]:
        return {}
    items = reply[1]
    out = {}
    for i in range(0, len(items) - 1, 2):
        key = items[i][1]
        val = items[i + 1][1]
        if not isinstance(key, (bytes, bytearray)):
            continue
        out[key.decode("utf-8", "replace")] = (
            norm.normalize_text(val) if isinstance(val, (bytes, bytearray))
            else val)
    return out


def _maps(reply):
    """An array-of-flat-maps reply, sorted by the instance's port."""
    if reply[0] != "array" or not reply[1]:
        return []
    return sorted((_kv(e) for e in reply[1]),
                  key=lambda m: int(m.get("port") or 0))


def _emit_map(ctx, label, m, fields, adv=False):
    for f in fields:
        ctx.emit("sentinel/%s/%s" % (label, f), m.get(f), adv=adv)


def _emit_maps(ctx, label, maps, fields, adv=False):
    ctx.emit("sentinel/%s/count" % label, len(maps), adv=adv)
    for i, m in enumerate(maps):
        _emit_map(ctx, "%s-%d" % (label, i), m, fields, adv=adv)


def _master(c, name=MASTER):
    return _kv(c.cmd("SENTINEL", "master", name))


def _addr(c, name=MASTER):
    """(ip, port) from get-master-addr-by-name, or None if unknown."""
    r = c.cmd("SENTINEL", "get-master-addr-by-name", name)
    if r[0] != "array" or not r[1]:
        return None
    return tuple(x[1].decode("utf-8", "replace") for x in r[1])


def _wait_master_field(c, field, want, deadline=60.0):
    end = time.time() + deadline
    while time.time() < end:
        if _master(c).get(field) == want:
            return True
        time.sleep(0.2)
    return False


def _wait_addr_port(c, port, deadline=60.0):
    end = time.time() + deadline
    while time.time() < end:
        a = _addr(c)
        if a and a[1] == str(port):
            return True
        time.sleep(0.2)
    return False


def _wait_flags(c, sub, want, deadline=60.0):
    """Every instance sentinel lists under `sub` is in its steady flag state.

    A `,disconnected` suffix means sentinel's link to that instance happens to
    be down at this instant -- a reconnect timer, not a rendering. Waiting for
    the steady state keeps `flags` scored without scoring the timer.
    """
    end = time.time() + deadline
    while time.time() < end:
        maps = _maps(c.cmd("SENTINEL", sub, MASTER))
        if maps and all(m.get("flags") == want for m in maps):
            return True
        time.sleep(0.2)
    return False


def _wait_slaves_field(c, field, want, deadline=60.0):
    """Every listed slave reports `field` as `want`."""
    end = time.time() + deadline
    while time.time() < end:
        maps = _maps(c.cmd("SENTINEL", "slaves", MASTER))
        if maps and all(m.get(field) == want for m in maps):
            return True
        time.sleep(0.2)
    return False


def _wait_slave_masters(c, deadline=60.0):
    """Every listed slave has reported which master it follows."""
    end = time.time() + deadline
    while time.time() < end:
        maps = _maps(c.cmd("SENTINEL", "slaves", MASTER))
        if maps and all(m.get("master-host") not in (None, "?") for m in maps):
            return True
        time.sleep(0.2)
    return False


def _wait_role(server, want, deadline=90.0):
    """Poll an instance's role on a fresh connection each time.

    Sentinel reconfigures an instance with `SLAVEOF`, `CONFIG REWRITE` and
    `CLIENT KILL TYPE normal` in one transaction, so every connection open at
    that moment is closed on purpose. Reconnecting is the answer; treating the
    close as an error would turn correct behaviour into a crash.
    """
    end = time.time() + deadline
    while time.time() < end:
        try:
            with server.client() as c:
                if srv.info_field(c, "replication", "role") == want:
                    return True
        except Exception:
            pass
        time.sleep(0.2)
    return False


def _log_lines(server, needles):
    """A server's own log lines carrying one of `needles`, sorted.

    Sorted because two instances re-attaching to a new master write their lines
    in whichever order the box scheduled them; which lines exist is the
    observation, the interleaving is not.
    """
    return sorted(l for l in norm.normalize_text(server.log()).splitlines()
                  if any(n in l for n in needles))


def _events(server):
    """The whitelisted event lines from a sentinel's log, grouped by event.

    A sentinel log also carries `+sdown`/`-sdown`, vote and epoch chatter whose
    presence depends on how the box was scheduled; those are left out rather
    than recorded and then hoped about.
    """
    lines = norm.normalize_text(server.log()).splitlines()
    out = {}
    for ev in LOG_EVENTS:
        out[ev.strip()] = sorted(
            l.split(" ", 1)[1] if l.startswith("<LOGSTAMP>") else l
            for l in lines if ev in l)
    return out


def _emit_events(ctx, label, server, adv=True):
    ev = _events(server)
    for name in sorted(ev):
        ctx.emit("sentinel/%s/log-%s" % (label, name.lstrip("+")), ev[name],
                 adv=adv)


EPOCH_LINE = re.compile(r"^(sentinel (?:current|leader|config)-epoch\b.*?) "
                        r"\d+$")


def _conf_lines(path, keep=("sentinel ", "port ", "bind ")):
    """The sentinel's rewritten config, restricted to the lines it owns.

    The epoch lines keep their shape but lose their number: an epoch counts
    election rounds, and a re-vote would move it without anything about the
    addresses -- which is what this file is recorded for -- having changed.
    """
    try:
        with srv.open_regular(path) as f:  # no link followed
            blob = f.read()
    except OSError as e:
        return ["read-failed: %s" % e.__class__.__name__]
    out = []
    for line in norm.normalize_text(blob).splitlines():
        if not any(line.startswith(k) for k in keep):
            continue
        out.append(EPOCH_LINE.sub(r"\1 <N>", line))
    return sorted(out)


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]

    master = ctx.server("master", p[0], _redis_directives(p[0]))
    master.start().wait_ready()
    # Distinct slave priorities decide the promotion up front: sentinel sorts
    # candidates by priority before anything else, so the failover below picks
    # slave 1 every time instead of picking whichever slave happened to be a
    # few bytes ahead.
    slaves = []
    for i, priority in ((1, 10), (2, 100)):
        s = ctx.server("slave%d" % i, p[i],
                       _redis_directives(p[i], extra=[
                           "slaveof ::1 %d" % p[0],
                           "slave-priority %d" % priority]))
        s.start().wait_ready()
        slaves.append(s)

    mc = master.client()
    sc = [s.client() for s in slaves]
    mc.cmd("SELECT", 0)
    mc.cmd("MSET", "sk", "sv")
    for i, c in enumerate(sc):
        ctx.emit("sentinel/pre/link-up-slave%d" % (i + 1),
                 srv.wait_info(c, "replication", "master_link_status", "up"))
    ctx.emit("sentinel/pre/master-sees-slaves",
             srv.wait_info(mc, "replication", "connected_slaves", "2"))

    # -- two sentinels monitoring an IPv6 master ---------------------------
    sent = []
    for i in (3, 4):
        s = ctx.server("sentinel%d" % i, p[i], _sentinel_directives(p[i], p[0]),
                       binary="redis-sentinel", logfile="sentinel.log")
        s.start().wait_ready()
        sent.append(s)
    s1, s2 = [s.client() for s in sent]

    ctx.emit("sentinel/discovered-slaves", _wait_master_field(s1, "num-slaves",
                                                              "2"))
    ctx.emit("sentinel/discovered-sentinel",
             _wait_master_field(s1, "num-other-sentinels", "1"))
    ctx.emit("sentinel/peer-discovered-sentinel",
             _wait_master_field(s2, "num-other-sentinels", "1"))
    ctx.emit("sentinel/slaves-link-ok",
             _wait_slaves_field(s1, "master-link-status", "ok"))

    ctx.emit("sentinel/slaves-link-steady", _wait_flags(s1, "slaves", "slave"))
    ctx.emit("sentinel/sentinels-link-steady",
             _wait_flags(s1, "sentinels", "sentinel"))
    ctx.emit("sentinel/master-addr", list(_addr(s1) or []))
    ctx.emit("sentinel/master-addr-peer", list(_addr(s2) or []))
    _emit_map(ctx, "master", _master(s1), MASTER_FIELDS)
    ctx.emit("sentinel/master/field-names", sorted(_master(s1)))
    ctx.emit("sentinel/master/config-epoch", _master(s1).get("config-epoch"))
    _emit_maps(ctx, "slaves", _maps(s1.cmd("SENTINEL", "slaves", MASTER)),
               SLAVE_FIELDS)
    _emit_maps(ctx, "sentinels",
               _maps(s1.cmd("SENTINEL", "sentinels", MASTER)), SENTINEL_FIELDS)
    _emit_maps(ctx, "masters", _maps(s1.cmd("SENTINEL", "masters")),
               MASTER_FIELDS)

    # -- the queries a sentinel answers about an address --------------------
    ctx.emit_reply("sentinel/ckquorum", s1.cmd("SENTINEL", "ckquorum", MASTER))
    ctx.emit_reply("sentinel/ckquorum-unknown",
                   s1.cmd("SENTINEL", "ckquorum", "nosuchmaster"), adv=True)
    ctx.emit_reply("sentinel/addr-unknown",
                   s1.cmd("SENTINEL", "get-master-addr-by-name",
                          "nosuchmaster"), adv=True)
    ctx.emit_reply("sentinel/is-down-by-addr",
                   s1.cmd("SENTINEL", "is-master-down-by-addr", "::1", p[0],
                          0, "*"), adv=True)
    ctx.emit_reply("sentinel/is-down-by-bracketed-addr",
                   s1.cmd("SENTINEL", "is-master-down-by-addr", "[::1]", p[0],
                          0, "*"), adv=True)
    ctx.emit_reply("sentinel/is-down-by-wrong-addr",
                   s1.cmd("SENTINEL", "is-master-down-by-addr", "::1", p[5],
                          0, "*"), adv=True)
    ctx.emit_reply("sentinel/is-down-bad-port",
                   s1.cmd("SENTINEL", "is-master-down-by-addr", "::1",
                          "notaport", 0, "*"), adv=True)
    ctx.emit_reply("sentinel/slaves-unknown",
                   s1.cmd("SENTINEL", "slaves", "nosuchmaster"), adv=True)
    ctx.emit_reply("sentinel/bad-subcommand",
                   s1.cmd("SENTINEL", "nosuchsub"), adv=True)

    # -- what it writes to its own config file ------------------------------
    ctx.emit_reply("sentinel/flushconfig", s1.cmd("SENTINEL", "flushconfig"))
    ctx.emit("sentinel/config", _conf_lines(sent[0].conf))
    _emit_events(ctx, "before-failover", sent[0])

    # -- address forms handed to SENTINEL MONITOR ---------------------------
    # Driven on the second sentinel so the shared master's state, and the first
    # sentinel's log and config, stay exactly as recorded above.
    for label, host in (("v6-literal", "::1"),
                        ("bracketed", "[::1]"),
                        ("scoped", "::1%lo"),
                        ("v4-mapped", "::ffff:127.0.0.1"),
                        ("v4-literal", "127.0.0.1"),
                        ("unspecified", "::"),
                        ("expanded-loopback", "0:0:0:0:0:0:0:1"),
                        ("hostname", "localhost"),
                        ("unresolvable", "no-such-host.invalid")):
        ctx.emit_reply("sentinel/monitor-%s" % label,
                       s2.cmd("SENTINEL", "monitor", "probe", host, p[5], 1),
                       adv=True)
        ctx.emit("sentinel/monitor-%s-stored" % label,
                 (_master(s2, "probe") or {}).get("ip"), adv=True)
        ctx.emit_reply("sentinel/monitor-%s-removed" % label,
                       s2.cmd("SENTINEL", "remove", "probe"), adv=True)
    ctx.emit_reply("sentinel/monitor-bad-port",
                   s2.cmd("SENTINEL", "monitor", "probe", "::1", "notaport", 1),
                   adv=True)
    ctx.emit_reply("sentinel/monitor-bad-quorum",
                   s2.cmd("SENTINEL", "monitor", "probe", "::1", p[5], "x"),
                   adv=True)
    ctx.emit_reply("sentinel/monitor-arity",
                   s2.cmd("SENTINEL", "monitor", "probe", "::1"), adv=True)
    ctx.emit_reply("sentinel/remove-unknown",
                   s2.cmd("SENTINEL", "remove", "probe"), adv=True)

    # -- a forced failover: the address moves --------------------------------
    # `SENTINEL failover` promotes without waiting for a failure, so the
    # address change is driven by this scenario rather than by a timer.
    ctx.emit_reply("sentinel/failover", s1.cmd("SENTINEL", "failover", MASTER))
    ctx.emit_reply("sentinel/failover-while-in-progress",
                   s1.cmd("SENTINEL", "failover", MASTER), adv=True)
    promoted = None
    end = time.time() + 90.0
    while time.time() < end:
        a = _addr(s1)
        if a and a[1] != str(p[0]):
            promoted = a
            break
        time.sleep(0.2)
    ctx.emit("sentinel/failover-happened", promoted is not None)
    ctx.emit("sentinel/promoted-addr", list(promoted or []))
    ctx.emit("sentinel/promoted-is-highest-priority-slave",
             promoted == ("::1", str(p[1])))
    ctx.emit("sentinel/peer-agrees-on-new-master",
             _wait_addr_port(s2, promoted[1]) if promoted else False)
    # The reconfiguration closes this connection on purpose (see `_wait_role`),
    # so the old master is re-examined over a new one.
    mc.close()
    ctx.emit("sentinel/old-master-demoted", _wait_role(master, "slave"))
    mc = master.client()
    ctx.emit("sentinel/old-master-points-at-new",
             srv.info_field(mc, "replication", "master_host"))
    ctx.emit("sentinel/old-master-relinked",
             srv.wait_info(mc, "replication", "master_link_status", "up", 90.0))
    # The line `slaveofCommand` logs is the address text that sentinel actually
    # handed to the demoted master, rather than sentinel's own rendering of it.
    ctx.emit("sentinel/old-master-slaveof-log",
             _log_lines(master, ("SLAVE OF", "Connecting to MASTER")), adv=True)
    # Both surviving instances have to have re-attached before the promoted
    # master's log is read, or which of the two sync lines is already in it
    # depends on how the box scheduled them.
    sc[0].close()
    nc = slaves[0].client()
    ctx.emit("sentinel/new-master-has-two-slaves",
             srv.wait_info(nc, "replication", "connected_slaves", "2", 90.0))
    ctx.emit("sentinel/new-master-sync-log",
             _log_lines(slaves[0], ("asks for synchronization",
                                    "Synchronization with slave")), adv=True)
    ctx.emit("sentinel/new-master-slave-lines",
             sorted(l for l in
                    norm.normalize_text(
                        nc.cmd("INFO", "replication")[1]).splitlines()
                    if l.startswith("slave") and "ip=" in l), adv=True)
    nc.close()
    ctx.emit("sentinel/slaves-after-failover",
             _wait_master_field(s1, "num-slaves", "2"))
    # A slave sentinel has re-listed but not yet re-INFOed reports its master
    # as `?`. That is a refresh timer, not an address rendering, so the suite
    # waits it out instead of recording whichever side of it the run landed on.
    ctx.emit("sentinel/slave-masters-known", _wait_slave_masters(s1))
    ctx.emit("sentinel/new-master-ips",
             sorted({m.get("master-host") for m in
                     _maps(s1.cmd("SENTINEL", "slaves", MASTER))}))
    ctx.emit("sentinel/new-master-ports",
             sorted({m.get("master-port") for m in
                     _maps(s1.cmd("SENTINEL", "slaves", MASTER))}))
    ctx.emit("sentinel/master-flags-steady",
             _wait_master_field(s1, "flags", "master"))
    _emit_map(ctx, "master-after-failover", _master(s1), MASTER_FIELDS)
    _emit_events(ctx, "after-failover", sent[0])
    # The peer reaches the same conclusion over its own hello channel, and does
    # so at its own pace; its log is only comparable once it has got there.
    ctx.emit("sentinel/peer-slaves-after-failover",
             _wait_master_field(s2, "num-slaves", "2"))
    ctx.emit("sentinel/peer-slave-masters-known", _wait_slave_masters(s2))
    _emit_events(ctx, "peer-after-failover", sent[1])

    # -- restart from the file sentinel rewrote itself -----------------------
    s1.cmd("SENTINEL", "flushconfig")
    ctx.emit("sentinel/config-after-failover", _conf_lines(sent[0].conf))
    s1.close()
    sent[0].stop()
    revived = ctx.server("sentinel-revived", p[3], [],
                         binary="redis-sentinel", datadir=sent[0].dir,
                         logfile="revived.log", conf=sent[0].conf)
    revived.start().wait_ready()
    rc = revived.client()
    ctx.emit("sentinel/revived-addr-matches",
             _wait_addr_port(rc, promoted[1]) if promoted else False)
    ctx.emit("sentinel/revived-slaves", _wait_master_field(rc, "num-slaves",
                                                           "2"))
    ctx.emit("sentinel/revived-sentinels",
             _wait_master_field(rc, "num-other-sentinels", "1"))
    ctx.emit("sentinel/revived-flags-steady",
             _wait_master_field(rc, "flags", "master")
             and _wait_flags(rc, "slaves", "slave")
             and _wait_slaves_field(rc, "master-link-status", "ok"))
    ctx.emit("sentinel/revived-slave-masters-known", _wait_slave_masters(rc))
    _emit_map(ctx, "revived-master", _master(rc), MASTER_FIELDS)
    _emit_maps(ctx, "revived-slaves",
               _maps(rc.cmd("SENTINEL", "slaves", MASTER)), SLAVE_FIELDS,
               adv=True)

    rc.close()
    revived.stop()
    s2.close()
    sent[1].stop()
    for c in sc:
        c.close()
    for s in slaves:
        s.stop()
    mc.close()
    master.stop()
