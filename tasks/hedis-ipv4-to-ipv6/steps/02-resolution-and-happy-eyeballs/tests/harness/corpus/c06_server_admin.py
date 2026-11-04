"""Introspection and administration: CONFIG, INFO, CLIENT, COMMAND, MONITOR.

This is where the server describes *itself* — including how it names the
address it is bound to and the address a peer connected from — so it is both
ordinary regression surface and the place an address-text regression shows up
first.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import cmdrun  # noqa: E402
from cmdrun import ADV, SORT, ADVSORT  # noqa: E402
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import resp  # noqa: E402

PORTS = 2
FAMILY = "behaviour"

# Parameters whose value is a stable property of the configuration, not of the
# machine or the clock.
CONFIG_PARAMS = [
    "appendonly", "appendfsync", "activerehashing", "bind", "daemonize",
    "databases", "hash-max-ziplist-entries", "hash-max-ziplist-value",
    "list-max-ziplist-size", "logfile", "maxclients", "maxmemory-policy",
    "maxmemory-samples", "port", "repl-diskless-sync", "repl-ping-slave-period",
    "save", "set-max-intset-entries", "slave-read-only", "slave-serve-stale-data",
    "stop-writes-on-bgsave-error", "tcp-backlog", "tcp-keepalive", "timeout",
    "unixsocket", "zset-max-ziplist-entries", "cluster-enabled",
    "cluster-node-timeout", "cluster-announce-ip", "slave-announce-ip",
    "slave-announce-port",
]


def _config_table():
    t = []
    a = t.append
    for p in CONFIG_PARAMS:
        a(("get-%s" % p, ["CONFIG", "GET", p]))
    a(("get-glob-maxmemory", ["CONFIG", "GET", "maxmemory-*"], SORT))
    a(("get-glob-tcp", ["CONFIG", "GET", "tcp-*"], SORT))
    a(("get-unknown", ["CONFIG", "GET", "no-such-parameter"], ADV))
    a(("get-empty-pattern", ["CONFIG", "GET", ""], ADV))
    a(("set-known", ["CONFIG", "SET", "maxmemory-samples", 7]))
    a(("get-after-set", ["CONFIG", "GET", "maxmemory-samples"]))
    a(("set-restore", ["CONFIG", "SET", "maxmemory-samples", 5]))
    a(("set-unknown", ["CONFIG", "SET", "no-such-parameter", 1], ADV))
    a(("set-bad-value", ["CONFIG", "SET", "maxmemory-samples", "lots"], ADV))
    a(("set-bad-policy", ["CONFIG", "SET", "maxmemory-policy", "nonsense"], ADV))
    a(("set-readonly-param", ["CONFIG", "SET", "databases", 4], ADV))
    a(("set-maxmemory-unit", ["CONFIG", "SET", "maxmemory", "100mb"]))
    a(("get-maxmemory-unit", ["CONFIG", "GET", "maxmemory"], ADV))
    a(("set-maxmemory-bad-unit", ["CONFIG", "SET", "maxmemory", "100zb"], ADV))
    a(("set-maxmemory-off", ["CONFIG", "SET", "maxmemory", 0]))
    a(("set-appendonly-yes", ["CONFIG", "SET", "appendonly", "yes"]))
    a(("set-appendonly-no", ["CONFIG", "SET", "appendonly", "no"]))
    a(("set-save-empty", ["CONFIG", "SET", "save", ""]))
    a(("get-save-empty", ["CONFIG", "GET", "save"]))
    a(("set-save-pairs", ["CONFIG", "SET", "save", "900 1 300 10"]))
    a(("get-save-pairs", ["CONFIG", "GET", "save"]))
    a(("set-save-odd", ["CONFIG", "SET", "save", "900"], ADV))
    a(("set-save-restore", ["CONFIG", "SET", "save", ""]))
    a(("resetstat", ["CONFIG", "RESETSTAT"]))
    a(("bad-subcommand", ["CONFIG", "NOSUCH"], ADV))
    return t


def _command_table():
    t = []
    a = t.append
    a(("count-kind", ["COMMAND", "COUNT"]))
    for name in ("get", "set", "eval", "cluster", "subscribe", "georadius",
                 "zadd", "sort", "migrate", "client", "debug"):
        a(("info-%s" % name, ["COMMAND", "INFO", name]))
    a(("info-unknown", ["COMMAND", "INFO", "nosuchcmd"], ADV))
    a(("info-mixed-case", ["COMMAND", "INFO", "GeT"], ADV))
    a(("getkeys-get", ["COMMAND", "GETKEYS", "GET", "k"]))
    a(("getkeys-mset", ["COMMAND", "GETKEYS", "MSET", "a", "1", "b", "2"]))
    a(("getkeys-eval", ["COMMAND", "GETKEYS", "EVAL", "x", 2, "a", "b"]))
    a(("getkeys-sort", ["COMMAND", "GETKEYS", "SORT", "k"], ADV))
    a(("getkeys-zadd", ["COMMAND", "GETKEYS", "ZADD", "z", 1, "m"], ADV))
    a(("getkeys-georadius",
       ["COMMAND", "GETKEYS", "GEORADIUS", "g", 0, 0, 1, "km"], ADV))
    a(("getkeys-no-keys", ["COMMAND", "GETKEYS", "PING"], ADV))
    a(("getkeys-unknown", ["COMMAND", "GETKEYS", "NOSUCHCMD", "k"], ADV))
    a(("getkeys-bad-arity", ["COMMAND", "GETKEYS", "GET"], ADV))
    return t


def _debug_table():
    t = []
    a = t.append
    a(("seed", ["SET", "dk", "dv"]))
    a(("object", ["DEBUG", "OBJECT", "dk"]))
    a(("object-missing", ["DEBUG", "OBJECT", "absent"], ADV))
    a(("set-active-expire", ["DEBUG", "SET-ACTIVE-EXPIRE", 1]))
    a(("sleep-zero", ["DEBUG", "SLEEP", 0]))
    a(("jmap", ["DEBUG", "JMAP"], ADV))
    a(("stringmatch-len", ["DEBUG", "NOSUCH-SUBCOMMAND"], ADV))
    a(("quicklist", ["DEBUG", "QUICKLIST-PACKED-THRESHOLD", "1k"], ADV))
    return t


def _info_sections(text):
    """Split INFO output into {section: [line, ...]}, normalised."""
    sections = {}
    cur = "(none)"
    for line in norm.normalize_text(text).splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            cur = line[1:].strip()
            sections.setdefault(cur, [])
            continue
        sections.setdefault(cur, []).append(line)
    return sections


def run(ctx):
    port = ctx.port(0)
    s = ctx.server("admin", port, ctxmod.default_directives(port))
    s.start().wait_ready()

    with s.client() as c:
        c.cmd("SELECT", 9)
        c.cmd("FLUSHALL")
        cmdrun.run_table(ctx, c, "cfg", _config_table())
        cmdrun.run_table(ctx, c, "cmd", _command_table())
        cmdrun.run_table(ctx, c, "dbg", _debug_table())

        # -- INFO ----------------------------------------------------------
        kind, blob = c.cmd("INFO")
        ctx.emit("info/default-kind", kind)
        secs = _info_sections(blob)
        ctx.emit("info/default-sections", sorted(secs))
        for name, lines in sorted(secs.items()):
            ctx.emit("info/section-fields/%s" % name,
                     sorted(l.split(":", 1)[0] for l in lines))
        for field in ("redis_mode", "role", "connected_slaves", "loading",
                      "aof_enabled", "cluster_enabled", "tcp_port",
                      "rdb_last_bgsave_status", "maxmemory_policy",
                      "mem_allocator"):
            val = None
            for lines in secs.values():
                for l in lines:
                    if l.startswith(field + ":"):
                        val = l.split(":", 1)[1]
            ctx.emit("info/field/%s" % field, val)
        ctx.emit_text("info/replication-section",
                      "\n".join(secs.get("Replication", [])))
        ctx.emit_text("info/section-server",
                      "\n".join(secs.get("Server", [])), adv=True)

        for arg in ("server", "clients", "memory", "persistence", "stats",
                    "replication", "cpu", "commandstats", "cluster",
                    "keyspace", "all", "everything", "nosuchsection"):
            k, blob = c.cmd("INFO", arg)
            got = _info_sections(blob) if k in ("bulk",) else {}
            ctx.emit("info/arg-%s-sections" % arg, sorted(got),
                     adv=arg in ("everything", "nosuchsection", "all"))

        # -- CLIENT --------------------------------------------------------
        ctx.emit_reply("client/getname-unset", c.cmd("CLIENT", "GETNAME"))
        ctx.emit_reply("client/setname", c.cmd("CLIENT", "SETNAME", "scorer"))
        ctx.emit_reply("client/getname", c.cmd("CLIENT", "GETNAME"))
        ctx.emit_reply("client/setname-with-space",
                       c.cmd("CLIENT", "SETNAME", "a b"), adv=True)
        ctx.emit_reply("client/setname-with-newline",
                       c.cmd("CLIENT", "SETNAME", "a\nb"), adv=True)
        ctx.emit_reply("client/setname-empty",
                       c.cmd("CLIENT", "SETNAME", ""), adv=True)
        ctx.emit_reply("client/bad-subcommand",
                       c.cmd("CLIENT", "NOSUCH"), adv=True)

        # CLIENT LIST: the field *names* and their order are the contract; the
        # addr/laddr values are compared after ephemeral-port masking only, so
        # the host text itself is part of the comparison.
        kind, blob = c.cmd("CLIENT", "LIST")
        lines = norm.normalize_text(blob).splitlines()
        ctx.emit("client/list-kind", kind)
        ctx.emit("client/list-field-order",
                 [f.split("=", 1)[0] for f in lines[0].split(" ")] if lines else [])
        mine = [l for l in lines if "name=scorer" in l]
        ctx.emit("client/list-self", sorted(mine))
        ctx.emit("client/list-addr-shape", sorted(norm.addr_fields(blob)))

        # The peer address the server reports must be accepted back by
        # CLIENT KILL: that round trip is exactly what bracket-less
        # host:port formatting breaks.
        victim = resp.Client("::1", port, timeout=5.0)
        victim.cmd("CLIENT", "SETNAME", "victim")
        raw = c.cmd("CLIENT", "LIST")[1].decode("utf-8", "backslashreplace")
        vaddr = None
        for l in raw.splitlines():
            if "name=victim" in l:
                for f in l.split(" "):
                    if f.startswith("addr="):
                        vaddr = f.split("=", 1)[1]
        ctx.emit("client/victim-addr-bracketed",
                 bool(vaddr and vaddr.startswith("[") and "]:" in vaddr),
                 adv=True)
        ctx.emit_reply("client/kill-by-addr",
                       c.cmd("CLIENT", "KILL", vaddr or "missing"), adv=True)
        try:
            victim.cmd("PING")
            killed = False
        except Exception:
            killed = True
        ctx.emit("client/kill-took-effect", killed, adv=True)
        victim.close()
        ctx.emit_reply("client/kill-unknown-addr",
                       c.cmd("CLIENT", "KILL", "[::1]:1"), adv=True)
        ctx.emit_reply("client/kill-bare-v6-addr",
                       c.cmd("CLIENT", "KILL", "::1:1"), adv=True)
        ctx.emit_reply("client/kill-filter-addr",
                       c.cmd("CLIENT", "KILL", "ADDR", "[::1]:1"), adv=True)
        ctx.emit_reply("client/kill-filter-unknown-option",
                       c.cmd("CLIENT", "KILL", "NOSUCH", "x"), adv=True)
        ctx.emit_reply("client/kill-filter-skipme",
                       c.cmd("CLIENT", "KILL", "ID", 999999), adv=True)

        # -- misc ----------------------------------------------------------
        k, v = c.cmd("TIME")
        ctx.emit("srv/time-shape", [k, len(v), sorted({x[0] for x in v})])
        k, v = c.cmd("LASTSAVE")
        ctx.emit("srv/lastsave-kind", k)
        ctx.emit_reply("srv/role", c.cmd("ROLE"))
        ctx.emit_reply("srv/slowlog-reset", c.cmd("SLOWLOG", "RESET"))
        ctx.emit_reply("srv/slowlog-len", c.cmd("SLOWLOG", "LEN"))
        ctx.emit_reply("srv/slowlog-get", c.cmd("SLOWLOG", "GET"))
        ctx.emit_reply("srv/slowlog-bad", c.cmd("SLOWLOG", "NOSUCH"), adv=True)
        ctx.emit_reply("srv/echo", c.cmd("ECHO", "hello"))
        ctx.emit_reply("srv/echo-binary", c.cmd("ECHO", b"\x00\xff"), adv=True)
        ctx.emit_reply("srv/ping-arg", c.cmd("PING", "payload"))
        ctx.emit_reply("srv/ping-two-args", c.cmd("PING", "a", "b"), adv=True)
        ctx.emit_reply("srv/swapdb", c.cmd("SWAPDB", 0, 1), adv=True)
        ctx.emit_reply("srv/command-docs", c.cmd("COMMAND", "DOCS"), adv=True)

        # -- MONITOR -------------------------------------------------------
        mon = resp.Client("::1", port, timeout=6.0)
        ctx.emit_reply("mon/start", mon.cmd("MONITOR"))
        c.cmd("SET", "monk", "monv")
        c.cmd("GET", "monk")
        feed = []
        try:
            for _ in range(2):
                feed.append(norm.normalize_text(mon.reader.read()[1]))
        except Exception as e:  # noqa: BLE001 - recorded outcome
            feed.append("read-failed: %s" % type(e).__name__)
        ctx.emit("mon/feed", feed, adv=True)
        mon.close()
        time.sleep(0.1)
        ctx.emit_reply("mon/after-close-still-serving", c.cmd("GET", "monk"))
    s.stop()
