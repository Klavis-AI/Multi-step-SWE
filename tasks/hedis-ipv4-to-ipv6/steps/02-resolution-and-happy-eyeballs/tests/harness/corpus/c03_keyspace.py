"""Keyspace, expiry, serialisation and SORT surface."""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import cmdrun  # noqa: E402
from cmdrun import ADV, SORT, ADVSORT  # noqa: E402
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402

PORTS = 1
FAMILY = "behaviour"

I64MAX = 9223372036854775807


def _keyspace():
    t = []
    a = t.append
    a(("seed", ["MSET", "k1", "1", "k2", "2", "k3", "3"]))
    a(("exists-one", ["EXISTS", "k1"]))
    a(("exists-many", ["EXISTS", "k1", "k2", "absent"]))
    a(("type-string", ["TYPE", "k1"]))
    a(("type-missing", ["TYPE", "absent"]))
    a(("rename", ["RENAME", "k3", "k3b"]))
    a(("rename-missing", ["RENAME", "absent", "x"], ADV))
    a(("rename-self", ["RENAME", "k1", "k1"], ADV))
    a(("renamenx-conflict", ["RENAMENX", "k1", "k2"]))
    a(("del", ["DEL", "k3b", "absent"]))
    a(("keys-glob", ["KEYS", "k*"], SORT))
    a(("keys-question", ["KEYS", "k?"], SORT))
    a(("keys-class", ["KEYS", "k[12]"], SORT))
    a(("keys-escape", ["KEYS", "k\\*"], ADVSORT))
    a(("keys-empty-pattern", ["KEYS", ""], ADV))
    a(("dbsize", ["DBSIZE"]))
    # -- expiry -----------------------------------------------------------
    a(("expire", ["EXPIRE", "k1", 1000]))
    a(("ttl", ["TTL", "k1"]))
    a(("pttl-rounded", ["PERSIST", "k1"]))
    a(("ttl-after-persist", ["TTL", "k1"]))
    a(("ttl-no-expiry", ["TTL", "k2"]))
    a(("ttl-missing", ["TTL", "absent"]))
    a(("expire-negative", ["SET", "ek", "v"]))
    a(("expire-negative-do", ["EXPIRE", "ek", -1], ADV))
    a(("exists-after-negative-expire", ["EXISTS", "ek"], ADV))
    a(("expireat-past", ["SET", "ek2", "v"]))
    a(("expireat-past-do", ["EXPIREAT", "ek2", 1], ADV))
    a(("exists-after-past-expireat", ["EXISTS", "ek2"], ADV))
    a(("expire-huge", ["SET", "ek3", "v"]))
    a(("expire-huge-do", ["EXPIRE", "ek3", I64MAX], ADV))
    a(("ttl-huge", ["TTL", "ek3"], ADV))
    a(("pexpire-overflow", ["PEXPIRE", "ek3", I64MAX], ADV))
    a(("expire-missing", ["EXPIRE", "absent", 10], ADV))
    a(("expire-bad-arg", ["EXPIRE", "k2", "soon"], ADV))
    a(("set-clears-ttl", ["SET", "ek3", "v2"]))
    a(("ttl-after-set", ["TTL", "ek3"]))
    # -- databases ---------------------------------------------------------
    a(("move", ["SET", "mv", "v"]))
    a(("move-do", ["MOVE", "mv", 10]))
    a(("move-again", ["MOVE", "mv", 10], ADV))
    a(("move-same-db", ["SET", "mv2", "v"]))
    a(("move-same-db-do", ["MOVE", "mv2", 9], ADV))
    a(("move-bad-db", ["MOVE", "mv2", 999], ADV))
    a(("select-oob", ["SELECT", 999], ADV))
    a(("select-negative", ["SELECT", -1], ADV))
    a(("select-back", ["SELECT", 9]))
    # -- dump / restore ----------------------------------------------------
    a(("restore-bad-payload", ["RESTORE", "rk", 0, "garbage"], ADV))
    a(("restore-negative-ttl", ["RESTORE", "rk", -1, "garbage"], ADV))
    # -- object ------------------------------------------------------------
    a(("object-refcount-shared", ["SET", "sh", 100]))
    a(("object-encoding-int", ["OBJECT", "ENCODING", "sh"]))
    a(("object-encoding-missing", ["OBJECT", "ENCODING", "absent"], ADV))
    a(("object-bad-subcommand", ["OBJECT", "NOSUCH", "sh"], ADV))
    # -- sort ---------------------------------------------------------------
    a(("sort-seed", ["RPUSH", "sl", 3, 1, 2]))
    a(("sort-numeric", ["SORT", "sl"]))
    a(("sort-desc", ["SORT", "sl", "DESC"]))
    a(("sort-limit", ["SORT", "sl", "LIMIT", 1, 1]))
    a(("sort-alpha-seed", ["RPUSH", "sa", "b", "a", "c"]))
    a(("sort-alpha", ["SORT", "sa", "ALPHA"]))
    a(("sort-non-numeric", ["SORT", "sa"], ADV))
    a(("sort-by-hash-seed", ["MSET", "w_a", 3, "w_b", 1, "w_c", 2]))
    a(("sort-by", ["SORT", "sa", "BY", "w_*", "ALPHA"]))
    a(("sort-by-nosort", ["SORT", "sa", "BY", "nosort", "ALPHA"]))
    a(("sort-get", ["SORT", "sa", "BY", "w_*", "GET", "#", "GET", "w_*"]))
    a(("sort-store", ["SORT", "sl", "STORE", "sorted"]))
    a(("sort-store-result", ["LRANGE", "sorted", 0, -1]))
    a(("sort-missing", ["SORT", "absent"], ADV))
    a(("sort-limit-negative", ["SORT", "sl", "LIMIT", -1, -1], ADV))
    return t


