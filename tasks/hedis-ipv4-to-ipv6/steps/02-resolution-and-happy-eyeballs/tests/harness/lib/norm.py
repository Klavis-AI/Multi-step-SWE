"""Normalisation applied identically at recording and at replay.

Everything here removes a value that legitimately varies between two runs of the
*same* binary: clocks, pids, ephemeral source ports, generated ids, memory
figures, file descriptors. Nothing here removes a value the migration could
change -- in particular, no rule touches the textual form of an address, the
bracketing of a host/port pair, or the family of a socket.

The rules are data (`VOLATILE`), so `record.py` and `replay.py` cannot drift.
"""

import os
import re

# ---------------------------------------------------------------------------
# Fixed ports used by the corpus, plus the cluster-bus ports Redis derives from
# them (port + 10000). Anything outside this set that looks like a port in a
# peer address is an ephemeral source port and is normalised.
# ---------------------------------------------------------------------------
BASE_PORT = 7400
PORT_SPAN = 400
FIXED_PORTS = (set(range(BASE_PORT, BASE_PORT + PORT_SPAN))
               | set(range(BASE_PORT + 10000, BASE_PORT + 10000 + PORT_SPAN)))

# (pattern, replacement) applied in order to every recorded text blob.
VOLATILE = [
    # --- INFO: clocks, pids, sizes, ids -----------------------------------
    (r"(?m)^(run_id:)[0-9a-f]+$", r"\1<RUNID>"),
    (r"(?m)^(master_replid2?:)[0-9a-f]+$", r"\1<REPLID>"),
    (r"(?m)^(process_id:)\d+$", r"\1<PID>"),
    (r"(?m)^(uptime_in_seconds|uptime_in_days|lru_clock|server_time_usec):"
     r"-?\d+$", r"\1:<N>"),
    # `maxmemory` and `maxmemory_policy` are configuration, not measurement:
    # they are stable across runs and a migration that changed them would be a
    # real regression, so they are deliberately left alone.
    (r"(?m)^(used_memory\w*|total_system_memory\w*|"
     r"mem_fragmentation_ratio|allocator_\w+):\S+$",
     r"\1:<N>"),
    (r"(?m)^(rdb_\w*(time|sec|size|changes|bgsave_in_progress|last_bgsave_status"
     r"|current_bgsave_time_sec))\S*:\S+$", r"\1:<N>"),
    (r"(?m)^(aof_\w*(size|time|sec|in_progress|status|rewrite_scheduled|"
     r"pending_rewrite|buffer_length|delayed_fsync|last_write_status))\S*:\S+$",
     r"\1:<N>"),
    (r"(?m)^(loading_\w+):\S+$", r"\1:<N>"),
    (r"(?m)^(total_connections_received|total_commands_processed|"
     r"instantaneous_\w+|total_net_\w+|sync_\w+|expired_keys|evicted_keys|"
     r"keyspace_hits|keyspace_misses|pubsub_\w+|latest_fork_usec|"
     r"migrate_cached_sockets|rejected_connections|total_reads_processed|"
     r"total_writes_processed|total_forks|number_of_cached_scripts|"
     r"monitor_clients):-?\d+(\.\d+)?$", r"\1:<N>"),
    (r"(?m)^(used_cpu_\w+):\S+$", r"\1:<N>"),
    (r"(?m)^(connected_clients|blocked_clients|client_\w+|"
     r"cluster_connections):\d+$", r"\1:<N>"),
    (r"(?m)^(master_repl_offset|slave_repl_offset|slave_read_repl_offset|"
     r"repl_backlog_\w+|second_repl_offset|master_last_io_seconds_ago|"
     r"master_sync_in_progress|slave_priority|master_failover_state):\S+$",
     r"\1:<N>"),
    (r"(?m)^(config_file|executable):\S*$", r"\1:<PATH>"),
    # Build and host identity only. `tcp_port`, `cluster_enabled` and `hz` are
    # *not* here: they are determined by the configuration the corpus writes,
    # they are stable, and `tcp_port` in particular is one of the values this
    # migration could plausibly get wrong -- so it stays scored.
    (r"(?m)^(redis_build_id|redis_git_sha1|redis_git_dirty|os|arch_bits|"
     r"gcc_version|multiplexing_api|atomicvar_api):.*$", r"\1:<ENV>"),
    (r"(?m)^(db\d+):keys=\d+,expires=\d+,avg_ttl=\d+$", r"\1:<KEYSPACE>"),
    (r"(?m)^(slave\d+:ip=)(\S+?)(,port=\d+),state=\S+?,offset=\d+,lag=\d+$",
     r"\1\2\3,state=<S>,offset=<N>,lag=<N>"),
    # --- CLIENT LIST ------------------------------------------------------
    (r"\bid=\d+\b", "id=<N>"),
    (r"\bfd=\d+\b", "fd=<N>"),
    (r"\bage=\d+\b", "age=<N>"),
    (r"\bidle=\d+\b", "idle=<N>"),
    (r"\bqbuf=\d+\b", "qbuf=<N>"),
    (r"\bqbuf-free=\d+\b", "qbuf-free=<N>"),
    (r"\bobl=\d+\b", "obl=<N>"),
    (r"\boll=\d+\b", "oll=<N>"),
    (r"\bomem=\d+\b", "omem=<N>"),
    # (Redis 4+/6+ CLIENT LIST fields -- argv-mem, tot-mem, multimem, rbs, rbp,
    # watch -- do not exist in this tree, so no rule pretends to mask them.)
    # --- DEBUG OBJECT -----------------------------------------------------
    # The heap address and the LRU clock move between two runs of the same
    # binary; `encoding` and `serializedlength` do not, and are the point.
    (r"\bat:0x[0-9a-fA-F]+", "at:<PTR>"),
    (r"\blru:\d+", "lru:<N>"),
    (r"\blru_seconds_idle:\d+", "lru_seconds_idle:<N>"),
    # --- CLUSTER NODES / nodes.conf --------------------------------------
    (r"\b[0-9a-f]{40}\b", "<NODEID>"),
    # ping-sent pong-recv config-epoch on a CLUSTER NODES line
    (r"(?m)^(<NODEID> \S+ [^ ]+ (?:<NODEID>|-) )\d+ \d+ \d+"
     r"( (?:connected|disconnected))", r"\1<N> <N> <N>\2"),
    (r"(?m)^(vars currentEpoch )\d+( lastVoteEpoch )\d+$", r"\1<N>\2<N>"),
    # A replication id followed by an offset, as logged by PSYNC.
    (r"<NODEID>:\d+", "<NODEID>:<N>"),
    # --- MONITOR feed: "<unix.usec> [<db> <peer>] "CMD" "arg"" ------------
    # The peer's *host* text survives; only its ephemeral port and the clock
    # are removed.
    (r"(?m)^\d{10}\.\d{6} (\[\d+ )", r"<TS> \1"),
    # The peer is `host:port`, and this tree prints an IPv6 host *unbracketed*,
    # so the host itself contains colons. Greedy `\S+` backtracks to the last
    # colon, which splits `::1:60620` and `[::1]:60620` alike; a character class
    # that excluded `:` would silently skip every v6 peer.
    (r"(?m)^(<TS> \[\d+ )(\S+):(\d+)\]", r"\1\2:<EPORT>]"),
    # --- build identity: differs between two compilations of the *same*
    # source, so it cannot be part of an expectation. The version string
    # itself is deliberately left alone.
    (r"\bbuild=[0-9a-fA-F]+", "build=<BUILD>"),
    (r"\bsha=[0-9a-fA-F]+:\d+", "sha=<SHA>"),
    (r"\bcommit=[0-9a-fA-F]*", "commit=<SHA>"),
    # --- timestamps and pids in logs -------------------------------------
    (r"\bpid=\d+", "pid=<PID>"),
    (r"\bPID:\s*\d+", "PID: <PID>"),
    (r"\b\d+:[MSCX] \d\d \w\w\w \d\d:\d\d:\d\d\.\d\d\d\b", "<LOGSTAMP>"),
    (r"\b1[5-9]\d{8}\b", "<EPOCH>"),
    (r"\b\d{13}\b", "<EPOCHMS>"),
    # --- temp file names the server derives from its own pid ---------------
    # (Absolute directories are handled by `register_path`, not by a rule.)
    (r"(?m)(dump|temp)-\d+\.rdb", r"\1-<PID>.rdb"),
    (r"(?m)temp-rewriteaof(-bg)?-\d+\.aof", r"temp-rewriteaof\1-<PID>.aof"),
    # --- resolver failure text: an unresolvable single-label name is answered
    # NXDOMAIN by one host's DNS and SERVFAIL by another's (Docker's embedded
    # DNS on a user-defined network), so the same binary prints
    # gai_strerror(EAI_NONAME) on one machine and gai_strerror(EAI_AGAIN) on
    # the next. Both mean "this name did not resolve"; neither is the tree's
    # choice. Folded onto the EAI_NONAME text the expectations were recorded
    # with, so recording and replay agree on any host.
    (r"Temporary failure in name resolution", "Name or service not known"),
]

