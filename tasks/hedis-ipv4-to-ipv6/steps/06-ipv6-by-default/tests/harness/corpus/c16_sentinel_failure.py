"""Sentinel under an actual failure: an address that is learned, not commanded.

`c12_sentinel` drives the life cycle with `SENTINEL failover`, which promotes on
demand and never lets the failure-detection machinery run at all. This scenario
takes the other half. The master is stopped; the two sentinels have to notice it
is gone, agree with each other that it is gone, elect a leader between
themselves, promote the slave, and publish the new address -- and then the old
master comes back and has to be told where it now belongs.

Each of those steps moves an address between processes by a different route, and
two of them hand it to a program *outside* redis. The notification script and
the client-reconfig script are this project's published interface for "the
address changed": whatever a migration does to address text, it shows up in
their argv, in a form no other observation here can see. That argv is recorded.

Both sentinels write into one shared script log and the union is scored, because
which of the two wins the leader election is not something this scenario -- or
any caller -- gets to decide. For the same reason the scored view of the log is a
fixed vocabulary of event types, each recorded as "did this happen", rather than
a transcript: a transcript would also record how many rounds the election took.

The `probe` master near the end exists to exercise `SENTINEL SET`, which cannot
be pointed at `mymaster` without changing the failure the scenario is about. It
is monitored by one sentinel only, at a down-after far longer than this scenario
lives, so it never becomes an event.
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

PORTS = 6
FAMILY = "behaviour"

MASTER = "downmaster"

# Event types a completed two-sentinel failover produces, across both sentinels.
# Deliberately not the whole warning-level vocabulary: `+tilt`/`-tilt` fire when
# the event loop is starved, `-failover-abort-*` when a round is lost, and
# `+failover-end-for-timeout` when one runs long. All three are properties of how
# busy the machine was, so scoring them would score the machine.
NOTIFY_EVENTS = (
    "+monitor", "+sdown", "-sdown", "+odown", "-odown", "+new-epoch",
    "+try-failover", "+vote-for-leader", "+elected-leader",
    "+failover-state-select-slave", "+selected-slave", "+promoted-slave",
    "+failover-state-reconf-slaves", "+failover-end", "+switch-master",
    "+config-update-from",
)

# The ways this tree gives up on a failover, as it spells them. Named rather
# than matched by prefix so that what an observation records is one of a fixed
# set of words and not whatever a future event happens to be called.
FAILOVER_ABORTS = (
    "-failover-abort-not-elected", "-failover-abort-no-good-slave",
    "-failover-abort-slave-timeout",
)

# `SENTINEL SET` parameters this tree accepts, with a value that is safe to leave
# set on a master nothing is watching.
SET_PARAMS = (
    ("down-after-milliseconds", "45000"),
    ("failover-timeout", "90000"),
    ("parallel-syncs", "3"),
    ("quorum", "2"),
    ("auth-pass", "hunter2"),
    ("notification-script", "/bin/true"),
    ("client-reconfig-script", "/bin/true"),
)

PROBE_FIELDS = ("name", "ip", "port", "quorum", "down-after-milliseconds",
                "failover-timeout", "parallel-syncs", "notification-script",
                "client-reconfig-script")

# An epoch is a counter over failover rounds, so it says how many rounds the
# election took rather than what the sentinels concluded.
EPOCH_TAIL = re.compile(r"(?m)(#(?:quorum|voted|epoch) \S*)\s*\d+(/\d+)?\s*$")
TRAILING_N = re.compile(r"(?m)\s\d+$")


def _script(path, log):
    """Write a script sentinel can hand an address to, and make it runnable.

    `sleep` is not decoration: `SENTINEL PENDING-SCRIPTS` can only show a job
    that is still there to be shown, and a script that does one `printf` is gone
    before any client could ask.
    """
    # Created runnable and the score uid's in one step, following nothing in a
    # directory that uid owns (`srv.write_new`).
    srv.write_new(path, "#!/bin/sh\nprintf '%s\\n' \"$*\" >>" + log
                  + "\nsleep 2\n", mode=0o755)


def _sentinel_directives(port, master_port, notify, reconf):
    return [
        "bind ::1",
        "port %d" % port,
        "dir .",
        "logfile sentinel.log",
        "daemonize no",
        "sentinel monitor %s ::1 %d 2" % (MASTER, master_port),
        "sentinel down-after-milliseconds %s 2000" % MASTER,
        "sentinel failover-timeout %s 20000" % MASTER,
        "sentinel parallel-syncs %s 1" % MASTER,
        "sentinel notification-script %s %s" % (MASTER, notify),
        "sentinel client-reconfig-script %s %s" % (MASTER, reconf),
    ]


def _kv(reply):
    """A flat [k, v, k, v, ...] reply as a dict of text."""
    if not reply or reply[0] != "array":
        return {}
    flat = [e[1] for e in reply[1]]
    out = {}
    for i in range(0, len(flat) - 1, 2):
        k = flat[i]
        v = flat[i + 1]
        if isinstance(k, (bytes, bytearray)):
            k = k.decode("utf-8", "replace")
        if isinstance(v, (bytes, bytearray)):
            v = v.decode("utf-8", "replace")
        out[k] = v
    return out


def _addr(c, name=MASTER):
    r = c.cmd("SENTINEL", "get-master-addr-by-name", name)
    if r[0] != "array" or not r[1]:
        return None
    return tuple(e[1].decode("utf-8", "replace") for e in r[1])


def _wait_addr(c, port, deadline=90.0):
    end = time.time() + deadline
    while time.time() < end:
        a = _addr(c)
        if a and a[1] == str(port):
            return True
        time.sleep(0.1)
    return False


def _wait_field(c, field, want, name=MASTER, deadline=90.0):
    end = time.time() + deadline
    while time.time() < end:
        if _kv(c.cmd("SENTINEL", "master", name)).get(field) == want:
            return True
        time.sleep(0.1)
    return False


def _fresh(server, section, field):
    """One INFO field, read on a connection opened for the purpose.

    Sentinel reconfigures an instance with `SLAVEOF`, `CONFIG REWRITE` and
    `CLIENT KILL TYPE normal` in one transaction, so every connection that was
    open at that moment is closed on purpose. Every read of an instance that
    sentinel may have touched goes through here.
    """
    try:
        with server.client() as c:
            return srv.info_field(c, section, field)
    except Exception:  # noqa: BLE001 - the close is the behaviour, not a fault
        return None


def _wait_fresh(server, section, field, want, deadline=90.0):
    end = time.time() + deadline
    while time.time() < end:
        if _fresh(server, section, field) == want:
            return True
        time.sleep(0.2)
    return False


def _read(path):
    data = srv.read_regular(path)  # no link followed; b"" if unreadable
    return b"" if data is None else data


def _lines(path):
    text = norm.normalize_text(_read(path))
    if isinstance(text, list):
        text = text[1]
    return [l.rstrip() for l in text.splitlines() if l.strip()]


def _wait_line(path, needle, deadline=30.0):
    end = time.time() + deadline
    while time.time() < end:
        if needle in _read(path):
            return True
        time.sleep(0.1)
    return False


def _wait_log(server, needle, deadline=30.0):
    end = time.time() + deadline
    while time.time() < end:
        if needle in server.log():
            return True
        time.sleep(0.1)
    return False


def _failover_outcome(servers, deadline=60.0):
    """In one word, what this tree says its own failover did.

    Read from the sentinels' own logs rather than from the notification
    script's: `sentinelEvent` writes the log line itself, in order, from one
    process, and only *queues* the script, so two events a microsecond apart
    reach the script's file in whichever order their two processes get to it.
    Both sentinels are read because only the elected leader runs the failover
    state machine and which of the two wins is the tree's business.

    `+failover-end` on its own does not answer the question. When a failover
    has run past its `failover-timeout`, `sentinelFailoverDetectEnd` forces
    `not_reconfigured` to zero and falls through to write `+failover-end` as
    well, so the whole of the difference between "reconfigured everything" and
    "stopped waiting" is the event written immediately before it. A failover
    abandoned earlier than that never reaches either, and names its reason.

    This is deliberately an event and not a clock. The elapsed time it reports
    on is sentinel's own, between two of its own state changes, and with one
    slave -- the one just promoted -- there is nothing left to reconfigure when
    it is measured. A busy host slows the harness watching the failover, which
    is not what is being asked about, and slows this by one cron tick.
    """
    end = time.time() + deadline
    while True:
        logs = b"".join(s.log() for s in servers)
        if b"+failover-end-for-timeout" in logs:
            return "timed-out"
        if b"+failover-end " in logs:
            return "completed"
        if time.time() >= end:
            break
        time.sleep(0.1)
    for ev in FAILOVER_ABORTS:
        if ev.encode() in logs:
            return ev
    return "none"


def _wait_flag(c, needle, name=MASTER, deadline=30.0):
    """Wait until `needle` appears in an instance's flag list.

    A substring rather than the whole field: the flags are a set rendered in a
    fixed order, and which *other* flags are up at the moment the poll lands is
    a matter of how far the failover has got.
    """
    end = time.time() + deadline
    while time.time() < end:
        if needle in (_kv(c.cmd("SENTINEL", "master", name)).get("flags") or ""):
            return True
        time.sleep(0.05)
    return False


def _log_events(servers, prefixes):
    """Log lines announcing one of `prefixes`, unioned over `servers`, sorted.

    The script log only holds warning-level events. These are the notice-level
    ones -- `+convert-to-slave`, `+slave` -- which is where the address of an
    instance sentinel is *reconfiguring* is written down.

    A list of servers is unioned for the same reason the script log is: only one
    sentinel reconfigures an instance, whichever of them reaches it first, and
    that is the election's business rather than the tree's.
    """
    if not isinstance(servers, (list, tuple)):
        servers = [servers]
    out = set()
    for server in servers:
        text = norm.normalize_text(server.log())
        if isinstance(text, list):
            text = text[1]
        for line in text.splitlines():
            for pre in prefixes:
                i = line.find(pre)
                if i != -1:
                    out.add(TRAILING_N.sub("", line[i:].rstrip()))
    return sorted(out)


def _info_sections(c):
    """The section headers `INFO` on a sentinel answers with."""
    blob = c.cmd("INFO")[1]
    if not isinstance(blob, (bytes, bytearray)):
        return []
    return [l.strip() for l in blob.decode("utf-8", "replace").splitlines()
            if l.startswith("#")]


def _info_masters(c, section="default"):
    """The `masterN:...` lines out of a sentinel's INFO, normalised and sorted.

    This is the one line in `INFO` that carries an address, and it is formatted
    by its own code -- `sentinelInfoCommand`, not the instance formatter used by
    `SENTINEL master`.
    """
    r = c.cmd("INFO", section)
    blob = r[1]
    if not isinstance(blob, (bytes, bytearray)):
        return []
    text = norm.normalize_text(blob)
    if isinstance(text, list):
        text = text[1]
    return sorted(l.strip() for l in text.splitlines()
                  if re.match(r"^master\d+:", l))


def _cached_info_addresses(reply):
    """The `slaveN:ip=...` and `master_host:` lines out of SENTINEL INFO-CACHE.

    The cache holds each instance's whole `INFO` output, most of which is a
    measurement. The address lines inside it are not, and they are a rendering
    no other command in this suite reaches.
    """
    out = set()
    stack = [reply]
    while stack:
        node = stack.pop()
        if isinstance(node, tuple) and len(node) == 2:
            kind, val = node
            if kind == "array" and isinstance(val, list):
                stack.extend(val)
            elif isinstance(val, (bytes, bytearray)):
                text = norm.normalize_text(val)
                if isinstance(text, list):
                    text = text[1]
                for line in text.splitlines():
                    if re.match(r"^(slave\d+:ip=|master_host:|master_port:)",
                                line):
                        out.add(line.strip())
    return sorted(out)


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]
    d = ctx.workspace.sub("scripts")
    notify_log = os.path.join(d, "notify.log")
    reconf_log = os.path.join(d, "reconf.log")
    notify = os.path.join(d, "notify.sh")
    reconf = os.path.join(d, "reconf.sh")
    # The scripts run as the server's own user, and append to files it has to be
    # able to create: `_script` hands each script over and `sub` the directory,
    # with no chown that could follow a link.
    _script(notify, notify_log)
    _script(reconf, reconf_log)

    master = ctx.server("master", p[0], ctxmod.default_directives(p[0], "::1"))
    master.start().wait_ready()
    slave = ctx.server("slave", p[1],
                       ctxmod.default_directives(p[1], "::1", extra=[
                           "slaveof ::1 %d" % p[0]]))
    slave.start().wait_ready()
    mc = master.client()
    slc = slave.client()
    mc.cmd("SET", "before-failover", "v")
    ctx.emit("failover/pre/link-up",
             srv.wait_info(slc, "replication", "master_link_status", "up"))
    # `failover/revived/has-old-key` below asks the old master, restarted from
    # this directory, whether it still has this key -- which is a question about
    # what is on disk, and nothing so far has put it there. `save ""` is set, so
    # the only writer of `dump.rdb` is the replication sync's own fork, and that
    # fork may have snapshotted the dataset before the SET above. Hence an
    # explicit synchronous save, after the sync has finished so that it is the
    # last writer of the file the revived instance will load. Its reply is
    # scored too: a save that failed would leave the question below answering
    # about the instrument instead of about the server.
    ctx.emit_reply("failover/pre/save", mc.cmd("SAVE"))

    sent = []
    for i in (2, 3):
        s = ctx.server("sentinel%d" % i, p[i],
                       _sentinel_directives(p[i], p[0], notify, reconf),
                       binary="redis-sentinel", logfile="sentinel.log")
        s.start().wait_ready()
        sent.append(s)
    sa, sb = [s.client() for s in sent]

    ctx.emit("failover/pre/discovered-slave", _wait_field(sa, "num-slaves", "1"))
    ctx.emit("failover/pre/discovered-peer",
             _wait_field(sa, "num-other-sentinels", "1"))
    ctx.emit("failover/pre/peer-discovered-peer",
             _wait_field(sb, "num-other-sentinels", "1"))
    ctx.emit("failover/pre/addr", list(_addr(sa) or []))

    # -- what a sentinel says about itself -----------------------------------
    # `INFO` and `ROLE` on a sentinel are answered by sentinel's own code, not
    # by the server's, and the first of them prints the monitored address.
    ctx.emit("failover/info/sections", _info_sections(sa))
    ctx.emit("failover/info/masters", _info_masters(sa))
    ctx.emit("failover/info/masters-all", _info_masters(sa, "all"))
    ctx.emit("failover/info/masters-sentinel", _info_masters(sa, "sentinel"))
    ctx.emit("failover/info/replication-section-absent",
             "# Replication" not in _info_sections(sa), adv=True)
    ctx.emit_reply("failover/role", sa.cmd("ROLE"))
    ctx.emit("failover/info-cache/addresses",
             _cached_info_addresses(sa.cmd("SENTINEL", "info-cache", MASTER)))
    ctx.emit_reply("failover/info-cache-unknown",
                   sa.cmd("SENTINEL", "info-cache", "nosuchmaster"), adv=True)
    # No flag: the loop does not run, so this clears the simulation flags and
    # answers +OK. `help` is not asked for -- this tree answers it with the flag
    # list *and* a +OK, two replies to one command, which would leave every
    # later observation on this connection reading the previous one's answer.
    ctx.emit_reply("failover/simulate-failure-none",
                   sa.cmd("SENTINEL", "simulate-failure"), adv=True)
    ctx.emit_reply("failover/simulate-failure-unknown",
                   sa.cmd("SENTINEL", "simulate-failure", "no-such-flag"),
                   adv=True)

    # -- the master stops answering -------------------------------------------
    mc.close()
    master.stop()
    # A script is queued from the event loop and reaped from it as well, so the
    # only way to see one is to look while it is there.
    seen, fields = False, []
    end = time.time() + 30.0
    while time.time() < end:
        r = sa.cmd("SENTINEL", "pending-scripts")
        if r[0] == "array" and r[1]:
            seen = True
            fields = sorted(_kv(r[1][0]))
            break
        time.sleep(0.02)
    ctx.emit("failover/pending-scripts/seen", seen)
    ctx.emit("failover/pending-scripts/entry-fields", fields)

    ctx.emit("failover/detected-sdown", _wait_flag(sa, "s_down"), adv=True)
    ctx.emit("failover/promoted", _wait_addr(sa, p[1]))
    ctx.emit("failover/peer-agrees", _wait_addr(sb, p[1]))
    ctx.emit("failover/promoted-addr", list(_addr(sa) or []))
    ctx.emit("failover/flags-steady", _wait_field(sa, "flags", "master"))
    ctx.emit("failover/slave-became-master",
             _wait_fresh(slave, "replication", "role", "master", 60.0))
    # Whether this took three seconds or thirty is the machine's business. What
    # the failover did is the tree's, and the tree writes it down: it finished
    # because there was nothing left to reconfigure, or it stopped waiting for
    # something that never happened, or it was given up on for a named reason.
    ctx.emit("failover/outcome", _failover_outcome(sent), adv=True)

    # -- what the scripts were handed ----------------------------------------
    _wait_line(notify_log, b"+switch-master")
    # `start` and not `end`: this tree has two call sites for the client-reconfig
    # script and both pass `start`, so a wait for `end` is a wait for something
    # that is never written.
    _wait_line(reconf_log, b" start ")
    # Give the last few events, which arrive after the switch, their two-second
    # script run before the log is read.
    time.sleep(3.0)
    notify_lines = _lines(notify_log)
    types = sorted({l.split(" ", 1)[0] for l in notify_lines})
    for ev in NOTIFY_EVENTS:
        ctx.emit("failover/notify/saw%s" % ev, ev in types)
    ctx.emit("failover/notify/event-types", types, adv=True)
    # The address text, with the counters that ride alongside it taken out.
    ctx.emit("failover/notify/messages",
             sorted({TRAILING_N.sub("", EPOCH_TAIL.sub(r"\1<N>", l))
                     for l in notify_lines if l.startswith(("+sdown", "+odown",
                                                            "+switch-master",
                                                            "+promoted-slave",
                                                            "+selected-slave",
                                                            "-sdown"))}))
    # The client-reconfig script's argv is `<master> <role> <state> <from-ip>
    # <from-port> <to-ip> <to-port>`: two addresses, formatted for a consumer
    # that is not redis. Which sentinel called itself the leader is masked; the
    # addresses are not.
    ctx.emit("failover/reconf/lines",
             sorted({re.sub(r"^(\S+) (leader|observer) ", r"\1 <ROLE> ", l)
                     for l in _lines(reconf_log)}))

    # -- the old master comes back --------------------------------------------
    revived = ctx.server("master-revived", p[0],
                         ctxmod.default_directives(p[0], "::1",
                                                   extra=["logfile revived.log"]),
                         datadir=master.dir, logfile="revived.log")
    revived.start().wait_ready()
    ctx.emit("failover/revived/sdown-cleared",
             _wait_log(sent[0], b"-sdown slave"))
    with revived.client() as rc:
        ctx.emit_reply("failover/revived/has-old-key",
                       rc.cmd("EXISTS", "before-failover"))
        # An instance that was down across the failover is not re-pointed by
        # sentinel on its own -- it comes back believing it is still a master and
        # stays one -- so the operator points it at the address sentinel
        # published. That is also what makes the conversion below reachable:
        # sentinel converts a *slave* that starts calling itself a master, and
        # this is how it becomes a slave sentinel knows about.
        ctx.emit_reply("failover/revived/slaveof",
                       rc.cmd("SLAVEOF", "::1", str(p[1])))
    ctx.emit("failover/revived/link-up",
             _wait_fresh(revived, "replication", "master_link_status", "up",
                         30.0))
    ctx.emit("failover/revived/master-host",
             _fresh(revived, "replication", "master_host"))
    ctx.emit("failover/revived/master-port",
             _fresh(revived, "replication", "master_port"))
    ctx.emit("failover/revived/slaveof-log",
             _log_events(revived, ("SLAVE OF", "Connecting to MASTER",
                                   "MASTER <-> SLAVE sync: Finished")),
             adv=True)
    ctx.emit("failover/slaves-again", _wait_field(sa, "num-slaves", "1",
                                                  deadline=30.0))
    ctx.emit("failover/sdown-cleared", _wait_field(sa, "flags", "master",
                                                   deadline=30.0))

    # -- and then stops being one --------------------------------------------
    # The conversion is guarded on the instance having reported one role, and
    # having been up, for `SENTINEL_PUBLISH_PERIOD*4` = 8s. Waiting past that
    # before flipping it is what makes the result a fact rather than a race.
    time.sleep(9.0)
    with revived.client() as rc:
        ctx.emit_reply("failover/revived/slaveof-no-one",
                       rc.cmd("SLAVEOF", "NO", "ONE"))
    # A known slave that calls itself a master is pointed back at the master
    # sentinel holds the address of -- over a connection sentinel opens itself,
    # to an address it wrote down during the failover.
    ctx.emit("failover/revived/converted-back",
             _wait_fresh(revived, "replication", "role", "slave", 45.0))
    ctx.emit("failover/revived/converted-master-host",
             _fresh(revived, "replication", "master_host"))
    ctx.emit("failover/revived/converted-master-port",
             _fresh(revived, "replication", "master_port"))
    # `+reboot` is not asked for: it needs an INFO refresh to land on a run-id
    # that changed, and for an instance that was unreachable when its entry was
    # created that is not something this scenario can bound.
    ctx.emit("failover/reconfig-log",
             _log_events(sent, ("+convert-to-slave", "-sdown slave",
                                "+slave slave")), adv=True)

    # -- SENTINEL SET, on a master this scenario is not about ------------------
    ctx.emit_reply("failover/monitor-probe",
                   sa.cmd("SENTINEL", "monitor", "probe", "::1", p[4], 2))
    # Before anything else: the default down-after would make this a live master
    # in thirty seconds, and it is meant to be inert.
    ctx.emit_reply("failover/set-quiet",
                   sa.cmd("SENTINEL", "set", "probe",
                          "down-after-milliseconds", "600000"))
    for param, value in SET_PARAMS:
        ctx.emit_reply("failover/set-%s" % param,
                       sa.cmd("SENTINEL", "set", "probe", param, value))
    probe = _kv(sa.cmd("SENTINEL", "master", "probe"))
    for f in PROBE_FIELDS:
        ctx.emit("failover/probe/%s" % f, probe.get(f))
    ctx.emit_reply("failover/set-unknown-param",
                   sa.cmd("SENTINEL", "set", "probe", "no-such-option", "1"),
                   adv=True)
    ctx.emit_reply("failover/set-bad-value",
                   sa.cmd("SENTINEL", "set", "probe",
                          "down-after-milliseconds", "notanumber"), adv=True)
    ctx.emit_reply("failover/set-odd-arity",
                   sa.cmd("SENTINEL", "set", "probe", "quorum"), adv=True)
    ctx.emit_reply("failover/set-unknown-master",
                   sa.cmd("SENTINEL", "set", "nosuchmaster", "quorum", "2"),
                   adv=True)
    ctx.emit_reply("failover/set-missing-script",
                   sa.cmd("SENTINEL", "set", "probe", "notification-script",
                          "/no/such/script"), adv=True)
    ctx.emit_reply("failover/set-clear-script",
                   sa.cmd("SENTINEL", "set", "probe", "notification-script",
                          ""), adv=True)

    # -- RESET, which rebuilds a master's tables from its address --------------
    ctx.emit_reply("failover/reset-nomatch",
                   sa.cmd("SENTINEL", "reset", "no-such-*"), adv=True)
    ctx.emit_reply("failover/reset-glob", sa.cmd("SENTINEL", "reset", "prob*"))
    ctx.emit("failover/probe-addr-after-reset",
             list(_addr(sa, "probe") or []))
    ctx.emit_reply("failover/reset-all", sa.cmd("SENTINEL", "reset", "*"))
    ctx.emit("failover/addr-survives-reset", list(_addr(sa) or []))
    ctx.emit("failover/rediscovers-slave-after-reset",
             _wait_field(sa, "num-slaves", "1", deadline=30.0))
    ctx.emit("failover/rediscovers-peer-after-reset",
             _wait_field(sa, "num-other-sentinels", "1", deadline=30.0))
    ctx.emit_reply("failover/remove-probe",
                   sa.cmd("SENTINEL", "remove", "probe"))

    revived.stop()
    slc.close()
    slave.stop()
    sa.close()
    sb.close()
    for s in sent:
        s.stop()