def run(ctx):
    port = ctx.port(0)
    s = ctx.server("keyspace", port, ctxmod.default_directives(port))
    s.start().wait_ready()
    with s.client() as c:
        c.cmd("SELECT", 9)
        c.cmd("FLUSHALL")
        cmdrun.run_table(ctx, c, "ks", _keyspace())

        # DUMP/RESTORE round trip: the payload carries an RDB version and a
        # checksum, so the bytes are recorded rather than asserted.
        c.cmd("DEL", "dk")
        c.cmd("RPUSH", "dk", "a", "b", "c")
        kind, payload = c.cmd("DUMP", "dk")
        ctx.emit("ks/dump-kind", kind)
        ctx.emit("ks/dump-len", len(payload) if payload else None)
        ctx.emit_reply("ks/restore-roundtrip",
                       c.cmd("RESTORE", "dk2", 0, payload))
        ctx.emit_reply("ks/restore-roundtrip-content",
                       c.cmd("LRANGE", "dk2", 0, -1))
        ctx.emit_reply("ks/restore-busy-key",
                       c.cmd("RESTORE", "dk2", 0, payload), adv=True)
        ctx.emit_reply("ks/restore-truncated",
                       c.cmd("RESTORE", "dk3", 0, payload[:-1]), adv=True)
        ctx.emit_reply("ks/restore-corrupted-checksum",
                       c.cmd("RESTORE", "dk4", 0,
                             payload[:-1] + bytes([payload[-1] ^ 0xFF])),
                       adv=True)
        ctx.emit_reply("ks/restore-flipped-body",
                       c.cmd("RESTORE", "dk5", 0,
                             payload[:1] + bytes([payload[1] ^ 0x7F]) + payload[2:]),
                       adv=True)

        # A full SCAN sweep: cursors depend on the per-process hash seed, so
        # only the swept key set is compared, sorted.
        seen = []
        cursor = b"0"
        guard = 0
        while True:
            kind, val = c.cmd("SCAN", cursor, "COUNT", 10)
            cursor = val[0][1]
            seen.extend(e[1] for e in val[1][1])
            guard += 1
            if cursor == b"0" or guard > 500:
                break
        ctx.emit("ks/scan-sweep", sorted(
            x.decode("utf-8", "backslashreplace") for x in seen))
        ctx.emit("ks/scan-terminated", cursor == b"0")
        ctx.emit_reply("ks/scan-match", c.cmd("SCAN", 0, "MATCH", "nomatch*",
                                              "COUNT", 1000), adv=True)
        ctx.emit_reply("ks/scan-bad-cursor", c.cmd("SCAN", "notanumber"), adv=True)
        ctx.emit_reply("ks/scan-negative-count",
                       c.cmd("SCAN", 0, "COUNT", -1), adv=True)
        ctx.emit_reply("ks/hscan-missing", c.cmd("HSCAN", "absent", 0), adv=True)
        ctx.emit_reply("ks/sscan-on-string", c.cmd("SSCAN", "w_a", 0), adv=True)

        c.cmd("FLUSHDB")
        ctx.emit_reply("ks/dbsize-after-flush", c.cmd("DBSIZE"))
    s.stop()