def _compile(pattern):
    """Compile a rule, tolerating CRLF line endings.

    INFO, CLIENT LIST and CLUSTER NODES all use "\\r\\n", but `$` under
    re.MULTILINE anchors only before "\\n". A rule written to end at a line
    boundary would therefore silently never fire -- and a rule that never fires
    leaves a volatile field in the expectation.
    """
    if pattern.endswith("$") and not pattern.endswith("\\$"):
        pattern = pattern[:-1] + r"\r?$"
    return re.compile(pattern)


_COMPILED = [(_compile(p), r) for p, r in VOLATILE]

# Absolute paths registered at startup: the scratch workspace and the tree
# root. Both are chosen by the runner, not by the tree, so they must not enter
# an expectation -- but the *relative* part under them must, because a wrong
# file name is a real difference.
_PATHS = []


def register_path(path, token):
    """Replace an absolute path prefix with a fixed token everywhere."""
    p = os.path.abspath(path).rstrip("/")
    if not p or p == "/":
        return
    _PATHS.append((p, token))
    _PATHS.sort(key=lambda kv: len(kv[0]), reverse=True)

# Only the `host:port` fields. INFO's `slave0:ip=<host>,port=<n>` keeps the two
# apart with a comma, so including `ip=` here would chop an unbracketed v6
# literal at its own colon.
_EPHEMERAL_PEER = re.compile(
    r"(?P<pfx>\b(?:addr|laddr)=)(?P<host>\[[0-9A-Fa-f:.%]+\]|[0-9A-Za-z.:%\-]+)"
    r":(?P<port>\d+)")


def _mask_ephemeral_ports(text):
    """Replace a peer port that is not one of the corpus' fixed ports.

    The *host* part is untouched: address text is exactly what the migration
    changes and must never be normalised away.
    """

    def sub(m):
        port = int(m.group("port"))
        if port in FIXED_PORTS:
            return m.group(0)
        return "%s%s:<EPORT>" % (m.group("pfx"), m.group("host"))

    return _EPHEMERAL_PEER.sub(sub, text)


def normalize_text(text):
    if isinstance(text, (bytes, bytearray)):
        text = text.decode("utf-8", "backslashreplace")
    for prefix, token in _PATHS:
        text = text.replace(prefix, token)
    for rx, rep in _COMPILED:
        text = rx.sub(rep, text)
    return _mask_ephemeral_ports(text)


def rules_fired(text):
    """Indices of the VOLATILE rules that actually change `text`.

    The rules chain: the 40-hex rule rewrites a node id to `<NODEID>` *before*
    the rules that key off `<NODEID>` run. Asking whether each rule matches the
    raw input would therefore call those later rules dead when they are not.
    This applies the rules in order, exactly as `normalize_text` does, and
    reports which ones changed anything.
    """
    if isinstance(text, (bytes, bytearray)):
        text = text.decode("utf-8", "backslashreplace")
    for prefix, token in _PATHS:
        text = text.replace(prefix, token)
    fired = set()
    for i, (rx, rep) in enumerate(_COMPILED):
        new = rx.sub(rep, text)
        if new != text:
            fired.add(i)
        text = new
    return fired


def addr_fields(blob, field="addr"):
    """Every `<field>=…` value in a CLIENT LIST-style blob, normalised.

    Normalisation happens on the whole line first, so the ephemeral-port rule
    -- which keys off the `addr=` prefix -- actually fires. Splitting first and
    normalising the fragment would silently leave the port in.
    """
    out = []
    for line in normalize_text(blob).splitlines():
        for f in line.split(" "):
            if f.startswith(field + "="):
                out.append(f.split("=", 1)[1])
    return out


def _b2s(b):
    return b.decode("utf-8", "backslashreplace")


def reply_to_json(reply, normalize=True):
    """Convert a (kind, value) reply pair into JSON-safe, normalised data.

    The reply *shape* (the kind tags and nesting) is preserved verbatim; only
    the payload text goes through the volatile-field rules.
    """
    kind, val = reply
    if kind in ("nil", "nilarray"):
        return [kind, None]
    if kind == "int":
        return [kind, val]
    if kind == "array":
        return [kind, [reply_to_json(x, normalize) for x in val]]
    text = _b2s(val)
    if normalize:
        text = normalize_text(text)
    return [kind, text]
